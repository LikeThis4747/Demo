// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactResponseComponent.cpp
 * 职责：把机关预测和真实 Chaos 接触转换为受控全身物理飞行、环境落地、姿势整理与安全起身。
 * 边界：接触式 Heavy 的冲量仍只来自 Chaos；独立径向入口只施加调用方已计算的速度改变量，不播放受击动画。
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
	constexpr float AbsoluteMaximumFallbackAdjustmentCm = 200.0f;
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
		&& PreparationRollback.bValid
		&& RecoveryBaseline.bValid)
	{
		RestoreFailedPreparationTransition();
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

	const bool bApplied = ApplyParentSpaceControlMultipliers(StageTuning);
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

/** 只改变 ParentSpace 角向追随强度；线性驱动继续保持 PCA 校验过的零值。 */
bool UHeavyImpactResponseComponent::ApplyParentSpaceControlMultipliers(
	const FHeavyImpactControlStageTuning& StageTuning,
	const float StrengthAndTorqueScale)
{
	if (!IsValid(PhysicsControl) || !FMath::IsFinite(StrengthAndTorqueScale))
	{
		return false;
	}

	const float SafeScale = FMath::Clamp(StrengthAndTorqueScale, 0.0f, 1.0f);
	FPhysicsControlMultiplier Multipliers;
	Multipliers.AngularStrengthMultiplier =
		StageTuning.AngularStrengthMultiplier * SafeScale;
	Multipliers.AngularDampingRatioMultiplier =
		StageTuning.AngularDampingRatioMultiplier;
	Multipliers.MaxTorqueMultiplier = StageTuning.MaxTorqueMultiplier * SafeScale;
	return PhysicsControl->SetControlMultipliersInSet(
		TEXT("ParentSpace"),
		Multipliers,
		true);
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

/** 去重后原子进入 Prepared；只允许在返回 Accepted 前恢复失败的转换。 */
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

	if (RefreshSameSourceProtectionIfActive(Request.SourceActor))
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

	// 让局部物理表现同步归还 Body；不得放到角色转发层，否则无效 Heavy 也会产生副作用。
	OnPreContactCaptureRequested.Broadcast(Request);

	if (!CapturePreContactState(FailureReason)
		|| !EnterPrepared(Request, AllowedMaximumSeconds, FailureReason))
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact prepare failed on %s: %s"),
			*GetNameSafe(GetOwner()),
			*FailureReason);
		RestoreFailedPreparationTransition();
		return EHeavyImpactPrepareResult::Invalid;
	}

	// Accepted 是游戏表现的不可回滚边界。之后即使预期 Hit 未到，也必须继续 Heavy。
	PreparationRollback.Reset();
	// 只有完整进入 Prepared 的请求进入去重缓存；预测或状态转换失败可用同一 ID 稍后重试。
	RecordRecentImpactId(Request.ImpactId);
	return EHeavyImpactPrepareResult::Accepted;
}

