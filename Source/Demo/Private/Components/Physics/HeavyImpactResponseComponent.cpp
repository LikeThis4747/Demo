// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactResponseComponent.cpp
 * 职责：把机关预测和真实 Chaos 接触转换为受控全身物理飞行、环境落地与无 Tick 倒地。
 * 边界：所有冲量来自外部刚体接触；本文件不调用 AddImpulse、LaunchCharacter 或受击动画。
 */

#include "Components/Physics/HeavyImpactResponseComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/Physics/HeavyImpactTuningData.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsControlAsset.h"
#include "PhysicsControlComponent.h"
#include "PhysicsControlData.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeavyImpact, Log, All);

namespace HeavyImpactRuntime
{
	/** At low frame rates, keep the valid ETA window wider than one frame without changing normal 30/60 FPS tuning. */
	constexpr float MaximumPreparationFrameMultiplier = 2.5f;
	constexpr float AbsoluteMaximumPreparationSeconds = 0.5f;
}

/** 创建默认关闭、仅在事务期间启用的 PostPhysics Tick。 */
UHeavyImpactResponseComponent::UHeavyImpactResponseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

/** 保存角色显式注入的唯一依赖，不触发资产加载或运行时创建。 */
void UHeavyImpactResponseComponent::Configure(
	ACharacter* InCharacter,
	USkeletalMeshComponent* InMesh,
	UCapsuleComponent* InCapsule,
	UCharacterMovementComponent* InMovement,
	UPhysicsControlComponent* InPhysicsControl,
	UHeavyImpactTuningData* InTuning)
{
	check(!HasBegunPlay());

	Character = InCharacter;
	Mesh = InMesh;
	Capsule = InCapsule;
	Movement = InMovement;
	PhysicsControl = InPhysicsControl;
	Tuning = InTuning;
	bConfigured = IsValid(Character)
		&& IsValid(Mesh)
		&& IsValid(Capsule)
		&& IsValid(Movement)
		&& IsValid(PhysicsControl)
		&& IsValid(Tuning);
}

/** 提前验证并创建 PCA 记录；命中调用栈不做同步加载或运行时结构创建。 */
void UHeavyImpactResponseComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bConfigured)
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact on %s was not configured before BeginPlay."),
			*GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}

	FText ValidationError;
	if (!Tuning->Validate(Mesh, ValidationError))
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact validation failed on %s: %s"),
			*GetNameSafe(GetOwner()),
			*ValidationError.ToString());
		return;
	}

	if (!InitializePhysicsControlAuthority(ValidationError))
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact PCA initialization failed on %s: %s"),
			*GetNameSafe(GetOwner()),
			*ValidationError.ToString());
		return;
	}

	Mesh->OnComponentHit.AddUniqueDynamic(this, &UHeavyImpactResponseComponent::HandleMeshHit);
	bInitialized = true;
}

/** 恢复未提交快照，解绑 Delegate，并精确销毁本组件创建的 PCA 记录。 */
void UHeavyImpactResponseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (State == EHeavyImpactState::Prepared && Snapshot.bValid)
	{
		RestoreSnapshotAfterFalsePositive();
	}

	if (IsValid(Mesh))
	{
		Mesh->OnComponentHit.RemoveDynamic(this, &UHeavyImpactResponseComponent::HandleMeshHit);
	}

	DestroyOwnedPhysicsControlRecords();
	bInitialized = false;
	Super::EndPlay(EndPlayReason);
}

