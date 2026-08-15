// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ElectromagneticGrabComponent.cpp
 * 职责：执行有界屏幕选取、确定性曲线吸取与 UE Physics Handle 持有，并在所有退出路径恢复临时设置。
 * 边界：Chaos 负责碰撞、旋转和积分；正式投掷碰撞事务属于 MagneticObject，破碎组件只提供监听时长与抓取守卫。
 * 状态 Owner：本组件独占当前持有引用、吸取阶段、临时覆盖、释放锁和安全计时；不存在组件内参数兜底。
 * 接口约束：任何未来新增抓取入口都必须先检查破碎守卫，再在保存抓取快照前完整 DisarmThrownImpact。
 */

#include "Components/Magnetism/ElectromagneticGrabComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/Magnetism/MagneticObjectComponent.h"
#include "Components/Magnetism/MagneticThrowBreakComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Data/Magnetism/MagneticGrabTuningData.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Physics/DemoCollisionChannels.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeMagneticGrab, Log, All);

namespace UE::ZeroEscape::Magnetism
{
	/** 选取、持有遮挡和投掷瞄准统一使用 Visibility 通道，作为不可调的交互契约。 */
	constexpr ECollisionChannel InteractionTraceChannel = ECC_Visibility;

	/** 屏幕轮廓距离完成评分后，世界距离项所占的固定相对权重。 */
	constexpr float WorldDistanceScoreWeight = 0.35f;

	/** 每个道具优先级点降低的固定分数，用于处理屏幕轮廓重叠时的歧义。 */
	constexpr float PriorityScoreWeight = 0.05f;
}

/** 创建可按需 Tick 的组件；默认关闭 Tick，只有成功持有刚体后才开启。 */
UElectromagneticGrabComponent::UElectromagneticGrabComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

/** 保存角色装配引用，校验唯一 Tuning 资产，声明 PrePhysics 顺序并应用 Physics Handle 参数。 */
void UElectromagneticGrabComponent::Configure(UPhysicsHandleComponent* InPhysicsHandle, UCameraComponent* InViewCamera)
{
	if (GrabPhase != EGrabPhase::None || HeldComponent.IsValid() || bHandleTargetInterpolationOverridden)
	{
		ReleaseHeldObject(false);
	}
	else
	{
		RestoreHandleTargetInterpolation();
	}

	if (IsValid(PhysicsHandle))
	{
		PhysicsHandle->RemoveTickPrerequisiteComponent(this);
	}

	PhysicsHandle = InPhysicsHandle;
	ViewCamera = InViewCamera;
	bConfigurationReady = false;

	if (!IsValid(PhysicsHandle) || !IsValid(ViewCamera))
	{
		UE_LOG(
			LogZeroEscapeMagneticGrab,
			Error,
			TEXT("%s 磁力组件缺少 PhysicsHandle 或 ViewCamera，功能已停用。"),
			*GetNameSafe(GetOwner()));
		return;
	}

	if (!IsValid(TuningData))
	{
		UE_LOG(
			LogZeroEscapeMagneticGrab,
			Error,
			TEXT("%s 的 ElectromagneticGrab 尚未指定 MagneticGrabTuningData，磁力功能已停用。"),
			*GetNameSafe(GetOwner()));
		return;
	}

	FString ConfigurationError;
	if (!TuningData->IsConfigured(ConfigurationError))
	{
		UE_LOG(
			LogZeroEscapeMagneticGrab,
			Error,
			TEXT("%s 的磁力配置无效：%s"),
			*GetNameSafe(GetOwner()),
			*ConfigurationError);
		return;
	}

	PhysicsHandle->SetLinearStiffness(TuningData->HandleLinearStiffness);
	PhysicsHandle->SetLinearDamping(TuningData->HandleLinearDamping);
	PhysicsHandle->SetInterpolationSpeed(TuningData->HandleInterpolationSpeed);
	PhysicsHandle->AddTickPrerequisiteComponent(this);
	AvailableExplosionCharges = TuningData->InitialExplosionCharges;
	SetExplosionModeActive(false);
	bConfigurationReady = true;
}

