// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactResponseComponent.cpp
 * 职责：把机关预测和真实 Chaos 接触转换为受控全身物理飞行、环境落地与无 Tick 倒地。
 * 边界：所有冲量来自外部刚体接触；本文件不调用 AddImpulse、LaunchCharacter 或受击动画。
 */

#include "Components/Physics/HeavyImpactResponseComponent.h"

#include "Animation/HeavyImpactAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/PoseSnapshot.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/Physics/HeavyImpactTuningData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsControlAsset.h"
#include "PhysicsControlComponent.h"
#include "PhysicsControlData.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeavyImpact, Log, All);

namespace HeavyImpactRuntime
{
	/** At low frame rates, keep the valid ETA window wider than one frame without changing normal 30/60 FPS tuning. */
	constexpr float MaximumPreparationFrameMultiplier = 2.5f;
	constexpr float AbsoluteMaximumPreparationSeconds = 0.5f;
	constexpr float AbsoluteMaximumRecoveryAdjustmentCm = 60.0f;
	constexpr float RecoveryBlockedWarningSeconds = 5.0f;
	constexpr float RecoveryWatchdogPaddingSeconds = 1.0f;
	constexpr float RecoveryAsyncFrameDelaySeconds = 0.001f;
	const FName RecoverySlotName(TEXT("DefaultSlot"));

	/** 开发期严格 A/B：1 时保留同一状态机和 BodyModifier，只关闭全部 Physics Control 驱动。 */
	TAutoConsoleVariable<int32> CVarPureRagdollComparison(
		TEXT("demo.HeavyImpact.PureRagdoll"),
		0,
		TEXT("0: staged Physics Control (default). 1: pure ragdoll comparison for the next accepted HeavyImpact."),
		ECVF_Cheat);
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

	const FVector AcrossShoulders =
		Mesh->GetBoneLocation(Tuning->RightShoulderBone)
		- Mesh->GetBoneLocation(Tuning->LeftShoulderBone);
	const FVector TowardHead =
		Mesh->GetBoneLocation(Tuning->HeadBone)
		- Mesh->GetBoneLocation(Tuning->PelvisBone);
	const FVector UncalibratedBodyFront =
		FVector::CrossProduct(AcrossShoulders, TowardHead).GetSafeNormal();
	const FVector ActorForward =
		FVector::VectorPlaneProject(Character->GetActorForwardVector(), FVector::UpVector).GetSafeNormal();
	bBodyFrontCalibrationValid =
		!UncalibratedBodyFront.IsNearlyZero() && !ActorForward.IsNearlyZero();
	if (bBodyFrontCalibrationValid)
	{
		BodyFrontCalibrationSign =
			FVector::DotProduct(UncalibratedBodyFront, ActorForward) >= 0.0f ? 1.0f : -1.0f;
	}

	Mesh->OnComponentHit.AddUniqueDynamic(this, &UHeavyImpactResponseComponent::HandleMeshHit);
	Mesh->OnAnimInitialized.AddUniqueDynamic(
		this,
		&UHeavyImpactResponseComponent::HandleAnimInitialized);
	bInitialized = true;
}

