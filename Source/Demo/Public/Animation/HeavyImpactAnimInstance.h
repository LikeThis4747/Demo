// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimInstance.h"

#include "HeavyImpactAnimInstance.generated.h"

struct FPoseSnapshot;

/**
 * Minimal animation-side bridge for HeavyImpact recovery.
 * The response component owns recovery state and placement; this class only exposes a stored
 * physical pose to the AnimGraph while a get-up montage is blended in.
 */
UCLASS(Transient, Blueprintable)
class DEMO_API UHeavyImpactAnimInstance final : public UAnimInstance
{
	GENERATED_BODY()

public:
	static const FName DownedPoseSnapshotName;

	/** Store an already root-relocated physical pose for the Pose Snapshot AnimGraph node. */
	bool StoreHeavyImpactDownedPose(FPoseSnapshot&& Pose);

	/** Let the AnimGraph blend from the stored pose to the active get-up montage. */
	void ReleaseHeavyImpactDownedPose();

	/** Remove the stored pose after the blend no longer needs it. */
	void ClearHeavyImpactDownedPose();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Heavy Impact")
	bool bUseHeavyImpactPoseSnapshot = false;
};
