// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardLauncher.cpp
 * 职责：锁定第一个角色，在预警期预测移动目标并转动机械炮管，随后生成一次真实 Chaos 弹体。
 * 边界：发射器只决定初始速度；弹体离膛后不追踪、不修正速度，也不依赖关卡或 Actor 名称。
 * 轴约定：Muzzle/ProjectileSpawnPoint 局部 +X 是炮管方向；弹体局部 +Z 与该方向对齐。
 */

#include "Actors/Hazards/ThrustGuidedHazardLauncher.h"

#include "Actors/Hazards/ThrustGuidedHazardProjectile.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Data/Hazards/ThrustGuidedHazardTuningData.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogThrustGuidedHazardLauncher, Log, All);

namespace ThrustGuidedHazardLauncher
{
	constexpr int32 MaximumSolveIterations = 8;
	constexpr float TimeResidualToleranceSeconds = 0.01f;
	constexpr float AimPointResidualToleranceCentimeters = 15.0f;
	constexpr float SpawnPointResidualToleranceCentimeters = 1.0f;
	constexpr float DirectionResidualToleranceDegrees = 0.1f;
	constexpr float LaunchSpeedRelativeTolerance = 0.02f;

	bool IsFiniteVector(const FVector& Value)
	{
		return !Value.ContainsNaN()
			&& FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	/** 镜像 ProjectileBody 对场景对象类型的阻挡响应，避免纯查询体造成出生假失败。 */
	bool ProjectileBlocksObjectType(const ECollisionChannel ObjectType)
	{
		switch (ObjectType)
		{
		case ECC_WorldStatic:
		case ECC_WorldDynamic:
		case ECC_PhysicsBody:
		case ECC_Pawn:
		case ECC_Visibility:
			return true;
		default:
			return false;
		}
	}

	const TCHAR* AimSourceToString(const EThrustGuidedHazardAimSource Source)
	{
		switch (Source)
		{
		case EThrustGuidedHazardAimSource::PredictedIntercept:
			return TEXT("PredictedIntercept");
		case EThrustGuidedHazardAimSource::CurrentTarget:
			return TEXT("CurrentTarget");
		case EThrustGuidedHazardAimSource::ClosestReachable:
			return TEXT("ClosestReachable");
		case EThrustGuidedHazardAimSource::MechanicallyLimited:
			return TEXT("MechanicallyLimited");
		case EThrustGuidedHazardAimSource::MechanicalForward:
		default:
			return TEXT("MechanicalForward");
		}
	}
}

AThrustGuidedHazardLauncher::AThrustGuidedHazardLauncher()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	HousingVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HousingVisualRoot"));
	HousingVisualRoot->SetupAttachment(SceneRoot);

	AimPivot = CreateDefaultSubobject<USceneComponent>(TEXT("AimPivot"));
	AimPivot->SetupAttachment(SceneRoot);

	Muzzle = CreateDefaultSubobject<USceneComponent>(TEXT("Muzzle"));
	Muzzle->SetupAttachment(AimPivot);
	Muzzle->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));

	ProjectileSpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ProjectileSpawnPoint"));
	ProjectileSpawnPoint->SetupAttachment(Muzzle);

	TriggerAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("TriggerAnchor"));
	TriggerAnchor->SetupAttachment(SceneRoot);
	TriggerAnchor->SetRelativeLocation(FVector(600.0f, 0.0f, 0.0f));

	TriggerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerVolume"));
	TriggerVolume->SetupAttachment(TriggerAnchor);
	TriggerVolume->SetMobility(EComponentMobility::Movable);
	TriggerVolume->SetCanEverAffectNavigation(false);
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TriggerVolume->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerVolume->SetGenerateOverlapEvents(true);

	WarningVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WarningVisualRoot"));
	WarningVisualRoot->SetupAttachment(HousingVisualRoot);
	WarningVisualRoot->SetVisibility(false, true);

	const UThrustGuidedHazardTuningData* Defaults =
		GetDefault<UThrustGuidedHazardTuningData>();
	ApplyTriggerGeometry(*Defaults);
	ApplyProjectileSpawnOffset(*Defaults);
}

void AThrustGuidedHazardLauncher::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const UThrustGuidedHazardTuningData* PreviewData = IsValid(TuningData)
		? TuningData.Get()
		: GetDefault<UThrustGuidedHazardTuningData>();
	FString Error;
	if (IsValid(PreviewData) && PreviewData->IsConfigured(Error))
	{
		ApplyTriggerGeometry(*PreviewData);
		ApplyProjectileSpawnOffset(*PreviewData);
	}
}