/** 从 PCA 创建独占记录，并确认默认编译数据不会在 BeginPlay 让角色进入模拟或世界锚定。 */
bool UHeavyImpactResponseComponent::InitializePhysicsControlAuthority(FText& OutError)
{
	if (!PhysicsControl->GetAllControlNames().IsEmpty()
		|| !PhysicsControl->GetAllBodyModifierNames().IsEmpty())
	{
		OutError = NSLOCTEXT(
			"HeavyImpact",
			"NonExclusivePhysicsControl",
			"PhysicsControl already owns runtime records. Disable legacy setup first.");
		return false;
	}

	PhysicsControl->Activate(true);
	PhysicsControl->PhysicsControlAsset = Tuning->PhysicsControlAsset.Get();
	const bool bCreated = PhysicsControl->CreateControlsAndBodyModifiersFromPhysicsControlAsset(
		Mesh,
		nullptr,
		NAME_None);

	// CreateFromPCA 失败时也可能留下部分记录；先抓取，再按名称精确清理。
	OwnedControlNames = PhysicsControl->GetAllControlNames();
	OwnedBodyModifierNames = PhysicsControl->GetAllBodyModifierNames();
	if (!bCreated)
	{
		OutError = NSLOCTEXT(
			"HeavyImpact",
			"CreatePCARecordsFailed",
			"CreateControlsAndBodyModifiersFromPhysicsControlAsset failed.");
		DestroyOwnedPhysicsControlRecords();
		return false;
	}

	if (OwnedControlNames.IsEmpty() || OwnedBodyModifierNames.IsEmpty())
	{
		OutError = NSLOCTEXT(
			"HeavyImpact",
			"MissingPCARecords",
			"PCA created no parent controls or no body modifiers.");
		DestroyOwnedPhysicsControlRecords();
		return false;
	}

	static const FName RequiredLimbSets[] = {
		TEXT("Head"),
		TEXT("ArmLeft"),
		TEXT("ArmRight"),
		TEXT("LegLeft"),
		TEXT("LegRight"),
		TEXT("Spine")
	};

	for (const FName LimbSet : RequiredLimbSets)
	{
		if (PhysicsControl->GetControlNamesInSet(LimbSet).IsEmpty()
			|| PhysicsControl->GetBodyModifierNamesInSet(LimbSet).IsEmpty())
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "EmptyRuntimeLimb", "PCA limb {0} created no parent control or no body modifier for this Physics Asset."),
				FText::FromName(LimbSet));
			DestroyOwnedPhysicsControlRecords();
			return false;
		}
	}

	const TArray<FName>& ParentSpaceControls =
		PhysicsControl->GetControlNamesInSet(TEXT("ParentSpace"));
	const TArray<FName>& WorldSpaceControls =
		PhysicsControl->GetControlNamesInSet(TEXT("WorldSpace"));
	const TArray<FName>& AllBodyModifiers =
		PhysicsControl->GetBodyModifierNamesInSet(TEXT("All"));
	const UPhysicsAsset* MeshPhysicsAsset = Mesh->GetPhysicsAsset();
	const int32 ExpectedBodyCount = IsValid(MeshPhysicsAsset)
		? MeshPhysicsAsset->SkeletalBodySetups.Num()
		: 0;
	if (!WorldSpaceControls.IsEmpty()
		|| ParentSpaceControls.Num() != OwnedControlNames.Num()
		|| AllBodyModifiers.Num() != OwnedBodyModifierNames.Num()
		|| AllBodyModifiers.Num() != ExpectedBodyCount)
	{
		OutError = FText::Format(
			NSLOCTEXT("HeavyImpact", "IncompleteRuntimeCoverage", "PCA runtime coverage is incomplete or non-exclusive: Parent={0}/{1}, World={2}, Bodies={3}/{4}."),
			FText::AsNumber(ParentSpaceControls.Num()),
			FText::AsNumber(OwnedControlNames.Num()),
			FText::AsNumber(WorldSpaceControls.Num()),
			FText::AsNumber(AllBodyModifiers.Num()),
			FText::AsNumber(ExpectedBodyCount));
		DestroyOwnedPhysicsControlRecords();
		return false;
	}

	if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfileInactive))
	{
		OutError = NSLOCTEXT(
			"HeavyImpact",
			"InactiveProfileFailed",
			"PCA Inactive profile could not be invoked.");
		DestroyOwnedPhysicsControlRecords();
		return false;
	}

	// 编译后的 CharacterSetupData 已由 Tuning 校验为 Kinematic/QueryOnly/Controls Disabled。
	PhysicsControl->SetComponentTickEnabled(false);
	return true;
}

/** 状态只通过本函数修改，并向角色适配层广播前后值。 */
void UHeavyImpactResponseComponent::SetState(EHeavyImpactState NewState)
{
	if (State == NewState)
	{
		return;
	}

	const EHeavyImpactState Previous = State;
	State = NewState;
	StateElapsedSeconds = 0.0f;
	OnStateChanged.Broadcast(Previous, NewState);
}

/** 调用项目必须具备的编译后 Profile；失败只记错，不暗中改用另一套视觉方案。 */
bool UHeavyImpactResponseComponent::InvokeRequiredProfile(FName ProfileName)
{
	if (!IsValid(PhysicsControl)
		|| !IsValid(Tuning)
		|| !IsValid(Tuning->PhysicsControlAsset)
		|| !Tuning->PhysicsControlAsset->Profiles.Contains(ProfileName))
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact missing required profile %s on %s."),
			*ProfileName.ToString(),
			*GetNameSafe(GetOwner()));
		return false;
	}

	if (!PhysicsControl->InvokeControlProfile(ProfileName, NAME_None, NAME_None))
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact failed to invoke profile %s on %s."),
			*ProfileName.ToString(),
			*GetNameSafe(GetOwner()));
		return false;
	}

	return true;
}

