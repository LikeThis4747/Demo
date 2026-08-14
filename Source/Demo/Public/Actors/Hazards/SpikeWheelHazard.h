// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file SpikeWheelHazard.h
 * A small spline-driven rolling hazard with per-instance route variation and local re-hit protection.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SpikeWheelHazard.generated.h"

class AActor;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class USpikeWheelHazardTuningData;
class USplineComponent;
class UStaticMeshComponent;

struct FSpikeWheelContactState
{
	bool bInside = false;
	double NextEligibleHitTimeSeconds = 0.0;
};

/** Project-owned spike wheel. Blueprint supplies presentation assets only. */
UCLASS()
class DEMO_API ASpikeWheelHazard final : public AActor
{
	GENERATED_BODY()

public:
	ASpikeWheelHazard();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 延迟生成专用：在 FinishSpawningActor 前固定一格路线、路线种子与归一化相位。 */
	bool ConfigurePopulationPlacement(
		int32 InRouteVariantSeed,
		float InNormalizedPhase01);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool BuildRoute(FString& OutError);
	void ApplyGeometry();
	void UpdateWheelTransform(float TravelDistanceCm);
	void ProcessContact(AActor* Target);
	void DisableHazard(const FString& Reason);

	UFUNCTION()
	void HandleHurtZoneBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleHurtZoneEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard|Spike Wheel", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** Editor-visible authored runtime route. It has no collision or gameplay responsibility. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard|Spike Wheel", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USplineComponent> RouteSpline;

	/** The moving overlap primitive is also the path root so swept overlap state updates reliably. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard|Spike Wheel", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> HurtZone;

	/** Spins around local Y while HurtZone supplies translation and path-facing rotation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard|Spike Wheel", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> WheelSpinRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard|Spike Wheel", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> WheelMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hazard|Spike Wheel", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpikeWheelHazardTuningData> TuningData;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Hazard|Spike Wheel|Route",
		meta = (AllowPrivateAccess = "true", ClampMin = "1", ClampMax = "3"))
	int32 FootprintSpanTiles = 2;

	/** Base seed. World-grid position is mixed in so separate placed traps vary deterministically. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Hazard|Spike Wheel|Route", meta = (AllowPrivateAccess = "true"))
	int32 RouteVariantSeed = 1337;

	TMap<TWeakObjectPtr<AActor>, FSpikeWheelContactState> ContactStates;
	float SplineLengthCm = 0.0f;
	float DistanceAlongSplineCm = 0.0f;
	float TraversalPhaseCm = 0.0f;
	float TravelDirectionSign = 1.0f;
	float VisualRollDegrees = 0.0f;
	bool bActiveRouteClosedLoop = true;
	/** 仅由 ConfigurePopulationPlacement 在 BeginPlay 前写入。 */
	float PopulationNormalizedPhase01 = 0.0f;
	/** 为 true 时路线不再混入世界位置，并使用显式归一化相位。 */
	bool bHasPopulationRouteConfiguration = false;
};