void AThrustGuidedHazardLauncher::BeginPlay()
{
	Super::BeginPlay();
	Phase = EThrustGuidedHazardLauncherPhase::Disabled;
	WarningVisualRoot->SetVisibility(false, true);

	if (!IsValid(TuningData))
	{
		DisableHazard(TEXT("未指定 ThrustGuidedHazardTuningData。"));
		return;
	}

	FString Error;
	if (!TuningData->IsConfigured(Error))
	{
		DisableHazard(Error);
		return;
	}
	if (!ProjectileClass)
	{
		DisableHazard(TEXT("未指定 AThrustGuidedHazardProjectile 子类。"));
		return;
	}
	if (!GetActorScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		DisableHazard(TEXT("Launcher Actor Scale 必须保持 (1,1,1)。"));
		return;
	}

	const UWorld* World = GetWorld();
	if (!IsValid(World)
		|| !FMath::IsFinite(World->GetGravityZ())
		|| FMath::IsNearlyZero(World->GetGravityZ()))
	{
		DisableHazard(TEXT("World 重力无效，无法推导固定初速。"));
		return;
	}

	ApplyTriggerGeometry(*TuningData);
	ApplyProjectileSpawnOffset(*TuningData);
	if (!CacheNeutralAssembly(Error))
	{
		DisableHazard(Error);
		return;
	}

	const FTransform NeutralSpawn =
		BuildHypotheticalSpawnTransform(NeutralAimDirection);
	const FThrustGuidedHazardSpawnCheckResult NeutralCheck =
		CheckFinalSpawnClearance(NeutralSpawn);
	if (NeutralCheck.Result == EThrustGuidedHazardSpawnCheck::StaticAssemblyFault
		|| NeutralCheck.Result == EThrustGuidedHazardSpawnCheck::InvalidQuery)
	{
		DisableHazard(NeutralCheck.Reason);
		return;
	}
	if (NeutralCheck.Result == EThrustGuidedHazardSpawnCheck::RuntimeObstruction)
	{
		UE_LOG(
			LogThrustGuidedHazardLauncher,
			Warning,
			TEXT("Launcher %s arms with a movable obstruction near the neutral muzzle: %s. Fire-time clearance remains authoritative."),
			*GetNameSafe(this),
			*NeutralCheck.Reason);
	}

	TriggerVolume->OnComponentBeginOverlap.AddDynamic(
		this,
		&AThrustGuidedHazardLauncher::HandleTriggerBeginOverlap);
	Phase = EThrustGuidedHazardLauncherPhase::Armed;
	// SetCollisionEnabled 会同步刷新已有重叠；必须先进入 Armed，才能接住盒内角色。
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Display,
		TEXT("Launcher %s armed. DerivedSpeed=%.2f NeutralDirection=%s Spawn=%s."),
		*GetNameSafe(this),
		CalculateDerivedLaunchSpeed(),
		*NeutralAimDirection.ToCompactString(),
		*ProjectileSpawnPoint->GetComponentLocation().ToCompactString());
}

void AThrustGuidedHazardLauncher::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearWarningState();
	if (IsValid(TriggerVolume))
	{
		TriggerVolume->OnComponentBeginOverlap.RemoveDynamic(
			this,
			&AThrustGuidedHazardLauncher::HandleTriggerBeginOverlap);
	}
	Super::EndPlay(EndPlayReason);
}

void AThrustGuidedHazardLauncher::ApplyTriggerGeometry(
	const UThrustGuidedHazardTuningData& Tuning)
{
	TriggerVolume->SetBoxExtent(Tuning.TriggerHalfExtent, true);
	TriggerVolume->SetRelativeLocationAndRotation(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		false,
		nullptr,
		ETeleportType::None);
}

void AThrustGuidedHazardLauncher::ApplyProjectileSpawnOffset(
	const UThrustGuidedHazardTuningData& Tuning)
{
	ProjectileSpawnPoint->SetRelativeLocationAndRotation(
		FVector(Tuning.ProjectileHalfHeight + Tuning.SpawnClearanceMargin, 0.0f, 0.0f),
		FRotator::ZeroRotator,
		false,
		nullptr,
		ETeleportType::None);
}

bool AThrustGuidedHazardLauncher::CacheNeutralAssembly(FString& OutError)
{
	bNeutralAssemblyCached = false;
	if (!IsValid(AimPivot)
		|| !IsValid(Muzzle)
		|| !IsValid(ProjectileSpawnPoint)
		|| Muzzle->GetAttachParent() != AimPivot
		|| ProjectileSpawnPoint->GetAttachParent() != Muzzle)
	{
		OutError = TEXT("AimPivot -> Muzzle -> ProjectileSpawnPoint 原生层级无效。");
		return false;
	}
	if (!AimPivot->GetComponentScale().Equals(FVector::OneVector, KINDA_SMALL_NUMBER)
		|| !Muzzle->GetComponentScale().Equals(FVector::OneVector, KINDA_SMALL_NUMBER)
		|| !ProjectileSpawnPoint->GetComponentScale().Equals(
			FVector::OneVector,
			KINDA_SMALL_NUMBER))
	{
		OutError = TEXT("AimPivot/Muzzle/ProjectileSpawnPoint 必须保持单位缩放。");
		return false;
	}

	NeutralAimPivotWorldRotation = AimPivot->GetComponentQuat().GetNormalized();
	NeutralAimRotation = Muzzle->GetComponentQuat().GetNormalized();
	NeutralAimDirection = Muzzle->GetForwardVector().GetSafeNormal();
	NeutralMuzzleRelativeTransform = Muzzle->GetRelativeTransform();
	NeutralSpawnPointRelativeTransform = ProjectileSpawnPoint->GetRelativeTransform();
	if (NeutralAimPivotWorldRotation.ContainsNaN()
		|| NeutralAimRotation.ContainsNaN()
		|| NeutralAimDirection.IsNearlyZero()
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(NeutralAimDirection)
		|| NeutralMuzzleRelativeTransform.ContainsNaN()
		|| NeutralSpawnPointRelativeTransform.ContainsNaN())
	{
		OutError = TEXT("瞄准组件包含非有限变换或无效中性前向。");
		return false;
	}

	bNeutralAssemblyCached = true;
	OutError.Reset();
	return true;
}

