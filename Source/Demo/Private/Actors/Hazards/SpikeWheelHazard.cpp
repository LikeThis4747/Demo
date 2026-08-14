// Copyright Epic Games, Inc. All Rights Reserved.

/** @file SpikeWheelHazard.cpp Builds and runs one deterministic, instance-varied spike-wheel route. */

#include "Actors/Hazards/SpikeWheelHazard.h"

#include "Components/SceneComponent.h"
#include "Characters/ZeroEscapeCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Hazards/SpikeWheelHazardTuningData.h"
#include "Data/Physics/CharacterImpactSourceProfile.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "Interfaces/CharacterImpactReceiver.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpikeWheel, Log, All);

namespace SpikeWheelHazard
{
	float WrapPositive(const float Value, const float Period)
	{
		const float Remainder = FMath::Fmod(Value, Period);
		return Remainder < 0.0f ? Remainder + Period : Remainder;
	}
}

ASpikeWheelHazard::ASpikeWheelHazard()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	RouteSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
	RouteSpline->SetupAttachment(SceneRoot);
	RouteSpline->SetMobility(EComponentMobility::Movable);
	RouteSpline->SetDrawDebug(true);
	RouteSpline->SetCanEverAffectNavigation(false);

	HurtZone = CreateDefaultSubobject<USphereComponent>(TEXT("HurtZone"));
	HurtZone->SetupAttachment(SceneRoot);
	HurtZone->SetMobility(EComponentMobility::Movable);
	HurtZone->SetSphereRadius(65.0f);
	HurtZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HurtZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	HurtZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	HurtZone->SetGenerateOverlapEvents(true);
	HurtZone->SetCanEverAffectNavigation(false);

	WheelSpinRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WheelSpinRoot"));
	WheelSpinRoot->SetupAttachment(HurtZone);
	WheelSpinRoot->SetMobility(EComponentMobility::Movable);

	WheelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WheelMesh"));
	WheelMesh->SetupAttachment(WheelSpinRoot);
	WheelMesh->SetMobility(EComponentMobility::Movable);
	WheelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WheelMesh->SetCanEverAffectNavigation(false);
}

void ASpikeWheelHazard::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	FString Error;
	if (IsValid(TuningData) && TuningData->IsConfigured(Error) && BuildRoute(Error))
	{
		ApplyGeometry();
		return;
	}

	RouteSpline->ClearSplinePoints(true);
}

void ASpikeWheelHazard::BeginPlay()
{
	Super::BeginPlay();

	FString Error;
	if (!GetActorScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		DisableHazard(TEXT("Actor scale must remain (1,1,1) so route footprint and contact radius agree."));
		return;
	}
	if (!IsValid(TuningData) || !TuningData->IsConfigured(Error) || !BuildRoute(Error))
	{
		DisableHazard(Error.IsEmpty() ? TEXT("TuningData is not assigned.") : Error);
		return;
	}

	ApplyGeometry();
	HurtZone->OnComponentBeginOverlap.AddUniqueDynamic(this, &ASpikeWheelHazard::HandleHurtZoneBeginOverlap);
	HurtZone->OnComponentEndOverlap.AddUniqueDynamic(this, &ASpikeWheelHazard::HandleHurtZoneEndOverlap);
	HurtZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetActorTickEnabled(true);
}

void ASpikeWheelHazard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetActorTickEnabled(false);
	HurtZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HurtZone->OnComponentBeginOverlap.RemoveDynamic(this, &ASpikeWheelHazard::HandleHurtZoneBeginOverlap);
	HurtZone->OnComponentEndOverlap.RemoveDynamic(this, &ASpikeWheelHazard::HandleHurtZoneEndOverlap);
	ContactStates.Reset();
	Super::EndPlay(EndPlayReason);
}

