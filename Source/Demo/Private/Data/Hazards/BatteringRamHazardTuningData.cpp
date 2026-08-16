// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file BatteringRamHazardTuningData.cpp
 * 职责：拒绝无法形成有效冲程、有限时序或有效预测体积的冲锤配置。
 * 边界：只校验数据契约，不读取世界和 Actor 状态。
 */

#include "Data/Hazards/BatteringRamHazardTuningData.h"

/** 校验所有输入为有限值且位于公开编辑范围内；运行时不静默修正非法资产。 */
bool UBatteringRamHazardTuningData::IsConfigured(FString& OutError) const
{
	OutError.Reset();

	const auto Reject = [&OutError](const TCHAR* Message)
	{
		OutError = Message;
		return false;
	};

	if (!FMath::IsFinite(Damage)
		|| RamBodyHalfExtent.ContainsNaN()
		|| !FMath::IsFinite(StrokeDistance)
		|| !FMath::IsFinite(RetractedWaitSeconds)
		|| !FMath::IsFinite(WarningSeconds)
		|| !FMath::IsFinite(ExtensionSeconds)
		|| !FMath::IsFinite(RetractionSeconds)
		|| !FMath::IsFinite(PreparationLookAheadDistance)
		|| !FMath::IsFinite(MaximumPreparationLeadTime)
		|| !FMath::IsFinite(PhysicalResponseScale))
	{
		return Reject(TEXT("冲锤配置包含非有限数值。"));
	}

	if (Damage < 0.0f || Damage > 1000.0f)
	{
		return Reject(TEXT("Damage must be within 0..1000."));
	}

	if (RamBodyHalfExtent.X < 1.0f || RamBodyHalfExtent.X > 500.0f
		|| RamBodyHalfExtent.Y < 1.0f || RamBodyHalfExtent.Y > 500.0f
		|| RamBodyHalfExtent.Z < 1.0f || RamBodyHalfExtent.Z > 500.0f)
	{
		return Reject(TEXT("RamBodyHalfExtent 的三个分量都必须位于 1~500 cm。"));
	}

	if (StrokeDistance < 10.0f || StrokeDistance > 1000.0f)
	{
		return Reject(TEXT("StrokeDistance 必须位于 10~1000 cm。"));
	}

	if (RetractedWaitSeconds < 0.05f || RetractedWaitSeconds > 10.0f
		|| WarningSeconds < 0.05f || WarningSeconds > 5.0f
		|| ExtensionSeconds < 0.05f || ExtensionSeconds > 5.0f
		|| RetractionSeconds < 0.05f || RetractionSeconds > 10.0f)
	{
		return Reject(TEXT("冲锤阶段时间超出各自公开编辑范围。"));
	}

	if (PreparationLookAheadDistance < 10.0f || PreparationLookAheadDistance > 1000.0f)
	{
		return Reject(TEXT("PreparationLookAheadDistance 必须位于 10~1000 cm。"));
	}

	if (MaximumPreparationLeadTime < 0.08f || MaximumPreparationLeadTime > 0.5f)
	{
		return Reject(TEXT("MaximumPreparationLeadTime 必须位于 0.08~0.5 s。"));
	}

	if (PhysicalResponseScale < 0.0f || PhysicalResponseScale > 1.0f)
	{
		return Reject(TEXT("PhysicalResponseScale 必须位于 0~1。"));
	}

	return true;
}
