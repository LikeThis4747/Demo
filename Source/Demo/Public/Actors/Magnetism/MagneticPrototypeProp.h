// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticPrototypeProp.h
 * Declares a self-contained magnetic physics prop used by the first playable interaction test.
 * The actor owns presentation and initial mass; Chaos owns motion and the marker owns magnetic tuning.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MagneticPrototypeProp.generated.h"

class UMagneticObjectComponent;
class UStaticMeshComponent;

/** Minimal reusable magnetic rigid body that can later be spawned by runtime PCG. */
UCLASS()
class DEMO_API AMagneticPrototypeProp final : public AActor
{
	GENERATED_BODY()

public:
	/** Creates a simulated cube body and attaches the magnetic interaction contract. */
	AMagneticPrototypeProp();

	/** Applies per-instance shape scale and mass for the runtime prototype lineup. */
	void ConfigurePrototype(const FVector& InScale, float InMassKilograms);

protected:
	/** Reapplies the configured mass after Chaos has created the runtime physics state. */
	virtual void BeginPlay() override;

private:
	/** Applies the authored mass override to the live rigid body when it is valid. */
	void ApplyConfiguredMass();

	/** Simulated root body that renders, collides, rotates, and receives Physics Handle forces. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prototype", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> MagneticBody;

	/** Marker and per-object tuning contract consumed by the electromagnetic grab component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prototype", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMagneticObjectComponent> MagneticObject;

	/** Initial mass applied after physics-state creation and whenever prototype configuration changes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prototype", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", Units = "kg"))
	float InitialMassKilograms = 20.0f;
};