/** 恢复未提交快照，解绑 Delegate，并精确销毁本组件创建的 PCA 记录。 */
void UHeavyImpactResponseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelRecoveryAsync(true);
	if (State == EHeavyImpactState::Prepared
		&& FalsePositiveRollback.bValid
		&& RecoveryBaseline.bValid)
	{
		RestoreSnapshotAfterFalsePositive();
	}

	if (IsValid(Mesh))
	{
		Mesh->OnComponentHit.RemoveDynamic(this, &UHeavyImpactResponseComponent::HandleMeshHit);
		Mesh->OnAnimInitialized.RemoveDynamic(
			this,
			&UHeavyImpactResponseComponent::HandleAnimInitialized);
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

/** 保留同一 Profile 的移动、碰撞、重力和 CCD；A/B 只改变关节驱动是否存在。 */
bool UHeavyImpactResponseComponent::ApplyPhysicalStage(
	const FName ProfileName,
	const FHeavyImpactControlStageTuning& StageTuning)
{
	if (!InvokeRequiredProfile(ProfileName))
	{
		return false;
	}

	if (bPureRagdollComparisonActive)
	{
		const bool bDisabled = PhysicsControl->SetControlsEnabled(
			OwnedControlNames,
			false,
			true,
			false);
		if (!bDisabled)
		{
			UE_LOG(
				LogHeavyImpact,
				Error,
				TEXT("HeavyImpact pure-ragdoll comparison failed to disable controls for profile %s on %s."),
				*ProfileName.ToString(),
				*GetNameSafe(GetOwner()));
		}
		return bDisabled;
	}

	FPhysicsControlMultiplier Multipliers;
	Multipliers.AngularStrengthMultiplier = StageTuning.AngularStrengthMultiplier;
	Multipliers.AngularDampingRatioMultiplier = StageTuning.AngularDampingRatioMultiplier;
	Multipliers.MaxTorqueMultiplier = StageTuning.MaxTorqueMultiplier;
	const bool bApplied = PhysicsControl->SetControlMultipliersInSet(
		TEXT("ParentSpace"),
		Multipliers,
		true);
	if (!bApplied)
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact failed to apply stage multipliers for profile %s on %s."),
			*ProfileName.ToString(),
			*GetNameSafe(GetOwner()));
	}
	return bApplied;
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

	if (!CapturePreContactState(FailureReason)
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
bool UHeavyImpactResponseComponent::CapturePreContactState(FString& OutReason)
{
	FalsePositiveRollback.Reset();
	RecoveryBaseline.Reset();
	FHeavyImpactFalsePositiveRollback NewRollback;
	FHeavyImpactRecoveryBaseline NewBaseline;

	const UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();
	if (!IsValid(PhysicsAsset)
		|| !IsValid(Mesh->GetAttachParent())
		|| Mesh->IsUsingAbsoluteLocation()
		|| Mesh->IsUsingAbsoluteRotation()
		|| Mesh->IsUsingAbsoluteScale())
	{
		OutReason = TEXT("Mesh has no PhysicsAsset/attach parent or uses absolute transform flags.");
		return false;
	}

	NewRollback.ActorTransform = Character->GetActorTransform();
	NewRollback.CharacterVelocity = Movement->Velocity;
	NewRollback.MovementMode = Movement->MovementMode;
	NewRollback.CustomMovementMode = Movement->CustomMovementMode;
	NewBaseline.MeshAttachParent = Mesh->GetAttachParent();
	NewBaseline.MeshAttachSocket = Mesh->GetAttachSocketName();
	NewBaseline.MeshRelativeTransform = Mesh->GetRelativeTransform();
	NewBaseline.bMeshPauseAnims = Mesh->bPauseAnims;
	NewBaseline.MeshCollisionEnabled = Mesh->GetCollisionEnabled();
	NewBaseline.MeshObjectType = Mesh->GetCollisionObjectType();
	NewBaseline.MeshResponses = Mesh->GetCollisionResponseToChannels();
	NewBaseline.bMeshBodyInstanceNotify = Mesh->BodyInstance.bNotifyRigidBodyCollision;
	NewBaseline.bMeshBodyInstanceCCD = Mesh->BodyInstance.bUseCCD;
	NewBaseline.CapsuleCollisionEnabled = Capsule->GetCollisionEnabled();
	NewBaseline.CapsuleResponses = Capsule->GetCollisionResponseToChannels();

	NewBaseline.Bodies.Reserve(PhysicsAsset->SkeletalBodySetups.Num());
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
			return false;
		}

		FHeavyImpactBodySnapshot& BodySnapshot = NewBaseline.Bodies.AddDefaulted_GetRef();
		BodySnapshot.BoneName = BodySetup->BoneName;
		// 保存公开配置位；UE5.8 的 IsInstanceSimulatingPhysics 内联依赖未从 Engine 模块导出。
		BodySnapshot.bWasSimulating = Body->bSimulatePhysics;
		if (BodySnapshot.bWasSimulating)
		{
			OutReason = FString::Printf(
				TEXT("Body %s was already simulating before HeavyImpact preparation."),
				*BodySetup->BoneName.ToString());
			return false;
		}
		BodySnapshot.bNotifyRigidBodyCollision = Body->bNotifyRigidBodyCollision;
		BodySnapshot.bUseCCD = Body->bUseCCD;
		BodySnapshot.PhysicsBlendWeight = Body->PhysicsBlendWeight;
		BodySnapshot.CollisionEnabled = Body->GetCollisionEnabled(false);
	}

	NewRollback.bValid = true;
	NewBaseline.bValid = true;
	FalsePositiveRollback = MoveTemp(NewRollback);
	RecoveryBaseline = MoveTemp(NewBaseline);
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

	bPureRagdollComparisonActive =
		HeavyImpactRuntime::CVarPureRagdollComparison.GetValueOnGameThread() == 1;
	if (bPureRagdollComparisonActive)
	{
		UE_LOG(
			LogHeavyImpact,
			Display,
			TEXT("HeavyImpact pure-ragdoll A/B armed for the accepted impact on %s."),
			*GetNameSafe(GetOwner()));
	}
	PhysicsControl->SetComponentTickEnabled(true);
	if (!ApplyPhysicalStage(Demo::HeavyImpact::ProfilePrepared, Tuning->PreparedControl))
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
void UHeavyImpactResponseComponent::HandleAnimInitialized()
{
	if (State != EHeavyImpactState::Recovering)
	{
		return;
	}

	UE_LOG(
		LogHeavyImpact,
		Error,
		TEXT("HeavyImpact AnimInstance was initialized during recovery on %s; completing safely."),
		*GetNameSafe(GetOwner()));
	CompleteRecovery(TEXT("AnimInstance was initialized during recovery."), true);
}

void UHeavyImpactResponseComponent::CommitRealImpact(
	const FHitResult& Hit,
	const FVector& NormalImpulse)
{
	// Once real Chaos contact commits, no later path may access the pre-impact Actor transform.
	FalsePositiveRollback.Reset();
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
	else if (!ApplyPhysicalStage(Demo::HeavyImpact::ProfileFlight, Tuning->FlightControl))
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
	PreparedEntryFrame = 0;
}

/** 重新开启 Flight 约束和 PostPhysics 判稳；接触冲量已由 Chaos 传递。 */
void UHeavyImpactResponseComponent::ResumeFromDownedHit(
	const FHitResult& Hit,
	const FVector& NormalImpulse)
{
	CancelRecoveryAsync(false);
	TotalCommittedSeconds = 0.0f;
	StableElapsedSeconds = 0.0f;
	bFreeFallbackInvoked = false;
	bPendingDownedSleep = false;
	bHardTimeoutReported = false;
	PhysicsControl->SetComponentTickEnabled(true);
	SetState(EHeavyImpactState::Simulating);
	SetComponentTickEnabled(true);
	Mesh->WakeAllRigidBodies();

	if (!ApplyPhysicalStage(Demo::HeavyImpact::ProfileFlight, Tuning->FlightControl))
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
		if (ApplyPhysicalStage(
			Demo::HeavyImpact::ProfileLandingRecovery,
			Tuning->LandingControl))
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
			if (ApplyPhysicalStage(Demo::HeavyImpact::ProfileFlight, Tuning->FlightControl))
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
	RecoveryBlockedElapsedSeconds = 0.0f;
	bRecoveryBlockedWarningLogged = false;
	ScheduleRecoveryAttempt(Tuning->RecoveryDelaySeconds);
	SetComponentTickEnabled(false);

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact downed sleep completed on %s."),
		*GetNameSafe(GetOwner()));
}

