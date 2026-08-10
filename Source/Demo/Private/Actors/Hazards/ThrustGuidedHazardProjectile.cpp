// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardProjectile.cpp
 * 职责：用尾部 UPhysicsThrusterComponent 的真实施力完成短时制导，并让碰撞后的路线完全服从 Chaos。
 * 边界：不用 UProjectileMovementComponent，不直接写速度/角速度/Transform，不补冲量，不做碰撞抖动过滤。
 * 轴约定：胶囊 +Z 是弹体前向；UE5.8 PhysicsThruster.cpp 固定沿组件局部 -X 向直接父刚体施力。
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
#include "PhysicsEngine/PhysicsThrusterComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogThrustGuidedHazardProjectile, Log, All);

namespace ThrustGuidedHazardProjectile
{
	/** 重冲击准备只做短距离 ETA 采样，不承担推进或物理仿真。 */
	constexpr float PreparationSampleIntervalSeconds = 1.0f / 60.0f;

	/** 严重掉帧时允许的最大准备帧数，与现有 HeavyImpact 发送端语义一致。 */
	constexpr float MaximumPreparationFrameMultiplier = 2.5f;

	/** 帧感知准备窗口绝对上限，避免一次卡顿造成长期提前接管。 */
	constexpr float AbsoluteMaximumPreparationSeconds = 0.5f;

}

/** 创建唯一物理胶囊、直接子级 Thruster、查询球和纯美术挂点。 */
AThrustGuidedHazardProjectile::AThrustGuidedHazardProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

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
	ProjectileBody->SetGenerateOverlapEvents(true);
	ProjectileBody->SetNotifyRigidBodyCollision(true);
	ProjectileBody->SetSimulatePhysics(false);
	ProjectileBody->SetEnableGravity(true);
	ProjectileBody->SetUseCCD(true);
	ProjectileBody->BodyInstance.bGenerateWakeEvents = true;
	SetRootComponent(ProjectileBody);

	Thruster = CreateDefaultSubobject<UPhysicsThrusterComponent>(TEXT("Thruster"));
	Thruster->SetupAttachment(ProjectileBody);
	Thruster->SetMobility(EComponentMobility::Movable);
	Thruster->SetAutoActivate(false);
	Thruster->PrimaryComponentTick.bStartWithTickEnabled = false;
	Thruster->SetComponentTickEnabled(false);
	Thruster->ThrustStrength = 0.0f;

	// Thruster 与 Actor 都在 PrePhysics；此依赖保证先更新喷口，再由组件施力。
	Thruster->AddTickPrerequisiteActor(this);

	BodyVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyVisualRoot"));
	BodyVisualRoot->SetupAttachment(ProjectileBody);

	ExhaustVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ExhaustVisualRoot"));
	ExhaustVisualRoot->SetupAttachment(Thruster);
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

/** 延迟生成阶段只保存输入，禁止绕过 BeginPlay 的统一物理启动顺序。 */
void AThrustGuidedHazardProjectile::ConfigureLaunch(
	UThrustGuidedHazardTuningData* InTuningData,
	USceneComponent* InTargetComponent,
	const FGuid& InLaunchId)
{
	if (HasActorBegunPlay())
	{
		UE_LOG(
			LogThrustGuidedHazardProjectile,
			Error,
			TEXT("ConfigureLaunch called after BeginPlay on %s."),
			*GetNameSafe(this));
		return;
	}

	bLaunchConfigured = true;
	RuntimeTuningData = InTuningData;
	LockedTargetComponent = InTargetComponent;
	LockedTargetActor = IsValid(InTargetComponent)
		? InTargetComponent->GetOwner()
		: nullptr;
	LaunchId = InLaunchId;
}