/** 拒绝无法形成真实刚体接触、太早或太迟到达的预测请求。 */
bool UHeavyImpactResponseComponent::ValidatePreparationRequest(
	const FHeavyImpactPreparationRequest& Request,
	FString& OutReason,
	float& OutAllowedMaximumSeconds) const
{
	OutAllowedMaximumSeconds = 0.0f;

	if (!bInitialized)
	{
		OutReason = TEXT("HeavyImpact component is not initialized.");
		return false;
	}

	if (!Request.IsStructurallyValid(GetOwner(), OutReason))
	{
		return false;
	}

	const ECollisionEnabled::Type SourceCollision = Request.SourceComponent->GetCollisionEnabled();
	if (SourceCollision != ECollisionEnabled::PhysicsOnly
		&& SourceCollision != ECollisionEnabled::QueryAndPhysics)
	{
		OutReason = TEXT("SourceComponent has no physics collision.");
		return false;
	}

	if (Request.SourceComponent->GetCollisionResponseToChannel(ECC_PhysicsBody) != ECR_Block)
	{
		OutReason = TEXT("SourceComponent does not block ECC_PhysicsBody.");
		return false;
	}

	const UWorld* World = GetWorld();
	const float DeltaSeconds = IsValid(World) ? World->GetDeltaSeconds() : 0.0f;
	const float FrameAwareLeadSeconds = DeltaSeconds > 0.0f
		? DeltaSeconds * 1.25f
		: 0.0f;
	const float RequiredLeadSeconds = FMath::Max(
		Tuning->MinimumPreparationLeadSeconds,
		FrameAwareLeadSeconds);
	const float FrameAwareMaximumSeconds = FMath::Min(
		HeavyImpactRuntime::AbsoluteMaximumPreparationSeconds,
		DeltaSeconds * HeavyImpactRuntime::MaximumPreparationFrameMultiplier);
	const float AllowedMaximumSeconds = FMath::Max(
		Tuning->MaximumPreparationSeconds,
		FrameAwareMaximumSeconds);
	if (Request.EstimatedTimeToContactSeconds > AllowedMaximumSeconds)
	{
		OutReason = TEXT("Prediction arrived too early for the frame-aware preparation window.");
		return false;
	}

	if (Request.EstimatedTimeToContactSeconds < RequiredLeadSeconds)
	{
		OutReason = TEXT("Prediction arrived too late to cross a reliable PrePhysics update.");
		return false;
	}

	OutAllowedMaximumSeconds = AllowedMaximumSeconds;
	return true;
}

/** 去重后建立一次可回滚 Prepared 事务；任何部分失败都恢复快照。 */
EHeavyImpactPrepareResult UHeavyImpactResponseComponent::PrepareForImpact(
	const FHeavyImpactPreparationRequest& Request)
{
	if (HasSeenImpactId(Request.ImpactId))
	{
		return EHeavyImpactPrepareResult::Duplicate;
	}

	if (State != EHeavyImpactState::Inactive)
	{
		return EHeavyImpactPrepareResult::Busy;
	}

	FString FailureReason;
	float AllowedMaximumSeconds = 0.0f;
	if (!ValidatePreparationRequest(Request, FailureReason, AllowedMaximumSeconds))
	{
		if (ShouldLogRejectedImpact(Request.ImpactId))
		{
			UE_LOG(
				LogHeavyImpact,
				Warning,
				TEXT("HeavyImpact request rejected on %s: %s"),
				*GetNameSafe(GetOwner()),
				*FailureReason);
		}
		return EHeavyImpactPrepareResult::Invalid;
	}

	if (!CapturePreContactSnapshot(FailureReason)
		|| !EnterPrepared(Request, AllowedMaximumSeconds, FailureReason))
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact prepare failed on %s: %s"),
			*GetNameSafe(GetOwner()),
			*FailureReason);
		RestoreSnapshotAfterFalsePositive();
		return EHeavyImpactPrepareResult::Invalid;
	}

	// 只有完整进入 Prepared 的请求进入去重缓存；预测或状态转换失败可用同一 ID 稍后重试。
	RecordRecentImpactId(Request.ImpactId);
	return EHeavyImpactPrepareResult::Accepted;
}

/** 已接受 ID 的固定长度查询。 */
bool UHeavyImpactResponseComponent::HasSeenImpactId(const FGuid& Id) const
{
	return RecentImpactIds.Contains(Id);
}

