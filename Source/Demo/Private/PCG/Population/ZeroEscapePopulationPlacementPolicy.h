// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePopulationPlacementPolicy.h
 * 职责：用纯整数计算单条 Population 规则的目标格数与 Actor 数安全预算。
 * 边界：不加载资产、不消费随机数、不 Spawn；0 个目标是合法成功结果。
 */

#pragma once

#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

namespace ZeroEscape::LevelGeneration
{
	enum class EPopulationPlacementBudgetResult : uint8
	{
		Success = 0,
		InvalidRule = 1,
		InvalidCandidateCount = 2,
		SpawnBudgetExceeded = 3
	};

	class FPopulationPlacementPolicy final
	{
	public:
		/**
		 * 此处只校验单条规则；Actor 上限复用 C++ 的 MaxGridCells × MaxFloorCount。
		 * 用 int64 先乘，避免 LateralCount 导致 int32 溢出或意外海量 Spawn。
		 */
		static EPopulationPlacementBudgetResult Evaluate(
			const int32 CandidateCount,
			const int32 OneEveryNCells,
			const int32 MaxCount,
			const int32 LateralCount,
			int32& OutTargetCellCount,
			int32& OutActorCount)
		{
			OutTargetCellCount = 0;
			OutActorCount = 0;
			if (OneEveryNCells <= 0 || MaxCount <= 0 || LateralCount <= 0)
			{
				return EPopulationPlacementBudgetResult::InvalidRule;
			}

			constexpr int64 MaxGeneratedAddresses =
				static_cast<int64>(GenerationLimits::MaxGridCells)
				* GenerationLimits::MaxFloorCount;
			if (CandidateCount < 0
				|| static_cast<int64>(CandidateCount) > MaxGeneratedAddresses)
			{
				return EPopulationPlacementBudgetResult::InvalidCandidateCount;
			}

			OutTargetCellCount = FMath::Min(
				MaxCount, CandidateCount / OneEveryNCells);
			const int64 ActorCount =
				static_cast<int64>(OutTargetCellCount) * LateralCount;
			if (ActorCount > MaxGeneratedAddresses)
			{
				OutTargetCellCount = 0;
				return EPopulationPlacementBudgetResult::SpawnBudgetExceeded;
			}
			OutActorCount = static_cast<int32>(ActorCount);
			return EPopulationPlacementBudgetResult::Success;
		}
	};
}