/** 统一校验配置、直接附着合同和单位缩放，再启动真实刚体与短时推进。 */
void AThrustGuidedHazardProjectile::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(false);
	Phase = EThrustGuidedHazardProjectilePhase::Uninitialized;

	if (!bLaunchConfigured)
	{
		DisableProjectile(TEXT("弹体未通过延迟生成调用 ConfigureLaunch。"));
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

	if (!LaunchId.IsValid())
	{
		DisableProjectile(TEXT("LaunchId 无效。"));
		return;
	}

	if (!GetActorScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		DisableProjectile(TEXT("Projectile Actor Scale 必须保持 (1,1,1)。"));
		return;
	}

	if (Thruster->GetAttachParent() != ProjectileBody)
	{
		DisableProjectile(TEXT("Thruster 必须直接附着 ProjectileBody，否则 UE5.8 不会向刚体施力。"));
		return;
	}

	ApplyConfiguration(*RuntimeTuningData);

	ProjectileBody->OnComponentHit.AddDynamic(
		this,
		&AThrustGuidedHazardProjectile::HandleProjectileHit);
	ProjectileBody->OnComponentWake.AddDynamic(
		this,
		&AThrustGuidedHazardProjectile::HandleProjectileWake);
	ProjectileBody->OnComponentSleep.AddDynamic(
		this,
		&AThrustGuidedHazardProjectile::HandleProjectileSleep);
	PreparationVolume->OnComponentBeginOverlap.AddDynamic(
		this,
		&AThrustGuidedHazardProjectile::HandlePreparationVolumeBeginOverlap);
	PreparationVolume->OnComponentEndOverlap.AddDynamic(
		this,
		&AThrustGuidedHazardProjectile::HandlePreparationVolumeEndOverlap);

	ProjectileBody->SetLinearDamping(RuntimeTuningData->PoweredLinearDamping);
	ProjectileBody->SetAngularDamping(RuntimeTuningData->PoweredAngularDamping);
	ProjectileBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PreparationVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProjectileBody->SetSimulatePhysics(true);
	ProjectileBody->SetMassOverrideInKg(
		NAME_None,
		RuntimeTuningData->ProjectileMassKilograms,
		true);
	ProjectileBody->SetEnableGravity(!RuntimeTuningData->bDisableGravityWhilePowered);
	ProjectileBody->WakeAllRigidBodies();

	PoweredElapsedSeconds = 0.0f;
	bHadMeaningfulBlockingContact = false;
	ContactSequence = 0;
	PreparationCandidates.Reset();
	NotifiedReceiversThisLaunch.Reset();
	Phase = EThrustGuidedHazardProjectilePhase::PoweredControlled;

	Thruster->Activate(true);
	Thruster->SetComponentTickEnabled(true);
	ExhaustVisualRoot->SetVisibility(true, true);
	SetActorTickEnabled(true);
	StartPreparationMonitoring();

	const FVector InitialVelocity = ProjectileBody->GetPhysicsLinearVelocity();
	UE_LOG(
		LogThrustGuidedHazardProjectile,
		Display,
		TEXT("Projectile %s started LaunchId=%s Data=%s Mass=%.2f MaxAccel=%.2f TargetSpeed=%.2f MaxSpeed=%.2f Location=%s InitialVelocity=%s."),
		*GetNameSafe(this),
		*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*GetPathNameSafe(RuntimeTuningData),
		ProjectileBody->GetMass(),
		RuntimeTuningData->MaximumPoweredAcceleration,
		RuntimeTuningData->TargetPoweredSpeed,
		RuntimeTuningData->MaximumPoweredSpeed,
		*ProjectileBody->GetComponentLocation().ToCompactString(),
		*InitialVelocity.ToCompactString());

	if (!IsLockedTargetUsable())
	{
		FinishPoweredPhase(TEXT("target invalid at BeginPlay"));
	}
}