/** 爆炸不伪造接触源；只在 Inactive 时建立身体基线，其余状态复用当前 Heavy 事务。 */
EHeavyImpactPrepareResult UHeavyImpactResponseComponent::RequestRadialImpact(
	const FGuid& ImpactId,
	AActor* SourceActor,
	const FVector& VelocityChange)
{
	if (HasSeenImpactId(ImpactId))
	{
		return EHeavyImpactPrepareResult::Duplicate;
	}

	const bool bVelocityFinite = FMath::IsFinite(VelocityChange.X)
		&& FMath::IsFinite(VelocityChange.Y)
		&& FMath::IsFinite(VelocityChange.Z);
	if (!bInitialized
		|| !ImpactId.IsValid()
		|| !IsValid(SourceActor)
		|| SourceActor == GetOwner()
		|| !bVelocityFinite
		|| VelocityChange.IsNearlyZero())
	{
		return EHeavyImpactPrepareResult::Invalid;
	}

	// 起身恢复已接回动画身体；先复用现有安全收尾，再立即建立新的径向 Heavy 基线。
	if (State == EHeavyImpactState::Recovering)
	{
		CompleteRecovery(TEXT("Recovery was interrupted by a radial HeavyImpact."), true);
		if (State != EHeavyImpactState::Inactive)
		{
			return EHeavyImpactPrepareResult::Invalid;
		}
	}

	if (State == EHeavyImpactState::Inactive)
	{
		FHeavyImpactPreparationRequest CaptureSignal;
		CaptureSignal.ImpactId = ImpactId;
		CaptureSignal.SourceActor = SourceActor;
		CaptureSignal.SourceLinearVelocity = VelocityChange;
		OnPreContactCaptureRequested.Broadcast(CaptureSignal);

		FString FailureReason;
		if (!CapturePreContactState(FailureReason))
		{
			UE_LOG(LogHeavyImpact, Error,
				TEXT("Radial HeavyImpact capture failed on %s: %s"),
				*GetNameSafe(GetOwner()),
				*FailureReason);
			RestoreFailedPreparationTransition();
			return EHeavyImpactPrepareResult::Invalid;
		}

		ActiveRequest = FHeavyImpactPreparationRequest();
		ExpectedSourceActor = nullptr;
		ExpectedSourceComponent = nullptr;
		CommittedSourceActor = SourceActor;
		ActivePreparationTimeoutSeconds = 0.0f;
		bPhysicsBodyCollisionReleased = false;
		if (!EnterPreparedPhysicalState(FailureReason))
		{
			UE_LOG(LogHeavyImpact, Error,
				TEXT("Radial HeavyImpact prepare failed on %s: %s"),
				*GetNameSafe(GetOwner()),
				*FailureReason);
			RestoreFailedPreparationTransition();
			return EHeavyImpactPrepareResult::Invalid;
		}

		PreparationRollback.Reset();
	}
	else if (State == EHeavyImpactState::Downed)
	{
		ResumeFromDowned(SourceActor);
	}
	else if (State == EHeavyImpactState::Settling)
	{
		StableElapsedSeconds = 0.0f;
		TotalCommittedSeconds = 0.0f;
		CommittedSourceActor = SourceActor;
		Mesh->WakeAllRigidBodies();
		if (ApplyPhysicalStage(Demo::HeavyImpact::ProfileFlight, Tuning->FlightControl))
		{
			SetState(EHeavyImpactState::Simulating);
		}
		else
		{
			EnterFreeFallback(TEXT("Flight profile failed after radial HeavyImpact left Settling."));
		}
	}
	else if (State == EHeavyImpactState::Simulating)
	{
		CommittedSourceActor = SourceActor;
		StableElapsedSeconds = 0.0f;
		TotalCommittedSeconds = 0.0f;
	}

	PendingRadialVelocityChange += VelocityChange;
	PendingRadialSourceActor = SourceActor;
	RecordRecentImpactId(ImpactId);
	SetComponentTickEnabled(true);
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
	PreparationRollback.Reset();
	RecoveryBaseline.Reset();
	RecoveryBlockedStartTimeSeconds = -1.0;
	FHeavyImpactPreparationRollback NewRollback;
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

	NewRollback.CharacterVelocity = Movement->Velocity;
	NewRollback.MovementMode = Movement->MovementMode;
	NewRollback.CustomMovementMode = Movement->CustomMovementMode;
	NewBaseline.PreImpactCharacterTransform = Character->GetActorTransform();
	if (NewBaseline.PreImpactCharacterTransform.ContainsNaN()
		|| !NewBaseline.PreImpactCharacterTransform.GetRotation().IsNormalized())
	{
		OutReason = TEXT("Character transform was invalid before HeavyImpact preparation.");
		return false;
	}
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
	PreparationRollback = MoveTemp(NewRollback);
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

	const UWorld* World = GetWorld();
	const float DeltaSeconds = IsValid(World) ? FMath::Max(0.0f, World->GetDeltaSeconds()) : 0.0f;
	const float DeadlineMarginSeconds = FMath::Max(
		Tuning->MinimumPreparationLeadSeconds,
		DeltaSeconds * 1.25f);
	ActiveRequest = Request;
	ActivePreparationTimeoutSeconds = FMath::Min(
		AllowedMaximumSeconds,
		Request.EstimatedTimeToContactSeconds + DeadlineMarginSeconds);
	if (!FMath::IsFinite(ActivePreparationTimeoutSeconds)
		|| ActivePreparationTimeoutSeconds <= 0.0f)
	{
		OutReason = TEXT("Prepared transaction could not derive a valid ETA-based deadline.");
		return false;
	}
	ExpectedSourceActor = Request.SourceActor;
	ExpectedSourceComponent = Request.SourceComponent;
	CommittedSourceActor.Reset();
	bPhysicsBodyCollisionReleased = false;
	return EnterPreparedPhysicalState(OutReason);
}

/** 接触预测与径向爆炸共用的同步身体接管；来源和截止时间由各自入口提前写入。 */
bool UHeavyImpactResponseComponent::EnterPreparedPhysicalState(FString& OutReason)
{
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
	RecoveryBlockedStartTimeSeconds = -1.0;
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
		if (OtherActor == CommittedSourceActor.Get())
		{
			return;
		}

		if (IsValid(OtherActor)
			&& OtherActor != GetOwner()
			&& IsValid(OtherComponent)
			&& OtherComponent->IsAnySimulatingPhysics()
			&& NormalImpulse.SizeSquared()
				>= FMath::Square(Tuning->MinimumDownedReimpactImpulse))
		{
			ResumeFromDownedHit(OtherActor, Hit, NormalImpulse);
		}
		return;
	}

	if (ActiveRequest.ImpactId.IsValid()
		&& (State == EHeavyImpactState::Simulating || State == EHeavyImpactState::Settling)
		&& OtherActor == ExpectedSourceActor
		&& OtherComponent == ExpectedSourceComponent
		&& !NormalImpulse.IsNearlyZero(1.0f))
	{
		CommitLateAcceptedContact(Hit, NormalImpulse);
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

/** 恢复期 AnimInstance 被重建时安全结束当前事务。 */
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
	CommitAcceptedImpact(&Hit, NormalImpulse);
}