/** 保存已接受 ID，并从最旧记录开始裁剪。 */
void UHeavyImpactResponseComponent::RecordRecentImpactId(const FGuid& Id)
{
	RecentImpactIds.Add(Id);
	const int32 Overflow = RecentImpactIds.Num() - Tuning->RecentImpactHistorySize;
	if (Overflow > 0)
	{
		RecentImpactIds.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

/** 使用独立缓存抑制 Invalid 日志，不妨碍同一 ID 稍后重新提交。 */
bool UHeavyImpactResponseComponent::ShouldLogRejectedImpact(const FGuid& Id)
{
	if (LoggedRejectedImpactIds.Contains(Id))
	{
		return false;
	}

	LoggedRejectedImpactIds.Add(Id);
	const int32 MaxRejectedIds = IsValid(Tuning)
		? FMath::Max(1, Tuning->RecentImpactHistorySize)
		: 16;
	const int32 Overflow = LoggedRejectedImpactIds.Num() - MaxRejectedIds;
	if (Overflow > 0)
	{
		LoggedRejectedImpactIds.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
	return true;
}

/** 保存角色外壳、动画、碰撞和每个 Physics Asset Body 的可恢复属性。 */
bool UHeavyImpactResponseComponent::CapturePreContactSnapshot(FString& OutReason)
{
	Snapshot.Reset();

	const UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();
	if (!IsValid(PhysicsAsset) || !IsValid(Mesh->GetAttachParent()))
	{
		OutReason = TEXT("Mesh has no PhysicsAsset or attach parent.");
		return false;
	}

	Snapshot.ActorTransform = Character->GetActorTransform();
	Snapshot.CharacterVelocity = Movement->Velocity;
	Snapshot.MovementMode = Movement->MovementMode;
	Snapshot.CustomMovementMode = Movement->CustomMovementMode;
	Snapshot.MeshAttachParent = Mesh->GetAttachParent();
	Snapshot.MeshAttachSocket = Mesh->GetAttachSocketName();
	Snapshot.MeshRelativeTransform = Mesh->GetRelativeTransform();
	Snapshot.bMeshPauseAnims = Mesh->bPauseAnims;
	Snapshot.MeshCollisionEnabled = Mesh->GetCollisionEnabled();
	Snapshot.MeshObjectType = Mesh->GetCollisionObjectType();
	Snapshot.MeshResponses = Mesh->GetCollisionResponseToChannels();
	Snapshot.bMeshBodyInstanceNotify = Mesh->BodyInstance.bNotifyRigidBodyCollision;
	Snapshot.bMeshBodyInstanceCCD = Mesh->BodyInstance.bUseCCD;
	Snapshot.CapsuleCollisionEnabled = Capsule->GetCollisionEnabled();
	Snapshot.CapsuleResponses = Capsule->GetCollisionResponseToChannels();

	Snapshot.Bodies.Reserve(PhysicsAsset->SkeletalBodySetups.Num());
	for (const USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!IsValid(BodySetup))
		{
			continue;
		}

		FBodyInstance* Body = Mesh->GetBodyInstance(BodySetup->BoneName);
		if (!Body)
		{
			OutReason = FString::Printf(
				TEXT("Missing runtime body for bone %s."),
				*BodySetup->BoneName.ToString());
			Snapshot.Reset();
			return false;
		}

		FHeavyImpactBodySnapshot& BodySnapshot = Snapshot.Bodies.AddDefaulted_GetRef();
		BodySnapshot.BoneName = BodySetup->BoneName;
		// 保存公开配置位；UE5.8 的 IsInstanceSimulatingPhysics 内联依赖未从 Engine 模块导出。
		BodySnapshot.bWasSimulating = Body->bSimulatePhysics;
		if (BodySnapshot.bWasSimulating)
		{
			OutReason = FString::Printf(
				TEXT("Body %s was already simulating before HeavyImpact preparation."),
				*BodySetup->BoneName.ToString());
			Snapshot.Reset();
			return false;
		}
		BodySnapshot.bNotifyRigidBodyCollision = Body->bNotifyRigidBodyCollision;
		BodySnapshot.bUseCCD = Body->bUseCCD;
		BodySnapshot.PhysicsBlendWeight = Body->PhysicsBlendWeight;
		BodySnapshot.CollisionEnabled = Body->GetCollisionEnabled(false);
	}

	Snapshot.bValid = true;
	return true;
}

/** 按严格顺序停止 Character 驱动、冻结姿态、脱离 Mesh 并在接触前启用全身物理。 */
bool UHeavyImpactResponseComponent::EnterPrepared(
	const FHeavyImpactPreparationRequest& Request,
	const float AllowedMaximumSeconds,
	FString& OutReason)
{
	if (!FMath::IsFinite(AllowedMaximumSeconds) || AllowedMaximumSeconds <= 0.0f)
	{
		OutReason = TEXT("Prepared transaction has no valid timeout.");
		return false;
	}

	ActiveRequest = Request;
	ActivePreparationTimeoutSeconds = AllowedMaximumSeconds;
	ExpectedSourceActor = Request.SourceActor;
	ExpectedSourceComponent = Request.SourceComponent;

	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	Mesh->bPauseAnims = true;

	ActorToPelvisZ = Character->GetActorLocation().Z
		- Mesh->GetBoneLocation(Tuning->PelvisBone).Z;
	Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	Mesh->SetAllBodiesNotifyRigidBodyCollision(true);

	PhysicsControl->SetComponentTickEnabled(true);
	if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfilePrepared))
	{
		OutReason = TEXT("Prepared PCA profile failed.");
		return false;
	}

	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->SetAllBodiesPhysicsBlendWeight(1.0f, false);
	Mesh->WakeAllRigidBodies();
	if (!Mesh->IsSimulatingPhysics(Tuning->PelvisBone))
	{
		OutReason = TEXT("Pelvis was not simulating after Prepared transition.");
		return false;
	}

	bFreeFallbackInvoked = false;
	bPendingDownedSleep = false;
	bHardTimeoutReported = false;
	PreparedEntryFrame = GFrameCounter;
	TotalCommittedSeconds = 0.0f;
	StableElapsedSeconds = 0.0f;
	SetState(EHeavyImpactState::Prepared);
	SetComponentTickEnabled(true);
	return true;
}