/** 推进期先更新油门和姿态转矩；固定 Thruster 随后在同一 PrePhysics 帧施力。 */
void AThrustGuidedHazardProjectile::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Phase != EThrustGuidedHazardProjectilePhase::PoweredControlled)
	{
		return;
	}

	if (!IsValid(RuntimeTuningData)
		|| !FMath::IsFinite(DeltaSeconds)
		|| DeltaSeconds <= 0.0f)
	{
		FinishPoweredPhase(TEXT("invalid powered tick input"));
		return;
	}

	PoweredElapsedSeconds += DeltaSeconds;
	if (PoweredElapsedSeconds >= RuntimeTuningData->PoweredDurationSeconds)
	{
		FinishPoweredPhase(TEXT("powered timeout"));
		return;
	}

	if (!IsLockedTargetUsable())
	{
		FinishPoweredPhase(TEXT("locked target became invalid"));
		return;
	}

	FVector DesiredDirection = FVector::ZeroVector;
	float Throttle = 0.0f;
	if (!TryCalculateControlCommand(DesiredDirection, Throttle)
		|| !ApplyPhysicalAttitudeControl(DesiredDirection))
	{
		FinishPoweredPhase(TEXT("control command became invalid or exceeded safety speed"));
		return;
	}

	const float ActualMass = ProjectileBody->GetMass();
	if (!FMath::IsFinite(ActualMass) || ActualMass <= 0.0f)
	{
		FinishPoweredPhase(TEXT("rigid body mass became invalid"));
		return;
	}

	Thruster->ThrustStrength =
		ActualMass
		* RuntimeTuningData->MaximumPoweredAcceleration
		* FMath::Clamp(Throttle, 0.0f, 1.0f);
}

/** 清理全部本地委托、Timer 和弱引用；不会操作 Owner、角色或关卡。 */
void AThrustGuidedHazardProjectile::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	StopPreparationMonitoring();

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
	if (IsValid(PreparationVolume))
	{
		PreparationVolume->OnComponentBeginOverlap.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandlePreparationVolumeBeginOverlap);
		PreparationVolume->OnComponentEndOverlap.RemoveDynamic(
			this,
			&AThrustGuidedHazardProjectile::HandlePreparationVolumeEndOverlap);
	}

	LockedTargetComponent.Reset();
	LockedTargetActor.Reset();
	PreparationCandidates.Reset();
	NotifiedReceiversThisLaunch.Reset();

	Super::EndPlay(EndPlayReason);
}

/** 胶囊 +Z 是纵轴，Thruster 位于 -Z 尾端，默认把自身 -X 力轴映射到胶囊 +Z。 */
void AThrustGuidedHazardProjectile::ApplyConfiguration(
	const UThrustGuidedHazardTuningData& Tuning)
{
	ProjectileBody->SetCapsuleSize(
		Tuning.ProjectileRadius,
		Tuning.ProjectileHalfHeight,
		true);
	PreparationVolume->SetSphereRadius(
		Tuning.PreparationLookAheadDistance,
		true);

	const FQuat NeutralThrusterRotation =
		FQuat::FindBetweenNormals(FVector(-1.0f, 0.0f, 0.0f), FVector::UpVector);
	Thruster->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -Tuning.ProjectileHalfHeight),
		NeutralThrusterRotation,
		false,
		nullptr,
		ETeleportType::None);
	// BeginPlay/Tick 验证完成前绝不保留资产或 CDO 的旧推力。
	Thruster->ThrustStrength = 0.0f;
}

/** 目标组件必须仍有效、已注册且仍属于锁定 Actor；不按玩家/AI 类型分支。 */
bool AThrustGuidedHazardProjectile::IsLockedTargetUsable() const
{
	const USceneComponent* TargetComponent = LockedTargetComponent.Get();
	const AActor* TargetActor = LockedTargetActor.Get();
	return IsValid(TargetComponent)
		&& IsValid(TargetActor)
		&& TargetComponent->IsRegistered()
		&& TargetComponent->GetOwner() == TargetActor;
}