/** Accepted 是物理流程边界；只有真实 Hit 才保持既有 OnImpactCommitted 事件语义。 */
void UHeavyImpactResponseComponent::CommitAcceptedImpact(
	const FHitResult* Hit,
	const FVector& NormalImpulse,
	const bool bTriggeredByRadial)
{
	check(State == EHeavyImpactState::Prepared);
	const bool bHasRealContact = Hit != nullptr;
	if (bTriggeredByRadial && PendingRadialSourceActor.IsValid())
	{
		CommittedSourceActor = PendingRadialSourceActor;
	}
	else
	{
		CommittedSourceActor = ExpectedSourceActor;
	}
	bPhysicsBodyCollisionReleased = false;
	const bool bPreparedCrossedFrameBoundary = GFrameCounter > PreparedEntryFrame;
	if (!bPreparedCrossedFrameBoundary)
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact accepted response reached %s before Prepared crossed a full frame boundary; first physical step may be uncontrolled."),
			*GetNameSafe(GetOwner()));
	}

	SetState(EHeavyImpactState::Simulating);
	StableElapsedSeconds = 0.0f;
	TotalCommittedSeconds = 0.0f;
	if (!bPreparedCrossedFrameBoundary)
	{
		EnterFreeFallback(TEXT("Accepted response began before Prepared crossed a full frame boundary."));
	}
	else if (!ApplyPhysicalStage(Demo::HeavyImpact::ProfileFlight, Tuning->FlightControl))
	{
		EnterFreeFallback(TEXT("Flight profile failed after accepted preparation."));
	}
	ApplyPendingRadialImpact();

	if (bHasRealContact)
	{
		const FHeavyImpactPreparationRequest CommittedRequest = ActiveRequest;
		ActiveRequest = FHeavyImpactPreparationRequest();
		UE_LOG(
			LogHeavyImpact,
			Verbose,
			TEXT("HeavyImpact committed: %s, bone=%s, contact=%s, solver impulse=%s"),
			*GetNameSafe(GetOwner()),
			*Hit->BoneName.ToString(),
			*Hit->ImpactPoint.ToCompactString(),
			*NormalImpulse.ToCompactString());
		OnImpactCommitted.Broadcast(CommittedRequest);
	}
	else if (bTriggeredByRadial)
	{
		UE_LOG(
			LogHeavyImpact,
			Verbose,
			TEXT("HeavyImpact Prepared response on %s was promoted by a radial impact; any original expected contact remains eligible as a late contact."),
			*GetNameSafe(GetOwner()));
	}
	else
	{
		UE_LOG(
			LogHeavyImpact,
			Warning,
			TEXT("HeavyImpact accepted preparation timed out on %s; continuing the physical response without a fabricated impulse."),
			*GetNameSafe(GetOwner()));
	}

	ActivePreparationTimeoutSeconds = 0.0f;
	PreparedEntryFrame = 0;
}

void UHeavyImpactResponseComponent::CommitLateAcceptedContact(
	const FHitResult& Hit,
	const FVector& NormalImpulse)
{
	check(ActiveRequest.ImpactId.IsValid());
	TotalCommittedSeconds = 0.0f;
	StableElapsedSeconds = 0.0f;
	bPhysicsBodyCollisionReleased = false;
	Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	if (State == EHeavyImpactState::Settling)
	{
		if (ApplyPhysicalStage(Demo::HeavyImpact::ProfileFlight, Tuning->FlightControl))
		{
			SetState(EHeavyImpactState::Simulating);
		}
		else
		{
			EnterFreeFallback(TEXT("Flight profile failed after late accepted contact."));
		}
	}

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact received its late accepted contact: %s, bone=%s, contact=%s, solver impulse=%s"),
		*GetNameSafe(GetOwner()),
		*Hit.BoneName.ToString(),
		*Hit.ImpactPoint.ToCompactString(),
		*NormalImpulse.ToCompactString());
	const FHeavyImpactPreparationRequest CommittedRequest = ActiveRequest;
	ActiveRequest = FHeavyImpactPreparationRequest();
	CommittedSourceActor = ExpectedSourceActor;
	OnImpactCommitted.Broadcast(CommittedRequest);
}

/** 重新开启 Flight 约束和 PostPhysics 判稳；接触冲量已由 Chaos 传递。 */
void UHeavyImpactResponseComponent::ResumeFromDownedHit(
	AActor* SourceActor,
	const FHitResult& Hit,
	const FVector& NormalImpulse)
{
	ResumeFromDowned(SourceActor);

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact Downed body woke from real contact: %s bone=%s impulse=%s"),
		*GetNameSafe(GetOwner()),
		*Hit.BoneName.ToString(),
		*NormalImpulse.ToCompactString());
}

/** 唤醒 Downed 的同一物理身体；调用方决定冲量来自真实接触还是独立径向入口。 */
void UHeavyImpactResponseComponent::ResumeFromDowned(AActor* SourceActor)
{
	// The sleeping pose is still authoritative until the new impact resumes Chaos simulation.
	Mesh->bPauseAnims = true;
	CancelRecoveryAsync(false);
	TotalCommittedSeconds = 0.0f;
	StableElapsedSeconds = 0.0f;
	CommittedSourceActor = SourceActor;
	bPhysicsBodyCollisionReleased = false;
	bFreeFallbackInvoked = false;
	bPendingDownedSleep = false;
	ActiveRequest = FHeavyImpactPreparationRequest();
	PendingRadialVelocityChange = FVector::ZeroVector;
	PendingRadialSourceActor.Reset();
	RecoveryBlockedStartTimeSeconds = -1.0;
	PhysicsControl->SetComponentTickEnabled(true);
	SetState(EHeavyImpactState::Simulating);
	SetComponentTickEnabled(true);
	Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	Mesh->WakeAllRigidBodies();

	if (!ApplyPhysicalStage(Demo::HeavyImpact::ProfileFlight, Tuning->FlightControl))
	{
		EnterFreeFallback(TEXT("Flight profile failed after Downed was hit again."));
	}
}

/** 只在 Flight 已经接管全身物理后施加速度改变量；多次同帧请求先累加再消费。 */
void UHeavyImpactResponseComponent::ApplyPendingRadialImpact()
{
	if (PendingRadialVelocityChange.IsNearlyZero()
		|| State != EHeavyImpactState::Simulating
		|| !IsValid(Mesh)
		|| !Mesh->IsAnySimulatingPhysics())
	{
		return;
	}

	const FVector VelocityChange = PendingRadialVelocityChange;
	PendingRadialVelocityChange = FVector::ZeroVector;
	PendingRadialSourceActor.Reset();
	Mesh->WakeAllRigidBodies();
	Mesh->AddImpulse(VelocityChange, NAME_None, true);
	TotalCommittedSeconds = 0.0f;
	StableElapsedSeconds = 0.0f;

	UE_LOG(LogHeavyImpact, Verbose,
		TEXT("HeavyImpact applied radial velocity change on %s: %s"),
		*GetNameSafe(GetOwner()),
		*VelocityChange.ToCompactString());
}

