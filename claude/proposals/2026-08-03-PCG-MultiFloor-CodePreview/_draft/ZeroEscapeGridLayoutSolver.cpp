// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGridLayoutSolver.cpp
 * 职责：把完整结构已经冻结的单层稠密约束交给现有二维 FWfcSolver，并在完整叶子上验收路线覆盖。
 * 边界：不重新决定楼梯、房间或端口；所有完整布局重试共享调用方持有的预算账本。
 */

#include "PCG/ZeroEscapeGridLayoutSolver.h"

#include "Containers/ArrayView.h"
#include "Containers/Queue.h"
#include "Containers/StaticArray.h"

#include "PCG/ZeroEscapeGenerationCore.h"

namespace ZeroEscape::LevelGeneration
{
	namespace GridLayoutPrivate
	{
		bool FailFloor(
			FZeroEscapeGenerationReport& OutReport,
			const EZeroEscapeGenerationStage Stage,
			const EZeroEscapeGenerationFailure Failure,
			const FString& Message,
			const int32 ActualValue = 0,
			const int32 LimitValue = 0)
		{
			OutReport.Stage = Stage;
			OutReport.Failure = Failure;
			OutReport.RelatedStableId = INDEX_NONE;
			OutReport.ActualValue = ActualValue;
			OutReport.LimitValue = LimitValue;
			OutReport.Message = Message;
			return false;
		}

		void AccumulateWfcMetrics(
			FZeroEscapeGenerationMetrics& InOutTotal,
			const FZeroEscapeGenerationMetrics& Source)
		{
			InOutTotal.WfcSolveAttemptCount += Source.WfcSolveAttemptCount;
			InOutTotal.WfcObservationCount += Source.WfcObservationCount;
			InOutTotal.WfcCandidateAttemptCount += Source.WfcCandidateAttemptCount;
			InOutTotal.WfcPropagationCount += Source.WfcPropagationCount;
			InOutTotal.WfcContradictionCount += Source.WfcContradictionCount;
			InOutTotal.WfcLocalAdjacencyContradictionCount +=
				Source.WfcLocalAdjacencyContradictionCount;
			InOutTotal.WfcCountContradictionCount += Source.WfcCountContradictionCount;
			InOutTotal.WfcMaxConsecutiveContradictionCount +=
				Source.WfcMaxConsecutiveContradictionCount;
			InOutTotal.WfcConnectedContradictionCount +=
				Source.WfcConnectedContradictionCount;
			InOutTotal.WfcGlobalBanContradictionCount +=
				Source.WfcGlobalBanContradictionCount;
			InOutTotal.WfcBacktrackCount += Source.WfcBacktrackCount;
			InOutTotal.WfcCollapsedCandidateRejectionCount +=
				Source.WfcCollapsedCandidateRejectionCount;
			InOutTotal.WfcInvariantFailureCount += Source.WfcInvariantFailureCount;
		}

		int32 MakeFloorWfcSalt(
			const int32 WholeLayoutAttemptIndex,
			const int32 FloorIndex,
			const int32 LocalSolveAttemptIndex)
		{
			uint32 Mixed = static_cast<uint32>(WholeLayoutAttemptIndex) * 0x9E3779B9u;
			Mixed ^= static_cast<uint32>(FloorIndex) * 0x85EBCA6Bu;
			Mixed ^= static_cast<uint32>(LocalSolveAttemptIndex) * 0xC2B2AE35u;
			Mixed ^= Mixed >> 16;
			Mixed *= 0x7FEB352Du;
			Mixed ^= Mixed >> 15;
			return static_cast<int32>(Mixed);
		}