/** 按下时只选取一次，持有过程中准星经过其他物体不会改变当前目标。 */
void UElectromagneticGrabComponent::BeginGrabInput()
{
	if (!IsConfigurationReady() || bAwaitingGrabRelease || IsHoldingObject())
	{
		return;
	}

	UMagneticObjectComponent* MagneticObject = nullptr;
	if (UPrimitiveComponent* CandidateComponent = FindBestCandidate(MagneticObject))
	{
		GrabCandidate(CandidateComponent, MagneticObject);
	}
}

/** 松开既是普通放下，也是投掷或安全释放后重新允许下一次抓取的显式复位。 */
void UElectromagneticGrabComponent::EndGrabInput()
{
	bAwaitingGrabRelease = false;

	if (IsHoldingObject())
	{
		ReleaseHeldObject(false);
	}
}

/** 先恢复刚体临时设置，再施加速度变化冲量，使允许质量范围内的基础投掷速度可预测。 */
void UElectromagneticGrabComponent::ThrowHeldObject()
{
	UPrimitiveComponent* ComponentToThrow = HeldComponent.Get();
	if (!IsConfigurationReady() || !IsValid(ComponentToThrow) || !IsHoldingObject())
	{
		return;
	}

	UMagneticObjectComponent* MagneticObject = HeldMagneticObject.Get();
	UMagneticThrowBreakComponent* BreakComponent = IsValid(ComponentToThrow->GetOwner())
		? ComponentToThrow->GetOwner()->FindComponentByClass<UMagneticThrowBreakComponent>()
		: nullptr;
	const float BreakMonitoringSeconds = IsValid(BreakComponent)
		? BreakComponent->GetFormalThrowMonitoringSeconds(ComponentToThrow)
		: 0.0f;
	const float SpeedMultiplier = IsValid(MagneticObject) ? MagneticObject->ThrowSpeedMultiplier : 1.0f;
	const FVector CenterOfMass = ComponentToThrow->GetCenterOfMass();
	const FVector ThrowDirection = (CalculateAimPoint() - CenterOfMass).GetSafeNormal();
	const FVector DesiredVelocity = ThrowDirection * TuningData->ThrowSpeed * SpeedMultiplier;
	const FVector ExistingVelocity = ComponentToThrow->GetPhysicsLinearVelocity();
	UMagneticGrabTuningData* ExplosionTuningForThrow =
		bExplosionModeActive && AvailableExplosionCharges > 0 ? TuningData.Get() : nullptr;

	ReleaseHeldObject(true);

	if (IsValid(ComponentToThrow) && !ThrowDirection.IsNearlyZero())
	{
		if (IsValid(MagneticObject))
		{
			const bool bArmed = MagneticObject->ArmThrownImpact(
				ComponentToThrow,
				GetOwner(),
				TuningData->ThrownWeaponActiveDuration,
				BreakMonitoringSeconds,
				ExplosionTuningForThrow);
			if (bArmed && IsValid(ExplosionTuningForThrow))
			{
				AvailableExplosionCharges = FMath::Max(0, AvailableExplosionCharges - 1);
			}
		}

		// 使用速度变化冲量而非固定冲量，让策划填写的目标速度不随允许质量线性衰减。
		ComponentToThrow->AddImpulse(DesiredVelocity - ExistingVelocity, NAME_None, true);
	}
}

/** E 只切换持有状态；次数只在 ThrowHeldObject 成功武装后结算。 */
void UElectromagneticGrabComponent::ToggleExplosionMode()
{
	if (!IsConfigurationReady() || !IsHoldingObject())
	{
		return;
	}

	if (!bExplosionModeActive)
	{
		if (AvailableExplosionCharges <= 0)
		{
			return;
		}

		const UWorld* World = GetWorld();
		ExplosionModeActivatedWorldTimeSeconds = IsValid(World)
			? static_cast<double>(World->GetTimeSeconds())
			: 0.0;
		SetExplosionModeActive(true);
		return;
	}

	const UWorld* World = GetWorld();
	const double CurrentWorldTimeSeconds = IsValid(World)
		? static_cast<double>(World->GetTimeSeconds())
		: ExplosionModeActivatedWorldTimeSeconds;
	if (CurrentWorldTimeSeconds - ExplosionModeActivatedWorldTimeSeconds
		< static_cast<double>(TuningData->ExplosionModeCancelLockSeconds))
	{
		return;
	}

	SetExplosionModeActive(false);
}