/** 只让预期机关提交 Prepared；Downed 只接受有求解器冲量的动态刚体再次唤醒。 */
void UHeavyImpactResponseComponent::HandleMeshHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (State == EHeavyImpactState::Downed)
	{
		if (IsValid(OtherActor)
			&& OtherActor != GetOwner()
			&& IsValid(OtherComponent)
			&& OtherComponent->IsAnySimulatingPhysics()
			&& !NormalImpulse.IsNearlyZero(1.0f))
		{
			ResumeFromDownedHit(Hit, NormalImpulse);
		}
		return;
	}

	if (State != EHeavyImpactState::Prepared)
	{
		return;
	}

	if (OtherActor != ExpectedSourceActor || OtherComponent != ExpectedSourceComponent)
	{
		return;
	}

	if (NormalImpulse.IsNearlyZero(1.0f))
	{
		return;
	}

	CommitRealImpact(Hit, NormalImpulse);
}

/** 接受 Chaos 已产生的速度，切到 Flight Profile 并广播一次提交事件。 */
void UHeavyImpactResponseComponent::CommitRealImpact(
	const FHitResult& Hit,
	const FVector& NormalImpulse)
{
	const bool bPreparedCrossedFrameBoundary = GFrameCounter > PreparedEntryFrame;
	if (!bPreparedCrossedFrameBoundary)
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact real contact reached %s before Prepared crossed a full frame boundary; first contact may be uncontrolled."),
			*GetNameSafe(GetOwner()));
	}

	SetState(EHeavyImpactState::Simulating);
	StableElapsedSeconds = 0.0f;
	TotalCommittedSeconds = 0.0f;
	if (!bPreparedCrossedFrameBoundary)
	{
		EnterFreeFallback(TEXT("Real contact arrived before Prepared crossed a full frame boundary."));
	}
	else if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfileFlight))
	{
		EnterFreeFallback(TEXT("Flight profile failed after real contact."));
	}

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact committed: %s, bone=%s, contact=%s, solver impulse=%s"),
		*GetNameSafe(GetOwner()),
		*Hit.BoneName.ToString(),
		*Hit.ImpactPoint.ToCompactString(),
		*NormalImpulse.ToCompactString());

	OnImpactCommitted.Broadcast(ActiveRequest);
	ActiveRequest = FHeavyImpactPreparationRequest();
	ActivePreparationTimeoutSeconds = 0.0f;
	Snapshot.Reset();
	PreparedEntryFrame = 0;
}

/** 重新开启 Flight 约束和 PostPhysics 判稳；接触冲量已由 Chaos 传递。 */
void UHeavyImpactResponseComponent::ResumeFromDownedHit(
	const FHitResult& Hit,
	const FVector& NormalImpulse)
{
	TotalCommittedSeconds = 0.0f;
	StableElapsedSeconds = 0.0f;
	bFreeFallbackInvoked = false;
	bPendingDownedSleep = false;
	bHardTimeoutReported = false;
	PhysicsControl->SetComponentTickEnabled(true);
	SetState(EHeavyImpactState::Simulating);
	SetComponentTickEnabled(true);
	Mesh->WakeAllRigidBodies();

	if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfileFlight))
	{
		EnterFreeFallback(TEXT("Flight profile failed after Downed was hit again."));
	}

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact Downed body woke from real contact: %s bone=%s impulse=%s"),
		*GetNameSafe(GetOwner()),
		*Hit.BoneName.ToString(),
		*NormalImpulse.ToCompactString());
}

