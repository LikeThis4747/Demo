// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactAnimInstance.cpp
 * 职责：维护两类短生命周期 Pose Snapshot 及物理姿势准备的动画目标数据。
 * 边界：所有启停和 Alpha 都由 HeavyImpactResponseComponent 明确写入。
 */

#include "Animation/HeavyImpactAnimInstance.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/PoseSnapshot.h"

const FName UHeavyImpactAnimInstance::RecoveryPreparationPoseSnapshotName(
	TEXT("HeavyImpactRecoveryPreparationPose"));
const FName UHeavyImpactAnimInstance::DownedPoseSnapshotName(TEXT("HeavyImpactDownedPose"));

/** 校验并保存当前物理姿势，同时锁定本轮起身 Sequence 与采样时间。 */
bool UHeavyImpactAnimInstance::StoreHeavyImpactRecoveryPreparationPose(
	FPoseSnapshot&& PhysicalPose,
	UAnimSequenceBase* Sequence,
	const float SequenceTimeSeconds)
{
	ClearHeavyImpactRecoveryPreparation();
	if (!PhysicalPose.bIsValid
		|| PhysicalPose.LocalTransforms.IsEmpty()
		|| !IsValid(Sequence)
		|| !FMath::IsFinite(SequenceTimeSeconds)
		|| SequenceTimeSeconds < 0.0f
		|| SequenceTimeSeconds > Sequence->GetPlayLength())
	{
		return false;
	}

	PhysicalPose.SnapshotName = RecoveryPreparationPoseSnapshotName;
	FPoseSnapshot& StoredPose = AddPoseSnapshot(RecoveryPreparationPoseSnapshotName);
	StoredPose = MoveTemp(PhysicalPose);
	StoredPose.SnapshotName = RecoveryPreparationPoseSnapshotName;

	HeavyImpactRecoveryPreparationSequence = Sequence;
	HeavyImpactRecoveryPreparationTimeSeconds = SequenceTimeSeconds;
	HeavyImpactRecoveryPreparationAlpha = 0.0f;
	bUseHeavyImpactRecoveryPreparationPose =
		StoredPose.bIsValid && !StoredPose.LocalTransforms.IsEmpty();
	return bUseHeavyImpactRecoveryPreparationPose;
}

/** 将组件提交的准备进度限制到 AnimGraph 可安全消费的范围。 */
void UHeavyImpactAnimInstance::SetHeavyImpactRecoveryPreparationAlpha(const float Alpha)
{
	HeavyImpactRecoveryPreparationAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
}

/** 幂等清除物理姿势准备快照、目标资源、采样时间和 Alpha。 */
void UHeavyImpactAnimInstance::ClearHeavyImpactRecoveryPreparation()
{
	bUseHeavyImpactRecoveryPreparationPose = false;
	HeavyImpactRecoveryPreparationSequence = nullptr;
	HeavyImpactRecoveryPreparationTimeSeconds = 0.0f;
	HeavyImpactRecoveryPreparationAlpha = 0.0f;
	RemovePoseSnapshot(RecoveryPreparationPoseSnapshotName);
}

/** 保存最终根重定位后的倒地物理姿势，供 Montage 首帧混合。 */
bool UHeavyImpactAnimInstance::StoreHeavyImpactDownedPose(FPoseSnapshot&& Pose)
{
	bUseHeavyImpactPoseSnapshot = false;
	if (!Pose.bIsValid || Pose.LocalTransforms.IsEmpty())
	{
		RemovePoseSnapshot(DownedPoseSnapshotName);
		return false;
	}

	Pose.SnapshotName = DownedPoseSnapshotName;
	FPoseSnapshot& StoredPose = AddPoseSnapshot(DownedPoseSnapshotName);
	StoredPose = MoveTemp(Pose);
	StoredPose.SnapshotName = DownedPoseSnapshotName;
	bUseHeavyImpactPoseSnapshot =
		StoredPose.bIsValid && !StoredPose.LocalTransforms.IsEmpty();
	return bUseHeavyImpactPoseSnapshot;
}

/** 关闭最终倒地快照分支，让 AnimGraph 继续混入正在播放的 Montage。 */
void UHeavyImpactAnimInstance::ReleaseHeavyImpactDownedPose()
{
	bUseHeavyImpactPoseSnapshot = false;
}

/** 幂等移除最终倒地快照及其启用标记。 */
void UHeavyImpactAnimInstance::ClearHeavyImpactDownedPose()
{
	bUseHeavyImpactPoseSnapshot = false;
	RemovePoseSnapshot(DownedPoseSnapshotName);
}
