// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticGrabTuningData.cpp
 * 职责：在磁力组件启用前校验调参资产，阻止非法数值进入吸取曲线、Physics Handle 或安全释放逻辑。
 * 边界：不修改组件、不钳制或偷偷修正资产数值，也不提供第二套默认配置。
 * 状态 Owner：资产拥有参数；校验结果由调用方用于决定是否启用磁力功能。
 */

#include "Data/Magnetism/MagneticGrabTuningData.h"

/** 校验所有公开范围及 MinimumHoldDistance 不大于 HoldDistance 的跨属性约束。 */
bool UMagneticGrabTuningData::IsConfigured(FString& OutError) const
{
	OutError.Reset();

	/** 验证浮点属性既为有限数值又落在编辑器声明范围内，避免程序化赋值绕过元数据。 */
	const auto ValidateFloat = [&OutError](
		const TCHAR* PropertyName,
		const float Value,
		const float Minimum,
		const float Maximum) -> bool
	{
		if (!FMath::IsFinite(Value) || Value < Minimum || Value > Maximum)
		{
			OutError = FString::Printf(
				TEXT("磁力 Tuning DataAsset 的 %s=%g，合法范围为 %g~%g。"),
				PropertyName,
				Value,
				Minimum,
				Maximum);
			return false;
		}
		return true;
	};

	if (!ValidateFloat(TEXT("GrabRange"), GrabRange, 100.0f, 3000.0f)
		|| !ValidateFloat(TEXT("ScreenSelectionRadiusRatio"), ScreenSelectionRadiusRatio, 0.01f, 0.5f)
		|| !ValidateFloat(TEXT("MaximumGrabMass"), MaximumGrabMass, 1.0f, 300.0f)
		|| MaximumCandidateChecks < 1 || MaximumCandidateChecks > 128
		|| !ValidateFloat(TEXT("HoldDistance"), HoldDistance, 80.0f, 600.0f)
		|| !ValidateFloat(TEXT("HoldSideOffset"), HoldSideOffset, -300.0f, 300.0f)
		|| !ValidateFloat(TEXT("HoldHeight"), HoldHeight, -100.0f, 300.0f)
		|| !ValidateFloat(TEXT("HeldAngularDamping"), HeldAngularDamping, 0.0f, 50.0f)
		|| !ValidateFloat(TEXT("PullReferenceSpeed"), PullReferenceSpeed, 500.0f, 5000.0f)
		|| !ValidateFloat(TEXT("MinimumPullDuration"), MinimumPullDuration, 0.1f, 1.0f)
		|| !ValidateFloat(TEXT("PullArcHeightRatio"), PullArcHeightRatio, 0.0f, 0.2f)
		|| !ValidateFloat(TEXT("MaximumPullArcHeight"), MaximumPullArcHeight, 0.0f, 200.0f)
		|| !ValidateFloat(TEXT("MinimumHoldDistance"), MinimumHoldDistance, 20.0f, 300.0f)
		|| !ValidateFloat(TEXT("ObstructionClearance"), ObstructionClearance, 0.0f, 100.0f)
		|| !ValidateFloat(TEXT("ObstructionReleaseDelay"), ObstructionReleaseDelay, 0.05f, 3.0f)
		|| !ValidateFloat(TEXT("PullGracePeriod"), PullGracePeriod, 0.0f, 3.0f)
		|| !ValidateFloat(TEXT("MaximumHoldError"), MaximumHoldError, 100.0f, 2000.0f)
		|| !ValidateFloat(TEXT("ThrowSpeed"), ThrowSpeed, 100.0f, 8000.0f)
		|| !ValidateFloat(TEXT("AimTraceDistance"), AimTraceDistance, 1000.0f, 50000.0f)
		|| !ValidateFloat(TEXT("HandleLinearStiffness"), HandleLinearStiffness, 1.0f, 10000.0f)
		|| !ValidateFloat(TEXT("HandleLinearDamping"), HandleLinearDamping, 0.0f, 5000.0f)
		|| !ValidateFloat(TEXT("HandleInterpolationSpeed"), HandleInterpolationSpeed, 0.1f, 200.0f))
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("磁力 Tuning DataAsset 的 MaximumCandidateChecks=%d，合法范围为 1~128。"),
				MaximumCandidateChecks);
		}
		return false;
	}

	if (MinimumHoldDistance > HoldDistance)
	{
		OutError = TEXT("磁力 Tuning DataAsset 的 MinimumHoldDistance 不得大于 HoldDistance。");
		return false;
	}

	return true;
}