/** 在物理事务状态下更新计时、外壳位置和稳定判断。 */
void UHeavyImpactResponseComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StateElapsedSeconds += DeltaTime;

	if (State == EHeavyImpactState::Downed && bPendingDownedSleep)
	{
		FinishPendingDownedSleep();
		return;
	}

	if (State == EHeavyImpactState::Prepared)
	{
		UpdatePhysicalFollow(DeltaTime);
		if (StateElapsedSeconds >= ActivePreparationTimeoutSeconds)
		{
			CancelUncommittedPreparation(TEXT("Expected source did not make contact."));
		}
		return;
	}

	if (State == EHeavyImpactState::Simulating || State == EHeavyImpactState::Settling)
	{
		TotalCommittedSeconds += DeltaTime;
		UpdatePhysicalFollow(DeltaTime, false, State == EHeavyImpactState::Settling);
		UpdateStability(DeltaTime);
	}
}

/** 将 Actor/Capsule 外壳跟到真实骨盆；只复制水平朝向，不复制 Pitch/Roll。 */
void UHeavyImpactResponseComponent::UpdatePhysicalFollow(
	float DeltaTime,
	bool bSnap,
	bool bUseGroundedCapsulePlacement)
{
	if (!IsValid(Character) || !IsValid(Mesh) || !IsValid(Capsule))
	{
		return;
	}

	const FVector PelvisLocation = Mesh->GetBoneLocation(Tuning->PelvisBone);
	FVector DesiredActorLocation = PelvisLocation;
	if (bUseGroundedCapsulePlacement)
	{
		FHitResult GroundHit;
		if (TryGetGroundSupport(GroundHit))
		{
			DesiredActorLocation.Z = GroundHit.ImpactPoint.Z + Capsule->GetScaledCapsuleHalfHeight();
		}
		else
		{
			DesiredActorLocation.Z += ActorToPelvisZ;
		}
	}
	else
	{
		DesiredActorLocation.Z += ActorToPelvisZ;
	}

	const FVector NewLocation = bSnap
		? DesiredActorLocation
		: FMath::VInterpTo(
			Character->GetActorLocation(),
			DesiredActorLocation,
			DeltaTime,
			Tuning->CapsuleFollowInterpSpeed);

	const FVector PelvisForward = Mesh->GetBoneQuaternion(Tuning->PelvisBone).GetForwardVector();
	const FVector HorizontalForward = FVector::VectorPlaneProject(PelvisForward, FVector::UpVector);
	const float DesiredYaw = HorizontalForward.IsNearlyZero()
		? Character->GetActorRotation().Yaw
		: HorizontalForward.Rotation().Yaw;
	const FRotator NewRotation(0.0f, DesiredYaw, 0.0f);

	Character->SetActorLocationAndRotation(
		NewLocation,
		NewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

/** 只在最短模拟时间后，用速度和可行走支撑累计稳定时间。 */
void UHeavyImpactResponseComponent::UpdateStability(float DeltaTime)
{
	const FVector LinearVelocity = Mesh->GetPhysicsLinearVelocity(Tuning->PelvisBone);
	const FVector AngularVelocityDeg = Mesh->GetPhysicsAngularVelocityInDegrees(Tuning->PelvisBone);
	const bool bSlowEnough =
		LinearVelocity.Size() <= Tuning->StableLinearSpeedCmPerSecond
		&& AngularVelocityDeg.Size() <= Tuning->StableAngularSpeedDegPerSecond;

	FHitResult GroundHit;
	const bool bSupported = TryGetGroundSupport(GroundHit);
	const bool bMinimumTimeElapsed = TotalCommittedSeconds >= Tuning->MinimumSimulationSeconds;

	if (!bFreeFallbackInvoked
		&& State == EHeavyImpactState::Simulating
		&& bMinimumTimeElapsed
		&& bSlowEnough
		&& bSupported)
	{
		if (InvokeRequiredProfile(Demo::HeavyImpact::ProfileLandingRecovery))
		{
			SetState(EHeavyImpactState::Settling);
		}
		else
		{
			EnterFreeFallback(TEXT("LandingRecovery profile failed."));
		}
	}

	if (bMinimumTimeElapsed && bSlowEnough && bSupported)
	{
		StableElapsedSeconds += DeltaTime;
		if (StableElapsedSeconds >= Tuning->RequiredStableSeconds)
		{
			EnterDowned(TEXT("Body remained slow and supported."));
			return;
		}
	}
	else
	{
		StableElapsedSeconds = 0.0f;
		if (!bFreeFallbackInvoked && State == EHeavyImpactState::Settling)
		{
			if (InvokeRequiredProfile(Demo::HeavyImpact::ProfileFlight))
			{
				SetState(EHeavyImpactState::Simulating);
			}
			else
			{
				EnterFreeFallback(TEXT("Flight profile failed while leaving Settling."));
			}
		}
	}

	if (!bFreeFallbackInvoked
		&& TotalCommittedSeconds >= Tuning->FreeFallbackAfterSeconds)
	{
		EnterFreeFallback(TEXT("Physical response exceeded normal duration."));
	}

	if (TotalCommittedSeconds >= Tuning->ForceDownedAfterSeconds)
	{
		if (bSupported)
		{
			Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector, false);
			Mesh->SetAllPhysicsAngularVelocityInDegrees(FVector::ZeroVector, false);
			EnterDowned(TEXT("Supported hard safety timeout."));
		}
		else if (!bHardTimeoutReported)
		{
			bHardTimeoutReported = true;
			UE_LOG(
				LogHeavyImpact,
				Error,
				TEXT("HeavyImpact hard timeout reached without ground support on %s; body remains free instead of sleeping in air."),
				*GetNameSafe(GetOwner()));
		}
	}
}

/** 从骨盆向下查询世界几何，并复用 CharacterMovement 的可行走法线判断。 */
bool UHeavyImpactResponseComponent::TryGetGroundSupport(FHitResult& OutGroundHit) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Mesh) || !IsValid(Movement))
	{
		return false;
	}

	const FVector Start = Mesh->GetBoneLocation(Tuning->PelvisBone);
	const FVector End = Start - FVector::UpVector * Tuning->GroundProbeDistance;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HeavyImpactGround), false);
	Params.AddIgnoredActor(GetOwner());
	if (IsValid(ExpectedSourceActor))
	{
		Params.AddIgnoredActor(ExpectedSourceActor);
	}

	if (!World->LineTraceSingleByObjectType(OutGroundHit, Start, End, Objects, Params))
	{
		return false;
	}

	return Movement->IsWalkable(OutGroundHit);
}