/** Prepared 超时的唯一回滚入口。 */
void UHeavyImpactResponseComponent::ScheduleRecoveryAttempt(const float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || State != EHeavyImpactState::Downed)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(RecoveryRetryTimer);
	RecoveryPhase = EHeavyImpactRecoveryPhase::WaitingForSpace;
	const uint32 TransactionSerial = ++RecoveryTransactionSerial;
	const FTimerDelegate AttemptDelegate = FTimerDelegate::CreateUObject(
		this,
		&UHeavyImpactResponseComponent::TryBeginRecovery,
		TransactionSerial);
	if (DelaySeconds <= 0.0f)
	{
		World->GetTimerManager().SetTimer(
			RecoveryRetryTimer,
			AttemptDelegate,
			HeavyImpactRuntime::RecoveryAsyncFrameDelaySeconds,
			false);
	}
	else
	{
		World->GetTimerManager().SetTimer(
			RecoveryRetryTimer,
			AttemptDelegate,
			DelaySeconds,
			false);
	}
}

void UHeavyImpactResponseComponent::TryBeginRecovery(const uint32 ExpectedTransactionSerial)
{
	if (State != EHeavyImpactState::Downed
		|| RecoveryPhase != EHeavyImpactRecoveryPhase::WaitingForSpace
		|| RecoveryTransactionSerial != ExpectedTransactionSerial)
	{
		return;
	}

	FHeavyImpactRecoveryPlan Plan;
	FString FailureReason;
	if (BuildRecoveryPlan(Plan, FailureReason)
		&& BeginPhysicalToAnimationHandoff(Plan, FailureReason))
	{
		return;
	}

	RecoveryBlockedElapsedSeconds += Tuning->RecoveryRetrySeconds;
	if (!bRecoveryBlockedWarningLogged
		&& RecoveryBlockedElapsedSeconds >= HeavyImpactRuntime::RecoveryBlockedWarningSeconds)
	{
		bRecoveryBlockedWarningLogged = true;
		UE_LOG(
			LogHeavyImpact,
			Warning,
			TEXT("HeavyImpact recovery remains blocked on %s: %s"),
			*GetNameSafe(GetOwner()),
			*FailureReason);
	}

	ScheduleRecoveryAttempt(Tuning->RecoveryRetrySeconds);
}

bool UHeavyImpactResponseComponent::BuildRecoveryPlan(
	FHeavyImpactRecoveryPlan& OutPlan,
	FString& OutReason)
{
	if (!RecoveryBaseline.bValid)
	{
		OutReason = TEXT("Committed recovery baseline is missing.");
		return false;
	}

	if (!DetermineRecoveryOrientation(OutPlan.bFaceUp, OutPlan.CapsuleRotation, OutReason))
	{
		return false;
	}

	OutPlan.Animation = OutPlan.bFaceUp
		? Tuning->GetUpFaceUpAnimation
		: Tuning->GetUpFaceDownAnimation;
	const USkeletalMesh* RuntimeSkeletalMesh = Mesh->GetSkeletalMeshAsset();
	if (!IsValid(RuntimeSkeletalMesh)
		|| !IsValid(OutPlan.Animation)
		|| OutPlan.Animation->GetSkeleton() != RuntimeSkeletalMesh->GetSkeleton()
		|| OutPlan.Animation->HasRootMotion()
		|| !FMath::IsFinite(OutPlan.Animation->RateScale)
		|| OutPlan.Animation->RateScale <= 0.0f)
	{
		OutReason = TEXT("Selected recovery sequence is not valid for the runtime Mesh Skeleton.");
		return false;
	}

	return TryFindRecoveryCapsuleLocation(
		OutPlan.CapsuleRotation,
		OutPlan.CapsuleLocation,
		OutReason);
}

