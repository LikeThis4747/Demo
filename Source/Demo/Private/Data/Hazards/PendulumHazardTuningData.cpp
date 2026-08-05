// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PendulumHazardTuningData.cpp
 * 职责：在摆锤启用物理前校验所有调参范围和几何关系。
 * 边界：不修改参数、不生成第二套默认值、不依赖 Level0 Actor 实例。
 */

#include "Data/Hazards/PendulumHazardTuningData.h"

/** 拒绝非有限数值、越界值以及会触地或让目标摆幅撞上硬限位的组合。 */
bool UPendulumHazardTuningData::IsConfigured(FString& OutError) const
{
	OutError.Reset();

	const auto ValidateFloat = [&OutError](
		const TCHAR* PropertyName,
		const float Value,
		const float Minimum,
		const float Maximum) -> bool
	{
		if (!FMath::IsFinite(Value) || Value < Minimum || Value > Maximum)
		{
			OutError = FString::Printf(
				TEXT("摆锤 Tuning DataAsset 的 %s=%g，合法范围为 %g~%g。"),
				PropertyName,
				Value,
				Minimum,
				Maximum);
			return false;
		}
		return true;
	};

	if (!ValidateFloat(TEXT("PivotHeight"), PivotHeight, 100.0f, 2000.0f)
		|| !ValidateFloat(TEXT("PendulumLength"), PendulumLength, 100.0f, 1500.0f)
		|| !ValidateFloat(TEXT("BobRadius"), BobRadius, 10.0f, 250.0f)
		|| !ValidateFloat(TEXT("BobMassKilograms"), BobMassKilograms, 1.0f, 5000.0f)
		|| !ValidateFloat(TEXT("TargetAmplitudeDegrees"), TargetAmplitudeDegrees, 1.0f, 80.0f)
		|| !ValidateFloat(TEXT("MainAxisLimitDegrees"), MainAxisLimitDegrees, 2.0f, 85.0f)
		|| !ValidateFloat(TEXT("SecondaryAxisLimitDegrees"), SecondaryAxisLimitDegrees, 0.0f, 15.0f)
		|| !ValidateFloat(TEXT("LinearDamping"), LinearDamping, 0.0f, 10.0f)
		|| !ValidateFloat(TEXT("AngularDamping"), AngularDamping, 0.0f, 10.0f)
		|| !ValidateFloat(
			TEXT("MaximumAssistSpeedDeltaPerPass"),
			MaximumAssistSpeedDeltaPerPass,
			0.0f,
			200.0f))
	{
		return false;
	}

	if (BobRadius >= PendulumLength)
	{
		OutError = TEXT("摆锤 Tuning DataAsset 的 BobRadius 必须小于 PendulumLength。");
		return false;
	}

	if (PivotHeight <= PendulumLength + BobRadius)
	{
		OutError = TEXT("摆锤 Tuning DataAsset 的 PivotHeight 必须大于 PendulumLength + BobRadius，避免最低点触地。");
		return false;
	}

	if (TargetAmplitudeDegrees >= MainAxisLimitDegrees)
	{
		OutError = TEXT("摆锤 Tuning DataAsset 的 TargetAmplitudeDegrees 必须小于 MainAxisLimitDegrees。");
		return false;
	}

	return true;
}