/** 在物理事务状态下更新计时、外壳位置和稳定判断。 */
void UHeavyImpactResponseComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StateElapsedSeconds += DeltaTime;
	if (State == EHeavyImpactState::Recovering
		&& RecoveryPhase == EHeavyImpactRecoveryPhase::BlendingSnapshotToMontage)
	{
		UpdateRecoverySnapshotBlend(DeltaTime);
		return;
	}

	if (State == EHeavyImpactState::Downed && bPendingDownedSleep)
	{
		FinishPendingDownedSleep();
		return;
	}

	if (State == EHeavyImpactState::Prepared)
	{
		UpdatePhysicalFollow(DeltaTime);
		const bool bCrossedPreparedFrame = GFrameCounter > PreparedEntryFrame;
		const bool bRadialReady = bCrossedPreparedFrame
			&& !PendingRadialVelocityChange.IsNearlyZero();
		if (bRadialReady)
		{
			CommitAcceptedImpact(nullptr, FVector::ZeroVector, true);
		}
		else if (bCrossedPreparedFrame
			&& StateElapsedSeconds >= ActivePreparationTimeoutSeconds)
		{
			CommitAcceptedImpact(nullptr, FVector::ZeroVector);
		}
		return;
	}

	if (State == EHeavyImpactState::Simulating || State == EHeavyImpactState::Settling)
	{
		ApplyPendingRadialImpact();
		TotalCommittedSeconds += DeltaTime;
		ReleasePhysicsBodyCollisionIfDue();
		UpdatePhysicalFollow(DeltaTime, false, State == EHeavyImpactState::Settling);
		UpdateStability(DeltaTime);
	}
}

/** 保留首个 Chaos 接触的真实冲量后，避免 PhysicsBody 持续夹持；静态/动态关卡几何不变。 */
void UHeavyImpactResponseComponent::ReleasePhysicsBodyCollisionIfDue()
{
	if (bPhysicsBodyCollisionReleased
		|| !IsValid(Mesh)
		|| !IsValid(Tuning)
		|| TotalCommittedSeconds < Tuning->PhysicsBodyReleaseDelaySeconds)
	{
		return;
	}

	Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	bPhysicsBodyCollisionReleased = true;
	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact released PhysicsBody contact on %s after %.3f seconds; world geometry remains blocking."),
		*GetNameSafe(GetOwner()),
		TotalCommittedSeconds);
}

/** 同一机关仍在请求范围内时延长保护；过期、销毁或不同来源均不阻断新事务。 */
bool UHeavyImpactResponseComponent::RefreshSameSourceProtectionIfActive(
	AActor* RequestSourceActor)
{
	UWorld* World = GetWorld();
	AActor* ProtectedActor = ProtectedSourceActor.Get();
	if (!IsValid(World)
		|| !IsValid(Tuning)
		|| !IsValid(RequestSourceActor)
		|| RequestSourceActor != ProtectedActor)
	{
		return false;
	}

	const float CurrentTimeSeconds = World->GetTimeSeconds();
	if (CurrentTimeSeconds >= SameSourceProtectionUntilSeconds)
	{
		ProtectedSourceActor.Reset();
		SameSourceProtectionUntilSeconds = 0.0f;
		return false;
	}

	SameSourceProtectionUntilSeconds =
		CurrentTimeSeconds + Tuning->SameSourceProtectionSeconds;
	return true;
}

/** 从完成的真实事务建立同一来源保护；没有有效来源时明确清空旧保护。 */
void UHeavyImpactResponseComponent::BeginSameSourceProtection()
{
	UWorld* World = GetWorld();
	AActor* SourceActor = CommittedSourceActor.Get();
	if (!IsValid(World) || !IsValid(Tuning) || !IsValid(SourceActor))
	{
		ProtectedSourceActor.Reset();
		SameSourceProtectionUntilSeconds = 0.0f;
		return;
	}

	ProtectedSourceActor = SourceActor;
	SameSourceProtectionUntilSeconds =
		World->GetTimeSeconds() + Tuning->SameSourceProtectionSeconds;
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

/**
 * 只在最短模拟时间后，用连续低能量窗口判定物理运动已经结束。
 * 正常落地仍要求可行走支撑；但挂在墙边或几何夹缝中的低能量姿势也必须有有界出口。
 */
void UHeavyImpactResponseComponent::UpdateStability(const float DeltaTime)
{
	const FVector LinearVelocity = Mesh->GetPhysicsLinearVelocity(Tuning->PelvisBone);
	const FVector AngularVelocityDeg = Mesh->GetPhysicsAngularVelocityInDegrees(Tuning->PelvisBone);
	const float LinearSpeed = LinearVelocity.Size();
	const float AngularSpeed = AngularVelocityDeg.Size();
	const bool bSlowEnough = LinearSpeed <= Tuning->StableLinearSpeedCmPerSecond
		&& AngularSpeed <= Tuning->StableAngularSpeedDegPerSecond;
	const bool bClearlyActive = LinearSpeed > Tuning->StableLinearSpeedCmPerSecond * 1.5f
		|| AngularSpeed > Tuning->StableAngularSpeedDegPerSecond * 1.5f;

	FHitResult GroundHit;
	const bool bSupported = TryGetGroundSupport(GroundHit);
	const bool bMinimumTimeElapsed = TotalCommittedSeconds >= Tuning->MinimumSimulationSeconds;

	if (!bMinimumTimeElapsed)
	{
		return;
	}

	if (bSlowEnough)
	{
		StableElapsedSeconds += DeltaTime;
		if (bSupported
			&& !bFreeFallbackInvoked
			&& State == EHeavyImpactState::Simulating)
		{
			if (!ApplyPhysicalStage(
				Demo::HeavyImpact::ProfileLandingRecovery,
				Tuning->LandingControl))
			{
				EnterFreeFallback(TEXT("LandingRecovery profile failed."));
				return;
			}

			SetState(EHeavyImpactState::Settling);
		}

		if (StableElapsedSeconds >= Tuning->RequiredStableSeconds)
		{
			EnterDowned(
				bSupported
					? TEXT("Supported low-energy motion remained stable long enough.")
					: TEXT("Unsupported low-energy pose remained stalled long enough."));
			return;
		}
	}
	else if (!bSupported || bClearlyActive)
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
	else
	{
		// 接触求解的小幅噪声不应让已经积累的自然稳定进度瞬间归零。
		StableElapsedSeconds = FMath::Max(0.0f, StableElapsedSeconds - DeltaTime);
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
	const bool bStartingNewDownedTransaction = State != EHeavyImpactState::Downed;
	UpdatePhysicalFollow(0.0f, true, true);
	Mesh->bPauseAnims = true;
	CancelRecoveryAsync(false);
	if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfileFreeFallback))
	{
		EnterFreeFallback(TEXT("FreeFallback profile failed while entering Downed."));
	}

	ExpectedSourceActor = nullptr;
	ExpectedSourceComponent = nullptr;
	ActiveRequest = FHeavyImpactPreparationRequest();
	PendingRadialVelocityChange = FVector::ZeroVector;
	PendingRadialSourceActor.Reset();
	if (bStartingNewDownedTransaction)
	{
		RecoveryBlockedStartTimeSeconds = -1.0;
	}
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
	if (RecoveryBlockedStartTimeSeconds < 0.0)
	{
		if (const UWorld* World = GetWorld(); IsValid(World))
		{
			RecoveryBlockedStartTimeSeconds = World->GetTimeSeconds();
		}
	}
	ScheduleRecoveryAttempt(0.0f);
	SetComponentTickEnabled(false);

	UE_LOG(
		LogHeavyImpact,
		Verbose,
		TEXT("HeavyImpact downed sleep completed on %s."),
		*GetNameSafe(GetOwner()));
}