/** 受控阶段的唯一出口；不改速度，只撤销施力并切换为低阻尼 Chaos 自由运动。 */
void AThrustGuidedHazardProjectile::FinishPoweredPhase(const TCHAR* Reason)
{
	if (Phase != EThrustGuidedHazardProjectilePhase::PoweredControlled)
	{
		return;
	}

	FVector LinearVelocity = FVector::ZeroVector;
	FVector BodyForward = FVector::UpVector;
	if (IsValid(ProjectileBody))
	{
		LinearVelocity = ProjectileBody->GetPhysicsLinearVelocity();
		BodyForward = ProjectileBody->GetUpVector().GetSafeNormal();
	}
	const float ForwardSpeed =
		FVector::DotProduct(LinearVelocity, BodyForward);
	const float LateralSpeed =
		(LinearVelocity - BodyForward * ForwardSpeed).Size();
	float AppliedThrottle = 0.0f;
	if (IsValid(Thruster)
		&& IsValid(ProjectileBody)
		&& IsValid(RuntimeTuningData))
	{
		const float FullThrust =
			ProjectileBody->GetMass()
			* RuntimeTuningData->MaximumPoweredAcceleration;
		if (FMath::IsFinite(FullThrust) && FullThrust > KINDA_SMALL_NUMBER)
		{
			AppliedThrottle = FMath::Clamp(
				Thruster->ThrustStrength / FullThrust,
				0.0f,
				1.0f);
		}
	}

	Phase = EThrustGuidedHazardProjectilePhase::Coasting;
	LockedTargetComponent.Reset();
	LockedTargetActor.Reset();

	if (IsValid(Thruster))
	{
		Thruster->ThrustStrength = 0.0f;
		Thruster->Deactivate();
		Thruster->SetComponentTickEnabled(false);
	}
	if (IsValid(ExhaustVisualRoot))
	{
		ExhaustVisualRoot->SetVisibility(false, true);
	}
	if (IsValid(ProjectileBody))
	{
		ProjectileBody->SetEnableGravity(true);
		if (IsValid(RuntimeTuningData))
		{
			ProjectileBody->SetLinearDamping(
				RuntimeTuningData->CoastingLinearDamping);
			ProjectileBody->SetAngularDamping(
				RuntimeTuningData->CoastingAngularDamping);
		}
	}
	SetActorTickEnabled(false);

	UE_LOG(
		LogThrustGuidedHazardProjectile,
		Display,
		TEXT("LaunchId=%s powered phase finished after %.3f s: %s; TotalSpeed=%.2f ForwardSpeed=%.2f LateralSpeed=%.2f Throttle=%.3f Contact=%u; Chaos owns motion."),
		*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
		PoweredElapsedSeconds,
		Reason != nullptr ? Reason : TEXT("unspecified"),
		LinearVelocity.Size(),
		ForwardSpeed,
		LateralSpeed,
		AppliedThrottle,
		ContactSequence);
}

