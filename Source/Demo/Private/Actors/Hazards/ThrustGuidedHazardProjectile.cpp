// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardProjectile.cpp
 * 职责：实现从炮架预装到离膛的同一个 Chaos 胶囊刚体、一次质心冲量、连续接触阶段和可选 HeavyImpact 准备。
 * 边界：不使用 Thruster/ProjectileMovement，不 Tick、不追踪目标，不改写飞行中速度或 Transform。
 */

#include "Actors/Hazards/ThrustGuidedHazardProjectile.h"

#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Data/Hazards/ThrustGuidedHazardTuningData.h"
#include "Engine/World.h"
#include "Interfaces/HeavyImpactReceiver.h"
#include "Physics/HeavyImpactTypes.h"
#include "PhysicsEngine/BodyInstance.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogThrustGuidedHazardProjectile, Log, All);

namespace ThrustGuidedHazardProjectile
{
	/** 重冲击准备只做短距离 ETA 采样，不承担弹体运动。 */
	constexpr float PreparationSampleIntervalSeconds = 1.0f / 60.0f;

	/** 严重掉帧时允许的最大准备帧数，与现有 HeavyImpact 发送端语义一致。 */
	constexpr float MaximumPreparationFrameMultiplier = 2.5f;

	/** 帧感知准备窗口绝对上限，避免一次卡顿造成长期提前接管。 */
	constexpr float AbsoluteMaximumPreparationSeconds = 0.5f;

	/** 发射器和弹体各自推导初速时允许的浮点差异，单位 cm/s。 */
	constexpr float MinimumLaunchSpeedTolerance = 1.0f;

	/** 配置或生成失败后的无碰撞残体保留时间，单位 s。 */
	constexpr float DisabledProjectileCleanupDelaySeconds = 1.0f;
}

/** 创建唯一物理胶囊、查询球和两个纯美术挂点。 */
AThrustGuidedHazardProjectile::AThrustGuidedHazardProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	ProjectileBody = CreateDefaultSubobject<UCapsuleComponent>(TEXT("ProjectileBody"));
	ProjectileBody->SetMobility(EComponentMobility::Movable);
	ProjectileBody->SetCanEverAffectNavigation(false);
	ProjectileBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileBody->SetCollisionObjectType(ECC_PhysicsBody);
	ProjectileBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProjectileBody->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	ProjectileBody->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	ProjectileBody->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	ProjectileBody->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	ProjectileBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ProjectileBody->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	ProjectileBody->SetGenerateOverlapEvents(false);
	ProjectileBody->SetNotifyRigidBodyCollision(true);
	ProjectileBody->SetSimulatePhysics(false);
	ProjectileBody->SetEnableGravity(true);
	ProjectileBody->SetUseCCD(true);
	ProjectileBody->BodyInstance.bGenerateWakeEvents = true;
	SetRootComponent(ProjectileBody);

	BodyVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyVisualRoot"));
	BodyVisualRoot->SetupAttachment(ProjectileBody);

	ExhaustVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ExhaustVisualRoot"));
	ExhaustVisualRoot->SetupAttachment(ProjectileBody);
	ExhaustVisualRoot->SetVisibility(false, true);

	PreparationVolume = CreateDefaultSubobject<USphereComponent>(TEXT("PreparationVolume"));
	PreparationVolume->SetupAttachment(ProjectileBody);
	PreparationVolume->SetMobility(EComponentMobility::Movable);
	PreparationVolume->SetCanEverAffectNavigation(false);
	PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreparationVolume->SetCollisionObjectType(ECC_WorldDynamic);
	PreparationVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	PreparationVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PreparationVolume->SetGenerateOverlapEvents(true);

	ApplyConfiguration(*GetDefault<UThrustGuidedHazardTuningData>());
}

/** 延迟生成阶段只注入配置；FinishSpawningActor 后保持预装而不启动物理。 */
void AThrustGuidedHazardProjectile::ConfigureLoaded(
	UThrustGuidedHazardTuningData* InTuningData)
{
	if (HasActorBegunPlay())
	{
		UE_LOG(
			LogThrustGuidedHazardProjectile,
			Error,
			TEXT("ConfigureLoaded called after BeginPlay on %s."),
			*GetNameSafe(this));
		return;
	}

	bLoadedConfigured = true;
	RuntimeTuningData = InTuningData;
	PendingLaunchVelocity = FVector::ZeroVector;
	LaunchId = FGuid();
}