/** 为当前 Downed 事务安排一次可取消的安全空间重试。 */
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

	const UWorld* World = GetWorld();
	const double NowSeconds = IsValid(World) ? World->GetTimeSeconds() : 0.0;
	if (RecoveryBlockedStartTimeSeconds < 0.0)
	{
		RecoveryBlockedStartTimeSeconds = NowSeconds;
	}
	const double BlockedSeconds = FMath::Max(0.0, NowSeconds - RecoveryBlockedStartTimeSeconds);
	if (BlockedSeconds >= Tuning->MaximumRecoveryBlockedSeconds)
	{
		FHeavyImpactRecoveryPlan FallbackPlan;
		FString FallbackReason;
		const bool bFallbackPlanBuilt =
			BuildPreImpactFallbackRecoveryPlan(FallbackPlan, FallbackReason);
		if (bFallbackPlanBuilt
			&& BeginPhysicalToAnimationHandoff(FallbackPlan, FallbackReason))
		{
			UE_LOG(
				LogHeavyImpact,
				Warning,
				TEXT("HeavyImpact recovery on %s used a validated pre-impact-seeded location after %.2f blocked seconds."),
				*GetNameSafe(GetOwner()),
				BlockedSeconds);
			return;
		}

		if (bFallbackPlanBuilt)
		{
			FHitResult EmergencyPlacementHit;
			const bool bPlaced = Character->SetActorLocationAndRotation(
				FallbackPlan.CapsuleLocation,
				FallbackPlan.CapsuleRotation,
				true,
				&EmergencyPlacementHit,
				ETeleportType::TeleportPhysics);
			if (bPlaced
				&& Character->GetActorLocation().Equals(FallbackPlan.CapsuleLocation, 1.0f))
			{
				UE_LOG(
					LogHeavyImpact,
					Error,
					TEXT("HeavyImpact animation handoff failed on %s after the recovery deadline: %s. Restoring gameplay without the get-up animation."),
					*GetNameSafe(GetOwner()),
					*FallbackReason);
				SetState(EHeavyImpactState::Recovering);
				RecoveryPhase = EHeavyImpactRecoveryPhase::None;
				CompleteRecovery(TEXT("Infrastructure fallback restored gameplay at a validated location."), true);
				return;
			}
		}

		const FTransform LastResortTransform = RecoveryBaseline.PreImpactCharacterTransform;
		Character->SetActorTransform(
			LastResortTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact could not resolve a safe get-up capsule on %s after %.2f seconds: %s. Restoring the validated pre-impact character transform without a get-up animation."),
			*GetNameSafe(GetOwner()),
			BlockedSeconds,
			*FallbackReason);
		SetState(EHeavyImpactState::Recovering);
		RecoveryPhase = EHeavyImpactRecoveryPhase::None;
		CompleteRecovery(TEXT("Recovery deadline restored the pre-impact character transform."), true);
		return;
	}

	ScheduleRecoveryAttempt(Tuning->RecoveryRetrySeconds);
}

bool UHeavyImpactResponseComponent::BuildRecoveryPlan(
	FHeavyImpactRecoveryPlan& OutPlan,
	FString& OutReason)
{
	OutPlan = FHeavyImpactRecoveryPlan();
	if (!PopulateRecoveryAnimation(OutPlan, OutReason))
	{
		return false;
	}
	return TryFindRecoveryCapsuleLocation(
		OutPlan.CapsuleRotation,
		OutPlan.CapsuleLocation,
		OutReason);
}