void AThrustGuidedHazardLauncher::EnterWarning(ACharacter& TargetCharacter)
{
	if (Phase != EThrustGuidedHazardLauncherPhase::Armed)
	{
		return;
	}

	Phase = EThrustGuidedHazardLauncherPhase::Warning;
	LockedTargetActor = &TargetCharacter;
	bHasLastValidTargetState = false;
	LastAimSolution = FThrustGuidedHazardAimSolution();
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WarningVisualRoot->SetVisibility(true, true);

	const UWorld* World = GetWorld();
	LastAimUpdateWorldSeconds = IsValid(World) ? World->GetTimeSeconds() : 0.0;
	CaptureLastValidTargetState();
	UpdateWarningAim();

	GetWorldTimerManager().SetTimer(
		AimTimerHandle,
		this,
		&AThrustGuidedHazardLauncher::UpdateWarningAim,
		TuningData->AimUpdateIntervalSeconds,
		true);
	GetWorldTimerManager().SetTimer(
		WarningTimerHandle,
		this,
		&AThrustGuidedHazardLauncher::FireLockedTarget,
		TuningData->WarningSeconds,
		false);
	// 先建立完整内部状态和 Timer，再允许 Blueprint 表现回调，避免回调销毁 Actor 后继续写状态。
	ReceiveWarningStarted(&TargetCharacter);

	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Display,
		TEXT("Launcher %s locked first character %s for %.3f s warning."),
		*GetNameSafe(this),
		*GetNameSafe(&TargetCharacter),
		TuningData->WarningSeconds);
}

bool AThrustGuidedHazardLauncher::CaptureLastValidTargetState()
{
	ACharacter* Target = LockedTargetActor.Get();
	if (!IsValid(Target))
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
	if (!IsValid(Capsule)
		|| !Capsule->IsRegistered()
		|| Capsule->GetOwner() != Target)
	{
		return false;
	}

	const FVector Location = Capsule->GetComponentLocation();
	const FVector Velocity = Target->GetVelocity();
	if (!ThrustGuidedHazardLauncher::IsFiniteVector(Location)
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(Velocity))
	{
		return false;
	}

	LastValidTargetLocation = Location;
	LastValidTargetVelocity = Velocity;
	bHasLastValidTargetState = true;
	return true;
}

void AThrustGuidedHazardLauncher::UpdateWarningAim()
{
	if (Phase != EThrustGuidedHazardLauncherPhase::Warning
		|| !IsValid(TuningData)
		|| !bNeutralAssemblyCached)
	{
		return;
	}

	CaptureLastValidTargetState();
	FThrustGuidedHazardAimSolution Candidate;
	bool bSolved = bHasLastValidTargetState
		&& TrySolvePredictedIntercept(Candidate);
	if (!bSolved && bHasLastValidTargetState)
	{
		const FVector CurrentAimPoint =
			LastValidTargetLocation
			+ FVector::UpVector * TuningData->TargetAimHeightOffset;
		bSolved = TrySolveAimToPoint(
			CurrentAimPoint,
			false,
			EThrustGuidedHazardAimSource::CurrentTarget,
			Candidate);
		if (!bSolved)
		{
			bSolved = TrySolveAimToPoint(
				CurrentAimPoint,
				true,
				EThrustGuidedHazardAimSource::ClosestReachable,
				Candidate);
		}
	}
	if (!bSolved)
	{
		Candidate = BuildMechanicalFallback();
	}

	const UWorld* World = GetWorld();
	const double Now = IsValid(World) ? World->GetTimeSeconds() : LastAimUpdateWorldSeconds;
	const float DeltaSeconds = FMath::Clamp(
		static_cast<float>(Now - LastAimUpdateWorldSeconds),
		0.0f,
		0.25f);
	LastAimUpdateWorldSeconds = Now;
	Candidate.bSlewLimited = AdvanceAimPivot(
		Candidate.DesiredLaunchVelocity.GetSafeNormal(),
		DeltaSeconds);
	LastAimSolution = Candidate;

	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Verbose,
		TEXT("Launcher %s aim Source=%s Aim=%s Spawn=%s Flight=%.3f Iter=%d Residual(time=%.4f target=%.2f spawn=%.2f dir=%.3f) Limits(yaw=%d pitch=%d slew=%d)."),
		*GetNameSafe(this),
		ThrustGuidedHazardLauncher::AimSourceToString(Candidate.Source),
		*Candidate.AimPoint.ToCompactString(),
		*Candidate.HypotheticalSpawnPoint.ToCompactString(),
		Candidate.EstimatedFlightTime,
		Candidate.IterationCount,
		Candidate.TimeResidualSeconds,
		Candidate.AimPointResidualCentimeters,
		Candidate.SpawnPointResidualCentimeters,
		Candidate.DirectionResidualDegrees,
		Candidate.bYawLimited,
		Candidate.bPitchLimited,
		Candidate.bSlewLimited);
}

float AThrustGuidedHazardLauncher::CalculateDerivedLaunchSpeed() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(TuningData) || !IsValid(World))
	{
		return 0.0f;
	}
	const float GravityMagnitude = FMath::Abs(World->GetGravityZ());
	const float SineDoubleAngle = FMath::Sin(
		2.0f * FMath::DegreesToRadians(TuningData->PreferredLaunchAngleDegrees));
	if (!FMath::IsFinite(GravityMagnitude)
		|| !FMath::IsFinite(SineDoubleAngle)
		|| GravityMagnitude <= KINDA_SMALL_NUMBER
		|| SineDoubleAngle <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	const float Speed = FMath::Sqrt(
		GravityMagnitude * TuningData->ReferenceRange / SineDoubleAngle);
	return FMath::IsFinite(Speed) ? Speed : 0.0f;
}