bool AThrustGuidedHazardProjectile::IsLoaded() const
{
	return Phase == EThrustGuidedHazardProjectilePhase::Loaded;
}

/** 校验预装合同；真实弹体此时可见，但没有碰撞、重力、物理模拟或寿命倒计时。 */
void AThrustGuidedHazardProjectile::BeginPlay()
{
	Super::BeginPlay();
	Phase = EThrustGuidedHazardProjectilePhase::Uninitialized;

	if (!bLoadedConfigured)
	{
		DisableProjectile(TEXT("弹体未通过延迟生成调用 ConfigureLoaded。"));
		return;
	}

	if (!IsValid(RuntimeTuningData))
	{
		DisableProjectile(TEXT("未注入 ThrustGuidedHazardTuningData。"));
		return;
	}

	FString Error;
	if (!RuntimeTuningData->IsConfigured(Error))
	{
		DisableProjectile(Error);
		return;
	}

	if (!GetActorScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		DisableProjectile(TEXT("Projectile Actor Scale 必须保持 (1,1,1)。"));
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		DisableProjectile(TEXT("弹体没有有效 World。"));
		return;
	}

	if (!ProjectileBody->GetComponentTransform().IsValid())
	{
		DisableProjectile(TEXT("ProjectileBody 预装 Transform 无效。"));
		return;
	}

	ApplyConfiguration(*RuntimeTuningData);
	ProjectileBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileBody->SetSimulatePhysics(false);
	ProjectileBody->SetEnableGravity(false);
	PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExhaustVisualRoot->SetVisibility(false, true);
	ContactSequence = 0;
	PreparationCandidates.Reset();
	NotifiedReceiversThisLaunch.Reset();
	Phase = EThrustGuidedHazardProjectilePhase::Loaded;

	UE_LOG(
		LogThrustGuidedHazardProjectile,
		Display,
		TEXT("Projectile %s loaded Data=%s Location=%s."),
		*GetNameSafe(this),
		*GetPathNameSafe(RuntimeTuningData),
		*ProjectileBody->GetComponentLocation().ToCompactString());
}

