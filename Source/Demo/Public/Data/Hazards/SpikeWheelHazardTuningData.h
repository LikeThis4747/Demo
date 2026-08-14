// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file SpikeWheelHazardTuningData.h
 * Owns spike-wheel route templates and runtime tuning. Route points use logical-tile units relative to the first cell center.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "SpikeWheelHazardTuningData.generated.h"

class UCharacterImpactSourceProfile;

/** One authored patrol shape. Point count is intentionally unrestricted. */
USTRUCT(BlueprintType)
struct DEMO_API FSpikeWheelRoutePattern
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spike Wheel Route")
	FName PatternId = NAME_None;

	/** Consecutive logical cells reserved along local +X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spike Wheel Route", meta = (ClampMin = "1", ClampMax = "3"))
	int32 FootprintSpanTiles = 1;

	/** Closed routes wrap; open routes continuously ping-pong. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spike Wheel Route")
	bool bClosedLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spike Wheel Route")
	TArray<FVector2D> ControlPointsInTileUnits;
};

/** Authoritative data for one family of simple spline-driven spike wheels. */
UCLASS(BlueprintType)
class DEMO_API USpikeWheelHazardTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	USpikeWheelHazardTuningData();
	bool IsConfigured(FString& OutError) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Route", meta = (ClampMin = "100.0", ClampMax = "2000.0", Units = "cm"))
	float TileSizeCm = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Route", meta = (ClampMin = "1.0", ClampMax = "2000.0", Units = "cm/s"))
	float MoveSpeedCmPerSecond = 350.0f;

	/** Blueprint mesh scale must visually match this rolling radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Geometry", meta = (ClampMin = "5.0", ClampMax = "250.0", Units = "cm"))
	float WheelRadiusCm = 55.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Geometry", meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	float WheelCenterHeightCm = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Contact", meta = (ClampMin = "5.0", ClampMax = "250.0", Units = "cm"))
	float HurtRadiusCm = 65.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Contact", meta = (ClampMin = "0.0"))
	float Damage = 20.0f;

	/** A target must also fully leave and overlap again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Contact", meta = (ClampMin = "0.05", ClampMax = "5.0", Units = "s"))
	float RehitLockSeconds = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Standing Impact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StandingImpactStrength = 1.0f;

	/** This P0 profile must map Player to Stop and Pursuer to None. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Standing Impact")
	TObjectPtr<UCharacterImpactSourceProfile> StandingImpactSourceProfile = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spike Wheel|Route")
	TArray<FSpikeWheelRoutePattern> RoutePatterns;
};