bool UHeavyImpactResponseComponent::DetermineRecoveryOrientation(
	bool& bOutFaceUp,
	FRotator& OutRotation,
	FString& OutReason)
{
	const FVector Pelvis = Mesh->GetBoneLocation(Tuning->PelvisBone);
	const FVector TowardHead = Mesh->GetBoneLocation(Tuning->HeadBone) - Pelvis;
	const FVector AcrossShoulders =
		Mesh->GetBoneLocation(Tuning->RightShoulderBone)
		- Mesh->GetBoneLocation(Tuning->LeftShoulderBone);
	const FVector BodyFront =
		FVector::CrossProduct(AcrossShoulders, TowardHead).GetSafeNormal()
		* BodyFrontCalibrationSign;

	FVector HorizontalForward;
	if (bBodyFrontCalibrationValid && !BodyFront.IsNearlyZero())
	{
		bOutFaceUp = FVector::DotProduct(BodyFront, FVector::UpVector) >= 0.0f;
		const FVector HorizontalHeadDirection =
			FVector::VectorPlaneProject(TowardHead, FVector::UpVector).GetSafeNormal();
		HorizontalForward = bOutFaceUp ? -HorizontalHeadDirection : HorizontalHeadDirection;
	}
	else
	{
		bOutFaceUp = true;
	}

	if (HorizontalForward.IsNearlyZero())
	{
		HorizontalForward =
			FVector::VectorPlaneProject(Character->GetActorForwardVector(), FVector::UpVector).GetSafeNormal();
		if (!bRecoveryOrientationWarningLogged)
		{
			bRecoveryOrientationWarningLogged = true;
			UE_LOG(
				LogHeavyImpact,
				Warning,
				TEXT("HeavyImpact recovery orientation on %s used the current Actor yaw because the physical bone vectors were degenerate."),
				*GetNameSafe(GetOwner()));
		}
	}

	if (HorizontalForward.IsNearlyZero())
	{
		OutReason = TEXT("Could not derive a finite horizontal get-up direction.");
		return false;
	}

	const float YawOffset = bOutFaceUp
		? Tuning->FaceUpYawOffsetDegrees
		: Tuning->FaceDownYawOffsetDegrees;
	OutRotation = FRotator(0.0f, HorizontalForward.Rotation().Yaw + YawOffset, 0.0f);
	OutReason.Reset();
	return true;
}

bool UHeavyImpactResponseComponent::TryFindRecoveryCapsuleLocation(
	const FRotator& UprightRotation,
	FVector& OutLocation,
	FString& OutReason) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Character) || !IsValid(Movement) || !IsValid(Capsule))
	{
		OutReason = TEXT("Recovery placement dependencies are unavailable.");
		return false;
	}

	const FVector PelvisAnchor = Mesh->GetBoneLocation(Tuning->PelvisBone);
	const float MaximumAdjustment = FMath::Min(
		Tuning->MaxRecoveryHorizontalAdjustmentCm,
		HeavyImpactRuntime::AbsoluteMaximumRecoveryAdjustmentCm);

	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FCollisionShape StandingCapsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeavyImpactRecoveryPath), false, Character);
	const FCollisionResponseParams ResponseParams(RecoveryBaseline.CapsuleResponses);

	// Establish a geometry-free sweep start without requiring it to have a floor. This lets the
	// ring search reach a nearby ledge/floor while still refusing to sweep out of penetration.
	FVector ResolvedPathStart = Character->GetActorLocation();
	FRotator ResolvedPathRotation = UprightRotation;
	if (!World->FindTeleportSpot(Character, ResolvedPathStart, ResolvedPathRotation)
		|| FVector::VectorPlaneProject(ResolvedPathStart - PelvisAnchor, FVector::UpVector).Size()
			> MaximumAdjustment + UE_KINDA_SMALL_NUMBER
		|| World->OverlapBlockingTestByChannel(
			ResolvedPathStart,
			UprightRotation.Quaternion(),
			Capsule->GetCollisionObjectType(),
			StandingCapsule,
			QueryParams,
			ResponseParams))
	{
		OutReason = TEXT("The final physical pelvis has no trustworthy free capsule sweep start.");
		return false;
	}

	const FVector2D Directions[] = {
		FVector2D(1.0f, 0.0f),
		FVector2D(-1.0f, 0.0f),
		FVector2D(0.0f, 1.0f),
		FVector2D(0.0f, -1.0f),
		FVector2D(1.0f, 1.0f).GetSafeNormal(),
		FVector2D(1.0f, -1.0f).GetSafeNormal(),
		FVector2D(-1.0f, 1.0f).GetSafeNormal(),
		FVector2D(-1.0f, -1.0f).GetSafeNormal()
	};

	TArray<float, TInlineAllocator<4>> SearchRadii;
	SearchRadii.Add(0.0f);
	for (float SearchRadius = Tuning->RecoverySearchStepCm;
		SearchRadius < MaximumAdjustment - UE_KINDA_SMALL_NUMBER;
		SearchRadius += Tuning->RecoverySearchStepCm)
	{
		SearchRadii.Add(SearchRadius);
	}
	if (MaximumAdjustment > 0.0f)
	{
		SearchRadii.AddUnique(MaximumAdjustment);
	}

	for (const float SearchRadius : SearchRadii)
	{
		const int32 DirectionCount = FMath::IsNearlyZero(SearchRadius)
			? 1
			: UE_ARRAY_COUNT(Directions);
		for (int32 DirectionIndex = 0; DirectionIndex < DirectionCount; ++DirectionIndex)
		{
			const FVector2D Offset = FMath::IsNearlyZero(SearchRadius)
				? FVector2D::ZeroVector
				: Directions[DirectionIndex] * SearchRadius;
			FVector Candidate;
			if (!TryResolveRecoveryCandidate(
					PelvisAnchor,
					Offset,
					UprightRotation,
					MaximumAdjustment,
					Candidate))
			{
				continue;
			}

			if (!Candidate.Equals(ResolvedPathStart, 0.1f))
			{
				FHitResult PathHit;
				const bool bPathBlocked = World->SweepSingleByChannel(
					PathHit,
					ResolvedPathStart,
					Candidate,
					UprightRotation.Quaternion(),
					Capsule->GetCollisionObjectType(),
					StandingCapsule,
					QueryParams,
					ResponseParams);
				if (bPathBlocked || PathHit.bStartPenetrating)
				{
					continue;
				}
			}

			OutLocation = Candidate;
			OutReason.Reset();
			return true;
		}
	}

	OutReason = TEXT("No walkable, non-overlapping capsule location was reachable within 60 cm.");
	return false;
}