/** 关闭姿态控制并保持 Simulated/Gravity/Collision；从不产生额外线性冲量。 */
void UHeavyImpactResponseComponent::EnterFreeFallback(const TCHAR* Reason)
{
	bFreeFallbackInvoked = true;
	StableElapsedSeconds = 0.0f;

	if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfileFreeFallback))
	{
		const bool bControlsDisabled = PhysicsControl->SetControlsEnabled(
			OwnedControlNames,
			false,
			true,
			false);
		const bool bMovementSet = PhysicsControl->SetBodyModifiersMovementType(
			OwnedBodyModifierNames,
			EPhysicsMovementType::Simulated,
			true,
			false);
		const bool bCollisionSet = PhysicsControl->SetBodyModifiersCollisionType(
			OwnedBodyModifierNames,
			ECollisionEnabled::QueryAndPhysics,
			true,
			false);
		const bool bGravitySet = PhysicsControl->SetBodyModifiersGravityMultiplier(
			OwnedBodyModifierNames,
			1.0f,
			true,
			false);
		const bool bBlendSet = PhysicsControl->SetBodyModifiersPhysicsBlendWeight(
			OwnedBodyModifierNames,
			1.0f,
			true,
			false);

		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact direct FreeFallback on %s: controls=%d movement=%d collision=%d gravity=%d blend=%d"),
			*GetNameSafe(GetOwner()),
			bControlsDisabled,
			bMovementSet,
			bCollisionSet,
			bGravitySet,
			bBlendSet);
	}

	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->SetAllBodiesPhysicsBlendWeight(1.0f, false);
	Mesh->WakeAllRigidBodies();
	PhysicsControl->SetComponentTickEnabled(true);
	if (State == EHeavyImpactState::Settling)
	{
		SetState(EHeavyImpactState::Simulating);
	}

	UE_LOG(
		LogHeavyImpact,
		Warning,
		TEXT("HeavyImpact entered FreeFallback on %s: %s"),
		*GetNameSafe(GetOwner()),
		Reason);
}

/** 对齐真实地面落点，调用 FreeFallback，并等下一次 PrePhysics 后再睡眠。 */
void UHeavyImpactResponseComponent::EnterDowned(const TCHAR* Reason)
{
	UpdatePhysicalFollow(0.0f, true, true);
	if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfileFreeFallback))
	{
		EnterFreeFallback(TEXT("FreeFallback profile failed while entering Downed."));
	}

	ExpectedSourceActor = nullptr;
	ExpectedSourceComponent = nullptr;
	bPendingDownedSleep = true;
	SetState(EHeavyImpactState::Downed);
	SetComponentTickEnabled(true);

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact downed pending one PrePhysics update on %s: %s"),
		*GetNameSafe(GetOwner()),
		Reason);
}

/** FreeFallback 已在上一帧进入求解器后，睡眠并关闭共享组件与 PhysicsControl Tick。 */
void UHeavyImpactResponseComponent::FinishPendingDownedSleep()
{
	bPendingDownedSleep = false;
	Mesh->PutAllRigidBodiesToSleep();
	PhysicsControl->SetComponentTickEnabled(false);
	SetComponentTickEnabled(false);

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact downed sleep completed on %s."),
		*GetNameSafe(GetOwner()));
}