/** 动态前置只决定期望朝向；油门按有符号前向速度自然收小。 */
bool AThrustGuidedHazardProjectile::TryCalculateControlCommand(
	FVector& OutDesiredDirection,
	float& OutThrottle) const
{
	OutDesiredDirection = FVector::ZeroVector;
	OutThrottle = 0.0f;

	if (!IsLockedTargetUsable()
		|| !IsValid(RuntimeTuningData)
		|| !IsValid(ProjectileBody))
	{
		return false;
	}

	const USceneComponent* TargetComponent = LockedTargetComponent.Get();
	const FVector BodyCenter = ProjectileBody->GetCenterOfMass();
	const FVector BodyForward = ProjectileBody->GetUpVector().GetSafeNormal();
	const FVector BodyVelocity = ProjectileBody->GetPhysicsLinearVelocity();
	const FVector TargetLocation = TargetComponent->GetComponentLocation();
	const FVector TargetVelocity = TargetComponent->GetComponentVelocity();
	if (BodyCenter.ContainsNaN()
		|| BodyForward.IsNearlyZero()
		|| BodyForward.ContainsNaN()
		|| BodyVelocity.ContainsNaN()
		|| TargetLocation.ContainsNaN()
		|| TargetVelocity.ContainsNaN())
	{
		return false;
	}

	const float ForwardSpeed =
		FVector::DotProduct(BodyVelocity, BodyForward);
	const FVector LateralVelocity =
		BodyVelocity - BodyForward * ForwardSpeed;
	const float LateralSpeed = LateralVelocity.Size();
	const float TotalSpeed = BodyVelocity.Size();
	const float ComponentLimit = RuntimeTuningData->MaximumPoweredSpeed;
	const float EmergencyTotalLimit =
		RuntimeTuningData->MaximumPoweredSpeed
		+ RuntimeTuningData->SpeedControlBand;
	if (!FMath::IsFinite(ForwardSpeed)
		|| !FMath::IsFinite(LateralSpeed)
		|| !FMath::IsFinite(TotalSpeed))
	{
		return false;
	}

	const TCHAR* SafetyReason = nullptr;
	if (FMath::Abs(ForwardSpeed) >= ComponentLimit)
	{
		SafetyReason = TEXT("forward component limit");
	}
	else if (LateralSpeed >= ComponentLimit)
	{
		SafetyReason = TEXT("lateral component limit");
	}
	else if (TotalSpeed >= EmergencyTotalLimit)
	{
		SafetyReason = TEXT("combined total speed limit");
	}
	if (SafetyReason != nullptr)
	{
		UE_LOG(
			LogThrustGuidedHazardProjectile,
			Warning,
			TEXT("LaunchId=%s powered safety stop (%s): TotalSpeed=%.2f ForwardSpeed=%.2f LateralSpeed=%.2f Limits=%.2f/%.2f."),
			*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
			SafetyReason,
			TotalSpeed,
			ForwardSpeed,
			LateralSpeed,
			ComponentLimit,
			EmergencyTotalLimit);
		return false;
	}

	const float Distance = FVector::Distance(BodyCenter, TargetLocation);
	if (!FMath::IsFinite(Distance))
	{
		return false;
	}
	const float LeadSeconds = FMath::Clamp(
		Distance / FMath::Max(RuntimeTuningData->TargetPoweredSpeed, 1.0f),
		0.0f,
		RuntimeTuningData->MaximumTargetLeadTimeSeconds);
	const FVector PredictedTarget =
		TargetLocation + TargetVelocity * LeadSeconds;
	OutDesiredDirection =
		(PredictedTarget - BodyCenter).GetSafeNormal();
	if (OutDesiredDirection.IsNearlyZero()
		|| OutDesiredDirection.ContainsNaN())
	{
		return false;
	}

	const float SpeedThrottle = FMath::Clamp(
		(RuntimeTuningData->TargetPoweredSpeed - ForwardSpeed)
			/ RuntimeTuningData->SpeedControlBand,
		0.0f,
		1.0f);
	const float AlignmentThrottle = FMath::Max(
		0.0f,
		FVector::DotProduct(BodyForward, OutDesiredDirection));
	OutThrottle = SpeedThrottle * AlignmentThrottle;
	return FMath::IsFinite(OutThrottle);
}