/** 真实重冲击只中断正在吸取或持有的事务；空手调用不得改变输入释放锁。 */
void UElectromagneticGrabComponent::InterruptAndRelease()
{
	const bool bHasActiveGrabPhase =
		GrabPhase == EGrabPhase::Pulling || GrabPhase == EGrabPhase::Holding;
	const bool bHandleHasObject =
		IsValid(PhysicsHandle) && IsValid(PhysicsHandle->GetGrabbedComponent());
	if (!bHasActiveGrabPhase && !bHandleHasObject)
	{
		return;
	}

	ReleaseHeldObject(true);
}

/** 同时检查本地弱引用与 UE Physics Handle，避免只凭一侧状态误判仍在持有。 */
bool UElectromagneticGrabComponent::IsHoldingObject() const
{
	return HeldComponent.IsValid()
		&& IsValid(PhysicsHandle)
		&& PhysicsHandle->GetGrabbedComponent() == HeldComponent.Get();
}

/** 只更新活动持有目标；装配失效、Handle 状态失配、持续阻挡或误差过大时统一安全释放。 */
void UElectromagneticGrabComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsConfigurationReady() || !IsHoldingObject())
	{
		ReleaseHeldObject(true);
		return;
	}

	if (GrabPhase == EGrabPhase::None)
	{
		ReleaseHeldObject(true);
		return;
	}

	if (GrabPhase == EGrabPhase::Holding && bRestoreHandleInterpolationNextTick)
	{
		RestoreHandleTargetInterpolation();
	}

	bool bIsObstructed = false;
	const FVector SafeTarget = ResolveSafeHoldLocation(CalculateDesiredHoldLocation(), bIsObstructed);
	ObstructedElapsedSeconds = bIsObstructed ? ObstructedElapsedSeconds + DeltaTime : 0.0f;

	if (GrabPhase == EGrabPhase::Pulling)
	{
		PullElapsedSeconds = FMath::Min(PullElapsedSeconds + DeltaTime, PullDurationSeconds);
		PhysicsHandle->SetTargetLocation(CalculatePullTarget(SafeTarget));

		if (PullElapsedSeconds >= PullDurationSeconds)
		{
			EnterHoldingPhase();
		}
	}
	else
	{
		HoldingElapsedSeconds += DeltaTime;
		PhysicsHandle->SetTargetLocation(SafeTarget);
	}

	const UPrimitiveComponent* HeldBody = HeldComponent.Get();
	const float HoldError = FVector::Distance(HeldBody->GetCenterOfMass(), SafeTarget);
	const bool bExceededObstructionDelay = ObstructedElapsedSeconds >= TuningData->ObstructionReleaseDelay;
	const bool bExceededStableError = GrabPhase == EGrabPhase::Holding
		&& HoldingElapsedSeconds >= TuningData->PullGracePeriod
		&& HoldError >= TuningData->MaximumHoldError;

	if (bExceededObstructionDelay || bExceededStableError)
	{
		ReleaseHeldObject(true);
	}
}

/** World 或 Owner 销毁前恢复全部临时物理覆盖，随后再交还基类生命周期。 */
void UElectromagneticGrabComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseHeldObject(false);
	if (IsValid(PhysicsHandle))
	{
		PhysicsHandle->RemoveTickPrerequisiteComponent(this);
	}
	Super::EndPlay(EndPlayReason);
}

/** 返回 Configure 的校验缓存，同时防御运行期对象被销毁；不在 Tick 中重复遍历全部参数。 */
bool UElectromagneticGrabComponent::IsConfigurationReady() const
{
	return bConfigurationReady
		&& IsValid(PhysicsHandle)
		&& IsValid(ViewCamera)
		&& IsValid(TuningData);
}

