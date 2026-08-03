// Copyright Epic Games, Inc. All Rights Reserved.

/** 纯值策略：过滤 Generator 最终报告，并判定哪些失败允许有限换 Seed 重试。 */

#pragma once

#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

namespace ZeroEscape::GameFlow
{
	struct FGameSetupGateSnapshot
	{
		int64 ActiveOperationId = 0;
		int64 LastHandledOperationId = 0;
		bool bTerminal = false;
		bool bEndingPlay = false;
	};

	class FGameSetupGate final
	{
	public:
		static bool AcceptFinalReport(
			const FGameSetupGateSnapshot& State,
			const int64 ReportOperationId)
		{
			return !State.bTerminal
				&& !State.bEndingPlay
				&& State.ActiveOperationId > 0
				&& ReportOperationId == State.ActiveOperationId
				&& ReportOperationId > State.LastHandledOperationId;
		}

		/** 只允许换 Seed 有机会改变结果的失败；固定配置、装配与导航准备错误不重试。 */
		static bool IsRecoverableGenerationFailure(
			const FZeroEscapeGenerationReport& Report)
		{
			switch (Report.Failure)
			{
			case EZeroEscapeGenerationFailure::CapacityInsufficient:
			case EZeroEscapeGenerationFailure::StructurePlacementFailed:
			case EZeroEscapeGenerationFailure::RequiredRouteTooLong:
			case EZeroEscapeGenerationFailure::RequiredRouteTooShort:
			case EZeroEscapeGenerationFailure::NoValidWfcSolution:
			case EZeroEscapeGenerationFailure::SolverBudgetExhausted:
			case EZeroEscapeGenerationFailure::GlobalConnectivityFailed:
				return true;
			case EZeroEscapeGenerationFailure::NavigationBuildTimeout:
				return Report.Stage == EZeroEscapeGenerationStage::NavigationBuild;
			case EZeroEscapeGenerationFailure::NavigationValidationFailed:
				return Report.Stage == EZeroEscapeGenerationStage::NavigationValidation;
			default:
				return false;
			}
		}

		/** 固定无状态映射：相同失败 Seed 必须得到相同的下一 Seed。 */
		static int32 DeriveAutomaticRetrySeed(const int32 CurrentSeed)
		{
			const uint32 NextBits = static_cast<uint32>(CurrentSeed) * 196314165u
				+ 907633515u;
			int32 NextSeed = static_cast<int32>(NextBits & 0x7fffffffu);
			if (NextSeed == CurrentSeed)
			{
				NextSeed = CurrentSeed == MAX_int32 ? 0 : CurrentSeed + 1;
			}
			return NextSeed;
		}
	};
}