bool UHeavyImpactResponseComponent::TryResolveRecoveryCandidate(
	const FVector& PelvisAnchor,
	const FVector2D& HorizontalOffset,
	const FRotator& UprightRotation,
	const float MaximumAdjustment,
	FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FCollisionShape StandingCapsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	FVector ProbeLocation(
		PelvisAnchor.X + HorizontalOffset.X,
		PelvisAnchor.Y + HorizontalOffset.Y,
		PelvisAnchor.Z + HalfHeight);

	FFindFloorResult FloorResult;
	Movement->ComputeFloorDist(
		ProbeLocation,
		Tuning->GroundProbeDistance,
		Tuning->GroundProbeDistance,
		FloorResult,
		Radius);
	if (!FloorResult.IsWalkableFloor())
	{
		return false;
	}

	const float TargetFloorGap =
		(UCharacterMovementComponent::MIN_FLOOR_DIST
			+ UCharacterMovementComponent::MAX_FLOOR_DIST) * 0.5f;
	const float InitialFloorDistance = FloorResult.bLineTrace
		? FloorResult.LineDist
		: FloorResult.FloorDist;
	FVector Candidate = ProbeLocation
		- FVector::UpVector * (InitialFloorDistance - TargetFloorGap);
	FRotator CandidateRotation = UprightRotation;
	if (!World->FindTeleportSpot(Character, Candidate, CandidateRotation))
	{
		return false;
	}

	const FVector HorizontalDelta =
		FVector::VectorPlaneProject(Candidate - PelvisAnchor, FVector::UpVector);
	if (HorizontalDelta.Size() > MaximumAdjustment + UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FFindFloorResult AdjustedFloor;
	Movement->ComputeFloorDist(
		Candidate,
		Tuning->GroundProbeDistance,
		Tuning->GroundProbeDistance,
		AdjustedFloor,
		Radius);
	if (!AdjustedFloor.IsWalkableFloor())
	{
		return false;
	}
	const float AdjustedFloorDistance = AdjustedFloor.bLineTrace
		? AdjustedFloor.LineDist
		: AdjustedFloor.FloorDist;
	Candidate -= FVector::UpVector * (AdjustedFloorDistance - TargetFloorGap);

	const FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(HeavyImpactRecoveryCandidate),
		false,
		Character);
	const FCollisionResponseParams ResponseParams(RecoveryBaseline.CapsuleResponses);
	if (World->OverlapBlockingTestByChannel(
		Candidate,
		UprightRotation.Quaternion(),
		Capsule->GetCollisionObjectType(),
		StandingCapsule,
		QueryParams,
		ResponseParams))
	{
		return false;
	}

	OutLocation = Candidate;
	return true;
}

bool UHeavyImpactResponseComponent::CaptureRelocatedRecoveryPose(
	const FHeavyImpactRecoveryPlan& Plan,
	FPoseSnapshot& OutPose,
	FString& OutReason) const
{
	if (!Character->GetActorLocation().Equals(Plan.CapsuleLocation, 1.0f))
	{
		OutReason = TEXT("Capsule moved after recovery placement validation.");
		return false;
	}

	Mesh->SnapshotPose(OutPose);
	if (!OutPose.bIsValid || OutPose.LocalTransforms.IsEmpty())
	{
		OutReason = TEXT("Skeletal Mesh did not provide a valid physical pose snapshot.");
		return false;
	}

	USceneComponent* AttachParent = RecoveryBaseline.MeshAttachParent.Get();
	if (!IsValid(AttachParent))
	{
		OutReason = TEXT("Original Mesh attach parent no longer exists.");
		return false;
	}

	const FTransform PhysicalRootWorld =
		OutPose.LocalTransforms[0] * Mesh->GetComponentTransform();
	const FTransform DesiredMeshWorld =
		RecoveryBaseline.MeshRelativeTransform
		* AttachParent->GetSocketTransform(RecoveryBaseline.MeshAttachSocket, RTS_World);
	OutPose.LocalTransforms[0] = PhysicalRootWorld.GetRelativeTransform(DesiredMeshWorld);
	OutReason.Reset();
	return true;
}

