// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGridLayoutSolver.h
 * 职责：在调用方提供的固定格、禁用格和固定边上复用二维 FWfcSolver，并验收单层路线覆盖。
 * 边界：不放置跨层结构，不读取 UObject，不创建表现对象，不修改二维 WFC 内部算法。
 */

#pragma once

#include "CoreMinimal.h"

#include "PCG/ZeroEscapeGenerationAssets.h"
#include "PCG/ZeroEscapeGenerationTypes.h"
#include "PCG/ZeroEscapeWfcSolver.h"

namespace ZeroEscape::LevelGeneration
{
	/** 同一次整栋候选的所有楼层共享；调用失败也不会自动刷新。 */
	struct FZeroEscapeSharedWfcBudget
	{
		int32 RemainingSolveAttempts = 0;
		int32 RemainingCandidateAttempts = 0;
		int32 RemainingBacktracks = 0;
		int32 ConsumedSolveAttempts = 0;
		int32 ConsumedCandidateAttempts = 0;
		int32 ConsumedBacktracks = 0;
	};

	/** 一层已经由完整结构投影完成的稠密二维求解输入。 */
	struct FZeroEscapeConstrainedFloorInput
	{
		FZeroEscapeGenerationSignature Signature;
		int32 WholeLayoutAttemptIndex = 0;
		int32 FloorIndex = 0;
		FIntPoint GridSize = FIntPoint::ZeroValue;
		FIntPoint RequiredEnterCoordinate = FIntPoint::ZeroValue;
		FIntPoint RequiredLeaveCoordinate = FIntPoint::ZeroValue;
		TArray<FGridCellConstraint> Constraints;
		TArray<uint8> StructureWalkableByCell;
		/** 仅保留可玩性所需的硬下限：一层 2 格，其他层 1 格。 */
		int32 MinTotalWalkableCellCount = 1;
		int32 MaxTotalWalkableCellCount = 1;
		int32 MinOrdinaryWalkableCellCount = 1;
		/** 旧硬限制保留为求解器安全边界，调用方应传网格轴长。 */
		int32 MaxConsecutiveStraightTiles = 1;
		int32 MaxSolveAttemptsForThisFloor = 1;

		/** 以下字段只引导候选权重和有限候选择优，不参与硬拒绝。 */
		int32 PreferredTotalWalkableCellCount = 1;
		int32 PreferredOrdinaryWalkableCellCount = 1;
		int32 PreferredMaxConsecutiveStraightTiles = 0;
		double PreferredRouteCoverageRatio = 0.0;
	};

	/** 一层成功叶子的稠密 OpeningMask 和同一次 BFS 派生指标。 */
	struct FZeroEscapeConstrainedFloorResult
	{
		TArray<uint8> OpeningMaskByCell;
		int32 OrdinaryWalkableCellCount = 0;
		int32 TotalWalkableCellCount = 0;
		int32 RequiredRouteLengthTiles = 0;
		int32 FarthestRouteLengthTiles = 0;
		double RouteCoverageRatio = 0.0;
	};

	class FGridLayoutSolver final
	{
	public:
		/**
		 * 成功时原子提交完整稠密结果；失败时 OutResult 为空。
		 * 每棵二维搜索树只领取共享剩余预算的确定性份额，实际消耗立即从共享账本扣除。
		 */
		static bool SolveConstrainedFloor(
			const FZeroEscapeConstrainedFloorInput& Input,
			const FZeroEscapeWfcShapeWeights& Weights,
			FZeroEscapeSharedWfcBudget& InOutBudget,
			FZeroEscapeConstrainedFloorResult& OutResult,
			FZeroEscapeGenerationReport& OutReport);
	};
}