/** 只评分有界数量的磁性 PhysicsBody，并用屏幕轮廓而非单一质心改善不规则物体选取。 */
UPrimitiveComponent* UElectromagneticGrabComponent::FindBestCandidate(UMagneticObjectComponent*& OutMagneticObject) const
{
	OutMagneticObject = nullptr;

	const UWorld* World = GetWorld();
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* PlayerController = IsValid(OwnerPawn) ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (!IsConfigurationReady() || !IsValid(World) || !IsValid(PlayerController))
	{
		return nullptr;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return nullptr;
	}

	const FVector CameraLocation = ViewCamera->GetComponentLocation();
	const FQuat QueryRotation = FQuat::Identity;
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectQueryParams.AddObjectTypesToQuery(Demo::CollisionChannels::AttackProjectileBody);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MagneticGrabSelection), false, GetOwner());
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		CameraLocation,
		QueryRotation,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(TuningData->GrabRange),
		QueryParams);

	const FVector2D ViewportCenter(static_cast<float>(ViewportWidth) * 0.5f, static_cast<float>(ViewportHeight) * 0.5f);
	const float SelectionRadiusPixels = static_cast<float>(FMath::Min(ViewportWidth, ViewportHeight))
		* TuningData->ScreenSelectionRadiusRatio;
	float BestScore = TNumericLimits<float>::Max();
	UPrimitiveComponent* BestComponent = nullptr;
	TSet<UPrimitiveComponent*> CheckedComponents;
	int32 CheckedCount = 0;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Candidate = Overlap.Component.Get();
		if (!IsValid(Candidate)
			|| CheckedComponents.Contains(Candidate)
			|| CheckedCount >= TuningData->MaximumCandidateChecks)
		{
			continue;
		}

		CheckedComponents.Add(Candidate);
		++CheckedCount;

		AActor* CandidateActor = Candidate->GetOwner();
		UMagneticObjectComponent* MagneticObject = IsValid(CandidateActor)
			? CandidateActor->FindComponentByClass<UMagneticObjectComponent>()
			: nullptr;
		if (!IsValid(MagneticObject) || !MagneticObject->CanGrab(Candidate, TuningData->MaximumGrabMass))
		{
			continue;
		}

		const FVector CenterOfMass = Candidate->GetCenterOfMass();
		const float WorldDistance = FVector::Distance(CameraLocation, CenterOfMass);
		if (WorldDistance > TuningData->GrabRange || !IsCandidateVisible(Candidate, CameraLocation))
		{
			continue;
		}

		FVector2D CenterScreenPosition;
		if (!PlayerController->ProjectWorldLocationToScreen(CenterOfMass, CenterScreenPosition))
		{
			continue;
		}

		// 把 Chaos 包围球半径投影到屏幕，让大体积或不规则物体靠近轮廓时也能进入容错区。
		FVector2D RadiusScreenPosition;
		const FVector RadiusSample = CenterOfMass + ViewCamera->GetRightVector() * Candidate->Bounds.SphereRadius;
		const bool bProjectedRadius = PlayerController->ProjectWorldLocationToScreen(RadiusSample, RadiusScreenPosition);
		const float ProjectedRadiusPixels = bProjectedRadius
			? FVector2D::Distance(CenterScreenPosition, RadiusScreenPosition)
			: 0.0f;
		const float ScreenDistancePixels = FVector2D::Distance(CenterScreenPosition, ViewportCenter);
		const float SilhouetteMissPixels = FMath::Max(0.0f, ScreenDistancePixels - ProjectedRadiusPixels);
		if (SilhouetteMissPixels > SelectionRadiusPixels)
		{
			continue;
		}

		const float ScreenScore = SilhouetteMissPixels / FMath::Max(SelectionRadiusPixels, 1.0f);
		const float DistanceScore = WorldDistance / TuningData->GrabRange;
		const float CandidateScore = ScreenScore
			+ DistanceScore * UE::ZeroEscape::Magnetism::WorldDistanceScoreWeight
			- MagneticObject->SelectionPriority * UE::ZeroEscape::Magnetism::PriorityScoreWeight;

		if (CandidateScore < BestScore)
		{
			BestScore = CandidateScore;
			BestComponent = Candidate;
			OutMagneticObject = MagneticObject;
		}
	}

	return BestComponent;
}