bool UHeavyImpactResponseComponent::BeginPhysicalToAnimationHandoff(
	const FHeavyImpactRecoveryPlan& Plan,
	FString& OutReason)
{
	USceneComponent* RecoveryAttachParent = RecoveryBaseline.MeshAttachParent.Get();
	if (State != EHeavyImpactState::Downed
		|| !RecoveryBaseline.bValid
		|| !IsValid(RecoveryAttachParent)
		|| !RecoveryAttachParent->CanAttachAsChild(Mesh, RecoveryBaseline.MeshAttachSocket)
		|| !IsValid(Plan.Animation))
	{
		OutReason = TEXT("Recovery handoff prerequisites are no longer valid.");
		return false;
	}

	HeavyImpactAnimInstance = Cast<UHeavyImpactAnimInstance>(Mesh->GetAnimInstance());
	if (!IsValid(HeavyImpactAnimInstance))
	{
		OutReason = TEXT("Runtime AnimInstance is not UHeavyImpactAnimInstance.");
		return false;
	}
	const FTransform DownedActorTransform = Character->GetActorTransform();
	FHitResult PlacementHit;
	const bool bPlaced = Character->SetActorLocationAndRotation(
		Plan.CapsuleLocation,
		Plan.CapsuleRotation,
		true,
		&PlacementHit,
		ETeleportType::TeleportPhysics);
	if (!bPlaced
		|| !Character->GetActorLocation().Equals(Plan.CapsuleLocation, 1.0f))
	{
		Character->SetActorTransform(
			DownedActorTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		OutReason = TEXT("Validated standing Capsule placement failed during the handoff transaction.");
		return false;
	}

	FPoseSnapshot PhysicalPose;
	if (!CaptureRelocatedRecoveryPose(Plan, PhysicalPose, OutReason))
	{
		Character->SetActorTransform(
			DownedActorTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		return false;
	}
	if (!HeavyImpactAnimInstance->StoreHeavyImpactDownedPose(MoveTemp(PhysicalPose)))
	{
		Character->SetActorTransform(
			DownedActorTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		HeavyImpactAnimInstance->ClearHeavyImpactDownedPose();
		OutReason = TEXT("AnimInstance rejected the relocated physical pose snapshot.");
		return false;
	}

	PhysicsControl->SetComponentTickEnabled(true);
	if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfileInactive))
	{
		HeavyImpactAnimInstance->ClearHeavyImpactDownedPose();
		Character->SetActorTransform(
			DownedActorTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		PhysicsControl->SetComponentTickEnabled(false);
		OutReason = TEXT("Inactive Physics Control profile failed before handoff commit.");
		return false;
	}

	Mesh->SetAllBodiesSimulatePhysics(false);
	RestoreBodyBaseline(RecoveryBaseline);
	if (!RestoreCharacterShell(RecoveryBaseline))
	{
		HeavyImpactAnimInstance->ClearHeavyImpactDownedPose();
		Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Character->SetActorTransform(
			DownedActorTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->bPauseAnims = true;
		Mesh->SetCollisionObjectType(ECC_PhysicsBody);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Mesh->SetAllBodiesNotifyRigidBodyCollision(true);
		InvokeRequiredProfile(Demo::HeavyImpact::ProfileFreeFallback);
		Mesh->SetAllBodiesSimulatePhysics(true);
		Mesh->SetAllBodiesPhysicsBlendWeight(1.0f, false);
		Mesh->PutAllRigidBodiesToSleep();
		PhysicsControl->SetComponentTickEnabled(false);
		OutReason = TEXT("Mesh reattachment failed; physical Downed state was restored.");
		return false;
	}
	PhysicsControl->SetComponentTickEnabled(false);
	SetState(EHeavyImpactState::Recovering);
	RecoveryPhase = EHeavyImpactRecoveryPhase::PlayingMontage;
	ActiveRecoverySequence = Plan.Animation;

	// Evaluate the stored-pose branch immediately so attachment cannot expose a reference-pose frame.
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();

	const uint32 TransactionSerial = RecoveryTransactionSerial;
	GetWorld()->GetTimerManager().SetTimer(
		RecoveryMontageStartTimer,
		FTimerDelegate::CreateUObject(
			this,
			&UHeavyImpactResponseComponent::StartRecoveryMontage,
			TransactionSerial),
		HeavyImpactRuntime::RecoveryAsyncFrameDelaySeconds,
		false);
	SetComponentTickEnabled(false);
	OutReason.Reset();
	return true;
}

void UHeavyImpactResponseComponent::StartRecoveryMontage(
	const uint32 ExpectedTransactionSerial)
{
	if (!IsCurrentRecoveryTransaction(ExpectedTransactionSerial)
		|| Mesh->GetAnimInstance() != HeavyImpactAnimInstance
		|| !IsValid(ActiveRecoverySequence))
	{
		if (State == EHeavyImpactState::Recovering)
		{
			UE_LOG(
				LogHeavyImpact,
				Error,
				TEXT("HeavyImpact recovery Montage could not start on %s because the AnimInstance or sequence changed."),
				*GetNameSafe(GetOwner()));
			CompleteRecovery(TEXT("AnimInstance or recovery sequence changed before Montage start."));
		}
		return;
	}

	ActiveRecoveryMontage = HeavyImpactAnimInstance->PlaySlotAnimationAsDynamicMontage(
		ActiveRecoverySequence,
		HeavyImpactRuntime::RecoverySlotName,
		Tuning->RecoveryMontageBlendInSeconds,
		Tuning->RecoveryMontageBlendOutSeconds,
		Tuning->RecoveryPlayRate,
		1,
		-1.0f,
		0.0f);
	if (!IsValid(ActiveRecoveryMontage))
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact failed to create a dynamic get-up Montage on %s from sequence %s."),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(ActiveRecoverySequence));
		CompleteRecovery(TEXT("Dynamic get-up Montage failed after physical handoff."));
		return;
	}

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(
		this,
		&UHeavyImpactResponseComponent::HandleRecoveryMontageEnded);
	HeavyImpactAnimInstance->Montage_SetEndDelegate(
		MontageEndedDelegate,
		ActiveRecoveryMontage);

	GetWorld()->GetTimerManager().SetTimer(
		RecoveryPoseReleaseTimer,
		FTimerDelegate::CreateUObject(
			this,
			&UHeavyImpactResponseComponent::ReleaseRecoveryPoseSnapshot,
			ExpectedTransactionSerial),
		HeavyImpactRuntime::RecoveryAsyncFrameDelaySeconds,
		false);

	const float EffectiveRate = FMath::Max(
		FMath::Abs(ActiveRecoverySequence->RateScale * Tuning->RecoveryPlayRate),
		UE_KINDA_SMALL_NUMBER);
	const float WatchdogSeconds =
		ActiveRecoverySequence->GetPlayLength() / EffectiveRate
		+ Tuning->RecoveryMontageBlendInSeconds
		+ Tuning->RecoveryMontageBlendOutSeconds
		+ HeavyImpactRuntime::RecoveryWatchdogPaddingSeconds;
	GetWorld()->GetTimerManager().SetTimer(
		RecoveryMontageWatchdogTimer,
		FTimerDelegate::CreateUObject(
			this,
			&UHeavyImpactResponseComponent::HandleRecoveryMontageWatchdog,
			ExpectedTransactionSerial),
		FMath::Max(WatchdogSeconds, 1.0f),
		false);
}

void UHeavyImpactResponseComponent::ReleaseRecoveryPoseSnapshot(
	const uint32 ExpectedTransactionSerial)
{
	if (IsCurrentRecoveryTransaction(ExpectedTransactionSerial)
		&& IsValid(HeavyImpactAnimInstance)
		&& Mesh->GetAnimInstance() == HeavyImpactAnimInstance)
	{
		HeavyImpactAnimInstance->ReleaseHeavyImpactDownedPose();
		GetWorld()->GetTimerManager().SetTimer(
			RecoverySlotValidationTimer,
			FTimerDelegate::CreateUObject(
				this,
				&UHeavyImpactResponseComponent::ValidateRecoverySlotEvaluation,
				ExpectedTransactionSerial),
			HeavyImpactRuntime::RecoveryAsyncFrameDelaySeconds,
			false);
	}
}

void UHeavyImpactResponseComponent::ValidateRecoverySlotEvaluation(
	const uint32 ExpectedTransactionSerial)
{
	if (!IsCurrentRecoveryTransaction(ExpectedTransactionSerial)
		|| !IsValid(HeavyImpactAnimInstance)
		|| !IsValid(ActiveRecoveryMontage))
	{
		return;
	}

	const float SlotNodeWeight = HeavyImpactAnimInstance->GetSlotNodeGlobalWeight(
		HeavyImpactRuntime::RecoverySlotName);
	const float MontageWeight = HeavyImpactAnimInstance->GetSlotMontageGlobalWeight(
		HeavyImpactRuntime::RecoverySlotName);
	if (SlotNodeWeight <= UE_KINDA_SMALL_NUMBER
		|| MontageWeight <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact DefaultSlot was not evaluated by the AnimGraph on %s; completing without a silent fake success."),
			*GetNameSafe(GetOwner()));
		CompleteRecovery(TEXT("Recovery Slot was absent or not evaluated."), true);
	}
}

void UHeavyImpactResponseComponent::HandleRecoveryMontageWatchdog(
	const uint32 ExpectedTransactionSerial)
{
	if (!IsCurrentRecoveryTransaction(ExpectedTransactionSerial))
	{
		return;
	}

	UE_LOG(
		LogHeavyImpact,
		Error,
		TEXT("HeavyImpact recovery Montage watchdog expired on %s; completing at the validated ground position."),
		*GetNameSafe(GetOwner()));
	CompleteRecovery(TEXT("Recovery Montage end callback was not received."), true);
}

void UHeavyImpactResponseComponent::HandleRecoveryMontageEnded(
	UAnimMontage* Montage,
	const bool bInterrupted)
{
	if (State != EHeavyImpactState::Recovering
		|| Montage != ActiveRecoveryMontage)
	{
		return;
	}

	CompleteRecovery(
		bInterrupted
			? TEXT("Recovery Montage was interrupted; completed safely.")
			: TEXT("Recovery Montage completed."));
}

void UHeavyImpactResponseComponent::CompleteRecovery(
	const TCHAR* Reason,
	const bool bStopActiveMontage)
{
	if (State != EHeavyImpactState::Recovering)
	{
		return;
	}

	if (RecoveryBaseline.bValid)
	{
		Mesh->SetAllBodiesSimulatePhysics(false);
		RestoreBodyBaseline(RecoveryBaseline);
		RestoreCharacterShell(RecoveryBaseline);
	}

	CancelRecoveryAsync(bStopActiveMontage);
	Movement->StopMovementImmediately();
	Movement->Velocity = FVector::ZeroVector;
	FFindFloorResult FloorResult;
	Movement->FindFloor(Character->GetActorLocation(), FloorResult, false);
	if (FloorResult.IsWalkableFloor())
	{
		Movement->SetDefaultMovementMode();
	}
	else
	{
		Movement->SetMovementMode(MOVE_Falling);
	}

	ActiveRequest = FHeavyImpactPreparationRequest();
	ExpectedSourceActor = nullptr;
	ExpectedSourceComponent = nullptr;
	FalsePositiveRollback.Reset();
	RecoveryBaseline.Reset();
	bPureRagdollComparisonActive = false;
	PhysicsControl->SetComponentTickEnabled(false);
	SetComponentTickEnabled(false);
	SetState(EHeavyImpactState::Inactive);

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact recovery completed on %s: %s"),
		*GetNameSafe(GetOwner()),
		Reason);
}

