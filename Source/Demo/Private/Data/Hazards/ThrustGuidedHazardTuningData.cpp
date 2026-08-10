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

	if (!FMath::IsFinite(TriggerHalfExtent.X)
		|| !FMath::IsFinite(TriggerHalfExtent.Y)
		|| !FMath::IsFinite(TriggerHalfExtent.Z)
		|| !FMath::IsFinite(WarningSeconds)
		|| !FMath::IsFinite(MaximumInitialAimAngleDegrees)
		|| !FMath::IsFinite(SpawnClearanceMargin)
		|| !FMath::IsFinite(ProjectileRadius)
		|| !FMath::IsFinite(ProjectileHalfHeight)
		|| !FMath::IsFinite(ProjectileMassKilograms)
		|| !FMath::IsFinite(MaximumPoweredAcceleration)
		|| !FMath::IsFinite(TargetPoweredSpeed)
		|| !FMath::IsFinite(MaximumPoweredSpeed)
		|| !FMath::IsFinite(SpeedControlBand)
		|| !FMath::IsFinite(PoweredDurationSeconds)
		|| !FMath::IsFinite(PoweredLinearDamping)
		|| !FMath::IsFinite(PoweredAngularDamping)
		|| !FMath::IsFinite(CoastingLinearDamping)
		|| !FMath::IsFinite(CoastingAngularDamping)
		|| !FMath::IsFinite(MaximumTargetLeadTimeSeconds)
		|| !FMath::IsFinite(OrientationGain)
		|| !FMath::IsFinite(AngularVelocityDampingGain)
		|| !FMath::IsFinite(MaximumAngularAcceleration)
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

	if (MaximumPoweredAcceleration < 10.0f
		|| MaximumPoweredAcceleration > 5000.0f)
	{
		return Reject(TEXT("MaximumPoweredAcceleration 必须位于 10~5000 cm/s^2。"));
	}

	if (TargetPoweredSpeed < 50.0f || TargetPoweredSpeed > 3000.0f)
	{
		return Reject(TEXT("TargetPoweredSpeed 必须位于 50~3000 cm/s。"));
	}

	if (MaximumPoweredSpeed < 50.0f || MaximumPoweredSpeed > 5000.0f)
	{
		return Reject(TEXT("MaximumPoweredSpeed 必须位于 50~5000 cm/s。"));
	}

	if (TargetPoweredSpeed >= MaximumPoweredSpeed)
	{
		return Reject(TEXT("TargetPoweredSpeed 必须小于 MaximumPoweredSpeed。"));
	}

	if (SpeedControlBand < 1.0f || SpeedControlBand > 2000.0f)
	{
		return Reject(TEXT("SpeedControlBand 必须位于 1~2000 cm/s。"));
	}

	if (PoweredDurationSeconds < 0.05f || PoweredDurationSeconds > 5.0f)
	{
		return Reject(TEXT("PoweredDurationSeconds 必须位于 0.05~5 s。"));
	}

	if (PoweredLinearDamping < 0.0f || PoweredLinearDamping > 10.0f
		|| PoweredAngularDamping < 0.0f || PoweredAngularDamping > 10.0f
		|| CoastingLinearDamping < 0.0f || CoastingLinearDamping > 10.0f
		|| CoastingAngularDamping < 0.0f || CoastingAngularDamping > 10.0f)
	{
		return Reject(TEXT("受控与自由物理阶段的阻尼都必须位于 0~10。"));
	}

	if (PoweredLinearDamping < CoastingLinearDamping
		|| PoweredAngularDamping < CoastingAngularDamping)
	{
		return Reject(TEXT("受控阶段阻尼不得低于自由物理阶段阻尼。"));
	}

	if (MaximumTargetLeadTimeSeconds < 0.0f
		|| MaximumTargetLeadTimeSeconds > 1.0f)
	{
		return Reject(TEXT("MaximumTargetLeadTimeSeconds 必须位于 0~1 s。"));
	}

	if (OrientationGain < 0.0f || OrientationGain > 30.0f
		|| AngularVelocityDampingGain < 0.0f
		|| AngularVelocityDampingGain > 20.0f
		|| MaximumAngularAcceleration < 0.1f
		|| MaximumAngularAcceleration > 50.0f)
	{
		return Reject(TEXT("姿态控制增益或最大角加速度超出公开范围。"));
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
