// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticPrototypeProp.cpp
 * Implements a native magnetic prop so the baseline remains playable without Blueprint graph logic.
 */

#include "Actors/Magnetism/MagneticPrototypeProp.h"

#include "Components/Magnetism/MagneticObjectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

/** Assembles a Chaos body with engine collision and a reusable magnetic marker. */
AMagneticPrototypeProp::AMagneticPrototypeProp()
{
	PrimaryActorTick.bCanEverTick = false;

	MagneticBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MagneticBody"));
	SetRootComponent(MagneticBody);
	MagneticBody->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	MagneticBody->SetSimulatePhysics(true);
	MagneticBody->SetLinearDamping(0.15f);
	MagneticBody->SetAngularDamping(0.35f);

	MagneticObject = CreateDefaultSubobject<UMagneticObjectComponent>(TEXT("MagneticObject"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MagneticBody->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PrototypeMaterial(
		TEXT("/Game/ZeroEscape/Interaction/Magnetism/M_MagneticPrototype.M_MagneticPrototype"));
	if (PrototypeMaterial.Succeeded())
	{
		MagneticBody->SetMaterial(0, PrototypeMaterial.Object);
	}
}

/** Lets the prototype GameMode produce visibly different plates, crates, and bars from one actor class. */
void AMagneticPrototypeProp::ConfigurePrototype(const FVector& InScale, const float InMassKilograms)
{
	SetActorScale3D(InScale);
	InitialMassKilograms = FMath::Max(1.0f, InMassKilograms);
	ApplyConfiguredMass();
}

/** Ensures the mass override is applied after the runtime body exists. */
void AMagneticPrototypeProp::BeginPlay()
{
	Super::BeginPlay();
	ApplyConfiguredMass();
}

/** Updates Chaos through the engine mass-override API rather than duplicating mass or inertia integration. */
void AMagneticPrototypeProp::ApplyConfiguredMass()
{
	if (IsValid(MagneticBody))
	{
		MagneticBody->SetMassOverrideInKg(NAME_None, InitialMassKilograms, true);
	}
}
