// Copyright Epic Games, Inc. All Rights Reserved.

/** @file CharacterImpactTuningData.cpp 校验轻受击动画骨架与运行时数值，不修改 AnimBP。 */

#include "Data/Physics/CharacterImpactTuningData.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "PhysicsEngine/PhysicsAsset.h"

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

bool UCharacterImpactTuningData::IsPhysicalReactionConfigured(
	const USkeletalMeshComponent* Mesh,
	FString& OutError) const
{
	OutError.Reset();
	if (!bEnablePhysicalReaction)
	{
		return true;
	}

	const UPhysicsAsset* PhysicsAsset = IsValid(Mesh) ? Mesh->GetPhysicsAsset() : nullptr;
	if (!IsValid(PhysicsAsset))
	{
		OutError = TEXT("Physical presentation requires a valid PhysicsAsset.");
		return false;
	}

	const FName RequiredBodies[] =
	{
		UpperBodyRootBone,
		TorsoImpulseBone,
		LeftArmImpulseBone,
		RightArmImpulseBone,
		HeadImpulseBone
	};
	for (const FName BodyName : RequiredBodies)
	{
		if (BodyName.IsNone() || PhysicsAsset->FindBodyIndex(BodyName) == INDEX_NONE)
		{
			OutError = FString::Printf(
				TEXT("Physical presentation body '%s' is missing."),
				*BodyName.ToString());
			return false;
		}
	}

	const auto IsRootOrChild = [Mesh, this](const FName BoneName)
	{
		return BoneName == UpperBodyRootBone
			|| Mesh->BoneIsChildOf(BoneName, UpperBodyRootBone);
	};
	if (!IsRootOrChild(TorsoImpulseBone)
		|| !IsRootOrChild(LeftArmImpulseBone)
		|| !IsRootOrChild(RightArmImpulseBone)
		|| !IsRootOrChild(HeadImpulseBone))
	{
		OutError = TEXT("All physical impulse bodies must belong to UpperBodyRootBone.");
		return false;
	}

	const FPhysicalAnimationData& Settings = PhysicalAnimationSettings;
	if (!Settings.bIsLocalSimulation
		|| !FMath::IsFinite(Settings.OrientationStrength)
		|| Settings.OrientationStrength <= 0.0f
		|| !FMath::IsFinite(Settings.AngularVelocityStrength)
		|| Settings.AngularVelocityStrength < 0.0f
		|| !FMath::IsFinite(Settings.PositionStrength)
		|| !FMath::IsFinite(Settings.VelocityStrength)
		|| !FMath::IsFinite(Settings.MaxLinearForce)
		|| !FMath::IsFinite(Settings.MaxAngularForce)
		|| Settings.MaxAngularForce <= 0.0f
		|| !FMath::IsFinite(PhysicalImpulseAtFullStrength)
		|| PhysicalImpulseAtFullStrength <= 0.0f
		|| !FMath::IsFinite(MaximumPhysicalImpulseLeverArm)
		|| MaximumPhysicalImpulseLeverArm < 0.0f
		|| MaximumPhysicalImpulseLeverArm > 100.0f
		|| !FMath::IsFinite(PhysicalHoldSeconds)
		|| PhysicalHoldSeconds < MontageBlendInSeconds
		|| PhysicalHoldSeconds > 0.25f
		|| !FMath::IsFinite(PhysicalBlendOutSeconds)
		|| PhysicalBlendOutSeconds < 0.05f
		|| PhysicalBlendOutSeconds > 0.5f
		|| !FMath::IsFinite(MaxContinuousPhysicalSeconds)
		|| MaxContinuousPhysicalSeconds < PhysicalHoldSeconds + PhysicalBlendOutSeconds
		|| MaxContinuousPhysicalSeconds > 2.0f)
	{
		OutError = TEXT("Physical presentation drive, impulse or timing values are invalid.");
		return false;
	}

	return true;
}
