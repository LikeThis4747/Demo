// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardTuningData.cpp
 * 职责：拒绝无法形成合法触发、低弧机械瞄准、物理弹体或可选重冲击准备的配置。
 * 边界：只校验 DataAsset 数据合同，不读取世界重力、关卡摆位、Actor 或 Blueprint 状态。
 */

#include "Data/Hazards/ThrustGuidedHazardTuningData.h"

#include "Data/Physics/CharacterImpactSourceProfile.h"

/** 完整校验当前权威字段；删除旧推进字段后不保留兼容兜底。 */
bool UThrustGuidedHazardTuningData::IsConfigured(FString& OutError) const
{
	OutError.Reset();

	const auto Reject = [&OutError](const TCHAR* Message)
	{
		OutError = Message;
		return false;
	};

	if (!FMath::IsFinite(Damage)
		|| !FMath::IsFinite(TriggerHalfExtent.X)
		|| !FMath::IsFinite(TriggerHalfExtent.Y)
		|| !FMath::IsFinite(TriggerHalfExtent.Z)
		|| !FMath::IsFinite(WarningSeconds)
		|| !FMath::IsFinite(ReferenceRange)
		|| !FMath::IsFinite(PreferredLaunchAngleDegrees)
		|| !FMath::IsFinite(TargetAimHeightOffset)
		|| !FMath::IsFinite(AimUpdateIntervalSeconds)
		|| !FMath::IsFinite(MaximumAimYawDegrees)
		|| !FMath::IsFinite(MaximumAimPitchUpDegrees)
		|| !FMath::IsFinite(MaximumAimPitchDownDegrees)
		|| !FMath::IsFinite(AimTurnSpeedDegreesPerSecond)
		|| !FMath::IsFinite(FallbackElevationDegrees)
		|| !FMath::IsFinite(SpawnClearanceMargin)
		|| !FMath::IsFinite(ProjectileRadius)
		|| !FMath::IsFinite(ProjectileHalfHeight)
		|| !FMath::IsFinite(ProjectileMassKilograms)
		|| !FMath::IsFinite(ProjectileLifetimeSeconds)
		|| !FMath::IsFinite(BallisticAngularDamping)
		|| !FMath::IsFinite(PostImpactLinearDamping)
		|| !FMath::IsFinite(PostImpactAngularDamping)
		|| !FMath::IsFinite(MinimumStandingImpactStrength))
	{
		return Reject(TEXT("预判抛射机关配置包含非有限基础数值。"));
	}

	if (Damage < 0.0f || Damage > 1000.0f)
	{
		return Reject(TEXT("Damage must be within 0..1000."));
	}

	if (TriggerHalfExtent.X < 10.0f || TriggerHalfExtent.X > 2000.0f
		|| TriggerHalfExtent.Y < 10.0f || TriggerHalfExtent.Y > 2000.0f
		|| TriggerHalfExtent.Z < 10.0f || TriggerHalfExtent.Z > 2000.0f)
	{
		return Reject(TEXT("TriggerHalfExtent 的三个分量都必须位于 10~2000 cm。"));
	}

	if (WarningSeconds < 0.05f || WarningSeconds > 5.0f)
	{
		return Reject(TEXT("WarningSeconds 必须位于 0.05~5 s。"));
	}

	if (ReferenceRange < 300.0f || ReferenceRange > 3000.0f)
	{
		return Reject(TEXT("ReferenceRange 必须位于 300~3000 cm。"));
	}

	if (PreferredLaunchAngleDegrees < 5.0f
		|| PreferredLaunchAngleDegrees > 35.0f
		|| PreferredLaunchAngleDegrees >= 45.0f)
	{
		return Reject(TEXT("PreferredLaunchAngleDegrees 必须位于 5~35 deg 且保持低于 45 deg。"));
	}

	if (TargetAimHeightOffset < -100.0f || TargetAimHeightOffset > 200.0f)
	{
		return Reject(TEXT("TargetAimHeightOffset 必须位于 -100~200 cm。"));
	}

	if (AimUpdateIntervalSeconds < 0.02f
		|| AimUpdateIntervalSeconds > 0.2f
		|| AimUpdateIntervalSeconds > WarningSeconds)
	{
		return Reject(TEXT("AimUpdateIntervalSeconds 必须位于 0.02~0.2 s 且不大于 WarningSeconds。"));
	}

	if (MaximumAimYawDegrees < 0.0f || MaximumAimYawDegrees > 60.0f)
	{
		return Reject(TEXT("MaximumAimYawDegrees 必须位于 0~60 deg。"));
	}

	if (MaximumAimPitchUpDegrees < 1.0f
		|| MaximumAimPitchUpDegrees > 30.0f
		|| PreferredLaunchAngleDegrees > MaximumAimPitchUpDegrees)
	{
		return Reject(TEXT("MaximumAimPitchUpDegrees 必须位于 1~30 deg，且不得小于 PreferredLaunchAngleDegrees。"));
	}

	if (MaximumAimPitchDownDegrees < 0.0f
		|| MaximumAimPitchDownDegrees > 45.0f)
	{
		return Reject(TEXT("MaximumAimPitchDownDegrees 必须位于 0~45 deg。"));
	}

	if (AimTurnSpeedDegreesPerSecond < 1.0f
		|| AimTurnSpeedDegreesPerSecond > 360.0f)
	{
		return Reject(TEXT("AimTurnSpeedDegreesPerSecond 必须位于 1~360 deg/s。"));
	}

	if (FallbackElevationDegrees < 0.0f
		|| FallbackElevationDegrees > 20.0f
		|| FallbackElevationDegrees > MaximumAimPitchUpDegrees)
	{
		return Reject(TEXT("FallbackElevationDegrees 必须位于 0~20 deg，且不得超过向上机械上限。"));
	}

	if (SpawnClearanceMargin < 0.0f || SpawnClearanceMargin > 100.0f)
	{
		return Reject(TEXT("SpawnClearanceMargin 必须位于 0~100 cm。"));
	}

	if (ProjectileRadius < 5.0f || ProjectileRadius > 100.0f
		|| ProjectileHalfHeight < ProjectileRadius
		|| ProjectileHalfHeight > 250.0f)
	{
		return Reject(TEXT("ProjectileRadius 必须位于 5~100 cm，ProjectileHalfHeight 必须位于半径~250 cm。"));
	}

	if (ProjectileMassKilograms < 1.0f || ProjectileMassKilograms > 500.0f)
	{
		return Reject(TEXT("ProjectileMassKilograms 必须位于 1~500 kg。"));
	}

	if (ProjectileLifetimeSeconds < 1.0f || ProjectileLifetimeSeconds > 30.0f)
	{
		return Reject(TEXT("ProjectileLifetimeSeconds 必须位于 1~30 s。"));
	}

	if (BallisticAngularDamping < 0.0f || BallisticAngularDamping > 10.0f
		|| PostImpactLinearDamping < 0.0f || PostImpactLinearDamping > 10.0f
		|| PostImpactAngularDamping < 0.0f || PostImpactAngularDamping > 10.0f)
	{
		return Reject(TEXT("弹道角阻尼与首碰后阻尼都必须位于 0~10。"));
	}

	if (MinimumStandingImpactStrength < 0.0f
		|| MinimumStandingImpactStrength > 1.0f)
	{
		return Reject(TEXT("MinimumStandingImpactStrength 必须位于 0~1。"));
	}

	if (IsValid(StandingImpactSourceProfile))
	{
		if (MinimumStandingImpactStrength <= 0.0f)
		{
			return Reject(TEXT("启用 StandingImpactSourceProfile 时，MinimumStandingImpactStrength 必须大于零。"));
		}

		FString ProfileError;
		if (!StandingImpactSourceProfile->IsConfigured(ProfileError))
		{
			OutError = FString::Printf(
				TEXT("StandingImpactSourceProfile 配置无效：%s"),
				*ProfileError);
			return false;
		}
	}

	if (!bEnableHeavyImpactPreparation)
	{
		return true;
	}

	if (!FMath::IsFinite(PreparationLookAheadDistance)
		|| !FMath::IsFinite(MinimumHeavyImpactClosingSpeed)
		|| !FMath::IsFinite(MaximumPreparationLeadTime))
	{
		return Reject(TEXT("启用 HeavyImpact 后，准备配置不得包含非有限数值。"));
	}

	if (PreparationLookAheadDistance < 50.0f
		|| PreparationLookAheadDistance > 2000.0f
		|| PreparationLookAheadDistance < ProjectileHalfHeight)
	{
		return Reject(TEXT("PreparationLookAheadDistance 必须位于 50~2000 cm，且不小于弹体半高。"));
	}

	if (MinimumHeavyImpactClosingSpeed < 1.0f
		|| MinimumHeavyImpactClosingSpeed > 5000.0f)
	{
		return Reject(TEXT("MinimumHeavyImpactClosingSpeed 必须位于 1~5000 cm/s。"));
	}

	if (MaximumPreparationLeadTime < 0.08f || MaximumPreparationLeadTime > 0.5f)
	{
		return Reject(TEXT("MaximumPreparationLeadTime 必须位于 0.08~0.5 s。"));
	}

	return true;
}