/** 炮架解除挂接后，同一个真实弹体才开启 Chaos、寿命和唯一一次质心冲量。 */
bool AThrustGuidedHazardProjectile::LaunchFromLoaded(
	const FVector& InLaunchVelocity,
	const FGuid& InLaunchId,
	FString& OutError)
{
	OutError.Reset();
	if (!HasActorBegunPlay()
		|| Phase != EThrustGuidedHazardProjectilePhase::Loaded)
	{
		OutError = TEXT("弹体不处于可离膛的 Loaded 阶段。");
		return false;
	}
	if (!IsValid(RuntimeTuningData))
	{
		OutError = TEXT("预装弹体没有有效 ThrustGuidedHazardTuningData。");
		return false;
	}
	if (!InLaunchId.IsValid())
	{
		OutError = TEXT("LaunchId 无效。");
		return false;
	}
	if (!IsValid(ProjectileBody)
		|| ProjectileBody->GetAttachParent() != nullptr)
	{
		OutError = TEXT("真实弹体必须先从炮架解除挂接，才能开启 Chaos。");
		return false;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		OutError = TEXT("弹体没有有效 World。");
		return false;
	}

	const float GravityMagnitude = FMath::Abs(World->GetGravityZ());
	const float ReferenceAngleRadians = FMath::DegreesToRadians(
		RuntimeTuningData->PreferredLaunchAngleDegrees);
	const float SinDoubleAngle = FMath::Sin(2.0f * ReferenceAngleRadians);
	if (!FMath::IsFinite(GravityMagnitude)
		|| GravityMagnitude <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(SinDoubleAngle)
		|| SinDoubleAngle <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("World 重力或参考发射角无法推导有限初速。");
		return false;
	}

	const float ExpectedLaunchSpeed = FMath::Sqrt(
		GravityMagnitude * RuntimeTuningData->ReferenceRange / SinDoubleAngle);
	const float PendingLaunchSpeed = InLaunchVelocity.Size();
	const float LaunchSpeedTolerance = FMath::Max(
		ThrustGuidedHazardProjectile::MinimumLaunchSpeedTolerance,
		ExpectedLaunchSpeed * 0.001f);
	if (InLaunchVelocity.ContainsNaN()
		|| !FMath::IsFinite(PendingLaunchSpeed)
		|| PendingLaunchSpeed <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(ExpectedLaunchSpeed)
		|| !FMath::IsNearlyEqual(
			PendingLaunchSpeed,
			ExpectedLaunchSpeed,
			LaunchSpeedTolerance))
	{
		OutError = FString::Printf(
			TEXT("冻结初速度无效或与设计初速不一致。Pending=%.3f Expected=%.3f。"),
			PendingLaunchSpeed,
			ExpectedLaunchSpeed);
		return false;
	}
	if (!ProjectileBody->GetComponentTransform().IsValid())
	{
		OutError = TEXT("ProjectileBody 离膛 Transform 无效。");
		return false;
	}

	PendingLaunchVelocity = InLaunchVelocity;
	LaunchId = InLaunchId;

	ProjectileBody->OnComponentHit.AddUniqueDynamic(
		this,
		&AThrustGuidedHazardProjectile::HandleProjectileHit);
	ProjectileBody->OnComponentWake.AddUniqueDynamic(
		this,
		&AThrustGuidedHazardProjectile::HandleProjectileWake);
	ProjectileBody->OnComponentSleep.AddUniqueDynamic(
		this,
		&AThrustGuidedHazardProjectile::HandleProjectileSleep);

	if (RuntimeTuningData->bEnableHeavyImpactPreparation)
	{
		PreparationVolume->OnComponentBeginOverlap.AddUniqueDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandlePreparationVolumeBeginOverlap);
		PreparationVolume->OnComponentEndOverlap.AddUniqueDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandlePreparationVolumeEndOverlap);
		bPreparationBindingsActive = true;
	}

	ContactSequence = 0;
	PreparationCandidates.Reset();
	NotifiedReceiversThisLaunch.Reset();

	ProjectileBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProjectileBody->SetSimulatePhysics(true);
	ProjectileBody->SetMassOverrideInKg(
		NAME_None,
		RuntimeTuningData->ProjectileMassKilograms,
		true);
	ProjectileBody->SetEnableGravity(true);
	// SuggestProjectileVelocity 假设无空气阻力；离膛阶段线性阻尼固定为零以保持求解合同。
	ProjectileBody->SetLinearDamping(0.0f);
	ProjectileBody->SetAngularDamping(RuntimeTuningData->BallisticAngularDamping);
	ProjectileBody->SetUseCCD(true);
	ProjectileBody->WakeAllRigidBodies();

	const float ActualMass = ProjectileBody->GetMass();
	if (!FMath::IsFinite(ActualMass) || ActualMass <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("ProjectileBody 无法得到有限正质量。");
		DisableProjectile(OutError);
		return false;
	}

	Phase = EThrustGuidedHazardProjectilePhase::Ballistic;
	SetLifeSpan(RuntimeTuningData->ProjectileLifetimeSeconds);
	ProjectileBody->AddImpulse(
		PendingLaunchVelocity * ActualMass,
		NAME_None,
		false);
	ExhaustVisualRoot->SetVisibility(true, true);

	if (RuntimeTuningData->bEnableHeavyImpactPreparation)
	{
		PreparationVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		StartPreparationMonitoring();
	}

	UE_LOG(
		LogThrustGuidedHazardProjectile,
		Display,
		TEXT("Projectile %s started LaunchId=%s Data=%s Mass=%.2f Lifetime=%.2f LaunchVelocity=%s HeavyImpact=%s Location=%s."),
		*GetNameSafe(this),
		*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*GetPathNameSafe(RuntimeTuningData),
		ActualMass,
		RuntimeTuningData->ProjectileLifetimeSeconds,
		*PendingLaunchVelocity.ToCompactString(),
		RuntimeTuningData->bEnableHeavyImpactPreparation ? TEXT("true") : TEXT("false"),
		*ProjectileBody->GetComponentLocation().ToCompactString());
	return true;
}