bool ASpikeWheelHazard::BuildRoute(FString& OutError)
{
	TArray<int32> MatchingPatternIndices;
	for (int32 Index = 0; Index < TuningData->RoutePatterns.Num(); ++Index)
	{
		if (TuningData->RoutePatterns[Index].FootprintSpanTiles == FootprintSpanTiles)
		{
			MatchingPatternIndices.Add(Index);
		}
	}
	if (MatchingPatternIndices.IsEmpty())
	{
		OutError = FString::Printf(TEXT("No route pattern supports FootprintSpanTiles=%d."), FootprintSpanTiles);
		return false;
	}

	const FVector ActorLocation = GetActorLocation();
	uint32 EffectiveSeed = GetTypeHash(RouteVariantSeed);
	EffectiveSeed = HashCombineFast(EffectiveSeed, GetTypeHash(FMath::RoundToInt(ActorLocation.X / TuningData->TileSizeCm)));
	EffectiveSeed = HashCombineFast(EffectiveSeed, GetTypeHash(FMath::RoundToInt(ActorLocation.Y / TuningData->TileSizeCm)));
	EffectiveSeed = HashCombineFast(EffectiveSeed, GetTypeHash(FMath::RoundToInt(ActorLocation.Z / TuningData->TileSizeCm)));
	FRandomStream Random(static_cast<int32>(EffectiveSeed));
	const int32 PatternIndex = MatchingPatternIndices[Random.RandRange(0, MatchingPatternIndices.Num() - 1)];
	const FSpikeWheelRoutePattern& Pattern = TuningData->RoutePatterns[PatternIndex];
	const float MirrorSign = Random.RandRange(0, 1) == 0 ? 1.0f : -1.0f;
	const bool bReverse = Random.RandRange(0, 1) != 0;
	const float StartPhase = Random.FRand();

	RouteSpline->ClearSplinePoints(false);
	RouteSpline->SetClosedLoop(false, false);
	for (const FVector2D& Point : Pattern.ControlPointsInTileUnits)
	{
		RouteSpline->AddSplinePoint(
			FVector(Point.X * TuningData->TileSizeCm, Point.Y * TuningData->TileSizeCm * MirrorSign, TuningData->WheelCenterHeightCm),
			ESplineCoordinateSpace::Local,
			false);
	}
	for (int32 PointIndex = 0; PointIndex < RouteSpline->GetNumberOfSplinePoints(); ++PointIndex)
	{
		RouteSpline->SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
	}
	RouteSpline->SetClosedLoop(Pattern.bClosedLoop, false);
	RouteSpline->UpdateSpline();

	SplineLengthCm = RouteSpline->GetSplineLength();
	if (!FMath::IsFinite(SplineLengthCm) || SplineLengthCm <= 1.0f)
	{
		OutError = FString::Printf(TEXT("Selected route '%s' produced no usable spline length."), *Pattern.PatternId.ToString());
		return false;
	}

	bActiveRouteClosedLoop = Pattern.bClosedLoop;
	TravelDirectionSign = bReverse ? -1.0f : 1.0f;
	DistanceAlongSplineCm = StartPhase * SplineLengthCm;
	TraversalPhaseCm = bReverse && !bActiveRouteClosedLoop
		? 2.0f * SplineLengthCm - DistanceAlongSplineCm
		: DistanceAlongSplineCm;
	VisualRollDegrees = 0.0f;
	UpdateWheelTransform(0.0f);
	return true;
}

void ASpikeWheelHazard::ApplyGeometry()
{
	HurtZone->SetSphereRadius(TuningData->HurtRadiusCm, true);
}

void ASpikeWheelHazard::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float TravelDistance = TuningData->MoveSpeedCmPerSecond * DeltaSeconds;
	if (bActiveRouteClosedLoop)
	{
		const float SignedTravel = TravelDirectionSign * TravelDistance;
		DistanceAlongSplineCm = SpikeWheelHazard::WrapPositive(DistanceAlongSplineCm + SignedTravel, SplineLengthCm);
		UpdateWheelTransform(TravelDistance);
		return;
	}

	TraversalPhaseCm = SpikeWheelHazard::WrapPositive(TraversalPhaseCm + TravelDistance, 2.0f * SplineLengthCm);
	if (TraversalPhaseCm <= SplineLengthCm)
	{
		DistanceAlongSplineCm = TraversalPhaseCm;
		TravelDirectionSign = 1.0f;
	}
	else
	{
		DistanceAlongSplineCm = 2.0f * SplineLengthCm - TraversalPhaseCm;
		TravelDirectionSign = -1.0f;
	}
	UpdateWheelTransform(TravelDistance);
}

