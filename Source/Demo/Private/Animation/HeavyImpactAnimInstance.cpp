// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactAnimInstance.cpp
 * 职责：维护倒地 Pose Snapshot 及其到起身 Slot 的显式混合权重。
 * 边界：混合启停和 Alpha 均由 HeavyImpactResponseComponent 明确写入。
 */

#include "Animation/HeavyImpactAnimInstance.h"

#include "Animation/PoseSnapshot.h"

const FName UHeavyImpactAnimInstance::DownedPoseSnapshotName(TEXT("HeavyImpactDownedPose"));

namespace
{
	constexpr float ChargeGuardBlendDurationSeconds = 0.15f;
}

void UHeavyImpactAnimInstance::SetChargeGuardActive(const bool bActive)
{
	bChargeGuardActive = bActive;
}

void UHeavyImpactAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const float TargetAlpha = bChargeGuardActive ? 1.0f : 0.0f;
	const float BlendSpeed = 1.0f / ChargeGuardBlendDurationSeconds;
	ChargeGuardBlendAlpha = FMath::FInterpConstantTo(
		ChargeGuardBlendAlpha,
		TargetAlpha,
		FMath::Max(DeltaSeconds, 0.0f),
		BlendSpeed);
}

/** 保存最终根重定位后的倒地物理姿势，供 Montage 首帧混合。 */
bool UHeavyImpactAnimInstance::StoreHeavyImpactDownedPose(FPoseSnapshot&& Pose)
{
	HeavyImpactRecoveryBlendAlpha = 1.0f;
	if (!Pose.bIsValid || Pose.LocalTransforms.IsEmpty())
	{
		RemovePoseSnapshot(DownedPoseSnapshotName);
		return false;
	}

	Pose.SnapshotName = DownedPoseSnapshotName;
	FPoseSnapshot& StoredPose = AddPoseSnapshot(DownedPoseSnapshotName);
	StoredPose = MoveTemp(Pose);
	StoredPose.SnapshotName = DownedPoseSnapshotName;
	if (!StoredPose.bIsValid || StoredPose.LocalTransforms.IsEmpty())
	{
		RemovePoseSnapshot(DownedPoseSnapshotName);
		return false;
	}

	HeavyImpactRecoveryBlendAlpha = 0.0f;
	return true;
}

/** 将组件提交的 Snapshot-to-Slot 进度限制到 AnimGraph 可安全消费的范围。 */
void UHeavyImpactAnimInstance::SetHeavyImpactRecoveryBlendAlpha(const float Alpha)
{
	HeavyImpactRecoveryBlendAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
}

/** 幂等移除最终倒地快照及其启用标记。 */
void UHeavyImpactAnimInstance::ClearHeavyImpactDownedPose()
{
	HeavyImpactRecoveryBlendAlpha = 1.0f;
	RemovePoseSnapshot(DownedPoseSnapshotName);
}