bool AThrustGuidedHazardLauncher::TrySolveLowArcToPoint(
	const FVector& StartPoint,
	const FVector& TargetPoint,
	const bool bAcceptClosest,
	FVector& OutLaunchVelocity) const
{
	OutLaunchVelocity = FVector::ZeroVector;
	if (!ThrustGuidedHazardLauncher::IsFiniteVector(StartPoint)
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(TargetPoint))
	{
		return false;
	}
	const FVector Delta = TargetPoint - StartPoint;
	if (FVector(Delta.X, Delta.Y, 0.0f).SizeSquared() <= 1.0f)
	{
		return false;
	}
	const float Speed = CalculateDerivedLaunchSpeed();
	if (!FMath::IsFinite(Speed) || Speed <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	UGameplayStatics::FSuggestProjectileVelocityParameters Parameters(
		this,
		StartPoint,
		TargetPoint,
		Speed);
	Parameters.bFavorHighArc = false;
	Parameters.CollisionRadius = 0.0f;
	Parameters.OverrideGravityZ = 0.0f;
	Parameters.TraceOption = ESuggestProjVelocityTraceOption::DoNotTrace;
	Parameters.bAcceptClosestOnNoSolutions = bAcceptClosest;

	if (!UGameplayStatics::SuggestProjectileVelocity(Parameters, OutLaunchVelocity)
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(OutLaunchVelocity))
	{
		OutLaunchVelocity = FVector::ZeroVector;
		return false;
	}
	const float ReturnedSpeed = OutLaunchVelocity.Size();
	if (!FMath::IsFinite(ReturnedSpeed)
		|| ReturnedSpeed <= KINDA_SMALL_NUMBER
		|| FMath::Abs(ReturnedSpeed - Speed) > Speed * ThrustGuidedHazardLauncher::LaunchSpeedRelativeTolerance)
	{
		OutLaunchVelocity = FVector::ZeroVector;
		return false;
	}
	return true;
}

bool AThrustGuidedHazardLauncher::TrySolveAimToPoint(
	const FVector& TargetPoint,
	const bool bAcceptClosest,
	const EThrustGuidedHazardAimSource ExactSource,
	FThrustGuidedHazardAimSolution& OutSolution) const
{
	OutSolution = FThrustGuidedHazardAimSolution();
	if (!bNeutralAssemblyCached
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(TargetPoint))
	{
		return false;
	}

	const float Speed = CalculateDerivedLaunchSpeed();
	FVector Direction = NeutralAimDirection;
	FTransform SpawnTransform = BuildHypotheticalSpawnTransform(Direction);
	float PreviousTime = 0.0f;
	FVector PreviousSpawnPoint = SpawnTransform.GetLocation();

	for (int32 Iteration = 1;
		Iteration <= ThrustGuidedHazardLauncher::MaximumSolveIterations;
		++Iteration)
	{
		FVector SolvedVelocity = FVector::ZeroVector;
		if (!TrySolveLowArcToPoint(
				SpawnTransform.GetLocation(),
				TargetPoint,
				bAcceptClosest,
				SolvedVelocity))
		{
			return false;
		}

		bool bYawLimited = false;
		bool bPitchLimited = false;
		const FVector LimitedDirection = ClampToMechanicalLimits(
			SolvedVelocity.GetSafeNormal(),
			bYawLimited,
			bPitchLimited);
		if (LimitedDirection.IsNearlyZero())
		{
			return false;
		}
		if ((bYawLimited || bPitchLimited) && !bAcceptClosest)
		{
			return false;
		}

		const FTransform NewSpawnTransform =
			BuildHypotheticalSpawnTransform(LimitedDirection);
		const FVector NewSpawnPoint = NewSpawnTransform.GetLocation();
		const FVector HorizontalDelta = TargetPoint - NewSpawnPoint;
		const float HorizontalSpeed =
			FVector(SolvedVelocity.X, SolvedVelocity.Y, 0.0f).Size();
		const float NewTime = HorizontalSpeed > KINDA_SMALL_NUMBER
			? FVector(HorizontalDelta.X, HorizontalDelta.Y, 0.0f).Size() / HorizontalSpeed
			: 0.0f;
		if (!FMath::IsFinite(NewTime) || NewTime <= 0.0f)
		{
			return false;
		}

		const float TimeResidual = Iteration > 1
			? FMath::Abs(NewTime - PreviousTime)
			: BIG_NUMBER;
		const float SpawnResidual = Iteration > 1
			? FVector::Distance(NewSpawnPoint, PreviousSpawnPoint)
			: BIG_NUMBER;
		const float DirectionResidual = Iteration > 1
			? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(Direction, LimitedDirection),
				-1.0f,
				1.0f)))
			: BIG_NUMBER;

		OutSolution.DesiredLaunchVelocity = LimitedDirection * Speed;
		OutSolution.AimPoint = TargetPoint;
		OutSolution.HypotheticalSpawnPoint = NewSpawnPoint;
		OutSolution.EstimatedFlightTime = NewTime;
		OutSolution.TimeResidualSeconds = TimeResidual;
		OutSolution.AimPointResidualCentimeters = 0.0f;
		OutSolution.SpawnPointResidualCentimeters = SpawnResidual;
		OutSolution.DirectionResidualDegrees = DirectionResidual;
		OutSolution.IterationCount = Iteration;
		OutSolution.bYawLimited = bYawLimited;
		OutSolution.bPitchLimited = bPitchLimited;
		OutSolution.Source = bYawLimited || bPitchLimited
			? EThrustGuidedHazardAimSource::MechanicallyLimited
			: ExactSource;

		if (Iteration > 1
			&& TimeResidual <= ThrustGuidedHazardLauncher::TimeResidualToleranceSeconds
			&& SpawnResidual <= ThrustGuidedHazardLauncher::SpawnPointResidualToleranceCentimeters
			&& DirectionResidual <= ThrustGuidedHazardLauncher::DirectionResidualToleranceDegrees)
		{
			return true;
		}

		Direction = LimitedDirection;
		PreviousTime = NewTime;
		PreviousSpawnPoint = NewSpawnPoint;
		SpawnTransform = NewSpawnTransform;
	}
	return false;
}