bool UHeavyImpactResponseComponent::BuildPreImpactFallbackRecoveryPlan(
	FHeavyImpactRecoveryPlan& OutPlan,
	FString& OutReason)
{
	OutPlan = FHeavyImpactRecoveryPlan();
	if (!PopulateRecoveryAnimation(OutPlan, OutReason)
		|| RecoveryBaseline.PreImpactCharacterTransform.ContainsNaN()
		|| !RecoveryBaseline.PreImpactCharacterTransform.GetRotation().IsNormalized())
	{
		if (OutReason.IsEmpty())
		{
			OutReason = TEXT("Pre-impact Character transform is invalid.");
		}
		return false;
	}

	const FVector SeedAnchor = RecoveryBaseline.PreImpactCharacterTransform.GetLocation()
		- FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight();
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
	const float MaximumAdjustment = HeavyImpactRuntime::AbsoluteMaximumFallbackAdjustmentCm;
	TArray<float, TInlineAllocator<16>> SearchRadii;
	SearchRadii.Add(0.0f);
	for (float Radius = Tuning->RecoverySearchStepCm;
		Radius < MaximumAdjustment;
		Radius += Tuning->RecoverySearchStepCm)
	{
		SearchRadii.Add(Radius);
	}
	SearchRadii.AddUnique(MaximumAdjustment);

	for (const float Radius : SearchRadii)
	{
		const int32 DirectionCount = FMath::IsNearlyZero(Radius)
			? 1
			: UE_ARRAY_COUNT(Directions);
		for (int32 DirectionIndex = 0; DirectionIndex < DirectionCount; ++DirectionIndex)
		{
			const FVector2D Offset = FMath::IsNearlyZero(Radius)
				? FVector2D::ZeroVector
				: Directions[DirectionIndex] * FMath::Min(Radius, MaximumAdjustment);
			if (TryResolveRecoveryCandidate(
					SeedAnchor,
					Offset,
					OutPlan.CapsuleRotation,
					MaximumAdjustment,
					OutPlan.CapsuleLocation))
			{
				OutReason.Reset();
				return true;
			}
		}
	}

	OutReason = TEXT("Pre-impact safe-location seed no longer resolves to a walkable, non-overlapping standing capsule.");
	return false;
}