		bool ValidateInput(
			const FZeroEscapeConstrainedFloorInput& Input,
			const FZeroEscapeWfcShapeWeights& Weights,
			const FZeroEscapeSharedWfcBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int64 CellCount64 =
				static_cast<int64>(Input.GridSize.X) * Input.GridSize.Y;
			FString WeightError;
			if (Input.Signature.AlgorithmVersion <= 0
				|| Input.Signature.GenerationProfileVersion <= 0
				|| Input.WholeLayoutAttemptIndex < 0
				|| Input.FloorIndex < 0
				|| Input.GridSize.X < GenerationLimits::MinGridAxis
				|| Input.GridSize.Y < GenerationLimits::MinGridAxis
				|| Input.GridSize.X > GenerationLimits::MaxGridAxis
				|| Input.GridSize.Y > GenerationLimits::MaxGridAxis
				|| CellCount64 <= 0
				|| CellCount64 > GenerationLimits::MaxGridCells
				|| Input.Constraints.Num() != CellCount64
				|| Input.StructureWalkableByCell.Num() != CellCount64
				|| !Grid::IsInside(Input.RequiredEnterCoordinate, Input.GridSize)
				|| !Grid::IsInside(Input.RequiredLeaveCoordinate, Input.GridSize)
				|| Input.RequiredEnterCoordinate == Input.RequiredLeaveCoordinate
				|| Input.MinTotalWalkableCellCount <= 0
				|| Input.MaxTotalWalkableCellCount < Input.MinTotalWalkableCellCount
				|| Input.MinOrdinaryWalkableCellCount <= 0
				|| Input.MaxConsecutiveStraightTiles <= 0
				|| Input.MaxConsecutiveStraightTiles
					> FMath::Max(Input.GridSize.X, Input.GridSize.Y)
				|| Input.MaxSolveAttemptsForThisFloor <= 0
				|| !FMath::IsFinite(Input.MinRouteCoverageRatio)
				|| Input.MinRouteCoverageRatio < 0.0
				|| Input.MinRouteCoverageRatio > 1.0
				|| Budget.RemainingSolveAttempts < 0
				|| Budget.RemainingCandidateAttempts < 0
				|| Budget.RemainingBacktracks < 0
				|| !Weights.IsConfigured(WeightError))
			{
				return FailFloor(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					WeightError.IsEmpty()
						? TEXT("受约束楼层输入、路线比例或共享预算非法。")
						: WeightError);
			}

			int32 FixedStructureWalkableCount = 0;
			int32 PossibleWalkableCount = 0;
			for (int32 DenseIndex = 0; DenseIndex < Input.Constraints.Num(); ++DenseIndex)
			{
				const FGridCellConstraint& Cell = Input.Constraints[DenseIndex];
				const FIntPoint Expected(
					DenseIndex % Input.GridSize.X,
					DenseIndex / Input.GridSize.X);
				if (Cell.Coordinate != Expected
					|| Input.StructureWalkableByCell[DenseIndex] > 1
					|| (Input.StructureWalkableByCell[DenseIndex] != 0
						&& Cell.Domain != EGridCellDomain::Required))
				{
					return FailFloor(
						OutReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("楼层约束必须按 row-major 完整排列，结构可走格必须是 Required。"));
				}

				FixedStructureWalkableCount +=
					Input.StructureWalkableByCell[DenseIndex] != 0 ? 1 : 0;
				PossibleWalkableCount +=
					Cell.Domain != EGridCellDomain::Outside ? 1 : 0;
			}

			const int32 MinimumTotalWalkable = FMath::Max(
				Input.MinTotalWalkableCellCount,
				FixedStructureWalkableCount + Input.MinOrdinaryWalkableCellCount);
			if (MinimumTotalWalkable > PossibleWalkableCount
				|| Input.MaxTotalWalkableCellCount < MinimumTotalWalkable)
			{
				return FailFloor(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::CapacityInsufficient,
					TEXT("结构占用后本层无法同时满足总可走格上下限和普通可走格下限。"),
					MinimumTotalWalkable,
					FMath::Min(PossibleWalkableCount, Input.MaxTotalWalkableCellCount));
			}

			return true;
		}