bool AThrustGuidedHazardLauncher::TrySolvePredictedIntercept(
	FThrustGuidedHazardAimSolution& OutSolution) const
{
	OutSolution = FThrustGuidedHazardAimSolution();
	if (!bHasLastValidTargetState
		|| !IsValid(TuningData)
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(LastValidTargetLocation)
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(LastValidTargetVelocity))
	{
		return false;
	}

	const float Speed = CalculateDerivedLaunchSpeed();
	if (Speed <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const FVector InitialSpawn =
		BuildHypotheticalSpawnTransform(NeutralAimDirection).GetLocation();
	float EstimatedTime = FVector::Distance(InitialSpawn, LastValidTargetLocation) / Speed;
	FVector PreviousAimPoint =
		LastValidTargetLocation
		+ LastValidTargetVelocity * EstimatedTime
		+ FVector::UpVector * TuningData->TargetAimHeightOffset;
	FVector PreviousSpawnPoint = InitialSpawn;
	FVector PreviousDirection = NeutralAimDirection;

	for (int32 Iteration = 1;
		Iteration <= ThrustGuidedHazardLauncher::MaximumSolveIterations;
		++Iteration)
	{
		const FVector PredictedAimPoint =
			LastValidTargetLocation
			+ LastValidTargetVelocity * EstimatedTime
			+ FVector::UpVector * TuningData->TargetAimHeightOffset;
		FThrustGuidedHazardAimSolution PointSolution;
		if (!TrySolveAimToPoint(
				PredictedAimPoint,
				false,
				EThrustGuidedHazardAimSource::PredictedIntercept,
				PointSolution)
			|| PointSolution.bYawLimited
			|| PointSolution.bPitchLimited)
		{
			return false;
		}

		const float NewTime = PointSolution.EstimatedFlightTime;
		const float TimeResidual = FMath::Abs(NewTime - EstimatedTime);
		const float AimPointResidual = FVector::Distance(
			PredictedAimPoint,
			PreviousAimPoint);
		const float SpawnPointResidual = FVector::Distance(
			PointSolution.HypotheticalSpawnPoint,
			PreviousSpawnPoint);
		const FVector NewDirection =
			PointSolution.DesiredLaunchVelocity.GetSafeNormal();
		const float DirectionResidual = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(NewDirection, PreviousDirection),
			-1.0f,
			1.0f)));

		PointSolution.Source = EThrustGuidedHazardAimSource::PredictedIntercept;
		PointSolution.IterationCount = Iteration;
		PointSolution.TimeResidualSeconds = TimeResidual;
		PointSolution.AimPointResidualCentimeters = AimPointResidual;
		PointSolution.SpawnPointResidualCentimeters = SpawnPointResidual;
		PointSolution.DirectionResidualDegrees = DirectionResidual;
		OutSolution = PointSolution;

		if (TimeResidual <= ThrustGuidedHazardLauncher::TimeResidualToleranceSeconds
			&& AimPointResidual <= ThrustGuidedHazardLauncher::AimPointResidualToleranceCentimeters
			&& SpawnPointResidual <= ThrustGuidedHazardLauncher::SpawnPointResidualToleranceCentimeters
			&& DirectionResidual <= ThrustGuidedHazardLauncher::DirectionResidualToleranceDegrees)
		{
			return true;
		}

		EstimatedTime = NewTime;
		PreviousAimPoint = PredictedAimPoint;
		PreviousSpawnPoint = PointSolution.HypotheticalSpawnPoint;
		PreviousDirection = NewDirection;
	}
	return false;
}

FThrustGuidedHazardAimSolution
AThrustGuidedHazardLauncher::BuildMechanicalFallback() const
{
	FThrustGuidedHazardAimSolution Solution;
	bool bYawLimited = false;
	bool bPitchLimited = false;
	const FVector DesiredDirection = NeutralAimRotation.RotateVector(
		FRotator(TuningData->FallbackElevationDegrees, 0.0f, 0.0f).Vector());
	const FVector LimitedDirection = ClampToMechanicalLimits(
		DesiredDirection,
		bYawLimited,
		bPitchLimited);
	const float Speed = CalculateDerivedLaunchSpeed();
	const FTransform Spawn = BuildHypotheticalSpawnTransform(LimitedDirection);
	Solution.DesiredLaunchVelocity = LimitedDirection * Speed;
	Solution.HypotheticalSpawnPoint = Spawn.GetLocation();
	Solution.AimPoint =
		Spawn.GetLocation() + LimitedDirection * TuningData->ReferenceRange;
	Solution.EstimatedFlightTime = TuningData->ReferenceRange / FMath::Max(Speed, 1.0f);
	Solution.IterationCount = 1;
	Solution.Source = EThrustGuidedHazardAimSource::MechanicalForward;
	Solution.bYawLimited = bYawLimited;
	Solution.bPitchLimited = bPitchLimited;
	return Solution;
}