/** 快照可逆物理状态、在质心建立 Handle，并用全局角阻尼允许碰撞后自然旋转逐渐停止。 */
void UElectromagneticGrabComponent::GrabCandidate(
	UPrimitiveComponent* CandidateComponent,
	UMagneticObjectComponent* MagneticObject)
{
	if (!IsConfigurationReady()
		|| !IsValid(CandidateComponent)
		|| !IsValid(MagneticObject))
	{
		return;
	}

	if (UMagneticThrowBreakComponent* BreakComponent =
			CandidateComponent->GetOwner()->FindComponentByClass<UMagneticThrowBreakComponent>();
		IsValid(BreakComponent) && !BreakComponent->CanBeginGrab(CandidateComponent))
	{
		UE_LOG(LogZeroEscapeMagneticGrab, Verbose,
			TEXT("Rejected grab of %s because fracture replacement is already queued."),
			*GetNameSafe(CandidateComponent->GetOwner()));
		return;
	}

	// 先通过破碎守卫并结束共享投掷事务，再快照抓取基线；否则会把攻击通道/CCD/通知误当成原始值。
	MagneticObject->DisarmThrownImpact();

	HeldComponent = CandidateComponent;
	HeldMagneticObject = MagneticObject;
	PreviousAngularDamping = CandidateComponent->GetAngularDamping();
	PreviousPawnCollisionResponse = CandidateComponent->GetCollisionResponseToChannel(ECC_Pawn);
	PreviousCameraCollisionResponse = CandidateComponent->GetCollisionResponseToChannel(ECC_Camera);
	ObstructedElapsedSeconds = 0.0f;

	CandidateComponent->SetAngularDamping(TuningData->HeldAngularDamping);
	CandidateComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	// 持有物对 Camera 通道 Ignore：避免持有时物体挤入玩家第三人称弹簧臂触发相机回缩糊脸；释放时恢复。
	CandidateComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	const FVector PullStart = CandidateComponent->GetCenterOfMass();
	PhysicsHandle->GrabComponentAtLocation(CandidateComponent, NAME_None, PullStart);
	if (PhysicsHandle->GetGrabbedComponent() != CandidateComponent)
	{
		ReleaseHeldObject(false);
		return;
	}

	bool bIgnoredInitialObstruction = false;
	const FVector InitialSafeHoldLocation = ResolveSafeHoldLocation(
		CalculateDesiredHoldLocation(),
		bIgnoredInitialObstruction);
	BeginPull(PullStart, InitialSafeHoldLocation);
	SetComponentTickEnabled(true);
}

/** 关闭 Handle 的目标二次插值，并固定本次曲线起点、弧线方向、时长与弧高。 */
void UElectromagneticGrabComponent::BeginPull(
	const FVector& StartLocation,
	const FVector& InitialSafeHoldLocation)
{
	const float InitialTravelDistance = FVector::Distance(StartLocation, InitialSafeHoldLocation);
	const FVector DirectDirection = (InitialSafeHoldLocation - StartLocation).GetSafeNormal();

	GrabPhase = EGrabPhase::Pulling;
	PullStartLocation = StartLocation;
	PullElapsedSeconds = 0.0f;
	PullDurationSeconds = FMath::Max(
		TuningData->MinimumPullDuration,
		InitialTravelDistance / TuningData->PullReferenceSpeed);
	PullArcHeight = FMath::Min(
		InitialTravelDistance * TuningData->PullArcHeightRatio,
		TuningData->MaximumPullArcHeight);
	HoldingElapsedSeconds = 0.0f;

	// 固定弧线平面，既保持整体向上趋势，也避免玩家转动镜头时路径在空中扭转。
	PullArcDirection = FVector::VectorPlaneProject(FVector::UpVector, DirectDirection).GetSafeNormal();
	if (PullArcDirection.IsNearlyZero() && IsValid(ViewCamera))
	{
		PullArcDirection = FVector::VectorPlaneProject(
			ViewCamera->GetRightVector(),
			DirectDirection).GetSafeNormal();
	}
	if (PullArcDirection.IsNearlyZero())
	{
		PullArcDirection = FVector::UpVector;
	}

	bPreviousHandleInterpolateTarget = PhysicsHandle->bInterpolateTarget;
	bHandleTargetInterpolationOverridden = true;
	bRestoreHandleInterpolationNextTick = false;
	PhysicsHandle->bInterpolateTarget = false;
	PhysicsHandle->SetTargetLocation(StartLocation);
}