		bool BuildRouteMetrics(
			const FZeroEscapeConstrainedFloorInput& Input,
			const TConstArrayView<uint8> OpeningMasks,
			FZeroEscapeConstrainedFloorResult& OutResult,
			FString& OutError)
		{
			OutResult = {};
			OutError.Reset();
			if (OpeningMasks.Num() != Input.Constraints.Num())
			{
				OutError = TEXT("WFC 完整叶数量与本层稠密约束数量不一致。");
				return false;
			}

			const int32 EnterIndex =
				Grid::ToIndex(Input.RequiredEnterCoordinate, Input.GridSize);
			const int32 LeaveIndex =
				Grid::ToIndex(Input.RequiredLeaveCoordinate, Input.GridSize);
			if (OpeningMasks[EnterIndex] == 0 || OpeningMasks[LeaveIndex] == 0)
			{
				OutError = TEXT("本层 Required 进入点或离开点在完整叶中为空。");
				return false;
			}

			TArray<int32> Distances;
			Distances.Init(INDEX_NONE, OpeningMasks.Num());
			TQueue<int32> Queue;
			Distances[EnterIndex] = 0;
			Queue.Enqueue(EnterIndex);
			int32 CurrentIndex = INDEX_NONE;
			while (Queue.Dequeue(CurrentIndex))
			{
				const FIntPoint Current(
					CurrentIndex % Input.GridSize.X,
					CurrentIndex / Input.GridSize.X);
				const uint8 CurrentMask = OpeningMasks[CurrentIndex];
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if ((CurrentMask & Grid::DirectionBit(Direction)) == 0)
					{
						continue;
					}

					const FIntPoint Neighbor = Grid::Step(Current, Direction);
					if (!Grid::IsInside(Neighbor, Input.GridSize))
					{
						OutError = TEXT("WFC 完整叶包含指向本层网格外的开口。");
						return false;
					}

					const int32 NeighborIndex = Grid::ToIndex(Neighbor, Input.GridSize);
					const uint8 OppositeBit = Grid::DirectionBit(
						Grid::OppositeDirectionIndex(Direction));
					if ((OpeningMasks[NeighborIndex] & OppositeBit) == 0)
					{
						OutError = TEXT("WFC 完整叶含有不对称公共开口。");
						return false;
					}
					if (Distances[NeighborIndex] == INDEX_NONE)
					{
						Distances[NeighborIndex] = Distances[CurrentIndex] + 1;
						Queue.Enqueue(NeighborIndex);
					}
				}
			}

			int32 FixedStructureWalkableCount = 0;
			for (int32 DenseIndex = 0; DenseIndex < OpeningMasks.Num(); ++DenseIndex)
			{
				if (OpeningMasks[DenseIndex] == 0)
				{
					continue;
				}
				if (Distances[DenseIndex] == INDEX_NONE)
				{
					OutError = TEXT("本层完整叶包含不属于进入点连通分量的非空格。");
					return false;
				}
				++OutResult.TotalWalkableCellCount;
				OutResult.FarthestRouteLengthTiles = FMath::Max(
					OutResult.FarthestRouteLengthTiles,
					Distances[DenseIndex]);
				FixedStructureWalkableCount +=
					Input.StructureWalkableByCell[DenseIndex] != 0 ? 1 : 0;
			}

			OutResult.OrdinaryWalkableCellCount =
				OutResult.TotalWalkableCellCount - FixedStructureWalkableCount;
			OutResult.RequiredRouteLengthTiles = Distances[LeaveIndex];
			if (OutResult.RequiredRouteLengthTiles < 0
				|| OutResult.FarthestRouteLengthTiles <= 0
				|| OutResult.TotalWalkableCellCount < Input.MinTotalWalkableCellCount
				|| OutResult.TotalWalkableCellCount > Input.MaxTotalWalkableCellCount
				|| OutResult.OrdinaryWalkableCellCount
					< Input.MinOrdinaryWalkableCellCount)
			{
				OutError = TEXT("本层完整叶没有满足进入/离开路线或普通内容下限。");
				return false;
			}