FVector AThrustGuidedHazardLauncher::ClampToMechanicalLimits(
	const FVector& DesiredWorldDirection,
	bool& bOutYawLimited,
	bool& bOutPitchLimited) const
{
	bOutYawLimited = false;
	bOutPitchLimited = false;
	const FVector Desired = DesiredWorldDirection.GetSafeNormal();
	if (!bNeutralAssemblyCached || Desired.IsNearlyZero() || !IsValid(TuningData))
	{
		return NeutralAimDirection;
	}

	const FVector LocalDirection = NeutralAimRotation.UnrotateVector(Desired).GetSafeNormal();
	const float RawYawDegrees = FMath::RadiansToDegrees(
		FMath::Atan2(LocalDirection.Y, LocalDirection.X));
	const float RawPitchDegrees = FMath::RadiansToDegrees(FMath::Atan2(
		LocalDirection.Z,
		FMath::Sqrt(FMath::Square(LocalDirection.X) + FMath::Square(LocalDirection.Y))));
	const float LimitedYawDegrees = FMath::Clamp(
		RawYawDegrees,
		-TuningData->MaximumAimYawDegrees,
		TuningData->MaximumAimYawDegrees);
	const float LimitedPitchDegrees = FMath::Clamp(
		RawPitchDegrees,
		-TuningData->MaximumAimPitchDownDegrees,
		TuningData->MaximumAimPitchUpDegrees);
	bOutYawLimited = !FMath::IsNearlyEqual(RawYawDegrees, LimitedYawDegrees, 0.01f);
	bOutPitchLimited = !FMath::IsNearlyEqual(RawPitchDegrees, LimitedPitchDegrees, 0.01f);
	return NeutralAimRotation.RotateVector(
		FRotator(LimitedPitchDegrees, LimitedYawDegrees, 0.0f).Vector()).GetSafeNormal();
}

FTransform AThrustGuidedHazardLauncher::BuildHypotheticalSpawnTransform(
	const FVector& LimitedWorldDirection) const
{
	const FVector Direction = LimitedWorldDirection.GetSafeNormal();
	const FTransform PivotWorld(
		BuildAimPivotWorldRotation(Direction),
		AimPivot->GetComponentLocation(),
		FVector::OneVector);
	const FTransform MuzzleWorld = NeutralMuzzleRelativeTransform * PivotWorld;
	const FTransform SpawnPointWorld =
		NeutralSpawnPointRelativeTransform * MuzzleWorld;
	const FQuat CapsuleRotation =
		FQuat::FindBetweenNormals(FVector::UpVector, Direction).GetNormalized();
	return FTransform(CapsuleRotation, SpawnPointWorld.GetLocation(), FVector::OneVector);
}

FQuat AThrustGuidedHazardLauncher::BuildAimPivotWorldRotation(
	const FVector& LimitedWorldDirection) const
{
	const FVector Direction = LimitedWorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() || NeutralAimDirection.IsNearlyZero())
	{
		return NeutralAimPivotWorldRotation;
	}
	return (
		FQuat::FindBetweenNormals(NeutralAimDirection, Direction)
		* NeutralAimPivotWorldRotation).GetNormalized();
}

bool AThrustGuidedHazardLauncher::AdvanceAimPivot(
	const FVector& DesiredWorldDirection,
	const float DeltaSeconds)
{
	if (!IsValid(AimPivot)
		|| !IsValid(ProjectileSpawnPoint)
		|| !IsValid(TuningData))
	{
		return true;
	}
	bool bIgnoredYawLimit = false;
	bool bIgnoredPitchLimit = false;
	const FVector DesiredDirection = ClampToMechanicalLimits(
		DesiredWorldDirection,
		bIgnoredYawLimit,
		bIgnoredPitchLimit);

	auto ToNeutralYawPitch = [this](const FVector& WorldDirection)
	{
		const FVector LocalDirection =
			NeutralAimRotation.UnrotateVector(WorldDirection).GetSafeNormal();
		return FVector2D(
			FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.Y, LocalDirection.X)),
			FMath::RadiansToDegrees(FMath::Atan2(
				LocalDirection.Z,
				FMath::Sqrt(
					FMath::Square(LocalDirection.X)
					+ FMath::Square(LocalDirection.Y)))));
	};

	const FVector2D CurrentYawPitch = ToNeutralYawPitch(
		ProjectileSpawnPoint->GetForwardVector().GetSafeNormal());
	const FVector2D TargetYawPitch = ToNeutralYawPitch(DesiredDirection);
	FVector2D DeltaYawPitch(
		FMath::FindDeltaAngleDegrees(CurrentYawPitch.X, TargetYawPitch.X),
		TargetYawPitch.Y - CurrentYawPitch.Y);
	const float MaximumStepDegrees =
		TuningData->AimTurnSpeedDegreesPerSecond * FMath::Max(0.0f, DeltaSeconds);
	if (DeltaYawPitch.Size() > MaximumStepDegrees && MaximumStepDegrees > 0.0f)
	{
		DeltaYawPitch = DeltaYawPitch.GetSafeNormal() * MaximumStepDegrees;
	}
	else if (MaximumStepDegrees <= 0.0f)
	{
		DeltaYawPitch = FVector2D::ZeroVector;
	}

	const float NewYawDegrees = FMath::Clamp(
		CurrentYawPitch.X + DeltaYawPitch.X,
		-TuningData->MaximumAimYawDegrees,
		TuningData->MaximumAimYawDegrees);
	const float NewPitchDegrees = FMath::Clamp(
		CurrentYawPitch.Y + DeltaYawPitch.Y,
		-TuningData->MaximumAimPitchDownDegrees,
		TuningData->MaximumAimPitchUpDegrees);
	const FVector NewDirection = NeutralAimRotation.RotateVector(
		FRotator(NewPitchDegrees, NewYawDegrees, 0.0f).Vector()).GetSafeNormal();
	AimPivot->SetWorldRotation(
		BuildAimPivotWorldRotation(NewDirection),
		false,
		nullptr,
		ETeleportType::None);
	const FVector ActualDirection = ProjectileSpawnPoint->GetForwardVector().GetSafeNormal();
	const float ResidualDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(ActualDirection, DesiredDirection),
		-1.0f,
		1.0f)));
	return ResidualDegrees > ThrustGuidedHazardLauncher::DirectionResidualToleranceDegrees;
}