/** 使用绝对时间求值非对称进度；动态终点跟随玩家，固定起点与弧高避免路径漂移。 */
FVector UElectromagneticGrabComponent::CalculatePullTarget(const FVector& SafeHoldLocation) const
{
	if (PullDurationSeconds <= UE_SMALL_NUMBER)
	{
		return SafeHoldLocation;
	}

	const float NormalizedTime = FMath::Clamp(PullElapsedSeconds / PullDurationSeconds, 0.0f, 1.0f);
	const float TimeSquared = NormalizedTime * NormalizedTime;
	const float TimeToFourth = TimeSquared * TimeSquared;
	const float TravelProgress = TimeToFourth * (5.0f - 4.0f * NormalizedTime);
	const float ArcProgress = FMath::Sin(PI * TravelProgress);

	return FMath::Lerp(PullStartLocation, SafeHoldLocation, TravelProgress)
		+ PullArcDirection * PullArcHeight * ArcProgress;
}

/** 曲线终点帧仍让 Handle 直接采用目标；下一帧再恢复插值，防止末端叠加额外缓动。 */
void UElectromagneticGrabComponent::EnterHoldingPhase()
{
	GrabPhase = EGrabPhase::Holding;
	HoldingElapsedSeconds = 0.0f;
	bRestoreHandleInterpolationNextTick = bHandleTargetInterpolationOverridden;
}

/** 恢复抓取前的 Handle 目标插值状态；Handle 已销毁时也清掉本地覆盖标记。 */
void UElectromagneticGrabComponent::RestoreHandleTargetInterpolation()
{
	if (bHandleTargetInterpolationOverridden && IsValid(PhysicsHandle))
	{
		PhysicsHandle->bInterpolateTarget = bPreviousHandleInterpolateTarget;
	}

	bHandleTargetInterpolationOverridden = false;
	bRestoreHandleInterpolationNextTick = false;
}

/** 清空本次曲线运行态，确保下一次抓取不会继承旧距离、方向或计时。 */
void UElectromagneticGrabComponent::ResetPullState()
{
	GrabPhase = EGrabPhase::None;
	PullStartLocation = FVector::ZeroVector;
	PullArcDirection = FVector::UpVector;
	PullElapsedSeconds = 0.0f;
	PullDurationSeconds = 0.0f;
	PullArcHeight = 0.0f;
	HoldingElapsedSeconds = 0.0f;
	bPreviousHandleInterpolateTarget = true;
	bRestoreHandleInterpolationNextTick = false;
}

/** 先释放 UE Handle，再恢复角阻尼和 Pawn 碰撞，最后清空弱引用与计时，保证每条退出路径可回退。 */
void UElectromagneticGrabComponent::ReleaseHeldObject(const bool bRequireInputRelease)
{
	UPrimitiveComponent* ComponentToRelease = HeldComponent.Get();
	SetExplosionModeActive(false);
	RestoreHandleTargetInterpolation();

	if (IsValid(PhysicsHandle) && IsValid(PhysicsHandle->GetGrabbedComponent()))
	{
		PhysicsHandle->ReleaseComponent();
	}

	if (IsValid(ComponentToRelease))
	{
		ComponentToRelease->SetAngularDamping(PreviousAngularDamping);
		ComponentToRelease->SetCollisionResponseToChannel(ECC_Pawn, PreviousPawnCollisionResponse);
		ComponentToRelease->SetCollisionResponseToChannel(ECC_Camera, PreviousCameraCollisionResponse);
	}

	HeldComponent.Reset();
	HeldMagneticObject.Reset();
	ResetPullState();
	ObstructedElapsedSeconds = 0.0f;
	bAwaitingGrabRelease = bRequireInputRelease;
	SetComponentTickEnabled(false);
}

/** 状态事件总是携带切换前仍有效的精确持有刚体，便于后续表现恢复材质。 */
void UElectromagneticGrabComponent::SetExplosionModeActive(const bool bActive)
{
	if (bExplosionModeActive == bActive)
	{
		return;
	}

	bExplosionModeActive = bActive;
	if (UMagneticObjectComponent* MagneticObject = HeldMagneticObject.Get())
	{
		MagneticObject->SetExplosionPresentationActive(
			HeldComponent.Get(),
			bExplosionModeActive && IsValid(TuningData)
				? TuningData->ExplosionArmedOverlayMaterial.Get()
				: nullptr);
	}
	if (!bExplosionModeActive)
	{
		ExplosionModeActivatedWorldTimeSeconds = -1.0;
	}
	OnExplosionModeChanged.Broadcast(bExplosionModeActive, HeldComponent.Get());
}