void ASpikeWheelHazard::UpdateWheelTransform(const float TravelDistanceCm)
{
	const FVector Location = RouteSpline->GetLocationAtDistanceAlongSpline(DistanceAlongSplineCm, ESplineCoordinateSpace::Local);
	FVector Direction = RouteSpline->GetDirectionAtDistanceAlongSpline(DistanceAlongSplineCm, ESplineCoordinateSpace::Local) * TravelDirectionSign;
	if (!Direction.Normalize())
	{
		Direction = FVector::ForwardVector;
	}
	HurtZone->SetRelativeLocationAndRotation(Location, Direction.Rotation(), true, nullptr, ETeleportType::None);

	if (!FMath::IsNearlyZero(TravelDistanceCm))
	{
		VisualRollDegrees = FMath::Fmod(
			VisualRollDegrees - FMath::RadiansToDegrees(TravelDistanceCm / TuningData->WheelRadiusCm),
			360.0f);
		WheelSpinRoot->SetRelativeRotation(FRotator(VisualRollDegrees, 0.0f, 0.0f));
	}
}

void ASpikeWheelHazard::HandleHurtZoneBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!IsValid(OtherActor) || !IsValid(OtherComponent)
		|| !OtherActor->GetClass()->ImplementsInterface(UCharacterImpactReceiver::StaticClass()))
	{
		return;
	}

	const TWeakObjectPtr<AActor> TargetKey(OtherActor);
	FSpikeWheelContactState& State = ContactStates.FindOrAdd(TargetKey);
	if (State.bInside)
	{
		return;
	}
	State.bInside = true;

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < State.NextEligibleHitTimeSeconds)
	{
		return;
	}
	State.NextEligibleHitTimeSeconds = Now + TuningData->RehitLockSeconds;
	ProcessContact(OtherActor);
}

void ASpikeWheelHazard::HandleHurtZoneEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/)
{
	if (!IsValid(OtherActor) || HurtZone->IsOverlappingActor(OtherActor))
	{
		return;
	}
	if (FSpikeWheelContactState* State = ContactStates.Find(TWeakObjectPtr<AActor>(OtherActor)))
	{
		State->bInside = false;
	}
}

void ASpikeWheelHazard::ProcessContact(AActor* Target)
{
	FVector WorldDirection = Target->GetActorLocation() - HurtZone->GetComponentLocation();
	WorldDirection.Z = 0.0f;
	if (!WorldDirection.Normalize())
	{
		WorldDirection = HurtZone->GetForwardVector();
		WorldDirection.Z = 0.0f;
		if (!WorldDirection.Normalize())
		{
			WorldDirection = FVector::ForwardVector;
		}
	}

	FStandingImpactRequest Request;
	Request.ImpactId = FGuid::NewGuid();
	Request.SourceActor = this;
	Request.SourceComponent = HurtZone;
	Request.SourceProfile = TuningData->StandingImpactSourceProfile;
	Request.WorldDirection = WorldDirection;
	Request.ImpactPoint = Target->GetActorLocation();
	Request.NormalizedStrength = TuningData->StandingImpactStrength;
	const EStandingImpactSubmitResult SubmitResult = ICharacterImpactReceiver::Execute_SubmitStandingImpact(Target, Request);
	if (SubmitResult == EStandingImpactSubmitResult::Invalid)
	{
		UE_LOG(LogSpikeWheel, Warning, TEXT("%s standing impact request was rejected as invalid by %s."), *GetName(), *Target->GetName());
	}

	if (Target->IsA<AZeroEscapeCharacter>())
	{
		UGameplayStatics::ApplyDamage(Target, TuningData->Damage, GetInstigatorController(), this, UDamageType::StaticClass());
	}
	UE_LOG(LogSpikeWheel, Log, TEXT("%s contacted %s (result=%d, damage=%.1f)."),
		*GetName(), *Target->GetName(), static_cast<int32>(SubmitResult), Target->IsA<AZeroEscapeCharacter>() ? TuningData->Damage : 0.0f);
}

void ASpikeWheelHazard::DisableHazard(const FString& Reason)
{
	SetActorTickEnabled(false);
	HurtZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogSpikeWheel, Error, TEXT("%s disabled: %s"), *GetName(), *Reason);
}