bool AThrustGuidedHazardLauncher::BuildActualSpawnTransform(
	FTransform& OutSpawnTransform,
	FVector& OutLaunchVelocity,
	FString& OutError) const
{
	OutSpawnTransform = FTransform::Identity;
	OutLaunchVelocity = FVector::ZeroVector;
	if (!IsValid(ProjectileSpawnPoint))
	{
		OutError = TEXT("ProjectileSpawnPoint 无效。");
		return false;
	}
	const FVector Direction = ProjectileSpawnPoint->GetForwardVector().GetSafeNormal();
	const FVector Location = ProjectileSpawnPoint->GetComponentLocation();
	const float Speed = CalculateDerivedLaunchSpeed();
	if (Direction.IsNearlyZero()
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(Direction)
		|| !ThrustGuidedHazardLauncher::IsFiniteVector(Location)
		|| !FMath::IsFinite(Speed)
		|| Speed <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("实际炮管方向、质心出生点或固定初速无效。");
		return false;
	}
	OutSpawnTransform = FTransform(
		FQuat::FindBetweenNormals(FVector::UpVector, Direction).GetNormalized(),
		Location,
		FVector::OneVector);
	OutLaunchVelocity = Direction * Speed;
	OutError.Reset();
	return true;
}

FThrustGuidedHazardSpawnCheckResult
AThrustGuidedHazardLauncher::CheckFinalSpawnClearance(
	const FTransform& SpawnTransform) const
{
	FThrustGuidedHazardSpawnCheckResult Result;
	const UWorld* World = GetWorld();
	if (!IsValid(World)
		|| !IsValid(TuningData)
		|| SpawnTransform.ContainsNaN()
		|| !SpawnTransform.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		Result.Result = EThrustGuidedHazardSpawnCheck::InvalidQuery;
		Result.Reason = TEXT("出生查询缺少 World/Tuning 或 SpawnTransform 非法。");
		return Result;
	}

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		TuningData->ProjectileRadius,
		TuningData->ProjectileHalfHeight);
	if (!Shape.IsCapsule() || Shape.IsNearlyZero())
	{
		Result.Result = EThrustGuidedHazardSpawnCheck::InvalidQuery;
		Result.Reason = TEXT("出生胶囊形状无效。");
		return Result;
	}

	auto ClassifyComponent = [this, &SpawnTransform](
		UPrimitiveComponent* Component,
		const FVector& SuggestedPoint,
		FThrustGuidedHazardSpawnCheckResult& OutResult) -> bool
	{
		if (!IsValid(Component)
			|| Component->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics
			|| Component->GetCollisionResponseToChannel(ECC_PhysicsBody) != ECR_Block
			|| !ThrustGuidedHazardLauncher::ProjectileBlocksObjectType(
				Component->GetCollisionObjectType()))
		{
			return false;
		}

		AActor* Owner = Component->GetOwner();
		OutResult.BlockingComponent = Component;
		OutResult.ApproximateContactPoint = SuggestedPoint;
		if (!ThrustGuidedHazardLauncher::IsFiniteVector(OutResult.ApproximateContactPoint))
		{
			OutResult.ApproximateContactPoint = Component->GetComponentLocation();
		}
		FVector ClosestPoint = FVector::ZeroVector;
		if (Component->GetClosestPointOnCollision(
				SpawnTransform.GetLocation(),
				ClosestPoint) >= 0.0f
			&& ThrustGuidedHazardLauncher::IsFiniteVector(ClosestPoint))
		{
			OutResult.ApproximateContactPoint = ClosestPoint;
		}

		const bool bStaticAssembly = Owner == this
			|| Component->GetCollisionObjectType() == ECC_WorldStatic;
		OutResult.Result = bStaticAssembly
			? EThrustGuidedHazardSpawnCheck::StaticAssemblyFault
			: EThrustGuidedHazardSpawnCheck::RuntimeObstruction;
		OutResult.Reason = FString::Printf(
			TEXT("%s blocks projectile spawn (%s)."),
			*GetNameSafe(Component),
			bStaticAssembly ? TEXT("static assembly") : TEXT("runtime obstruction"));
		return true;
	};

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams OverlapParams(
		SCENE_QUERY_STAT(ThrustGuidedHazardFinalSpawn),
		false);
	World->OverlapMultiByChannel(
		Overlaps,
		SpawnTransform.GetLocation(),
		SpawnTransform.GetRotation(),
		ECC_PhysicsBody,
		Shape,
		OverlapParams);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (ClassifyComponent(
				Overlap.GetComponent(),
				SpawnTransform.GetLocation(),
				Result))
		{
			return Result;
		}
	}

	const FVector Direction = ProjectileSpawnPoint->GetForwardVector().GetSafeNormal();
	const FVector SweepStart =
		Muzzle->GetComponentLocation() + Direction * TuningData->ProjectileHalfHeight;
	const FVector SweepEnd = SpawnTransform.GetLocation();
	if (TuningData->SpawnClearanceMargin > KINDA_SMALL_NUMBER
		&& FVector::DistSquared(SweepStart, SweepEnd) > 1.0f)
	{
		TArray<FHitResult> SweepHits;
		FCollisionQueryParams SweepParams(
			SCENE_QUERY_STAT(ThrustGuidedHazardMarginSweep),
			false);
		SweepParams.AddIgnoredActor(this);
		World->SweepMultiByChannel(
			SweepHits,
			SweepStart,
			SweepEnd,
			SpawnTransform.GetRotation(),
			ECC_PhysicsBody,
			Shape,
			SweepParams);
		for (const FHitResult& Hit : SweepHits)
		{
			if (ClassifyComponent(Hit.GetComponent(), Hit.ImpactPoint, Result))
			{
				return Result;
			}
		}
	}

	Result.Result = EThrustGuidedHazardSpawnCheck::Clear;
	Result.Reason.Reset();
	return Result;
}