/** 使用角色原点提供稳定高度基准，并使用相机前/右方向保持持有点与瞄准直觉一致。 */
FVector UElectromagneticGrabComponent::CalculateDesiredHoldLocation() const
{
	if (!IsConfigurationReady() || !IsValid(GetOwner()))
	{
		return FVector::ZeroVector;
	}

	return GetOwner()->GetActorLocation()
		+ FVector::UpVector * TuningData->HoldHeight
		+ ViewCamera->GetForwardVector() * TuningData->HoldDistance
		+ ViewCamera->GetRightVector() * TuningData->HoldSideOffset;
}

/** 用一次有界射线阻止目标锚点穿过墙体；命中时在阻挡面前保留配置间隙。 */
FVector UElectromagneticGrabComponent::ResolveSafeHoldLocation(
	const FVector& DesiredLocation,
	bool& bOutObstructed) const
{
	bOutObstructed = false;
	const UWorld* World = GetWorld();
	if (!IsConfigurationReady() || !IsValid(World) || !IsValid(GetOwner()))
	{
		return DesiredLocation;
	}

	const FVector TraceStart = GetOwner()->GetActorLocation() + FVector::UpVector * TuningData->HoldHeight;
	const FVector TraceDirection = (DesiredLocation - TraceStart).GetSafeNormal();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MagneticHoldObstruction), false, GetOwner());
	if (const UPrimitiveComponent* HeldBody = HeldComponent.Get())
	{
		QueryParams.AddIgnoredActor(HeldBody->GetOwner());
	}

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(
		Hit,
		TraceStart,
		DesiredLocation,
		UE::ZeroEscape::Magnetism::InteractionTraceChannel,
		QueryParams))
	{
		return DesiredLocation;
	}

	bOutObstructed = true;
	const float SafeDistance = FMath::Max(
		TuningData->MinimumHoldDistance,
		Hit.Distance - TuningData->ObstructionClearance);
	return TraceStart + TraceDirection * SafeDistance;
}

/** 从相机穿过中心准星追踪；忽略玩家和当前持有物，使投掷方向收敛到可见场景命中点。 */
FVector UElectromagneticGrabComponent::CalculateAimPoint() const
{
	if (!IsConfigurationReady() || !IsValid(GetWorld()))
	{
		return FVector::ZeroVector;
	}

	const FVector TraceStart = ViewCamera->GetComponentLocation();
	const FVector TraceEnd = TraceStart + ViewCamera->GetForwardVector() * TuningData->AimTraceDistance;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MagneticThrowAim), false, GetOwner());
	if (const UPrimitiveComponent* HeldBody = HeldComponent.Get())
	{
		QueryParams.AddIgnoredActor(HeldBody->GetOwner());
	}

	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(
		Hit,
		TraceStart,
		TraceEnd,
		UE::ZeroEscape::Magnetism::InteractionTraceChannel,
		QueryParams)
		? Hit.ImpactPoint
		: TraceEnd;
}

/** 追踪候选最近碰撞表面；无阻挡或首个命中正是候选时才视为可见。 */
bool UElectromagneticGrabComponent::IsCandidateVisible(
	const UPrimitiveComponent* CandidateComponent,
	const FVector& CameraLocation) const
{
	if (!IsValid(CandidateComponent) || !IsValid(GetWorld()))
	{
		return false;
	}

	FVector SurfacePoint = CandidateComponent->GetCenterOfMass();
	CandidateComponent->GetClosestPointOnCollision(CameraLocation, SurfacePoint);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MagneticSelectionVisibility), false, GetOwner());
	FHitResult Hit;
	const bool bHitSomething = GetWorld()->LineTraceSingleByChannel(
		Hit,
		CameraLocation,
		SurfacePoint,
		UE::ZeroEscape::Magnetism::InteractionTraceChannel,
		QueryParams);

	return !bHitSomething
		|| Hit.GetComponent() == CandidateComponent
		|| Hit.GetActor() == CandidateComponent->GetOwner();
}