/** 把期望世界角加速度转换到质量空间，与局部惯量逐轴相乘后施加真实世界转矩。 */
bool AThrustGuidedHazardProjectile::ApplyPhysicalAttitudeControl(
	const FVector& DesiredDirection)
{
	if (!IsValid(RuntimeTuningData) || !IsValid(ProjectileBody))
	{
		return false;
	}

	const FVector BodyForward = ProjectileBody->GetUpVector().GetSafeNormal();
	const FVector NormalizedDesired = DesiredDirection.GetSafeNormal();
	if (BodyForward.IsNearlyZero()
		|| NormalizedDesired.IsNearlyZero()
		|| BodyForward.ContainsNaN()
		|| NormalizedDesired.ContainsNaN())
	{
		return false;
	}

	const float Dot = FMath::Clamp(
		FVector::DotProduct(BodyForward, NormalizedDesired),
		-1.0f,
		1.0f);
	const float ErrorRadians = FMath::Acos(Dot);
	FVector ErrorAxis =
		FVector::CrossProduct(BodyForward, NormalizedDesired).GetSafeNormal();
	if (ErrorAxis.IsNearlyZero() && Dot < 0.0f)
	{
		ErrorAxis = ProjectileBody->GetForwardVector().GetSafeNormal();
	}

	const FVector AngularVelocity =
		ProjectileBody->GetPhysicsAngularVelocityInRadians();
	FVector DesiredAngularAcceleration =
		ErrorAxis * ErrorRadians * RuntimeTuningData->OrientationGain
		- AngularVelocity * RuntimeTuningData->AngularVelocityDampingGain;
	DesiredAngularAcceleration = DesiredAngularAcceleration.GetClampedToMaxSize(
		RuntimeTuningData->MaximumAngularAcceleration);
	if (AngularVelocity.ContainsNaN()
		|| DesiredAngularAcceleration.ContainsNaN())
	{
		return false;
	}

	FBodyInstance& BodyInstance = ProjectileBody->BodyInstance;
	const FVector LocalInertia = BodyInstance.GetBodyInertiaTensor();
	const FTransform MassToWorld = BodyInstance.GetMassSpaceToWorldSpace();
	if (!FMath::IsFinite(LocalInertia.X)
		|| !FMath::IsFinite(LocalInertia.Y)
		|| !FMath::IsFinite(LocalInertia.Z)
		|| LocalInertia.X <= KINDA_SMALL_NUMBER
		|| LocalInertia.Y <= KINDA_SMALL_NUMBER
		|| LocalInertia.Z <= KINDA_SMALL_NUMBER
		|| MassToWorld.ContainsNaN())
	{
		return false;
	}

	const FVector LocalAngularAcceleration =
		MassToWorld.InverseTransformVectorNoScale(DesiredAngularAcceleration);
	const FVector LocalTorque(
		LocalAngularAcceleration.X * LocalInertia.X,
		LocalAngularAcceleration.Y * LocalInertia.Y,
		LocalAngularAcceleration.Z * LocalInertia.Z);
	const FVector WorldTorque =
		MassToWorld.TransformVectorNoScale(LocalTorque);
	if (WorldTorque.ContainsNaN())
	{
		return false;
	}

	ProjectileBody->AddTorqueInRadians(WorldTorque, NAME_None, false);
	return true;
}

/** 立即刷新当前重叠者并启动独立 Timer；同一弹体不会重复创建 Timer。 */
void AThrustGuidedHazardProjectile::StartPreparationMonitoring()
{
	if (Phase == EThrustGuidedHazardProjectilePhase::Disabled
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

/** 休眠和 EndPlay 都只清除本 Actor 的预测 Timer。 */
void AThrustGuidedHazardProjectile::StopPreparationMonitoring()
{
	GetWorldTimerManager().ClearTimer(PreparationTimerHandle);
}

/** Accepted/Duplicate 才完成该接收者本次通知；Busy/Invalid 保留以便下一采样重试。 */
void AThrustGuidedHazardProjectile::EvaluatePreparationCandidates()
{
	if (Phase == EThrustGuidedHazardProjectilePhase::Disabled
		|| Phase == EThrustGuidedHazardProjectilePhase::Sleeping
		|| !LaunchId.IsValid())
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
			// 同一个 LaunchId 可分别通知玩家、追猎者和其他接收者。
			NotifiedReceiversThisLaunch.Add(Candidate);
		}
	}
}

/**
 * 预测只决定接收端何时准备物理身体；SourceLinearVelocity 来自当前 Chaos 刚体，
 * 最终位移、转动和路线仍由随后发生的真实接触决定。
 */
