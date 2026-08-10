// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactAnimInstance.h
 * 职责：向 AnimGraph 暴露重冲击倒地物理姿势快照和唯一的起身混合权重。
 * 边界：不拥有重冲击状态、不检查空间、不驱动物理，也不自行播放 Montage。
 */

#pragma once

#include "Animation/AnimInstance.h"

#include "HeavyImpactAnimInstance.generated.h"

struct FPoseSnapshot;

/**
 * Minimal animation-side bridge for HeavyImpact recovery.
 * The response component owns recovery state and placement; this class only exposes captured
 * physical pose and its blend weight to the AnimGraph.
 */
UCLASS(Transient, Blueprintable)
class DEMO_API UHeavyImpactAnimInstance final : public UAnimInstance
{
	GENERATED_BODY()

public:
	static const FName DownedPoseSnapshotName;

	/** Store an already root-relocated physical pose for the Pose Snapshot AnimGraph node. */
	bool StoreHeavyImpactDownedPose(FPoseSnapshot&& Pose);

	/** Set the explicit Snapshot-to-Slot blend alpha requested by the response component. */
	void SetHeavyImpactRecoveryBlendAlpha(float Alpha);

	/** Remove the stored pose and restore the normal Slot output after the blend ends. */
	void ClearHeavyImpactDownedPose();

	/** Zero shows the stored physical pose; one shows the existing DefaultSlot output. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Heavy Impact")
	float HeavyImpactRecoveryBlendAlpha = 1.0f;
};