bool UHeavyImpactResponseComponent::PopulateRecoveryAnimation(
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
	OutPlan.AnimationStartTimeSeconds = OutPlan.bFaceUp
		? Tuning->FaceUpAnimationStartTimeSeconds
		: Tuning->FaceDownAnimationStartTimeSeconds;
	const USkeletalMesh* RuntimeSkeletalMesh = Mesh->GetSkeletalMeshAsset();
	if (!IsValid(RuntimeSkeletalMesh)
		|| !IsValid(OutPlan.Animation)
		|| OutPlan.Animation->GetSkeleton() != RuntimeSkeletalMesh->GetSkeleton()
		|| OutPlan.Animation->HasRootMotion()
		|| !FMath::IsFinite(OutPlan.Animation->RateScale)
		|| OutPlan.Animation->RateScale <= 0.0f
		|| !FMath::IsFinite(OutPlan.AnimationStartTimeSeconds)
		|| OutPlan.AnimationStartTimeSeconds < 0.0f
		|| OutPlan.AnimationStartTimeSeconds > OutPlan.Animation->GetPlayLength())
	{
		OutReason = TEXT("Selected recovery sequence is not valid for the runtime Mesh Skeleton.");
		return false;
	}

	OutReason.Reset();
	return true;
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

	// Prefer a full standing-capsule sweep whenever the prone pelvis has a nearby upright start.
	// A wall corner may legitimately have no such start, so this is not a gate for the bounded
	// search below; those cases use a smaller pelvis-clearance sweep and still require a fully
	// clear standing capsule at the final candidate.
	FVector ResolvedPathStart(
		PelvisAnchor.X,
		PelvisAnchor.Y,
		PelvisAnchor.Z + HalfHeight);
	FRotator ResolvedPathRotation = UprightRotation;
	const bool bHasStandingSweepStart =
		World->FindTeleportSpot(Character, ResolvedPathStart, ResolvedPathRotation)
		&& FVector::VectorPlaneProject(
			ResolvedPathStart - PelvisAnchor,
			FVector::UpVector).Size() <= 1.0f
		&& !World->OverlapBlockingTestByChannel(
			ResolvedPathStart,
			UprightRotation.Quaternion(),
			Capsule->GetCollisionObjectType(),
			StandingCapsule,
			QueryParams,
			ResponseParams);

	const float RelocationProbeRadius = FMath::Clamp(Radius * 0.25f, 4.0f, 12.0f);
	const FCollisionShape RelocationProbe =
		FCollisionShape::MakeSphere(RelocationProbeRadius);

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

			const FVector HorizontalDelta = FVector::VectorPlaneProject(
				Candidate - PelvisAnchor,
				FVector::UpVector);
			if (!HorizontalDelta.IsNearlyZero(0.1f))
			{
				FHitResult PathHit;
				bool bPathBlocked = false;
				if (bHasStandingSweepStart)
				{
					bPathBlocked = World->SweepSingleByChannel(
						PathHit,
						ResolvedPathStart,
						Candidate,
						UprightRotation.Quaternion(),
						Capsule->GetCollisionObjectType(),
						StandingCapsule,
						QueryParams,
						ResponseParams);
				}
				else
				{
					const float PathHeight = FMath::Max(
						PelvisAnchor.Z,
						Candidate.Z - HalfHeight + RelocationProbeRadius);
					const FVector ProbeStart(
						PelvisAnchor.X,
						PelvisAnchor.Y,
						PathHeight);
					const FVector ProbeEnd(
						Candidate.X,
						Candidate.Y,
						PathHeight);
					bPathBlocked = World->OverlapBlockingTestByChannel(
						ProbeStart,
						FQuat::Identity,
						Capsule->GetCollisionObjectType(),
						RelocationProbe,
						QueryParams,
						ResponseParams)
						|| World->SweepSingleByChannel(
							PathHit,
							ProbeStart,
							ProbeEnd,
							FQuat::Identity,
							Capsule->GetCollisionObjectType(),
							RelocationProbe,
							QueryParams,
							ResponseParams);
				}
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

	OutReason = FString::Printf(
		TEXT("No walkable, non-overlapping capsule location was reachable within %.0f cm without crossing blocking geometry."),
		MaximumAdjustment);
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
	const bool bDownedHandoff =
		State == EHeavyImpactState::Downed
		&& RecoveryPhase == EHeavyImpactRecoveryPhase::WaitingForSpace;
	USceneComponent* RecoveryAttachParent = RecoveryBaseline.MeshAttachParent.Get();
	if (!bDownedHandoff
		|| !RecoveryBaseline.bValid
		|| !IsValid(RecoveryAttachParent)
		|| !RecoveryAttachParent->CanAttachAsChild(Mesh, RecoveryBaseline.MeshAttachSocket)
		|| !IsValid(Plan.Animation)
		|| !FMath::IsFinite(Plan.AnimationStartTimeSeconds)
		|| Plan.AnimationStartTimeSeconds < 0.0f
		|| Plan.AnimationStartTimeSeconds > Plan.Animation->GetPlayLength())
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
	if (!StartRecoveryMontageNow(Plan, OutReason))
	{
		HeavyImpactAnimInstance->ClearHeavyImpactDownedPose();
		Character->SetActorTransform(
			DownedActorTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		return false;
	}

	PhysicsControl->SetComponentTickEnabled(true);
	if (!InvokeRequiredProfile(Demo::HeavyImpact::ProfileInactive))
	{
		ClearRecoveryAnimationState(true);
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
		ClearRecoveryAnimationState(true);
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
		Mesh->SetCollisionResponseToChannel(
			ECC_PhysicsBody,
			bPhysicsBodyCollisionReleased ? ECR_Ignore : ECR_Block);
		Mesh->SetAllBodiesNotifyRigidBodyCollision(true);
		InvokeRequiredProfile(Demo::HeavyImpact::ProfileFreeFallback);
		Mesh->SetAllBodiesSimulatePhysics(true);
		Mesh->SetAllBodiesPhysicsBlendWeight(1.0f, false);
		Mesh->PutAllRigidBodiesToSleep();
		PhysicsControl->SetComponentTickEnabled(false);
		OutReason = TEXT("Mesh reattachment failed; physical Downed state was restored.");
		EnterDowned(*OutReason);
		return false;
	}
	PhysicsControl->SetComponentTickEnabled(false);
	SetState(EHeavyImpactState::Recovering);
	RecoveryPhase = EHeavyImpactRecoveryPhase::BlendingSnapshotToMontage;
	RecoveryBlendElapsedSeconds = 0.0f;
	const uint32 TransactionSerial = ++RecoveryTransactionSerial;

	// Evaluate Alpha=0 immediately so attachment cannot expose Idle or a reference-pose frame.
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();

	const float EffectiveRate = FMath::Max(
		FMath::Abs(ActiveRecoverySequence->RateScale * Tuning->RecoveryPlayRate),
		UE_KINDA_SMALL_NUMBER);
	const float RemainingSequenceSeconds = FMath::Max(
		ActiveRecoverySequence->GetPlayLength()
			- ActiveRecoveryAnimationStartTimeSeconds,
		0.0f);
	const float WatchdogSeconds =
		RemainingSequenceSeconds / EffectiveRate
		+ Tuning->RecoverySnapshotBlendSeconds
		+ Tuning->RecoveryMontageBlendOutSeconds
		+ HeavyImpactRuntime::RecoveryWatchdogPaddingSeconds;
	GetWorld()->GetTimerManager().SetTimer(
		RecoveryMontageWatchdogTimer,
		FTimerDelegate::CreateUObject(
			this,
			&UHeavyImpactResponseComponent::HandleRecoveryMontageWatchdog,
			TransactionSerial),
		FMath::Max(WatchdogSeconds, 1.0f),
		false);
	SetComponentTickEnabled(true);
	OutReason.Reset();
	return true;
}

/** 同步创建起身 Montage；显式 Snapshot Alpha 是唯一淡入，Montage 自身 Blend In 固定为零。 */
bool UHeavyImpactResponseComponent::StartRecoveryMontageNow(
	const FHeavyImpactRecoveryPlan& Plan,
	FString& OutReason)
{
	if (Mesh->GetAnimInstance() != HeavyImpactAnimInstance
		|| !IsValid(Plan.Animation))
	{
		OutReason = TEXT("AnimInstance or recovery sequence changed before Montage start.");
		return false;
	}

	ActiveRecoverySequence = Plan.Animation;
	ActiveRecoveryAnimationStartTimeSeconds = Plan.AnimationStartTimeSeconds;
	ActiveRecoveryMontage = HeavyImpactAnimInstance->PlaySlotAnimationAsDynamicMontage(
		ActiveRecoverySequence,
		HeavyImpactRuntime::RecoverySlotName,
		0.0f,
		Tuning->RecoveryMontageBlendOutSeconds,
		Tuning->RecoveryPlayRate,
		1,
		-1.0f,
		ActiveRecoveryAnimationStartTimeSeconds);
	if (!IsValid(ActiveRecoveryMontage))
	{
		UE_LOG(
			LogHeavyImpact,
			Error,
			TEXT("HeavyImpact failed to create a dynamic get-up Montage on %s from sequence %s."),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(ActiveRecoverySequence));
		ActiveRecoverySequence = nullptr;
		ActiveRecoveryAnimationStartTimeSeconds = 0.0f;
		OutReason = TEXT("Dynamic get-up Montage could not be created before physical handoff.");
		return false;
	}

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(
		this,
		&UHeavyImpactResponseComponent::HandleRecoveryMontageEnded);
	HeavyImpactAnimInstance->Montage_SetEndDelegate(
		MontageEndedDelegate,
		ActiveRecoveryMontage);
	OutReason.Reset();
	return true;
}

/** 用唯一 Alpha 在短时间内从当前物理 Snapshot 过渡到已经播放的起身 Slot。 */
void UHeavyImpactResponseComponent::UpdateRecoverySnapshotBlend(const float DeltaTime)
{
	if (State != EHeavyImpactState::Recovering
		|| RecoveryPhase != EHeavyImpactRecoveryPhase::BlendingSnapshotToMontage)
	{
		return;
	}
	if (!IsValid(HeavyImpactAnimInstance)
		|| Mesh->GetAnimInstance() != HeavyImpactAnimInstance)
	{
		CompleteRecovery(TEXT("AnimInstance changed during Snapshot recovery blend."), true);
		return;
	}

	RecoveryBlendElapsedSeconds += DeltaTime;
	const float RawAlpha = FMath::Clamp(
		RecoveryBlendElapsedSeconds / Tuning->RecoverySnapshotBlendSeconds,
		0.0f,
		1.0f);
	HeavyImpactAnimInstance->SetHeavyImpactRecoveryBlendAlpha(
		FMath::SmoothStep(0.0f, 1.0f, RawAlpha));
	if (RawAlpha < 1.0f)
	{
		return;
	}

	HeavyImpactAnimInstance->ClearHeavyImpactDownedPose();
	RecoveryPhase = EHeavyImpactRecoveryPhase::PlayingMontage;
	const uint32 TransactionSerial = RecoveryTransactionSerial;
	GetWorld()->GetTimerManager().SetTimer(
		RecoverySlotValidationTimer,
		FTimerDelegate::CreateUObject(
			this,
			&UHeavyImpactResponseComponent::ValidateRecoverySlotEvaluation,
			TransactionSerial),
		HeavyImpactRuntime::RecoveryAsyncFrameDelaySeconds,
		false);
	SetComponentTickEnabled(false);
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

	BeginSameSourceProtection();

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
	CommittedSourceActor.Reset();
	PendingRadialVelocityChange = FVector::ZeroVector;
	PendingRadialSourceActor.Reset();
	PreparationRollback.Reset();
	RecoveryBaseline.Reset();
	bPureRagdollComparisonActive = false;
	bPhysicsBodyCollisionReleased = false;
	RecoveryBlockedStartTimeSeconds = -1.0;
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
			// This path is an abort/rollback. The dynamic Montage already owns the normal
			// configured blend-out, while an aborted paused Montage must disappear immediately.
			HeavyImpactAnimInstance->Montage_Stop(0.0f, ActiveRecoveryMontage);
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
	ActiveRecoveryAnimationStartTimeSeconds = 0.0f;
	RecoveryBlendElapsedSeconds = 0.0f;
	HeavyImpactAnimInstance = nullptr;
}

bool UHeavyImpactResponseComponent::IsCurrentRecoveryTransaction(
	const uint32 ExpectedTransactionSerial) const
{
	return State == EHeavyImpactState::Recovering
		&& (RecoveryPhase == EHeavyImpactRecoveryPhase::BlendingSnapshotToMontage
			|| RecoveryPhase == EHeavyImpactRecoveryPhase::PlayingMontage)
		&& RecoveryTransactionSerial == ExpectedTransactionSerial;
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

/** 恢复角色外壳；调用者决定是否需要恢复准备前 Transform。 */
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

void UHeavyImpactResponseComponent::RestoreFailedPreparationTransition()
{
	CancelRecoveryAsync(false);
	if (!PreparationRollback.bValid || !RecoveryBaseline.bValid)
	{
		PreparationRollback.Reset();
		RecoveryBaseline.Reset();
		ActiveRequest = FHeavyImpactPreparationRequest();
		ExpectedSourceActor = nullptr;
		ExpectedSourceComponent = nullptr;
		CommittedSourceActor.Reset();
		PendingRadialVelocityChange = FVector::ZeroVector;
		PendingRadialSourceActor.Reset();
		ActivePreparationTimeoutSeconds = 0.0f;
		PreparedEntryFrame = 0;
		bPureRagdollComparisonActive = false;
		bPhysicsBodyCollisionReleased = false;
		RecoveryBlockedStartTimeSeconds = -1.0;
		SetComponentTickEnabled(false);
		SetState(EHeavyImpactState::Inactive);
		return;
	}

	InvokeRequiredProfile(Demo::HeavyImpact::ProfileInactive);
	Mesh->SetAllBodiesSimulatePhysics(false);
	RestoreBodyBaseline(RecoveryBaseline);
	RestoreCharacterShell(RecoveryBaseline);

	Character->SetActorTransform(
		RecoveryBaseline.PreImpactCharacterTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	Movement->SetMovementMode(
		PreparationRollback.MovementMode,
		PreparationRollback.CustomMovementMode);
	Movement->Velocity = PreparationRollback.CharacterVelocity;

	PhysicsControl->SetComponentTickEnabled(false);
	ActiveRequest = FHeavyImpactPreparationRequest();
	ExpectedSourceActor = nullptr;
	ExpectedSourceComponent = nullptr;
	CommittedSourceActor.Reset();
	PendingRadialVelocityChange = FVector::ZeroVector;
	PendingRadialSourceActor.Reset();
	ActivePreparationTimeoutSeconds = 0.0f;
	PreparationRollback.Reset();
	RecoveryBaseline.Reset();
	PreparedEntryFrame = 0;
	bPureRagdollComparisonActive = false;
	bPhysicsBodyCollisionReleased = false;
	RecoveryBlockedStartTimeSeconds = -1.0;
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
