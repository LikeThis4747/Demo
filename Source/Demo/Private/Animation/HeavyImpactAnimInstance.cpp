// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/HeavyImpactAnimInstance.h"

#include "Animation/PoseSnapshot.h"

const FName UHeavyImpactAnimInstance::DownedPoseSnapshotName(TEXT("HeavyImpactDownedPose"));

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

void UHeavyImpactAnimInstance::ReleaseHeavyImpactDownedPose()
{
	bUseHeavyImpactPoseSnapshot = false;
}

void UHeavyImpactAnimInstance::ClearHeavyImpactDownedPose()
{
	bUseHeavyImpactPoseSnapshot = false;
	RemovePoseSnapshot(DownedPoseSnapshotName);
}