bool AThrustGuidedHazardProjectile::BuildPreparationRequest(
	const AActor& Receiver,
	FHeavyImpactPreparationRequest& OutRequest)
{
	if (!IsValid(ProjectileBody)
		|| !IsValid(RuntimeTuningData)
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

	// Physics Asset 最近点查询失败时，只回退同一个权威组件 Bounds，不改用角色外层 Capsule。
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
	const float EstimatedTimeToContact = SurfaceGap / ClosingSpeed;
	const UWorld* World = GetWorld();
	const float DeltaSeconds =
		IsValid(World) ? FMath::Max(0.0f, World->GetDeltaSeconds()) : 0.0f;
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
		BodyCenter + BodyVelocity * EstimatedTimeToContact;
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

/** 每次回调都保留诊断；只有第一个“有效阻挡”改变 Guided 状态，之后不去重真实接触。 */
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

	const AActor* ContactOwner = IsValid(OtherActor)
		? OtherActor
		: (IsValid(OtherComponent) ? OtherComponent->GetOwner() : nullptr);
	const bool bBlockingContact =
		Hit.bBlockingHit
		&& IsValid(OtherComponent)
		&& IsValid(ContactOwner)
		&& ContactOwner != this;
	if (bHadMeaningfulBlockingContact || !bBlockingContact)
	{
		return;
	}

	bHadMeaningfulBlockingContact = true;
	if (Hit.bStartPenetrating)
	{
		UE_LOG(
			LogThrustGuidedHazardProjectile,
			Warning,
			TEXT("LaunchId=%s received penetrating blocking contact with %s."),
			*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*GetNameSafe(ContactOwner));
	}

	FinishPoweredPhase(
		Hit.bStartPenetrating
			? TEXT("penetrating blocking contact")
			: TEXT("first blocking contact"));
}

/** 休眠后被外力唤醒只恢复 Coasting 预测，绝不恢复目标或制导。 */
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
		Phase = EThrustGuidedHazardProjectilePhase::Coasting;
		StartPreparationMonitoring();
	}
}

/** 只有无动力滑行可进入 Sleeping；推进期间的 AddForce 会继续保持刚体活跃。 */
void AThrustGuidedHazardProjectile::HandleProjectileSleep(
	UPrimitiveComponent* SleepingComponent,
	FName /*BoneName*/)
{
	if (SleepingComponent != ProjectileBody
		|| Phase != EThrustGuidedHazardProjectilePhase::Coasting)
	{
		return;
	}

	Phase = EThrustGuidedHazardProjectilePhase::Sleeping;
	StopPreparationMonitoring();
}

/** Overlap 只登记共享接口，不在回调中直接切换角色状态。 */
void AThrustGuidedHazardProjectile::HandlePreparationVolumeBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (Phase != EThrustGuidedHazardProjectilePhase::Disabled
		&& IsValid(OtherActor)
		&& OtherActor != this
		&& OtherActor->GetClass()->ImplementsInterface(
			UHeavyImpactReceiver::StaticClass()))
	{
		PreparationCandidates.Add(OtherActor);
	}
}

/** 多组件 Actor 仅在最后一个组件离开球体时移除。 */
void AThrustGuidedHazardProjectile::HandlePreparationVolumeEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/)
{
	if (IsValid(OtherActor)
		&& !PreparationVolume->IsOverlappingActor(OtherActor))
	{
		PreparationCandidates.Remove(OtherActor);
	}
}

/** 无法安全运行时关闭全部本地能力；不自动销毁，便于在 Level0 中诊断错误实例。 */
void AThrustGuidedHazardProjectile::DisableProjectile(const FString& Reason)
{
	StopPreparationMonitoring();
	Phase = EThrustGuidedHazardProjectilePhase::Disabled;
	SetActorTickEnabled(false);
	LockedTargetComponent.Reset();
	LockedTargetActor.Reset();
	PreparationCandidates.Reset();
	NotifiedReceiversThisLaunch.Reset();

	if (IsValid(Thruster))
	{
		Thruster->ThrustStrength = 0.0f;
		Thruster->Deactivate();
		Thruster->SetComponentTickEnabled(false);
	}
	if (IsValid(ExhaustVisualRoot))
	{
		ExhaustVisualRoot->SetVisibility(false, true);
	}
	if (IsValid(PreparationVolume))
	{
		PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (IsValid(ProjectileBody))
	{
		ProjectileBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ProjectileBody->SetSimulatePhysics(false);
	}

	UE_LOG(
		LogThrustGuidedHazardProjectile,
		Error,
		TEXT("Projectile %s disabled: %s"),
		*GetNameSafe(this),
		*Reason);
}