			OutResult.RouteCoverageRatio =
				static_cast<double>(OutResult.RequiredRouteLengthTiles)
				/ OutResult.FarthestRouteLengthTiles;
			OutResult.OpeningMaskByCell.Append(
				OpeningMasks.GetData(),
				OpeningMasks.Num());
			return true;
		}
	}

	bool FGridLayoutSolver::SolveConstrainedFloor(
		const FZeroEscapeConstrainedFloorInput& Input,
		const FZeroEscapeWfcShapeWeights& Weights,
		FZeroEscapeSharedWfcBudget& InOutBudget,
		FZeroEscapeConstrainedFloorResult& OutResult,
		FZeroEscapeGenerationReport& OutReport)
	{
		using namespace GridLayoutPrivate;
		OutResult = {};
		OutReport = {};
		if (!ValidateInput(Input, Weights, InOutBudget, OutReport))
		{
			return false;
		}

		if (InOutBudget.RemainingSolveAttempts <= 0
			|| InOutBudget.RemainingCandidateAttempts <= 0
			|| InOutBudget.RemainingBacktracks <= 0)
		{
			return FailFloor(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::SolverBudgetExhausted,
				TEXT("进入本层 WFC 前整栋共享预算已经耗尽。"));
		}

		TStaticArray<FTileVariant, 16> StaticVariants;
		FWfcSolver::BuildCanonicalVariants(Weights, StaticVariants);
		TArray<FTileVariant> Variants;
		Variants.Reserve(StaticVariants.Num());
		for (const FTileVariant& Variant : StaticVariants)
		{
			Variants.Add(Variant);
		}

		int32 FixedStructureWalkableCount = 0;
		int32 PossibleWalkableCount = 0;
		for (int32 DenseIndex = 0; DenseIndex < Input.Constraints.Num(); ++DenseIndex)
		{
			FixedStructureWalkableCount +=
				Input.StructureWalkableByCell[DenseIndex] != 0 ? 1 : 0;
			PossibleWalkableCount +=
				Input.Constraints[DenseIndex].Domain != EGridCellDomain::Outside ? 1 : 0;
		}

		FZeroEscapeWfcSolveSettings Settings;
		Settings.StartCoordinate = Input.RequiredEnterCoordinate;
		Settings.MinWalkableCellCount = FMath::Max(
			Input.MinTotalWalkableCellCount,
			FixedStructureWalkableCount + Input.MinOrdinaryWalkableCellCount);
		Settings.MaxWalkableCellCount = FMath::Min(
			PossibleWalkableCount,
			Input.MaxTotalWalkableCellCount);
		Settings.MaxConsecutiveStraightTiles = Input.MaxConsecutiveStraightTiles;

		FZeroEscapeGenerationMetrics AggregateMetrics;
		const int32 MaxTreesThisCall = FMath::Min(
			Input.MaxSolveAttemptsForThisFloor,
			InOutBudget.RemainingSolveAttempts);
		for (int32 LocalAttempt = 0; LocalAttempt < MaxTreesThisCall; ++LocalAttempt)
		{
			if (InOutBudget.RemainingSolveAttempts <= 0
				|| InOutBudget.RemainingCandidateAttempts <= 0
				|| InOutBudget.RemainingBacktracks <= 0)
			{
				OutReport.Metrics = AggregateMetrics;
				return FailFloor(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::SolverBudgetExhausted,
					TEXT("本层 WFC 消耗完整栋共享预算。"));
			}

			Settings.MaxCandidateAttempts = FMath::Max(
				1,
				(InOutBudget.RemainingCandidateAttempts
					+ InOutBudget.RemainingSolveAttempts - 1)
				/ InOutBudget.RemainingSolveAttempts);
			Settings.MaxBacktrackCount = FMath::Max(
				1,
				(InOutBudget.RemainingBacktracks
					+ InOutBudget.RemainingSolveAttempts - 1)
				/ InOutBudget.RemainingSolveAttempts);

			const int32 Salt = MakeFloorWfcSalt(
				Input.WholeLayoutAttemptIndex,
				Input.FloorIndex,
				LocalAttempt);
			FRandomStream Random = FGenerationCore::MakeRandomStream(
				Input.Signature.Seed,
				Input.Signature.AlgorithmVersion,
				ERandomDomain::WfcLayout,
				Salt);

			FZeroEscapeConstrainedFloorResult AcceptedCandidate;
			FString FatalCandidateError;
			auto ValidateCandidate =
				[&](const TConstArrayView<uint8> OpeningMasks)
					-> FWfcCollapsedCandidateEvaluation
				{
					FString CandidateError;
					FZeroEscapeConstrainedFloorResult Candidate;
					if (!BuildRouteMetrics(Input, OpeningMasks, Candidate, CandidateError))
					{
						FatalCandidateError = MoveTemp(CandidateError);
						return FWfcCollapsedCandidateEvaluation::Fatal(
							FatalCandidateError);
					}

					if (Candidate.RouteCoverageRatio + UE_DOUBLE_SMALL_NUMBER
						< Input.MinRouteCoverageRatio)
					{
						return FWfcCollapsedCandidateEvaluation::Reject(
							TEXT("本层进入点到离开点没有覆盖配置要求的路线深度。"),
							FMath::RoundToInt(Candidate.RouteCoverageRatio * 1000000.0),
							FMath::RoundToInt(Input.MinRouteCoverageRatio * 1000000.0));
					}

					AcceptedCandidate = MoveTemp(Candidate);
					return FWfcCollapsedCandidateEvaluation::Accept();
				};

			TArray<uint8> DenseOutput;
			FZeroEscapeGenerationReport AttemptReport;
			const bool bSolved = FWfcSolver::Solve(
				Input.GridSize,
				Input.Constraints,
				Settings,
				Variants,
				Random,
				ValidateCandidate,
				DenseOutput,
				AttemptReport);

			const int32 UsedSolveAttempts =
				FMath::Max(1, AttemptReport.Metrics.WfcSolveAttemptCount);
			const int32 UsedCandidates = AttemptReport.Metrics.WfcCandidateAttemptCount;
			const int32 UsedBacktracks = AttemptReport.Metrics.WfcBacktrackCount;
			InOutBudget.RemainingSolveAttempts = FMath::Max(
				0, InOutBudget.RemainingSolveAttempts - UsedSolveAttempts);
			InOutBudget.RemainingCandidateAttempts = FMath::Max(
				0, InOutBudget.RemainingCandidateAttempts - UsedCandidates);
			InOutBudget.RemainingBacktracks = FMath::Max(
				0, InOutBudget.RemainingBacktracks - UsedBacktracks);
			InOutBudget.ConsumedSolveAttempts += UsedSolveAttempts;
			InOutBudget.ConsumedCandidateAttempts += UsedCandidates;
			InOutBudget.ConsumedBacktracks += UsedBacktracks;
			AccumulateWfcMetrics(AggregateMetrics, AttemptReport.Metrics);

			if (bSolved)
			{
				if (DenseOutput != AcceptedCandidate.OpeningMaskByCell)
				{
					OutReport = MoveTemp(AttemptReport);
					OutReport.Metrics = AggregateMetrics;
					return FailFloor(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("WFC 提交结果与已验收候选不一致。"));
				}

				OutResult = MoveTemp(AcceptedCandidate);
				OutReport = MoveTemp(AttemptReport);
				OutReport.Stage = EZeroEscapeGenerationStage::None;
				OutReport.Failure = EZeroEscapeGenerationFailure::None;
				OutReport.RelatedStableId = INDEX_NONE;
				OutReport.ActualValue = 0;
				OutReport.LimitValue = 0;
				OutReport.Message.Reset();
				OutReport.Metrics = AggregateMetrics;
				return true;
			}

			OutReport = MoveTemp(AttemptReport);
			OutReport.Metrics = AggregateMetrics;
			if (!FatalCandidateError.IsEmpty()
				|| OutReport.Failure != EZeroEscapeGenerationFailure::SolverBudgetExhausted)
			{
				return false;
			}
		}

		OutReport.Metrics = AggregateMetrics;
		return FailFloor(
			OutReport,
			EZeroEscapeGenerationStage::WfcLayout,
			EZeroEscapeGenerationFailure::NoValidWfcSolution,
			TEXT("本层已用完允许的确定性 WFC 搜索树。"),
			AggregateMetrics.WfcSolveAttemptCount,
			Input.MaxSolveAttemptsForThisFloor);
	}
}