/** Prepared 超时的唯一回滚入口。 */
void UHeavyImpactResponseComponent::CancelUncommittedPreparation(const TCHAR* Reason)
{
	check(State == EHeavyImpactState::Prepared);
	RestoreSnapshotAfterFalsePositive();
	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact preparation cancelled on %s: %s"),
		*GetNameSafe(GetOwner()),
		Reason);
}

/** 恢复每个刚体的碰撞、CCD、Hit 通知、Blend 和模拟状态。 */
void UHeavyImpactResponseComponent::RestoreBodySnapshot()
{
	Mesh->BodyInstance.SetInstanceNotifyRBCollision(Snapshot.bMeshBodyInstanceNotify);
	Mesh->BodyInstance.SetUseCCD(Snapshot.bMeshBodyInstanceCCD);

	for (const FHeavyImpactBodySnapshot& BodySnapshot : Snapshot.Bodies)
	{
		FBodyInstance* Body = Mesh->GetBodyInstance(BodySnapshot.BoneName);
		if (!Body)
		{
			UE_LOG(
				LogHeavyImpact,
				Error,
				TEXT("Cannot restore missing body %s on %s."),
				*BodySnapshot.BoneName.ToString(),
				*GetNameSafe(GetOwner()));
			continue;
		}

		Body->SetCollisionEnabled(BodySnapshot.CollisionEnabled, true);
		Body->SetUseCCD(BodySnapshot.bUseCCD);
		Body->SetInstanceNotifyRBCollision(BodySnapshot.bNotifyRigidBodyCollision);
		Body->PhysicsBlendWeight = BodySnapshot.PhysicsBlendWeight;
		Body->SetInstanceSimulatePhysics(BodySnapshot.bWasSimulating, false, false);
	}
}

/** 完整撤销未提交的物理准备，并恢复受击前 Actor Transform。 */
void UHeavyImpactResponseComponent::RestoreSnapshotAfterFalsePositive()
{
	if (!Snapshot.bValid)
	{
		ActiveRequest = FHeavyImpactPreparationRequest();
		ExpectedSourceActor = nullptr;
		ExpectedSourceComponent = nullptr;
		ActivePreparationTimeoutSeconds = 0.0f;
		PreparedEntryFrame = 0;
		SetComponentTickEnabled(false);
		SetState(EHeavyImpactState::Inactive);
		return;
	}

	InvokeRequiredProfile(Demo::HeavyImpact::ProfileInactive);
	Mesh->SetAllBodiesSimulatePhysics(false);
	RestoreBodySnapshot();

	USceneComponent* AttachParent = Snapshot.MeshAttachParent.Get();
	if (IsValid(AttachParent))
	{
		Mesh->AttachToComponent(
			AttachParent,
			FAttachmentTransformRules::KeepWorldTransform,
			Snapshot.MeshAttachSocket);
		Mesh->SetRelativeTransform(Snapshot.MeshRelativeTransform);
	}
	else
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact could not restore missing Mesh attach parent on %s."),
			*GetNameSafe(GetOwner()));
	}

	Mesh->SetCollisionObjectType(Snapshot.MeshObjectType);
	Mesh->SetCollisionEnabled(Snapshot.MeshCollisionEnabled);
	Mesh->SetCollisionResponseToChannels(Snapshot.MeshResponses);
	Mesh->bPauseAnims = Snapshot.bMeshPauseAnims;
	Capsule->SetCollisionEnabled(Snapshot.CapsuleCollisionEnabled);
	Capsule->SetCollisionResponseToChannels(Snapshot.CapsuleResponses);

	Character->SetActorTransform(
		Snapshot.ActorTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	Movement->SetMovementMode(Snapshot.MovementMode, Snapshot.CustomMovementMode);
	Movement->Velocity = Snapshot.CharacterVelocity;

	PhysicsControl->SetComponentTickEnabled(false);
	ActiveRequest = FHeavyImpactPreparationRequest();
	ExpectedSourceActor = nullptr;
	ExpectedSourceComponent = nullptr;
	ActivePreparationTimeoutSeconds = 0.0f;
	Snapshot.Reset();
	PreparedEntryFrame = 0;
	SetComponentTickEnabled(false);
	SetState(EHeavyImpactState::Inactive);
}

/** 精确销毁本组件记录的名字，不调用会影响其他系统的 DestroyAll。 */
void UHeavyImpactResponseComponent::DestroyOwnedPhysicsControlRecords()
{
	if (!IsValid(PhysicsControl))
	{
		return;
	}

	if (!OwnedControlNames.IsEmpty())
	{
		PhysicsControl->DestroyControls(OwnedControlNames, true, false);
	}

	if (!OwnedBodyModifierNames.IsEmpty())
	{
		PhysicsControl->DestroyBodyModifiers(OwnedBodyModifierNames, true, false);
	}

	OwnedControlNames.Reset();
	OwnedBodyModifierNames.Reset();
}