void UHeavyImpactResponseComponent::CancelRecoveryAsync(const bool bStopActiveMontage)
{
	++RecoveryTransactionSerial;
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(RecoveryRetryTimer);
		TimerManager.ClearTimer(RecoveryMontageStartTimer);
		TimerManager.ClearTimer(RecoveryPoseReleaseTimer);
		TimerManager.ClearTimer(RecoverySlotValidationTimer);
		TimerManager.ClearTimer(RecoveryMontageWatchdogTimer);
	}
	ClearRecoveryAnimationState(bStopActiveMontage);
	RecoveryPhase = EHeavyImpactRecoveryPhase::None;
}

void UHeavyImpactResponseComponent::ClearRecoveryAnimationState(
	const bool bStopActiveMontage)
{
	if (IsValid(HeavyImpactAnimInstance) && IsValid(ActiveRecoveryMontage))
	{
		FOnMontageEnded EmptyDelegate;
		HeavyImpactAnimInstance->Montage_SetEndDelegate(EmptyDelegate, ActiveRecoveryMontage);
		if (bStopActiveMontage
			&& HeavyImpactAnimInstance->Montage_IsPlaying(ActiveRecoveryMontage))
		{
			HeavyImpactAnimInstance->Montage_Stop(
				Tuning ? Tuning->RecoveryMontageBlendOutSeconds : 0.0f,
				ActiveRecoveryMontage);
		}
	}

	if (IsValid(HeavyImpactAnimInstance))
	{
		HeavyImpactAnimInstance->ClearHeavyImpactDownedPose();
	}
	if (UHeavyImpactAnimInstance* CurrentAnimInstance =
		Cast<UHeavyImpactAnimInstance>(IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr);
		IsValid(CurrentAnimInstance) && CurrentAnimInstance != HeavyImpactAnimInstance)
	{
		CurrentAnimInstance->ClearHeavyImpactDownedPose();
	}

	ActiveRecoveryMontage = nullptr;
	ActiveRecoverySequence = nullptr;
	HeavyImpactAnimInstance = nullptr;
}

