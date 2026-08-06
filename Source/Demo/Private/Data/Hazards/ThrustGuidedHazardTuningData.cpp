// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardTuningData.cpp
 * 职责：拒绝无法形成有效触发体、物理弹体、有限推进或有限预测窗口的配置。
 * 边界：只校验 DataAsset 数据合同，不读取世界、关卡摆位或 Actor 状态。
 */

#include "Data/Hazards/ThrustGuidedHazardTuningData.h"

/** 校验所有输入为有限值且位于公开编辑范围内；非法资产由使用者明确停用。 */
bool UThrustGuidedHazardTuningData::IsConfigured(FString& OutError) const
{
	OutError.Reset();

	const auto Reject = [&OutError](const TCHAR* Message)
	{
		OutError = Message;
		return false;
	};

	if (TriggerHalfExtent.ContainsNaN()
		|| !FMath::IsFinite(WarningSeconds)
		|| !FMath::IsFinite(MaximumInitialAimAngleDegrees)
		|| !FMath::IsFinite(ProjectileRadius)
		|| !FMath::IsFinite(ProjectileHalfHeight)
		|| !FMath::IsFinite(ProjectileMassKilograms)
		|| !FMath::IsFinite(ThrustStrength)
		|| !FMath::IsFinite(PoweredDurationSeconds)
		|| !FMath::IsFinite(LinearDamping)
		|| !FMath::IsFinite(AngularDamping)
		|| !FMath::IsFinite(TargetLeadTimeSeconds)
		|| !FMath::IsFinite(OrientationGain)
		|| !FMath::IsFinite(AngularVelocityDampingGain)
		|| !FMath::IsFinite(MaximumGimbalAngleDegrees)
		|| !FMath::IsFinite(MaximumGimbalRateDegreesPerSecond)
		|| !FMath::IsFinite(PreparationLookAheadDistance)
		|| !FMath::IsFinite(MinimumHeavyImpactClosingSpeed)
		|| !FMath::IsFinite(MaximumPreparationLeadTime))
	{
		return Reject(TEXT("物理制导机关配置包含非有限数值。"));
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

	if (MaximumInitialAimAngleDegrees < 0.0f || MaximumInitialAimAngleDegrees > 60.0f)
	{
		return Reject(TEXT("MaximumInitialAimAngleDegrees 必须位于 0~60 deg。"));
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

	if (ThrustStrength < 1000.0f || ThrustStrength > 2000000.0f)
	{
		return Reject(TEXT("ThrustStrength 必须位于 1000~2000000。"));
	}

	if (PoweredDurationSeconds < 0.05f || PoweredDurationSeconds > 5.0f)
	{
		return Reject(TEXT("PoweredDurationSeconds 必须位于 0.05~5 s。"));
	}

	if (LinearDamping < 0.0f || LinearDamping > 10.0f
		|| AngularDamping < 0.0f || AngularDamping > 10.0f)
	{
		return Reject(TEXT("LinearDamping 和 AngularDamping 必须位于 0~10。"));
	}

	if (TargetLeadTimeSeconds < 0.0f || TargetLeadTimeSeconds > 0.5f
		|| OrientationGain < 0.0f || OrientationGain > 10.0f
		|| AngularVelocityDampingGain < 0.0f || AngularVelocityDampingGain > 10.0f)
	{
		return Reject(TEXT("制导前置时间或 PD 增益超出公开范围。"));
	}

	if (MaximumGimbalAngleDegrees < 0.0f || MaximumGimbalAngleDegrees > 45.0f
		|| MaximumGimbalRateDegreesPerSecond < 1.0f
		|| MaximumGimbalRateDegreesPerSecond > 720.0f)
	{
		return Reject(TEXT("喷口最大偏角必须位于 0~45 deg，转速必须位于 1~720 deg/s。"));
	}

	if (PreparationLookAheadDistance < 50.0f || PreparationLookAheadDistance > 2000.0f
		|| PreparationLookAheadDistance < ProjectileHalfHeight)
	{
		return Reject(TEXT("PreparationLookAheadDistance 必须位于 50~2000 cm，且不小于弹体半高。"));
	}

	if (MinimumHeavyImpactClosingSpeed < 1.0f || MinimumHeavyImpactClosingSpeed > 5000.0f)
	{
		return Reject(TEXT("MinimumHeavyImpactClosingSpeed 必须位于 1~5000 cm/s。"));
	}

	if (MaximumPreparationLeadTime < 0.08f || MaximumPreparationLeadTime > 0.5f)
	{
		return Reject(TEXT("MaximumPreparationLeadTime 必须位于 0.08~0.5 s。"));
	}

	return true;
}