void AThrustGuidedHazardLauncher::FireLockedTarget()
{
	if (Phase != EThrustGuidedHazardLauncherPhase::Warning)
	{
		return;
	}

	UpdateWarningAim();
	GetWorldTimerManager().ClearTimer(WarningTimerHandle);
	GetWorldTimerManager().ClearTimer(AimTimerHandle);

	FTransform SpawnTransform;
	FVector LaunchVelocity = FVector::ZeroVector;
	FString Error;
	if (!BuildActualSpawnTransform(SpawnTransform, LaunchVelocity, Error))
	{
		DisableHazard(Error);
		return;
	}

	const FThrustGuidedHazardSpawnCheckResult SpawnCheck =
		CheckFinalSpawnClearance(SpawnTransform);
	if (SpawnCheck.Result == EThrustGuidedHazardSpawnCheck::RuntimeObstruction)
	{
		CompleteBlockedDischarge(SpawnCheck);
		return;
	}
	if (SpawnCheck.Result != EThrustGuidedHazardSpawnCheck::Clear)
	{
		DisableHazard(SpawnCheck.Reason);
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		DisableHazard(TEXT("发射时 World 无效。"));
		return;
	}

	const FGuid LaunchId = FGuid::NewGuid();
	AThrustGuidedHazardProjectile* Projectile =
		World->SpawnActorDeferred<AThrustGuidedHazardProjectile>(
			ProjectileClass,
			SpawnTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
			ESpawnActorScaleMethod::OverrideRootScale);
	if (!IsValid(Projectile))
	{
		DisableHazard(TEXT("SpawnActorDeferred 未能创建弹体。"));
		return;
	}

	Projectile->ConfigureLaunch(TuningData, LaunchVelocity, LaunchId);
	AThrustGuidedHazardProjectile* FinishedProjectile = Cast<AThrustGuidedHazardProjectile>(
		UGameplayStatics::FinishSpawningActor(
			Projectile,
			SpawnTransform,
			ESpawnActorScaleMethod::OverrideRootScale));
	if (!IsValid(FinishedProjectile))
	{
		DisableHazard(TEXT("FinishSpawningActor 未能完成弹体生成。"));
		return;
	}
	Phase = EThrustGuidedHazardLauncherPhase::Spent;
	WarningVisualRoot->SetVisibility(false, true);

	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Display,
		TEXT("Launcher %s fired LaunchId=%s Source=%s Spawn=%s Velocity=%s Limits(yaw=%d pitch=%d slew=%d)."),
		*GetNameSafe(this),
		*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ThrustGuidedHazardLauncher::AimSourceToString(LastAimSolution.Source),
		*SpawnTransform.GetLocation().ToCompactString(),
		*LaunchVelocity.ToCompactString(),
		LastAimSolution.bYawLimited,
		LastAimSolution.bPitchLimited,
		LastAimSolution.bSlewLimited);

	LockedTargetActor.Reset();
	bHasLastValidTargetState = false;
	// 表现回调放在 C++ 状态收口之后；Blueprint 即使销毁 Launcher，也不会留下半完成状态。
	ReceiveProjectileFired(FinishedProjectile);
}

void AThrustGuidedHazardLauncher::ClearWarningState()
{
	GetWorldTimerManager().ClearTimer(WarningTimerHandle);
	GetWorldTimerManager().ClearTimer(AimTimerHandle);
	LockedTargetActor.Reset();
	bHasLastValidTargetState = false;
	LastAimUpdateWorldSeconds = 0.0;
	if (IsValid(WarningVisualRoot))
	{
		WarningVisualRoot->SetVisibility(false, true);
	}
}

void AThrustGuidedHazardLauncher::CompleteBlockedDischarge(
	const FThrustGuidedHazardSpawnCheckResult& CheckResult)
{
	ClearWarningState();
	Phase = EThrustGuidedHazardLauncherPhase::Spent;
	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Warning,
		TEXT("Launcher %s consumed its shot because the muzzle was blocked by %s at %s: %s"),
		*GetNameSafe(this),
		*GetNameSafe(CheckResult.BlockingComponent.Get()),
		*CheckResult.ApproximateContactPoint.ToCompactString(),
		*CheckResult.Reason);
	ReceiveBlockedDischarge(
		CheckResult.BlockingComponent.Get(),
		CheckResult.ApproximateContactPoint);
}

void AThrustGuidedHazardLauncher::HandleTriggerBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (OverlappedComponent != TriggerVolume
		|| Phase != EThrustGuidedHazardLauncherPhase::Armed)
	{
		return;
	}
	if (ACharacter* Character = Cast<ACharacter>(OtherActor))
	{
		EnterWarning(*Character);
	}
}

void AThrustGuidedHazardLauncher::DisableHazard(const FString& Reason)
{
	ClearWarningState();
	Phase = EThrustGuidedHazardLauncherPhase::Disabled;
	if (IsValid(TriggerVolume))
	{
		TriggerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Error,
		TEXT("Launcher %s disabled: %s"),
		*GetNameSafe(this),
		*Reason);
}
