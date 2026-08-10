// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactTuningData.cpp
 * 职责：在运行前拒绝缺失 PCA、未编译 Profile、错误骨骼和非法时序配置。
 * 边界：只做校验，不创建或修改 Physics Control 运行时记录。
 */

#include "Data/Physics/HeavyImpactTuningData.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Physics/HeavyImpactTypes.h"
#include "PhysicsControlAsset.h"

/** 为写实人形提供能一眼看出的阶段差异；倍率只作用于 ParentSpace 角向控制。 */
UHeavyImpactTuningData::UHeavyImpactTuningData()
{
	PreparedControl.AngularStrengthMultiplier = 2.0f;
	PreparedControl.AngularDampingRatioMultiplier = 1.25f;
	PreparedControl.MaxTorqueMultiplier = 3.0f;

	FlightControl.AngularStrengthMultiplier = 1.75f;
	FlightControl.AngularDampingRatioMultiplier = 1.0f;
	FlightControl.MaxTorqueMultiplier = 3.0f;

	LandingControl.AngularStrengthMultiplier = 2.25f;
	LandingControl.AngularDampingRatioMultiplier = 1.35f;
	LandingControl.MaxTorqueMultiplier = 3.5f;
}

/** 验证调参资产足以安全初始化唯一的重冲击 Physics Control 权威。 */
bool UHeavyImpactTuningData::Validate(const USkeletalMeshComponent* Mesh, FText& OutError) const
{
	auto Reject = [&OutError](const TCHAR* Message)
	{
		OutError = FText::FromString(Message);
		return false;
	};

	const auto ValidateControlStage = [&OutError](
		const TCHAR* StageName,
		const FHeavyImpactControlStageTuning& Stage) -> bool
	{
		const bool bFinite =
			FMath::IsFinite(Stage.AngularStrengthMultiplier)
			&& FMath::IsFinite(Stage.AngularDampingRatioMultiplier)
			&& FMath::IsFinite(Stage.MaxTorqueMultiplier);
		const bool bInRange =
			Stage.AngularStrengthMultiplier >= 0.1f
			&& Stage.AngularStrengthMultiplier <= 10.0f
			&& Stage.AngularDampingRatioMultiplier >= 0.1f
			&& Stage.AngularDampingRatioMultiplier <= 10.0f
			&& Stage.MaxTorqueMultiplier >= 0.1f
			&& Stage.MaxTorqueMultiplier <= 20.0f;
		if (!bFinite || !bInRange)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "InvalidControlStage", "HeavyImpact control stage {0} has invalid multipliers."),
				FText::FromString(StageName));
			return false;
		}
		return true;
	};

	if (!ValidateControlStage(TEXT("Prepared"), PreparedControl)
		|| !ValidateControlStage(TEXT("Flight"), FlightControl)
		|| !ValidateControlStage(TEXT("Landing"), LandingControl))
	{
		return false;
	}

	const bool bThresholdsFinite =
		FMath::IsFinite(MaximumPreparationSeconds)
		&& FMath::IsFinite(MinimumPreparationLeadSeconds)
		&& FMath::IsFinite(MinimumSimulationSeconds)
		&& FMath::IsFinite(StableLinearSpeedCmPerSecond)
		&& FMath::IsFinite(StableAngularSpeedDegPerSecond)
		&& FMath::IsFinite(RequiredStableSeconds)
		&& FMath::IsFinite(RecoveryHandoffStableSeconds)
		&& FMath::IsFinite(GroundProbeDistance)
		&& FMath::IsFinite(CapsuleFollowInterpSpeed)
		&& FMath::IsFinite(FreeFallbackAfterSeconds)
		&& FMath::IsFinite(ForceDownedAfterSeconds)
		&& FMath::IsFinite(RecoveryDelaySeconds)
		&& FMath::IsFinite(RecoveryRetrySeconds)
		&& FMath::IsFinite(MinimumDownedReimpactImpulse)
		&& FMath::IsFinite(MaxRecoveryHorizontalAdjustmentCm)
		&& FMath::IsFinite(RecoverySearchStepCm)
		&& FMath::IsFinite(RecoverySnapshotBlendSeconds)
		&& FMath::IsFinite(RecoveryMontageBlendOutSeconds)
		&& FMath::IsFinite(RecoveryPlayRate)
		&& FMath::IsFinite(FaceUpAnimationStartTimeSeconds)
		&& FMath::IsFinite(FaceDownAnimationStartTimeSeconds)
		&& FMath::IsFinite(FaceUpYawOffsetDegrees)
		&& FMath::IsFinite(FaceDownYawOffsetDegrees);
	if (!bThresholdsFinite)
	{
		return Reject(TEXT("HeavyImpact numeric thresholds must all be finite."));
	}

	if (MaximumPreparationSeconds <= 0.0f
		|| MinimumPreparationLeadSeconds <= 0.0f
		|| MinimumPreparationLeadSeconds >= MaximumPreparationSeconds
		|| MinimumSimulationSeconds < 0.0f
		|| StableLinearSpeedCmPerSecond < 0.0f
		|| StableAngularSpeedDegPerSecond < 0.0f
		|| RequiredStableSeconds <= 0.0f
		|| RecoveryHandoffStableSeconds < 0.03f
		|| RecoveryHandoffStableSeconds > 0.25f
		|| RecoveryHandoffStableSeconds > RequiredStableSeconds
		|| GroundProbeDistance <= 0.0f
		|| CapsuleFollowInterpSpeed <= 0.0f
		|| FreeFallbackAfterSeconds <= 0.0f
		|| ForceDownedAfterSeconds <= FreeFallbackAfterSeconds
		|| RecoveryDelaySeconds < 0.0f
		|| RecoveryRetrySeconds < 0.1f
		|| MinimumDownedReimpactImpulse < 1.0f
		|| MaxRecoveryHorizontalAdjustmentCm < 0.0f
		|| MaxRecoveryHorizontalAdjustmentCm > 60.0f
		|| RecoverySearchStepCm < 5.0f
		|| RecoverySearchStepCm > 60.0f
		|| (MaxRecoveryHorizontalAdjustmentCm > 0.0f
			&& RecoverySearchStepCm > MaxRecoveryHorizontalAdjustmentCm)
		|| RecoverySnapshotBlendSeconds < 0.05f
		|| RecoverySnapshotBlendSeconds > 0.5f
		|| RecoveryMontageBlendOutSeconds < 0.0f
		|| RecoveryMontageBlendOutSeconds > 1.0f
		|| RecoveryPlayRate < 0.1f
		|| RecoveryPlayRate > 2.0f
		|| FaceUpAnimationStartTimeSeconds < 0.0f
		|| FaceDownAnimationStartTimeSeconds < 0.0f)
	{
		return Reject(TEXT("HeavyImpact thresholds are outside their safe runtime ranges."));
	}

	if (RecentImpactHistorySize < 4 || RecentImpactHistorySize > 64)
	{
		return Reject(TEXT("HeavyImpact RecentImpactHistorySize must be between 4 and 64."));
	}

	if (!IsValid(Mesh))
	{
		return Reject(TEXT("HeavyImpact Mesh is missing."));
	}

	const USkeletalMesh* RuntimeSkeletalMesh = Mesh->GetSkeletalMeshAsset();
	if (!IsValid(RuntimeSkeletalMesh) || !IsValid(RuntimeSkeletalMesh->GetSkeleton()))
	{
		return Reject(TEXT("HeavyImpact Mesh has no valid SkeletalMesh or Skeleton."));
	}

	const auto ValidateRecoveryAnimation = [RuntimeSkeletalMesh, &OutError](
		const TCHAR* Label,
		const UAnimSequenceBase* Animation) -> bool
	{
		if (!IsValid(Animation))
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "MissingRecoveryAnimation", "Missing HeavyImpact recovery animation: {0}"),
				FText::FromString(Label));
			return false;
		}
		if (Animation->GetSkeleton() != RuntimeSkeletalMesh->GetSkeleton())
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "RecoverySkeletonMismatch", "HeavyImpact recovery animation {0} does not use the runtime Mesh Skeleton."),
				FText::FromString(Label));
			return false;
		}
		if (Animation->HasRootMotion())
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "RecoveryRootMotionForbidden", "HeavyImpact recovery animation {0} must have Root Motion disabled."),
				FText::FromString(Label));
			return false;
		}
		if (!FMath::IsFinite(Animation->RateScale) || Animation->RateScale <= 0.0f)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "RecoveryRateScaleInvalid", "HeavyImpact recovery animation {0} has an invalid RateScale."),
				FText::FromString(Label));
			return false;
		}
		return true;
	};

	if (!ValidateRecoveryAnimation(TEXT("FaceUp"), GetUpFaceUpAnimation)
		|| !ValidateRecoveryAnimation(TEXT("FaceDown"), GetUpFaceDownAnimation))
	{
		return false;
	}

	if (FaceUpAnimationStartTimeSeconds > GetUpFaceUpAnimation->GetPlayLength()
		|| FaceDownAnimationStartTimeSeconds > GetUpFaceDownAnimation->GetPlayLength())
	{
		return Reject(TEXT("HeavyImpact recovery animation start times must stay within their selected animation lengths."));
	}

	if (!IsValid(PhysicsControlAsset))
	{
		return Reject(TEXT("HeavyImpact PhysicsControlAsset is missing."));
	}

	if (PelvisBone.IsNone() || Mesh->GetBoneIndex(PelvisBone) == INDEX_NONE)
	{
		OutError = FText::Format(
			NSLOCTEXT("HeavyImpact", "MissingPelvis", "Missing pelvis bone: {0}"),
			FText::FromName(PelvisBone));
		return false;
	}

	const FName RecoveryBones[] = { HeadBone, LeftShoulderBone, RightShoulderBone };
	for (const FName BoneName : RecoveryBones)
	{
		if (BoneName.IsNone() || Mesh->GetBoneIndex(BoneName) == INDEX_NONE)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "MissingRecoveryBone", "Missing HeavyImpact recovery bone: {0}"),
				FText::FromName(BoneName));
			return false;
		}
	}

	const TArray<FName> RequiredProfiles = {
		Demo::HeavyImpact::ProfileInactive,
		Demo::HeavyImpact::ProfilePrepared,
		Demo::HeavyImpact::ProfileFlight,
		Demo::HeavyImpact::ProfileLandingRecovery,
		Demo::HeavyImpact::ProfileFreeFallback
	};

	for (const FName ProfileName : RequiredProfiles)
	{
		const FPhysicsControlControlAndModifierUpdates* Profile =
			PhysicsControlAsset->Profiles.Find(ProfileName);
		if (!Profile)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "MissingProfile", "PCA missing compiled profile: {0}"),
				FText::FromName(ProfileName));
			return false;
		}

		if (Profile->ControlUpdates.Num() != 1
			|| Profile->ControlUpdates[0].Name != TEXT("ParentSpace")
			|| !Profile->ControlMultiplierUpdates.IsEmpty()
			|| Profile->ModifierUpdates.Num() != 1
			|| Profile->ModifierUpdates[0].Name != TEXT("All"))
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "NonExclusiveProfile", "PCA profile {0} must contain exactly one ParentSpace control update and one All modifier update, with no multiplier or per-limb overrides."),
				FText::FromName(ProfileName));
			return false;
		}
	}

	const FPhysicsControlAndBodyModifierCreationDatas& Additional =
		PhysicsControlAsset->AdditionalControlsAndModifiers;
	const FPhysicsControlSetUpdates& AdditionalSets = PhysicsControlAsset->AdditionalSets;
	if (!Additional.Controls.IsEmpty()
		|| !Additional.Modifiers.IsEmpty()
		|| !AdditionalSets.ControlSetUpdates.IsEmpty()
		|| !AdditionalSets.ModifierSetUpdates.IsEmpty()
		|| !PhysicsControlAsset->InitialControlAndModifierUpdates.IsEmpty())
	{
		return Reject(TEXT("HeavyImpact PCA forbids additional controls, modifiers, sets, and initial updates; all runtime authority must come from the six limbs and five exclusive profiles."));
	}

	const auto FindFinalControlUpdate = [](
		const FPhysicsControlControlAndModifierUpdates& Profile,
		const FName Name) -> const FPhysicsControlNamedControlParameters*
	{
		const FPhysicsControlNamedControlParameters* Result = nullptr;
		for (const FPhysicsControlNamedControlParameters& Update : Profile.ControlUpdates)
		{
			if (Update.Name == Name)
			{
				Result = &Update;
			}
		}
		return Result;
	};

	const auto FindFinalModifierUpdate = [](
		const FPhysicsControlControlAndModifierUpdates& Profile,
		const FName Name) -> const FPhysicsControlNamedModifierParameters*
	{
		const FPhysicsControlNamedModifierParameters* Result = nullptr;
		for (const FPhysicsControlNamedModifierParameters& Update : Profile.ModifierUpdates)
		{
			if (Update.Name == Name)
			{
				Result = &Update;
			}
		}
		return Result;
	};

	const auto ValidateControlProfile = [this, &OutError, &FindFinalControlUpdate](
		const FName ProfileName,
		const bool bExpectedEnabled) -> bool
	{
		const FPhysicsControlControlAndModifierUpdates* Profile =
			PhysicsControlAsset->Profiles.Find(ProfileName);
		const FPhysicsControlNamedControlParameters* Update =
			Profile ? FindFinalControlUpdate(*Profile, TEXT("ParentSpace")) : nullptr;
		if (!Update || !Update->Data.bEnablebEnabled
			|| static_cast<bool>(Update->Data.bEnabled) != bExpectedEnabled)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "InvalidControlEnable", "PCA profile {0} must explicitly set ParentSpace control enabled state."),
				FText::FromName(ProfileName));
			return false;
		}

		if (!bExpectedEnabled)
		{
			return true;
		}

		const FPhysicsControlSparseData& Data = Update->Data;
		const bool bValidAngularOnlyDrive =
			Data.bEnableLinearStrength
			&& FMath::IsNearlyZero(Data.LinearStrength)
			&& Data.bEnableLinearExtraDamping
			&& FMath::IsNearlyZero(Data.LinearExtraDamping)
			&& Data.bEnableAngularStrength
			&& FMath::IsFinite(Data.AngularStrength)
			&& Data.AngularStrength > 0.0f
			&& Data.bEnableMaxTorque
			&& FMath::IsFinite(Data.MaxTorque)
			&& Data.MaxTorque > 0.0f
			&& Data.bEnablebUseSkeletalAnimation
			&& Data.bUseSkeletalAnimation;
		if (!bValidAngularOnlyDrive)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "InvalidAngularOnlyDrive", "PCA profile {0} must use zero linear drive and finite positive angular strength/MaxTorque on ParentSpace."),
				FText::FromName(ProfileName));
			return false;
		}
		return true;
	};

	const auto ValidateModifierProfile = [this, &OutError, &FindFinalModifierUpdate](
		const FName ProfileName,
		const EPhysicsMovementType MovementType,
		const ECollisionEnabled::Type CollisionType,
		const float GravityMultiplier,
		const float BlendWeight,
		const bool bEnableCCD) -> bool
	{
		const FPhysicsControlControlAndModifierUpdates* Profile =
			PhysicsControlAsset->Profiles.Find(ProfileName);
		const FPhysicsControlNamedModifierParameters* Update =
			Profile ? FindFinalModifierUpdate(*Profile, TEXT("All")) : nullptr;
		if (!Update)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "MissingModifierUpdate", "PCA profile {0} must explicitly update the All body modifier set."),
				FText::FromName(ProfileName));
			return false;
		}

		const FPhysicsControlModifierSparseData& Data = Update->Data;
		const bool bValid =
			Data.bEnableMovementType
			&& Data.MovementType == MovementType
			&& Data.bEnableCollisionType
			&& Data.CollisionType == CollisionType
			&& Data.bEnableGravityMultiplier
			&& FMath::IsFinite(Data.GravityMultiplier)
			&& FMath::IsNearlyEqual(Data.GravityMultiplier, GravityMultiplier)
			&& Data.bEnablePhysicsBlendWeight
			&& FMath::IsFinite(Data.PhysicsBlendWeight)
			&& FMath::IsNearlyEqual(Data.PhysicsBlendWeight, BlendWeight)
			&& Data.bEnablebEnableCCD
			&& static_cast<bool>(Data.bEnableCCD) == bEnableCCD;
		if (!bValid)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "InvalidModifierUpdate", "PCA profile {0} has incomplete or incorrect All body modifier settings."),
				FText::FromName(ProfileName));
			return false;
		}
		return true;
	};

	if (!ValidateControlProfile(Demo::HeavyImpact::ProfileInactive, false)
		|| !ValidateControlProfile(Demo::HeavyImpact::ProfilePrepared, true)
		|| !ValidateControlProfile(Demo::HeavyImpact::ProfileFlight, true)
		|| !ValidateControlProfile(Demo::HeavyImpact::ProfileLandingRecovery, true)
		|| !ValidateControlProfile(Demo::HeavyImpact::ProfileFreeFallback, false)
		|| !ValidateModifierProfile(
			Demo::HeavyImpact::ProfileInactive,
			EPhysicsMovementType::Kinematic,
			ECollisionEnabled::QueryOnly,
			1.0f,
			0.0f,
			false)
		|| !ValidateModifierProfile(
			Demo::HeavyImpact::ProfilePrepared,
			EPhysicsMovementType::Simulated,
			ECollisionEnabled::QueryAndPhysics,
			0.0f,
			1.0f,
			true)
		|| !ValidateModifierProfile(
			Demo::HeavyImpact::ProfileFlight,
			EPhysicsMovementType::Simulated,
			ECollisionEnabled::QueryAndPhysics,
			1.0f,
			1.0f,
			true)
		|| !ValidateModifierProfile(
			Demo::HeavyImpact::ProfileLandingRecovery,
			EPhysicsMovementType::Simulated,
			ECollisionEnabled::QueryAndPhysics,
			1.0f,
			1.0f,
			true)
		|| !ValidateModifierProfile(
			Demo::HeavyImpact::ProfileFreeFallback,
			EPhysicsMovementType::Simulated,
			ECollisionEnabled::QueryAndPhysics,
			1.0f,
			1.0f,
			true))
	{
		return false;
	}

	const FPhysicsControlCharacterSetupData& Setup = PhysicsControlAsset->CharacterSetupData;
	struct FRequiredLimb
	{
		FName LimbName;
		FName StartBone;
		bool bIncludeParentBone;
	};

	static const FRequiredLimb RequiredLimbs[] = {
		{ TEXT("Head"), TEXT("head"), false },
		{ TEXT("ArmLeft"), TEXT("upperarm_l"), false },
		{ TEXT("ArmRight"), TEXT("upperarm_r"), false },
		{ TEXT("LegLeft"), TEXT("thigh_l"), false },
		{ TEXT("LegRight"), TEXT("thigh_r"), false },
		{ TEXT("Spine"), TEXT("spine_01"), true }
	};

	if (Setup.LimbSetupData.Num() != UE_ARRAY_COUNT(RequiredLimbs))
	{
		return Reject(TEXT("HeavyImpact PCA must compile exactly six leaf-to-root limbs: Head, ArmLeft, ArmRight, LegLeft, LegRight, Spine."));
	}

	for (int32 LimbIndex = 0; LimbIndex < UE_ARRAY_COUNT(RequiredLimbs); ++LimbIndex)
	{
		const FPhysicsControlLimbSetupData& Limb = Setup.LimbSetupData[LimbIndex];
		const FRequiredLimb& Required = RequiredLimbs[LimbIndex];
		if (Limb.LimbName != Required.LimbName
			|| Limb.StartBone != Required.StartBone
			|| static_cast<bool>(Limb.bIncludeParentBone) != Required.bIncludeParentBone
			|| Limb.bCreateWorldSpaceControls
			|| !Limb.bCreateParentSpaceControls
			|| !Limb.bCreateBodyModifiers)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "InvalidLimbSetup", "PCA limb index {0} must be {1} at bone {2}, with only Spine including its parent; world controls are forbidden."),
				FText::AsNumber(LimbIndex),
				FText::FromName(Required.LimbName),
				FText::FromName(Required.StartBone));
			return false;
		}

		if (Mesh->GetBoneIndex(Required.StartBone) == INDEX_NONE)
		{
			OutError = FText::Format(
				NSLOCTEXT("HeavyImpact", "MissingLimbBone", "Mesh is missing required HeavyImpact limb bone: {0}"),
				FText::FromName(Required.StartBone));
			return false;
		}
	}

	if (Setup.DefaultParentSpaceControlData.bEnabled
		|| Setup.DefaultBodyModifierData.MovementType != EPhysicsMovementType::Kinematic
		|| Setup.DefaultBodyModifierData.CollisionType != ECollisionEnabled::QueryOnly
		|| !FMath::IsFinite(Setup.DefaultBodyModifierData.GravityMultiplier)
		|| !FMath::IsNearlyEqual(Setup.DefaultBodyModifierData.GravityMultiplier, 1.0f)
		|| !FMath::IsNearlyZero(Setup.DefaultBodyModifierData.PhysicsBlendWeight)
		|| Setup.DefaultBodyModifierData.bEnableCCD)
	{
		return Reject(TEXT("HeavyImpact PCA compiled defaults must be disabled parent controls with Kinematic QueryOnly gravity-on animation bodies and CCD off."));
	}

	OutError = FText::GetEmpty();
	return true;
}