bool UHeavyImpactResponseComponent::IsCurrentRecoveryTransaction(
	const uint32 ExpectedTransactionSerial) const
{
	return State == EHeavyImpactState::Recovering
		&& RecoveryPhase == EHeavyImpactRecoveryPhase::PlayingMontage
		&& RecoveryTransactionSerial == ExpectedTransactionSerial;
}

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
void UHeavyImpactResponseComponent::RestoreBodyBaseline(
	const FHeavyImpactRecoveryBaseline& Baseline)
{
	Mesh->BodyInstance.SetInstanceNotifyRBCollision(Baseline.bMeshBodyInstanceNotify);
	Mesh->BodyInstance.SetUseCCD(Baseline.bMeshBodyInstanceCCD);

	for (const FHeavyImpactBodySnapshot& BodySnapshot : Baseline.Bodies)
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
		Body->SetInstanceSimulatePhysics(BodySnapshot.bWasSimulating, true, true);
		Body->PhysicsBlendWeight = BodySnapshot.PhysicsBlendWeight;
	}
}

/** 完整撤销未提交的物理准备，并恢复受击前 Actor Transform。 */
bool UHeavyImpactResponseComponent::RestoreCharacterShell(
	const FHeavyImpactRecoveryBaseline& Baseline)
{
	USceneComponent* AttachParent = Baseline.MeshAttachParent.Get();
	bool bAttached = false;
	if (IsValid(AttachParent))
	{
		bAttached = Mesh->AttachToComponent(
			AttachParent,
			FAttachmentTransformRules::KeepWorldTransform,
			Baseline.MeshAttachSocket);
		if (bAttached)
		{
			Mesh->SetRelativeTransform(
				Baseline.MeshRelativeTransform,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
		else
		{
			UE_LOG(
				LogHeavyImpact,
				Error,
				TEXT("HeavyImpact failed to reattach Mesh on %s."),
				*GetNameSafe(GetOwner()));
		}
	}
	else
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact could not restore missing Mesh attach parent on %s."),
			*GetNameSafe(GetOwner()));
	}

	Mesh->SetCollisionObjectType(Baseline.MeshObjectType);
	Mesh->SetCollisionEnabled(Baseline.MeshCollisionEnabled);
	Mesh->SetCollisionResponseToChannels(Baseline.MeshResponses);
	Mesh->bPauseAnims = Baseline.bMeshPauseAnims;
	Capsule->SetCollisionEnabled(Baseline.CapsuleCollisionEnabled);
	Capsule->SetCollisionResponseToChannels(Baseline.CapsuleResponses);
	return bAttached;
}

void UHeavyImpactResponseComponent::RestoreSnapshotAfterFalsePositive()
{
	if (!FalsePositiveRollback.bValid || !RecoveryBaseline.bValid)
	{
		FalsePositiveRollback.Reset();
		RecoveryBaseline.Reset();
		ActiveRequest = FHeavyImpactPreparationRequest();
		ExpectedSourceActor = nullptr;
		ExpectedSourceComponent = nullptr;
		ActivePreparationTimeoutSeconds = 0.0f;
		PreparedEntryFrame = 0;
		bPureRagdollComparisonActive = false;
		SetComponentTickEnabled(false);
		SetState(EHeavyImpactState::Inactive);
		return;
	}

	InvokeRequiredProfile(Demo::HeavyImpact::ProfileInactive);
	Mesh->SetAllBodiesSimulatePhysics(false);
	RestoreBodyBaseline(RecoveryBaseline);
	RestoreCharacterShell(RecoveryBaseline);

	Character->SetActorTransform(
		FalsePositiveRollback.ActorTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	Movement->SetMovementMode(
		FalsePositiveRollback.MovementMode,
		FalsePositiveRollback.CustomMovementMode);
	Movement->Velocity = FalsePositiveRollback.CharacterVelocity;

	PhysicsControl->SetComponentTickEnabled(false);
	ActiveRequest = FHeavyImpactPreparationRequest();
	ExpectedSourceActor = nullptr;
	ExpectedSourceComponent = nullptr;
	ActivePreparationTimeoutSeconds = 0.0f;
	FalsePositiveRollback.Reset();
	RecoveryBaseline.Reset();
	PreparedEntryFrame = 0;
	bPureRagdollComparisonActive = false;
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