/** 清理所有真实绑定、Timer 和运行时集合；不依赖 Actor Tick。 */
void AThrustGuidedHazardProjectile::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	StopPreparationMonitoring();
	Phase = EThrustGuidedHazardProjectilePhase::Disabled;

	if (IsValid(ProjectileBody))
	{
		ProjectileBody->OnComponentHit.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandleProjectileHit);
		ProjectileBody->OnComponentWake.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandleProjectileWake);
		ProjectileBody->OnComponentSleep.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandleProjectileSleep);
	}

	if (bPreparationBindingsActive && IsValid(PreparationVolume))
	{
		PreparationVolume->OnComponentBeginOverlap.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandlePreparationVolumeBeginOverlap);
		PreparationVolume->OnComponentEndOverlap.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandlePreparationVolumeEndOverlap);
		bPreparationBindingsActive = false;
	}

	if (IsValid(PreparationVolume))
	{
		PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (IsValid(ExhaustVisualRoot))
	{
		ExhaustVisualRoot->SetVisibility(false, true);
	}

	PreparationCandidates.Reset();
	NotifiedReceiversThisLaunch.Reset();
	RuntimeTuningData = nullptr;
	PendingLaunchVelocity = FVector::ZeroVector;

	Super::EndPlay(EndPlayReason);
}

