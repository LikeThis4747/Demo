// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcSolver.h
 *
 * 职责：声明单层四邻域 Grid-WFC 的纯值输入、固定 16 状态变体、有界回溯和完成态验收入口。
 * 边界：不读取 UObject、DataAsset、World 或 StaticMesh；不解释 K-of-N、房间路线或表现资源。
 *
 * Solver 只负责局部开闭边传播、具体全局约束、最小熵观察、chronological backtracking 与原子导出。
 * Grid 通过 FWfcCollapsedCandidateValidator 验收完整叶子；RejectBranch 继续回溯，FatalError 立即失败。
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Containers/StaticArray.h"
#include "Math/RandomStream.h"
#include "Templates/Function.h"

#include "PCG/ZeroEscapeGenerationTypes.h"
#include "PCG/ZeroEscapeWfcConstraints.h"

struct FZeroEscapeWfcShapeWeights;

namespace ZeroEscape::LevelGeneration
{
	/**
	 * Cell 是否参与本次 WFC。
	 *
	 * Outside 固定为空；Required 必须选择非空 OpeningMask，但不要求调用方预刻一条外部开口；
	 * Optional 可以为空，也可以选择满足一元和相邻约束的任意非空四向形态。
	 */
	enum class EGridCellDomain : uint8
	{
		Outside = 0,
		Required = 1,
		Optional = 2
	};

	/**
	 * 单个逻辑 Cell 的一元约束。
	 *
	 * RequiredOpenMask 与 RequiredClosedMask 使用 N/E/S/W 四个稳定 bit。相邻 Cell 的开闭状态
	 * 仍由 WFC 二元传播统一保证；这里不保存 Mesh Socket、宽度等级或世界坐标。
	 */
	struct FGridCellConstraint
	{
		FIntPoint Coordinate = FIntPoint::ZeroValue;
		EGridCellDomain Domain = EGridCellDomain::Outside;
		uint8 RequiredOpenMask = 0;
		uint8 RequiredClosedMask = 0;
		int32 RegionId = INDEX_NONE;
		EZeroEscapeGridRegionKind RegionKind = EZeroEscapeGridRegionKind::Corridor;
	};

	/**
	 * 一个固定 WFC 状态。
	 *
	 * OpeningMask 同时是状态的稳定逻辑身份；Weight 只影响当前合法 Domain 内的观察概率，
	 * 不得通过零权重移除任何一个 Mask。
	 */
	struct FTileVariant
	{
		uint8 OpeningMask = 0;
		int32 Weight = 1;
	};

	/** 完整折叠候选交给 Grid 玩法验收后的三态结论。 */
	enum class EWfcCollapsedCandidateVerdict : uint8
	{
		/** 当前完整布局满足全部调用方规则，可以原子提交。 */
		Accept = 0,

		/** 当前布局结构合法但不满足路线/折返规则；作为当前分支矛盾继续回溯。 */
		RejectBranch = 1,

		/** 调用方发现配置或代码不变量错误；不得用回溯掩盖。 */
		FatalError = 2
	};

	/**
	 * 完整候选验收的轻量返回值。
	 *
	 * Solver 不解释 Message、ActualValue 或 LimitValue 的玩法语义；它只区分接受、可恢复拒绝和致命错误。
	 */
	struct FWfcCollapsedCandidateEvaluation
	{
		EWfcCollapsedCandidateVerdict Verdict = EWfcCollapsedCandidateVerdict::Accept;
		FString Message;
		int32 ActualValue = 0;
		int32 LimitValue = 0;
		int32 RelatedStableId = INDEX_NONE;

		static FWfcCollapsedCandidateEvaluation Accept()
		{
			return {};
		}

		static FWfcCollapsedCandidateEvaluation Reject(
			FString InMessage,
			const int32 InActualValue,
			const int32 InLimitValue)
		{
			FWfcCollapsedCandidateEvaluation Result;
			Result.Verdict = EWfcCollapsedCandidateVerdict::RejectBranch;
			Result.Message = MoveTemp(InMessage);
			Result.ActualValue = InActualValue;
			Result.LimitValue = InLimitValue;
			return Result;
		}

		static FWfcCollapsedCandidateEvaluation Fatal(
			FString InMessage,
			const int32 InRelatedStableId = INDEX_NONE)
		{
			FWfcCollapsedCandidateEvaluation Result;
			Result.Verdict = EWfcCollapsedCandidateVerdict::FatalError;
			Result.Message = MoveTemp(InMessage);
			Result.RelatedStableId = InRelatedStableId;
			return Result;
		}
	};

	/**
	 * 同步完成态验收函数。
	 *
	 * 输入 View 只在本次调用期间有效；调用方不得保存它，也不得修改 Solver 或 Random 状态。
	 */
	using FWfcCollapsedCandidateValidator =
		TFunctionRef<FWfcCollapsedCandidateEvaluation(TConstArrayView<uint8>)>;

	/** 完整 16 OpeningMask 的 Simple-Tiled WFC。 */
	class FWfcSolver final
	{
	public:
		/** 按 OpeningMask=0..15 的稳定顺序构建完整状态集；每个状态读取对应形态权重。 */
		static void BuildCanonicalVariants(
			const FZeroEscapeWfcShapeWeights& Weights,
			TStaticArray<FTileVariant, 16>& OutVariants);

		/**
		 * 运行带 Count、MaxConsecutive、Connected 和有界 chronological backtracking 的 WFC。
		 *
		 * OutOpeningMaskByCell 按 ZeroEscape::Grid::ToIndex 的 (Y, X) 稳定顺序导出。入口立即清空输出；
		 * 只有全部 Cell 折叠且 ValidateCollapsedCandidate 返回 Accept，才一次性提交结果。
		 */
		static bool Solve(
			FIntPoint GridSize,
			const TArray<FGridCellConstraint>& Constraints,
			const FZeroEscapeWfcSolveSettings& SolveSettings,
			const TArray<FTileVariant>& Variants,
			FRandomStream& Random,
			FWfcCollapsedCandidateValidator ValidateCollapsedCandidate,
			TArray<uint8>& OutOpeningMaskByCell,
			FZeroEscapeGenerationReport& OutReport);
	};
}
