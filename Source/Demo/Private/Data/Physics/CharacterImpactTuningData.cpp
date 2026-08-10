// Copyright Epic Games, Inc. All Rights Reserved.

/** @file CharacterImpactTuningData.cpp 校验轻受击动画骨架与运行时数值，不修改 AnimBP。 */

#include "Data/Physics/CharacterImpactTuningData.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"

bool UCharacterImpactTuningData::IsConfigured(
	const USkeletalMeshComponent* Mesh,
	FString& OutError) const
{
	OutError.Reset();
	if (!FMath::IsFinite(MontageBlendInSeconds)
		|| !FMath::IsFinite(MontageBlendOutSeconds)
		|| !FMath::IsFinite(MontagePlayRate)
		|| !FMath::IsFinite(MinimumStrengthDurationScale)
		|| !FMath::IsFinite(MaxContinuousLightSeconds)
		|| MontageBlendInSeconds < 0.0f
		|| MontageBlendInSeconds > 0.5f
		|| MontageBlendOutSeconds < 0.0f
		|| MontageBlendOutSeconds > 1.0f
		|| MontagePlayRate < 0.1f
		|| MontagePlayRate > 2.0f
		|| MinimumStrengthDurationScale < 0.0f
		|| MinimumStrengthDurationScale > 1.0f
		|| MaxContinuousLightSeconds < 0.1f
		|| MaxContinuousLightSeconds > 5.0f)
	{
		OutError = TEXT("CharacterImpact tuning numeric values are outside their declared safe ranges.");
		return false;
	}

	const USkeletalMesh* MeshAsset = IsValid(Mesh) ? Mesh->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* RuntimeSkeleton = IsValid(MeshAsset) ? MeshAsset->GetSkeleton() : nullptr;
	if (!IsValid(RuntimeSkeleton))
	{
		OutError = TEXT("CharacterImpact requires a valid runtime SkeletalMesh and Skeleton.");
		return false;
	}

	const auto ValidateAnimation = [&OutError, RuntimeSkeleton](
		const TCHAR* PropertyName,
		const UAnimSequenceBase* Animation) -> bool
	{
		if (IsValid(Animation) && Animation->GetSkeleton() != RuntimeSkeleton)
		{
			OutError = FString::Printf(
				TEXT("%s uses an incompatible Skeleton for the runtime Mesh."),
				PropertyName);
			return false;
		}
		return true;
	};

	return ValidateAnimation(TEXT("FrontReaction"), FrontReaction)
		&& ValidateAnimation(TEXT("LeftReaction"), LeftReaction)
		&& ValidateAnimation(TEXT("RightReaction"), RightReaction);
}