/** 配置唯一胶囊；HeavyImpact 关闭时不读取其半径或时间字段。 */
void AThrustGuidedHazardProjectile::ApplyConfiguration(
	const UThrustGuidedHazardTuningData& Tuning)
{
	ProjectileBody->SetCapsuleSize(
		Tuning.ProjectileRadius,
		Tuning.ProjectileHalfHeight,
		true);

	if (Tuning.bEnableHeavyImpactPreparation)
	{
		PreparationVolume->SetSphereRadius(
			Tuning.PreparationLookAheadDistance,
			true);
	}
	else
	{
		PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

/** 立即收集现有重叠并启动独立 60 Hz Timer；关闭开关时完全无副作用。 */
void AThrustGuidedHazardProjectile::StartPreparationMonitoring()
{
	if (!IsValid(RuntimeTuningData)
		|| !RuntimeTuningData->bEnableHeavyImpactPreparation
		|| !bPreparationBindingsActive
		|| Phase == EThrustGuidedHazardProjectilePhase::Disabled
		|| Phase == EThrustGuidedHazardProjectilePhase::Sleeping
		|| !IsValid(PreparationVolume))
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	PreparationVolume->GetOverlappingActors(OverlappingActors);
	for (AActor* Candidate : OverlappingActors)
	{
		if (IsValid(Candidate)
			&& Candidate != this
			&& Candidate->GetClass()->ImplementsInterface(
				UHeavyImpactReceiver::StaticClass()))
		{
			PreparationCandidates.Add(Candidate);
		}
	}

	EvaluatePreparationCandidates();
	if (!GetWorldTimerManager().IsTimerActive(PreparationTimerHandle))
	{
		GetWorldTimerManager().SetTimer(
			PreparationTimerHandle,
			this,
			&AThrustGuidedHazardProjectile::EvaluatePreparationCandidates,
			ThrustGuidedHazardProjectile::PreparationSampleIntervalSeconds,
			true);
	}
}

/** 休眠、禁用和 EndPlay 都只清除本 Actor 拥有的准备 Timer。 */
void AThrustGuidedHazardProjectile::StopPreparationMonitoring()
{
	GetWorldTimerManager().ClearTimer(PreparationTimerHandle);
}

/** Accepted/Duplicate 才完成该接收者通知；Busy/Invalid 保留以便后续采样重试。 */
void AThrustGuidedHazardProjectile::EvaluatePreparationCandidates()
{
	if (!IsValid(RuntimeTuningData)
		|| !RuntimeTuningData->bEnableHeavyImpactPreparation
		|| !bPreparationBindingsActive
		|| Phase == EThrustGuidedHazardProjectilePhase::Disabled
		|| Phase == EThrustGuidedHazardProjectilePhase::Sleeping
		|| !LaunchId.IsValid()
		|| !IsValid(PreparationVolume))
	{
		return;
	}

	TArray<TWeakObjectPtr<AActor>> CandidateSnapshot;
	CandidateSnapshot.Reserve(PreparationCandidates.Num());
	for (const TWeakObjectPtr<AActor>& Candidate : PreparationCandidates)
	{
		CandidateSnapshot.Add(Candidate);
	}

	for (const TWeakObjectPtr<AActor>& Candidate : CandidateSnapshot)
	{
		AActor* Receiver = Candidate.Get();
		if (!IsValid(Receiver)
			|| !PreparationVolume->IsOverlappingActor(Receiver))
		{
			PreparationCandidates.Remove(Candidate);
			continue;
		}

		if (NotifiedReceiversThisLaunch.Contains(Candidate))
		{
			continue;
		}

		FHeavyImpactPreparationRequest Request;
		if (!BuildPreparationRequest(*Receiver, Request))
		{
			continue;
		}

		const EHeavyImpactPrepareResult Result =
			IHeavyImpactReceiver::Execute_PrepareForHeavyImpact(Receiver, Request);
		if (Result == EHeavyImpactPrepareResult::Accepted
			|| Result == EHeavyImpactPrepareResult::Duplicate)
		{
			NotifiedReceiversThisLaunch.Add(Candidate);
		}
	}
}

/** 使用当前相对速度和世界重力估算短时间内的真实弹道接触准备。 */
bool AThrustGuidedHazardProjectile::BuildPreparationRequest(
	const AActor& Receiver,
	FHeavyImpactPreparationRequest& OutRequest)
{
	if (!IsValid(ProjectileBody)
		|| !IsValid(RuntimeTuningData)
		|| !RuntimeTuningData->bEnableHeavyImpactPreparation
		|| !LaunchId.IsValid())
	{
		return false;
	}

	const FVector BodyCenter = ProjectileBody->GetCenterOfMass();
	const FVector BodyVelocity = ProjectileBody->GetPhysicsLinearVelocity();
	const FVector ReceiverVelocity = Receiver.GetVelocity();
	if (BodyCenter.ContainsNaN()
		|| BodyVelocity.ContainsNaN()
		|| ReceiverVelocity.ContainsNaN())
	{
		return false;
	}

	UPrimitiveComponent* PredictionPrimitive =
		IHeavyImpactReceiver::Execute_GetHeavyImpactPredictionPrimitive(&Receiver);
	if (!IsValid(PredictionPrimitive)
		|| PredictionPrimitive->GetOwner() != &Receiver
		|| PredictionPrimitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		return false;
	}

	FVector ClosestSurfacePoint = FVector::ZeroVector;
	float ClosestSurfaceDistance = BIG_NUMBER;
	float ClosestSurfaceDistanceSquared = BIG_NUMBER;
	if (PredictionPrimitive->GetSquaredDistanceToCollision(
			BodyCenter,
			ClosestSurfaceDistanceSquared,
			ClosestSurfacePoint)
		&& FMath::IsFinite(ClosestSurfaceDistanceSquared)
		&& ClosestSurfaceDistanceSquared >= 0.0f
		&& !ClosestSurfacePoint.ContainsNaN())
	{
		ClosestSurfaceDistance = FMath::Sqrt(ClosestSurfaceDistanceSquared);
	}
	else
	{
		ClosestSurfacePoint = FVector::ZeroVector;
	}

	// Physics Asset 最近点查询失败时，只回退同一个权威组件 Bounds。
	if (ClosestSurfaceDistance == BIG_NUMBER)
	{
		const FVector BoundsOrigin = PredictionPrimitive->Bounds.Origin;
		const FVector BoundsExtent = PredictionPrimitive->Bounds.BoxExtent;
		const FVector ToBoundsCenter = BoundsOrigin - BodyCenter;
		const float CenterDistance = ToBoundsCenter.Size();
		const FVector BoundsDirection = ToBoundsCenter.GetSafeNormal();
		if (!FMath::IsFinite(CenterDistance) || BoundsDirection.IsNearlyZero())
		{
			return false;
		}

		const float ProjectedExtent =
			FVector::DotProduct(BoundsExtent, BoundsDirection.GetAbs());
		ClosestSurfaceDistance =
			FMath::Max(0.0f, CenterDistance - ProjectedExtent);
		ClosestSurfacePoint =
			BodyCenter + BoundsDirection * ClosestSurfaceDistance;
	}

	FVector ApproachDirection =
		(ClosestSurfacePoint - BodyCenter).GetSafeNormal();
	if (ApproachDirection.IsNearlyZero())
	{
		ApproachDirection =
			(Receiver.GetActorLocation() - BodyCenter).GetSafeNormal();
	}
	if (ApproachDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector RelativeVelocity = BodyVelocity - ReceiverVelocity;
	const float ClosingSpeed =
		FVector::DotProduct(RelativeVelocity, ApproachDirection);
	if (!FMath::IsFinite(ClosingSpeed)
		|| ClosingSpeed < RuntimeTuningData->MinimumHeavyImpactClosingSpeed)
	{
		return false;
	}

	const float BodySurfaceDistance =
		CalculateCapsuleRaySurfaceDistance(*ProjectileBody, ApproachDirection);
	if (BodySurfaceDistance <= 0.0f)
	{
		return false;
	}

	const float SurfaceGap =
		FMath::Max(0.0f, ClosestSurfaceDistance - BodySurfaceDistance);
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const FVector Gravity(0.0f, 0.0f, World->GetGravityZ());
	const float GravityAlongApproach =
		FVector::DotProduct(Gravity, ApproachDirection);
	float EstimatedTimeToContact = 0.0f;
	if (Gravity.ContainsNaN()
		|| !TryEstimateGravityAwareContactTime(
			SurfaceGap,
			ClosingSpeed,
			GravityAlongApproach,
			EstimatedTimeToContact))
	{
		return false;
	}

	const float DeltaSeconds = FMath::Max(0.0f, World->GetDeltaSeconds());
	const float FrameAwareMaximumSeconds = FMath::Min(
		ThrustGuidedHazardProjectile::AbsoluteMaximumPreparationSeconds,
		DeltaSeconds
			* ThrustGuidedHazardProjectile::MaximumPreparationFrameMultiplier);
	const float AllowedMaximumSeconds = FMath::Max(
		RuntimeTuningData->MaximumPreparationLeadTime,
		FrameAwareMaximumSeconds);
	if (!FMath::IsFinite(EstimatedTimeToContact)
		|| EstimatedTimeToContact > AllowedMaximumSeconds)
	{
		return false;
	}

	const FVector PredictedBodyCenter =
		BodyCenter
		+ BodyVelocity * EstimatedTimeToContact
		+ 0.5f * Gravity * FMath::Square(EstimatedTimeToContact);
	const FVector PredictedReceiverSurface =
		ClosestSurfacePoint + ReceiverVelocity * EstimatedTimeToContact;
	FVector PredictedContactDirection =
		(PredictedReceiverSurface - PredictedBodyCenter).GetSafeNormal();
	if (PredictedContactDirection.IsNearlyZero())
	{
		PredictedContactDirection = ApproachDirection;
	}

	const float PredictedBodySurfaceDistance =
		CalculateCapsuleRaySurfaceDistance(
			*ProjectileBody,
			PredictedContactDirection);
	if (PredictedBodySurfaceDistance <= 0.0f)
	{
		return false;
	}

	OutRequest = FHeavyImpactPreparationRequest();
	OutRequest.ImpactId = LaunchId;
	OutRequest.SourceActor = this;
	OutRequest.SourceComponent = ProjectileBody;
	OutRequest.PredictedImpactPoint =
		PredictedBodyCenter
		+ PredictedContactDirection * PredictedBodySurfaceDistance;
	OutRequest.SourceLinearVelocity = BodyVelocity;
	OutRequest.EstimatedTimeToContactSeconds = EstimatedTimeToContact;

	FString ValidationError;
	return !OutRequest.PredictedImpactPoint.ContainsNaN()
		&& OutRequest.IsStructurallyValid(&Receiver, ValidationError);
}

/** 解沿接近方向的一维匀加速位移，并选择最早有限非负根。 */
bool AThrustGuidedHazardProjectile::TryEstimateGravityAwareContactTime(
	const float SurfaceGap,
	const float ClosingSpeed,
	const float GravityAlongApproach,
	float& OutTimeSeconds)
{
	OutTimeSeconds = 0.0f;
	if (!FMath::IsFinite(SurfaceGap)
		|| !FMath::IsFinite(ClosingSpeed)
		|| !FMath::IsFinite(GravityAlongApproach)
		|| SurfaceGap < 0.0f
		|| ClosingSpeed <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	if (SurfaceGap <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	if (FMath::Abs(GravityAlongApproach) <= KINDA_SMALL_NUMBER)
	{
		OutTimeSeconds = SurfaceGap / ClosingSpeed;
		return FMath::IsFinite(OutTimeSeconds) && OutTimeSeconds >= 0.0f;
	}

	const float Discriminant =
		FMath::Square(ClosingSpeed)
		+ 2.0f * GravityAlongApproach * SurfaceGap;
	if (!FMath::IsFinite(Discriminant) || Discriminant < 0.0f)
	{
		return false;
	}

	const float SqrtDiscriminant = FMath::Sqrt(Discriminant);
	// 与水平极限连续的最早正根；该形式避免 (-Closing + SqrtD) 的浮点相消。
	const float StableDenominator = ClosingSpeed + SqrtDiscriminant;
	if (!FMath::IsFinite(StableDenominator)
		|| StableDenominator <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	OutTimeSeconds = 2.0f * SurfaceGap / StableDenominator;
	return FMath::IsFinite(OutTimeSeconds) && OutTimeSeconds >= 0.0f;
}

/** 解析胶囊圆柱段与端球的射线交点，避免用包围盒夸大弹体前表面。 */
float AThrustGuidedHazardProjectile::CalculateCapsuleRaySurfaceDistance(
	const UCapsuleComponent& Capsule,
	const FVector& WorldDirection)
{
	const FVector Direction = WorldDirection.GetSafeNormal();
	const FVector CapsuleAxis = Capsule.GetUpVector().GetSafeNormal();
	const float Radius = Capsule.GetScaledCapsuleRadius();
	const float SegmentHalfLength =
		Capsule.GetScaledCapsuleHalfHeight_WithoutHemisphere();
	if (Direction.IsNearlyZero()
		|| CapsuleAxis.IsNearlyZero()
		|| !FMath::IsFinite(Radius)
		|| !FMath::IsFinite(SegmentHalfLength)
		|| Radius <= 0.0f
		|| SegmentHalfLength < 0.0f)
	{
		return 0.0f;
	}

	if (SegmentHalfLength <= KINDA_SMALL_NUMBER)
	{
		return Radius;
	}

	const float AxisDot =
		FMath::Abs(FVector::DotProduct(Direction, CapsuleAxis));
	const float PerpendicularSquared =
		FMath::Max(0.0f, 1.0f - FMath::Square(AxisDot));
	if (PerpendicularSquared > KINDA_SMALL_NUMBER)
	{
		const float CylinderDistance =
			Radius / FMath::Sqrt(PerpendicularSquared);
		if (CylinderDistance * AxisDot <= SegmentHalfLength)
		{
			return CylinderDistance;
		}
	}

	const float SphereDiscriminant = FMath::Max(
		0.0f,
		FMath::Square(Radius)
			- FMath::Square(SegmentHalfLength) * PerpendicularSquared);
	return SegmentHalfLength * AxisDot + FMath::Sqrt(SphereDiscriminant);
}

/** 每次接触都记录；首个有效阻挡只切阶段、阻尼和表现，不接管速度。 */
void AThrustGuidedHazardProjectile::HandleProjectileHit(
	UPrimitiveComponent* /*HitComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	++ContactSequence;
	const FVector LinearVelocity = ProjectileBody->GetPhysicsLinearVelocity();
	const FVector AngularVelocity =
		ProjectileBody->GetPhysicsAngularVelocityInRadians();
	UE_LOG(
		LogThrustGuidedHazardProjectile,
		Verbose,
		TEXT("LaunchId=%s Contact=%u Other=%s Point=%s Normal=%s Impulse=%s Linear=%s Angular=%s."),
		*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ContactSequence,
		*GetNameSafe(OtherActor),
		*Hit.ImpactPoint.ToCompactString(),
		*Hit.ImpactNormal.ToCompactString(),
		*NormalImpulse.ToCompactString(),
		*LinearVelocity.ToCompactString(),
		*AngularVelocity.ToCompactString());

	AActor* ContactOwner = IsValid(OtherActor)
		? OtherActor
		: (IsValid(OtherComponent) ? OtherComponent->GetOwner() : nullptr);
	const bool bBlockingContact =
		Hit.bBlockingHit
		&& IsValid(OtherComponent)
		&& IsValid(ContactOwner)
		&& ContactOwner != this;
	if (Phase != EThrustGuidedHazardProjectilePhase::Ballistic
		|| !bBlockingContact)
	{
		return;
	}

	Phase = EThrustGuidedHazardProjectilePhase::FreePhysics;
	if (IsValid(RuntimeTuningData))
	{
		ProjectileBody->SetLinearDamping(
			RuntimeTuningData->PostImpactLinearDamping);
		ProjectileBody->SetAngularDamping(
			RuntimeTuningData->PostImpactAngularDamping);
	}

	if (Hit.bStartPenetrating)
	{
		UE_LOG(
			LogThrustGuidedHazardProjectile,
			Warning,
			TEXT("LaunchId=%s received penetrating first blocking contact with %s."),
			*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*GetNameSafe(ContactOwner));
	}

	ReceiveFirstBlockingImpact(
		ContactOwner,
		Hit.ImpactPoint,
		NormalImpulse);
}

/** 休眠后被真实外力唤醒只恢复自由物理标签和可选准备链。 */
void AThrustGuidedHazardProjectile::HandleProjectileWake(
	UPrimitiveComponent* WakingComponent,
	FName /*BoneName*/)
{
	if (WakingComponent != ProjectileBody
		|| Phase == EThrustGuidedHazardProjectilePhase::Disabled)
	{
		return;
	}

	if (Phase == EThrustGuidedHazardProjectilePhase::Sleeping)
	{
		Phase = EThrustGuidedHazardProjectilePhase::FreePhysics;
		StartPreparationMonitoring();
	}
}

/** Ballistic 或 FreePhysics 刚体休眠时停止可选准备 Timer，不销毁弹体。 */
void AThrustGuidedHazardProjectile::HandleProjectileSleep(
	UPrimitiveComponent* SleepingComponent,
	FName /*BoneName*/)
{
	if (SleepingComponent != ProjectileBody
		|| (Phase != EThrustGuidedHazardProjectilePhase::Ballistic
			&& Phase != EThrustGuidedHazardProjectilePhase::FreePhysics))
	{
		return;
	}

	Phase = EThrustGuidedHazardProjectilePhase::Sleeping;
	StopPreparationMonitoring();
}

/** HeavyImpact 开启时才登记共享接收接口，不在 Overlap 回调中切换角色状态。 */
void AThrustGuidedHazardProjectile::HandlePreparationVolumeBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (bPreparationBindingsActive
		&& IsValid(RuntimeTuningData)
		&& RuntimeTuningData->bEnableHeavyImpactPreparation
		&& Phase != EThrustGuidedHazardProjectilePhase::Disabled
		&& Phase != EThrustGuidedHazardProjectilePhase::Sleeping
		&& IsValid(OtherActor)
		&& OtherActor != this
		&& OtherActor->GetClass()->ImplementsInterface(
			UHeavyImpactReceiver::StaticClass()))
	{
		PreparationCandidates.Add(OtherActor);
	}
}

/** 多组件 Actor 仅在最后一个组件离开预测球时移除。 */
void AThrustGuidedHazardProjectile::HandlePreparationVolumeEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/)
{
	if (bPreparationBindingsActive
		&& IsValid(PreparationVolume)
		&& IsValid(OtherActor)
		&& !PreparationVolume->IsOverlappingActor(OtherActor))
	{
		PreparationCandidates.Remove(OtherActor);
	}
}

/** 无法安全运行时关闭模拟、碰撞、表现和可选 HeavyImpact 准备。 */
void AThrustGuidedHazardProjectile::DisableProjectile(const FString& Reason)
{
	StopPreparationMonitoring();
	Phase = EThrustGuidedHazardProjectilePhase::Disabled;
	if (IsValid(GetWorld()))
	{
		SetLifeSpan(
			ThrustGuidedHazardProjectile::DisabledProjectileCleanupDelaySeconds);
	}
	PreparationCandidates.Reset();
	NotifiedReceiversThisLaunch.Reset();

	if (IsValid(ProjectileBody))
	{
		ProjectileBody->OnComponentHit.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandleProjectileHit);
		ProjectileBody->OnComponentWake.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandleProjectileWake);
		ProjectileBody->OnComponentSleep.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandleProjectileSleep);
		ProjectileBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ProjectileBody->SetSimulatePhysics(false);
	}

	if (bPreparationBindingsActive && IsValid(PreparationVolume))
	{
		PreparationVolume->OnComponentBeginOverlap.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandlePreparationVolumeBeginOverlap);
		PreparationVolume->OnComponentEndOverlap.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandlePreparationVolumeEndOverlap);
		bPreparationBindingsActive = false;
	}
	if (IsValid(PreparationVolume))
	{
		PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (IsValid(ExhaustVisualRoot))
	{
		ExhaustVisualRoot->SetVisibility(false, true);
	}

	UE_LOG(
		LogThrustGuidedHazardProjectile,
		Error,
		TEXT("Projectile %s disabled: %s"),
		*GetNameSafe(this),
		*Reason);
}
