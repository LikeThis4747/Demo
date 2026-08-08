// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactAnimInstance.h
 * 职责：向 AnimGraph 暴露重冲击物理姿势快照、起身采样目标和混合权重。
 * 边界：不拥有重冲击状态、不检查空间、不驱动物理，也不自行播放 Montage。
 */

#pragma once

#include "Animation/AnimInstance.h"

#include "HeavyImpactAnimInstance.generated.h"

class UAnimSequenceBase;
struct FPoseSnapshot;

/**
 * Minimal animation-side bridge for HeavyImpact recovery.
 * The response component owns recovery state and placement; this class only exposes captured
 * physical poses and a sampled get-up target to the AnimGraph.
 */
UCLASS(Transient, Blueprintable)
class DEMO_API UHeavyImpactAnimInstance final : public UAnimInstance
{
	GENERATED_BODY()

public:
	static const FName RecoveryPreparationPoseSnapshotName;
	static const FName DownedPoseSnapshotName;

	/**
	 * Store the current physical pose and the sampled get-up target used by the AnimGraph.
	 * This bridge owns no gameplay state and never starts animation playback.
	 */
	bool StoreHeavyImpactRecoveryPreparationPose(
		FPoseSnapshot&& PhysicalPose,
		UAnimSequenceBase* Sequence,
		float SequenceTimeSeconds);

	/** Update the physical-to-get-up target blend requested by the response component. */
	void SetHeavyImpactRecoveryPreparationAlpha(float Alpha);

	/** Clear every transient value used by the physical pose-preparation AnimGraph branch. */
	void ClearHeavyImpactRecoveryPreparation();

	/** Store an already root-relocated physical pose for the Pose Snapshot AnimGraph node. */
	bool StoreHeavyImpactDownedPose(FPoseSnapshot&& Pose);

	/** Let the AnimGraph blend from the stored pose to the active get-up montage. */
	void ReleaseHeavyImpactDownedPose();

	/** Remove the stored pose after the blend no longer needs it. */
	void ClearHeavyImpactDownedPose();

	/** True while the AnimGraph should expose the physical-to-get-up preparation target. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Heavy Impact")
	bool bUseHeavyImpactRecoveryPreparationPose = false;

	/** Get-up sequence sampled by the preparation Sequence Evaluator. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Heavy Impact")
	TObjectPtr<UAnimSequenceBase> HeavyImpactRecoveryPreparationSequence = nullptr;

	/** Fixed sample time shared by the preparation target and the later dynamic Montage, in seconds. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Heavy Impact")
	float HeavyImpactRecoveryPreparationTimeSeconds = 0.0f;

	/** Blend from the captured physical pose toward the sampled get-up target, in [0, 1]. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Heavy Impact")
	float HeavyImpactRecoveryPreparationAlpha = 0.0f;

	/** True while the final relocated physical snapshot is blended into the active get-up Montage. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Heavy Impact")
	bool bUseHeavyImpactPoseSnapshot = false;
};
