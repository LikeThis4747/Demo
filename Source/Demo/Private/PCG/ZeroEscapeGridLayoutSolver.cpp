// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGridLayoutSolver.cpp
 * 职责：把完整结构已经冻结的单层稠密约束交给现有二维 FWfcSolver，并在完整叶子上验收路线覆盖。
 * 边界：不重新决定楼梯、房间或端口；所有完整布局重试共享调用方持有的预算账本。
 */

#include "PCG/ZeroEscapeGridLayoutSolver.h"

#include "Algo/Reverse.h"
#include "Containers/ArrayView.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
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

		int32 MakeFloorFallbackSalt(
			const int32 WholeLayoutAttemptIndex,
			const int32 FloorIndex)
		{
			uint32 Mixed = 0xF411BA6Bu;
			Mixed ^= static_cast<uint32>(WholeLayoutAttemptIndex) * 0x9E3779B9u;
			Mixed ^= static_cast<uint32>(FloorIndex) * 0x85EBCA6Bu;
			Mixed ^= Mixed >> 16;
			Mixed *= 0x7FEB352Du;
			return static_cast<int32>(Mixed ^ (Mixed >> 15));
		}

		uint32 MakePreferenceTie(
			const FZeroEscapeConstrainedFloorInput& Input,
			const int32 First,
			const int32 Second)
		{
			uint32 Mixed = static_cast<uint32>(Input.Signature.Seed) ^ 0x51A7E23Du;
			Mixed ^= static_cast<uint32>(Input.FloorIndex) * 0x9E3779B9u;
			Mixed ^= static_cast<uint32>(First) * 0x85EBCA6Bu;
			Mixed ^= static_cast<uint32>(Second) * 0xC2B2AE35u;
			Mixed ^= Mixed >> 16;
			Mixed *= 0x7FEB352Du;
			return Mixed ^ (Mixed >> 15);
		}

		bool IsAllowedPreferenceEdge(
			const FZeroEscapeConstrainedFloorInput& Input,
			const int32 FromIndex,
			const uint8 Direction,
			int32& OutNeighborIndex)
		{
			const FGridCellConstraint& From = Input.Constraints[FromIndex];
			const FIntPoint Neighbor = Grid::Step(From.Coordinate, Direction);
			if (From.Domain == EGridCellDomain::Outside
				|| !Grid::IsInside(Neighbor, Input.GridSize))
			{
				return false;
			}
			OutNeighborIndex = Grid::ToIndex(Neighbor, Input.GridSize);
			const FGridCellConstraint& To = Input.Constraints[OutNeighborIndex];
			return To.Domain != EGridCellDomain::Outside
				&& (From.RequiredClosedMask & Grid::DirectionBit(Direction)) == 0
				&& (To.RequiredClosedMask & Grid::DirectionBit(
					Grid::OppositeDirectionIndex(Direction))) == 0;
		}

		int32 CalculatePreferredPathStepCost(
			const int32 PreferredStraightTiles,
			const int32 MaxTrackedStraightRun,
			const uint8 IncomingDirection,
			const uint8 StraightRunLength,
			const uint8 NextDirection,
			uint8& OutStraightRunLength)
		{
			int32 Cost = 100;
			OutStraightRunLength = 1;
			if (IncomingDirection >= Grid::DirectionCount)
			{
				return Cost;
			}
			if (IncomingDirection == NextDirection)
			{
				OutStraightRunLength = static_cast<uint8>(FMath::Min(
					MaxTrackedStraightRun, static_cast<int32>(StraightRunLength) + 1));
				const int32 Excess = FMath::Max(
					0, static_cast<int32>(OutStraightRunLength) - PreferredStraightTiles);
				return Cost + 80 * Excess * Excess;
			}
			if (StraightRunLength == 1)
			{
				Cost += 150;
			}
			else if (StraightRunLength >= 2 && StraightRunLength <= 4)
			{
				Cost -= 15;
			}
			return Cost;
		}

		struct FPreferredPathHeapEntry
		{
			int64 Cost = MAX_int64;
			uint32 Tie = MAX_uint32;
			int32 StateIndex = INDEX_NONE;
		};

		struct FPreferredPathHeapPredicate
		{
			bool operator()(
				const FPreferredPathHeapEntry& A,
				const FPreferredPathHeapEntry& B) const
			{
				return A.Cost != B.Cost ? A.Cost > B.Cost : A.Tie > B.Tie;
			}
		};

		bool FindDirectionAwarePreferredPath(
			const FZeroEscapeConstrainedFloorInput& Input,
			TArray<int32>& OutPath)
		{
			OutPath.Reset();
			constexpr uint8 NoDirection = Grid::DirectionCount;
			const int32 MaxRun = FMath::Clamp(
				Input.PreferredMaxConsecutiveStraightTiles + 8,
				1,
				FMath::Max(Input.GridSize.X, Input.GridSize.Y));
			const int32 RunStride = MaxRun + 1;
			const int32 CellStride = 5 * RunStride;
			const int32 StateCount = Input.Constraints.Num() * CellStride;
			const auto Encode = [RunStride, CellStride](
				const int32 Cell, const uint8 Direction, const uint8 Run)
			{
				return Cell * CellStride + Direction * RunStride + Run;
			};
			const int32 StartCell = Grid::ToIndex(
				Input.RequiredEnterCoordinate, Input.GridSize);
			const int32 GoalCell = Grid::ToIndex(
				Input.RequiredLeaveCoordinate, Input.GridSize);
			const int32 StartState = Encode(StartCell, NoDirection, 0);
			TArray<int64> BestCost;
			BestCost.Init(MAX_int64, StateCount);
			TArray<uint32> BestTie;
			BestTie.Init(MAX_uint32, StateCount);
			TArray<int32> Parent;
			Parent.Init(INDEX_NONE, StateCount);
			TArray<FPreferredPathHeapEntry> Heap;
			BestCost[StartState] = 0;
			BestTie[StartState] = 0;
			Heap.HeapPush({0, 0, StartState}, FPreferredPathHeapPredicate());
			int32 GoalState = INDEX_NONE;
			while (!Heap.IsEmpty())
			{
				FPreferredPathHeapEntry Current;
				Heap.HeapPop(Current, FPreferredPathHeapPredicate(), EAllowShrinking::No);
				if (Current.Cost != BestCost[Current.StateIndex]
					|| Current.Tie != BestTie[Current.StateIndex])
				{
					continue;
				}
				const int32 Cell = Current.StateIndex / CellStride;
				const int32 LocalState = Current.StateIndex % CellStride;
				const uint8 Incoming = static_cast<uint8>(LocalState / RunStride);
				const uint8 Run = static_cast<uint8>(LocalState % RunStride);
				if (Cell == GoalCell)
				{
					GoalState = Current.StateIndex;
					break;
				}
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					int32 Neighbor = INDEX_NONE;
					if (!IsAllowedPreferenceEdge(Input, Cell, Direction, Neighbor))
					{
						continue;
					}
					uint8 NextRun = 1;
					const int64 NextCost = Current.Cost + CalculatePreferredPathStepCost(
						FMath::Max(1, Input.PreferredMaxConsecutiveStraightTiles),
						MaxRun, Incoming, Run, Direction, NextRun);
					const int32 NextState = Encode(Neighbor, Direction, NextRun);
					const uint32 Tie = MakePreferenceTie(Input, Current.StateIndex, NextState);
					if (NextCost < BestCost[NextState]
						|| (NextCost == BestCost[NextState] && Tie < BestTie[NextState]))
					{
						BestCost[NextState] = NextCost;
						BestTie[NextState] = Tie;
						Parent[NextState] = Current.StateIndex;
						Heap.HeapPush({NextCost, Tie, NextState}, FPreferredPathHeapPredicate());
					}
				}
			}
			for (int32 State = GoalState; State != INDEX_NONE; State = Parent[State])
			{
				OutPath.Add(State / CellStride);
			}
			Algo::Reverse(OutPath);
			TArray<int32> PositionByCell;
			PositionByCell.Init(INDEX_NONE, Input.Constraints.Num());
			TArray<int32> SimplePath;
			for (const int32 Cell : OutPath)
			{
				const int32 ExistingPosition = PositionByCell[Cell];
				if (ExistingPosition != INDEX_NONE)
				{
					for (int32 Index = SimplePath.Num() - 1;
						Index > ExistingPosition;
						--Index)
					{
						PositionByCell[SimplePath[Index]] = INDEX_NONE;
					}
					SimplePath.SetNum(ExistingPosition + 1, EAllowShrinking::No);
					continue;
				}
				PositionByCell[Cell] = SimplePath.Add(Cell);
			}
			OutPath = MoveTemp(SimplePath);
			return GoalState != INDEX_NONE && OutPath.Num() >= 2;
		}

		bool FindPreferredBranchPath(
			const FZeroEscapeConstrainedFloorInput& Input,
			const int32 GatewayIndex,
			const TArray<uint8>& MainPathCells,
			const TArray<uint8>& ReservedBranchCells,
			TArray<int32>& OutPath)
		{
			struct FStepOption
			{
				int32 Cell = INDEX_NONE;
				uint8 Direction = Grid::DirectionCount;
				uint8 Run = 1;
				int32 Weight = 1;
			};
			FRandomStream Random = FGenerationCore::MakeRandomStream(
				Input.Signature.Seed,
				ERandomDomain::WfcLayout,
				static_cast<int32>(MakePreferenceTie(Input, GatewayIndex, 0xB12A7C1)));
			const int32 LengthRange = Input.MaximumPreferredRewardBranchLengthTiles
				- Input.MinimumRewardBranchLengthTiles + 1;
			const int32 MaxRun = FMath::Clamp(
				Input.PreferredMaxConsecutiveStraightTiles + 8,
				1,
				FMath::Max(Input.GridSize.X, Input.GridSize.Y));
			for (int32 Attempt = 0; Attempt < 24; ++Attempt)
			{
				const int32 TargetLength = Input.MinimumRewardBranchLengthTiles
					+ Random.RandHelper(LengthRange);
				TArray<int32> Path = {GatewayIndex};
				uint8 Incoming = Grid::DirectionCount;
				uint8 Run = 0;
				bool bHasTurn = false;
				while (Path.Num() <= TargetLength)
				{
					TArray<FStepOption, TInlineAllocator<4>> Options;
					int32 TotalWeight = 0;
					for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
					{
						int32 Neighbor = INDEX_NONE;
						uint8 NextRun = 1;
						if (!IsAllowedPreferenceEdge(Input, Path.Last(), Direction, Neighbor)
							|| Input.Constraints[Neighbor].Domain != EGridCellDomain::Optional
							|| MainPathCells[Neighbor] != 0
							|| ReservedBranchCells[Neighbor] != 0
							|| Path.Contains(Neighbor))
						{
							continue;
						}
						const int32 StepCost = CalculatePreferredPathStepCost(
							FMath::Max(1, Input.PreferredMaxConsecutiveStraightTiles),
							MaxRun, Incoming, Run, Direction, NextRun);
						int32 Weight = FMath::Clamp(500 - StepCost, 1, 500);
						if (!bHasTurn && Path.Num() == TargetLength
							&& Incoming == Direction)
						{
							Weight = 1;
						}
						Options.Add({Neighbor, Direction, NextRun, Weight});
						TotalWeight += Weight;
					}
					if (Options.IsEmpty())
					{
						break;
					}
					int32 Roll = Random.RandHelper(TotalWeight);
					const FStepOption* Chosen = &Options.Last();
					for (const FStepOption& Option : Options)
					{
						Roll -= Option.Weight;
						if (Roll < 0)
						{
							Chosen = &Option;
							break;
						}
					}
					bHasTurn |= Incoming < Grid::DirectionCount
						&& Incoming != Chosen->Direction;
					Incoming = Chosen->Direction;
					Run = Chosen->Run;
					Path.Add(Chosen->Cell);
				}
				if (Path.Num() == TargetLength + 1 && bHasTurn)
				{
					OutPath = MoveTemp(Path);
					return true;
				}
			}
			OutPath.Reset();
			return false;
		}

		void AppendPreferredPath(
			const FZeroEscapeConstrainedFloorInput& Input,
			const TConstArrayView<int32> Path,
			const bool bPreferDeadEnd,
			TArray<FWfcCellOpeningPreference>& InOutPreferences)
		{
			for (int32 PathIndex = 0; PathIndex + 1 < Path.Num(); ++PathIndex)
			{
				const int32 First = Path[PathIndex];
				const int32 Second = Path[PathIndex + 1];
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if (Grid::Step(Input.Constraints[First].Coordinate, Direction)
						!= Input.Constraints[Second].Coordinate)
					{
						continue;
					}
					InOutPreferences[First].PreferredOpenMask |=
						Grid::DirectionBit(Direction);
					InOutPreferences[Second].PreferredOpenMask |= Grid::DirectionBit(
						Grid::OppositeDirectionIndex(Direction));
					break;
				}
			}
			if (bPreferDeadEnd)
			{
				for (int32 PathIndex = 1; PathIndex < Path.Num(); ++PathIndex)
				{
					FWfcCellOpeningPreference& Preference = InOutPreferences[Path[PathIndex]];
					Preference.PreferredClosedMask |= static_cast<uint8>(
						Grid::AllOpenEdges & ~Preference.PreferredOpenMask);
				}
			}
		}

		bool BuildRouteOpeningPreferences(
			const FZeroEscapeConstrainedFloorInput& Input,
			TArray<FWfcCellOpeningPreference>& OutPreferences)
		{
			OutPreferences.Reset();
			if (Input.RouteOpeningPreferenceLog2Strength <= 0.0f)
			{
				return true;
			}
			TArray<int32> MainPath;
			if (!FindDirectionAwarePreferredPath(Input, MainPath))
			{
				return false;
			}
			OutPreferences.Init(FWfcCellOpeningPreference(), Input.Constraints.Num());
			AppendPreferredPath(Input, MainPath, false, OutPreferences);
			TArray<uint8> MainPathCells;
			MainPathCells.Init(0, Input.Constraints.Num());
			for (const int32 Cell : MainPath)
			{
				MainPathCells[Cell] = 1;
			}
			TArray<uint8> ReservedBranchCells;
			ReservedBranchCells.Init(0, Input.Constraints.Num());
			TArray<int32> ChosenGatewayPositions;
			const int32 DesiredBranches = FMath::Min(
				Input.PreferredRewardBranchCount, FMath::Max(0, (MainPath.Num() - 2) / 2));
			for (int32 BranchIndex = 0; BranchIndex < DesiredBranches; ++BranchIndex)
			{
				const int32 IdealPosition = FMath::Clamp(
					(BranchIndex + 1) * (MainPath.Num() - 1) / (DesiredBranches + 1),
					1, MainPath.Num() - 2);
				TArray<int32> Positions;
				for (int32 Position = 1; Position + 1 < MainPath.Num(); ++Position)
				{
					Positions.Add(Position);
				}
				Positions.Sort([&](const int32 A, const int32 B)
				{
					const int32 DeltaA = FMath::Abs(A - IdealPosition);
					const int32 DeltaB = FMath::Abs(B - IdealPosition);
					return DeltaA != DeltaB ? DeltaA < DeltaB
						: MakePreferenceTie(Input, MainPath[A], BranchIndex)
							< MakePreferenceTie(Input, MainPath[B], BranchIndex);
				});
				for (const int32 Position : Positions)
				{
					const bool bTooClose = ChosenGatewayPositions.ContainsByPredicate(
						[Position](const int32 Existing)
						{
							return FMath::Abs(Existing - Position) < 2;
						});
					TArray<int32> BranchPath;
					if (bTooClose || !FindPreferredBranchPath(
							Input, MainPath[Position], MainPathCells,
							ReservedBranchCells, BranchPath))
					{
						continue;
					}
					AppendPreferredPath(Input, BranchPath, true, OutPreferences);
					ChosenGatewayPositions.Add(Position);
					for (int32 PathIndex = 1; PathIndex < BranchPath.Num(); ++PathIndex)
					{
						ReservedBranchCells[BranchPath[PathIndex]] = 1;
					}
					break;
				}
			}

			for (int32 CellIndex = 0; CellIndex < OutPreferences.Num(); ++CellIndex)
			{
				FWfcCellOpeningPreference& Preference = OutPreferences[CellIndex];
				const FGridCellConstraint& Constraint = Input.Constraints[CellIndex];
				if (Constraint.Domain == EGridCellDomain::Outside)
				{
					Preference = {};
					continue;
				}
				Preference.PreferredOpenMask &=
					static_cast<uint8>(~Constraint.RequiredClosedMask);
				Preference.PreferredClosedMask &=
					static_cast<uint8>(~Constraint.RequiredOpenMask);
				Preference.PreferredClosedMask &=
					static_cast<uint8>(~Preference.PreferredOpenMask);
			}
			return true;
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
			if (Input.WholeLayoutAttemptIndex < 0
				|| Input.FloorIndex < 0
				|| Input.GridSize.X < GenerationLimits::MinGridAxis
				|| Input.GridSize.Y < GenerationLimits::MinGridAxis
				|| Input.GridSize.X > GenerationLimits::MaxGridAxis
				|| Input.GridSize.Y > GenerationLimits::MaxGridAxis
				|| CellCount64 <= 0
				|| CellCount64 > GenerationLimits::MaxGridCells
				|| Input.Constraints.Num() != CellCount64
				|| Input.StructureWalkableByCell.Num() != CellCount64
				|| Input.HighCeilingWalkableByCell.Num() != CellCount64
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
				|| Input.PreferredTotalWalkableCellCount <= 0
				|| Input.PreferredOrdinaryWalkableCellCount <= 0
				|| Input.PreferredMaxConsecutiveStraightTiles < 0
				|| !FMath::IsFinite(Input.PreferredRouteCoverageRatio)
				|| Input.PreferredRouteCoverageRatio < 0.0
				|| Input.PreferredRouteCoverageRatio > 1.0
				|| !FMath::IsFinite(Input.PreferredRewardBranchCellRatio)
				|| Input.PreferredRewardBranchCellRatio < 0.0
				|| Input.PreferredRewardBranchCellRatio > 0.5
				|| !FMath::IsFinite(
					Input.PreferredAlternativeRouteCoverageRatio)
				|| Input.PreferredAlternativeRouteCoverageRatio < 0.0
				|| Input.PreferredAlternativeRouteCoverageRatio > 1.0
				|| !FMath::IsFinite(Input.RouteQualityEarlyAcceptThreshold)
				|| Input.RouteQualityEarlyAcceptThreshold < 0.0f
				|| Input.RouteQualityEarlyAcceptThreshold > 1.0f
				|| !FMath::IsFinite(Input.RouteOpeningPreferenceLog2Strength)
				|| Input.RouteOpeningPreferenceLog2Strength < 0.0f
				|| Input.RouteOpeningPreferenceLog2Strength > 4.0f
				|| Input.MinimumRewardBranchLengthTiles < 3
				|| Input.MaximumPreferredRewardBranchLengthTiles
					< Input.MinimumRewardBranchLengthTiles
				|| Input.MaximumPreferredRewardBranchLengthTiles > 12
				|| Input.PreferredRewardBranchCount < 0
				|| Input.PreferredRewardBranchCount > 12
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
					|| Input.HighCeilingWalkableByCell[DenseIndex] > 1
					|| (Input.HighCeilingWalkableByCell[DenseIndex] != 0
						&& Input.StructureWalkableByCell[DenseIndex] == 0)
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

		struct FFloorSoftQualityScore
		{
			int32 OrdinaryDeficit = 0;
			int32 QualityPenaltyMicros = 1000000;
			int32 TotalDeviation = 0;
			int32 RewardBranchDeviation = 0;
			int32 AlternativeCoverageDeficitMicros = 0;
			int32 OneCellTerminalSpurCount = 0;
			int32 LongStraightExcessSquared = 0;

			bool IsBetterThan(const FFloorSoftQualityScore& Other) const
			{
				if (OrdinaryDeficit != Other.OrdinaryDeficit)
				{
					return OrdinaryDeficit < Other.OrdinaryDeficit;
				}
				if (QualityPenaltyMicros != Other.QualityPenaltyMicros)
				{
					return QualityPenaltyMicros < Other.QualityPenaltyMicros;
				}
				if (TotalDeviation != Other.TotalDeviation)
				{
					return TotalDeviation < Other.TotalDeviation;
				}
				if (RewardBranchDeviation != Other.RewardBranchDeviation)
				{
					return RewardBranchDeviation < Other.RewardBranchDeviation;
				}
				if (AlternativeCoverageDeficitMicros
					!= Other.AlternativeCoverageDeficitMicros)
				{
					return AlternativeCoverageDeficitMicros
						< Other.AlternativeCoverageDeficitMicros;
				}
				if (OneCellTerminalSpurCount != Other.OneCellTerminalSpurCount)
				{
					return OneCellTerminalSpurCount
						< Other.OneCellTerminalSpurCount;
				}
				return LongStraightExcessSquared
					< Other.LongStraightExcessSquared;
			}
		};

		int32 MeasureLongestStraightRun(
			const FIntPoint GridSize,
			const TConstArrayView<uint8> OpeningMasks)
		{
			int32 Longest = 0;
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
			{
				int32 Run = 0;
				for (int32 X = 0; X + 1 < GridSize.X; ++X)
				{
					const uint8 Mask = OpeningMasks[
						Grid::ToIndex(FIntPoint(X, Y), GridSize)];
					Run = (Mask & Grid::DirectionBit(1)) != 0 ? Run + 1 : 0;
					Longest = FMath::Max(Longest, Run);
				}
			}
			for (int32 X = 0; X < GridSize.X; ++X)
			{
				int32 Run = 0;
				for (int32 Y = 0; Y + 1 < GridSize.Y; ++Y)
				{
					const uint8 Mask = OpeningMasks[
						Grid::ToIndex(FIntPoint(X, Y), GridSize)];
					Run = (Mask & Grid::DirectionBit(0)) != 0 ? Run + 1 : 0;
					Longest = FMath::Max(Longest, Run);
				}
			}
			return Longest;
		}

		bool CoordinateLess(const FIntVector A, const FIntVector B)
		{
			return A.Z != B.Z ? A.Z < B.Z
				: A.Y != B.Y ? A.Y < B.Y
				: A.X < B.X;
		}

		bool RewardBranchLess(
			const FZeroEscapeGeneratedRewardBranch& A,
			const FZeroEscapeGeneratedRewardBranch& B)
		{
			if (A.EndpointCoordinate != B.EndpointCoordinate)
			{
				return CoordinateLess(A.EndpointCoordinate, B.EndpointCoordinate);
			}
			if (A.GatewayCoordinate != B.GatewayCoordinate)
			{
				return CoordinateLess(A.GatewayCoordinate, B.GatewayCoordinate);
			}
			const int32 CommonCount = FMath::Min(
				A.PathCoordinates.Num(), B.PathCoordinates.Num());
			for (int32 Index = 0; Index < CommonCount; ++Index)
			{
				if (A.PathCoordinates[Index] != B.PathCoordinates[Index])
				{
					return CoordinateLess(
						A.PathCoordinates[Index], B.PathCoordinates[Index]);
				}
			}
			return A.PathCoordinates.Num() < B.PathCoordinates.Num();
		}

		bool BuildCollapsedNeighbors(
			const FIntPoint GridSize,
			const TConstArrayView<uint8> OpeningMasks,
			TArray<TArray<int32>>& OutNeighbors)
		{
			if (OpeningMasks.Num() != GridSize.X * GridSize.Y)
			{
				return false;
			}
			OutNeighbors.SetNum(OpeningMasks.Num());
			for (int32 Cell = 0; Cell < OpeningMasks.Num(); ++Cell)
			{
				const FIntPoint Coordinate(Cell % GridSize.X, Cell / GridSize.X);
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if ((OpeningMasks[Cell] & Grid::DirectionBit(Direction)) == 0)
					{
						continue;
					}
					const FIntPoint NeighborCoordinate = Grid::Step(Coordinate, Direction);
					if (!Grid::IsInside(NeighborCoordinate, GridSize))
					{
						return false;
					}
					const int32 Neighbor = Grid::ToIndex(NeighborCoordinate, GridSize);
					if ((OpeningMasks[Neighbor] & Grid::DirectionBit(
							Grid::OppositeDirectionIndex(Direction))) == 0)
					{
						return false;
					}
					OutNeighbors[Cell].Add(Neighbor);
				}
			}
			return true;
		}

		bool BuildStableShortestPath(
			const TArray<TArray<int32>>& Neighbors,
			const int32 Start,
			const int32 Goal,
			TArray<int32>& OutPath)
		{
			OutPath.Reset();
			if (!Neighbors.IsValidIndex(Start) || !Neighbors.IsValidIndex(Goal))
			{
				return false;
			}
			TArray<int32> Parent;
			Parent.Init(INDEX_NONE, Neighbors.Num());
			TQueue<int32> Queue;
			Parent[Start] = Start;
			Queue.Enqueue(Start);
			int32 Current = INDEX_NONE;
			while (Queue.Dequeue(Current) && Parent[Goal] == INDEX_NONE)
			{
				for (const int32 Neighbor : Neighbors[Current])
				{
					if (Parent[Neighbor] != INDEX_NONE)
					{
						continue;
					}
					Parent[Neighbor] = Current;
					Queue.Enqueue(Neighbor);
				}
			}
			if (Parent[Goal] == INDEX_NONE)
			{
				return false;
			}
			for (int32 Cell = Goal; Cell != Start; Cell = Parent[Cell])
			{
				OutPath.Add(Cell);
			}
			OutPath.Add(Start);
			Algo::Reverse(OutPath);
			return OutPath.Num() >= 2;
		}

		uint8 DirectionBetween(
			const int32 First,
			const int32 Second,
			const FIntPoint GridSize)
		{
			const FIntPoint Coordinate(First % GridSize.X, First / GridSize.X);
			const FIntPoint Neighbor(Second % GridSize.X, Second / GridSize.X);
			for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
			{
				if (Grid::Step(Coordinate, Direction) == Neighbor)
				{
					return Direction;
				}
			}
			return Grid::DirectionCount;
		}

		bool CanReachWithoutEdge(
			const TArray<TArray<int32>>& Neighbors,
			const int32 Start,
			const int32 Goal,
			const int32 RemovedFirst,
			const int32 RemovedSecond)
		{
			TArray<uint8> Visited;
			Visited.Init(0, Neighbors.Num());
			TQueue<int32> Queue;
			Visited[Start] = 1;
			Queue.Enqueue(Start);
			int32 Current = INDEX_NONE;
			while (Queue.Dequeue(Current))
			{
				if (Current == Goal)
				{
					return true;
				}
				for (const int32 Neighbor : Neighbors[Current])
				{
					const bool bRemoved =
						(Current == RemovedFirst && Neighbor == RemovedSecond)
						|| (Current == RemovedSecond && Neighbor == RemovedFirst);
					if (!bRemoved && Visited[Neighbor] == 0)
					{
						Visited[Neighbor] = 1;
						Queue.Enqueue(Neighbor);
					}
				}
			}
			return false;
		}

		bool AnalyzeCollapsedRouteStructure(
			const FZeroEscapeConstrainedFloorInput& Input,
			const TConstArrayView<uint8> OpeningMasks,
			FZeroEscapeConstrainedFloorResult& InOutResult)
		{
			InOutResult.CandidateRewardBranches.Reset();
			InOutResult.OneCellTerminalSpurCount = 0;
			InOutResult.CandidateRewardBranchCellRatio = 0.0;
			InOutResult.AlternativeRouteCoverageRatio = 0.0;
			InOutResult.HighCeilingWalkableCellCount = 0;
			InOutResult.HighCeilingMainRouteCoverageRatio = 0.0;
			InOutResult.StableMainRouteEdgeCount = 0;
			InOutResult.ReadableTurnCount = 0;
			InOutResult.LongestStraightRunTiles = 0;
			InOutResult.OneTileRunRatio = 1.0;
			InOutResult.bSoftRouteAnalysisSucceeded = false;

			TArray<TArray<int32>> Neighbors;
			const int32 Enter = Grid::ToIndex(
				Input.RequiredEnterCoordinate, Input.GridSize);
			const int32 Leave = Grid::ToIndex(
				Input.RequiredLeaveCoordinate, Input.GridSize);
			TArray<int32> MainPath;
			if (!BuildCollapsedNeighbors(Input.GridSize, OpeningMasks, Neighbors)
				|| !BuildStableShortestPath(Neighbors, Enter, Leave, MainPath))
			{
				return false;
			}

			InOutResult.StableMainRouteEdgeCount = MainPath.Num() - 1;
			for (const uint8 HighCeilingMarker : Input.HighCeilingWalkableByCell)
			{
				InOutResult.HighCeilingWalkableCellCount +=
					HighCeilingMarker != 0 ? 1 : 0;
			}
			int32 HighCeilingMainRouteCellCount = 0;
			for (const int32 Cell : MainPath)
			{
				HighCeilingMainRouteCellCount +=
					Input.HighCeilingWalkableByCell.IsValidIndex(Cell)
					&& Input.HighCeilingWalkableByCell[Cell] != 0 ? 1 : 0;
			}
			InOutResult.HighCeilingMainRouteCoverageRatio =
				InOutResult.HighCeilingWalkableCellCount > 0
					? static_cast<double>(HighCeilingMainRouteCellCount)
						/ InOutResult.HighCeilingWalkableCellCount
					: 0.0;
			InOutResult.LongestStraightRunTiles = MeasureLongestStraightRun(
				Input.GridSize, OpeningMasks);
			TArray<int32> RunLengths;
			uint8 PreviousDirection = Grid::DirectionCount;
			for (int32 PathIndex = 0; PathIndex + 1 < MainPath.Num(); ++PathIndex)
			{
				const uint8 Direction = DirectionBetween(
					MainPath[PathIndex], MainPath[PathIndex + 1], Input.GridSize);
				if (Direction >= Grid::DirectionCount)
				{
					return false;
				}
				if (Direction != PreviousDirection)
				{
					RunLengths.Add(1);
					PreviousDirection = Direction;
				}
				else
				{
					++RunLengths.Last();
				}
			}
			int32 OneTileRuns = 0;
			for (int32 RunIndex = 0; RunIndex < RunLengths.Num(); ++RunIndex)
			{
				OneTileRuns += RunLengths[RunIndex] == 1 ? 1 : 0;
				if (RunIndex > 0
					&& RunLengths[RunIndex - 1] >= 2
					&& RunLengths[RunIndex - 1] <= 4
					&& RunLengths[RunIndex] >= 2)
				{
					++InOutResult.ReadableTurnCount;
				}
			}
			InOutResult.OneTileRunRatio = RunLengths.IsEmpty()
				? 1.0 : static_cast<double>(OneTileRuns) / RunLengths.Num();

			int32 CandidateBranchCells = 0;
			for (int32 Endpoint = 0; Endpoint < Neighbors.Num(); ++Endpoint)
			{
				if (Endpoint == Enter || Endpoint == Leave
					|| Neighbors[Endpoint].Num() != 1)
				{
					continue;
				}
				TArray<int32> ReversePath = {Endpoint};
				int32 Previous = Endpoint;
				int32 Current = Neighbors[Endpoint][0];
				int32 Gateway = INDEX_NONE;
				for (int32 StepCount = 0; StepCount < Neighbors.Num(); ++StepCount)
				{
					if (Neighbors[Current].Num() >= 3)
					{
						Gateway = Current;
						break;
					}
					if (Neighbors[Current].Num() != 2
						|| Current == Enter || Current == Leave)
					{
						break;
					}
					ReversePath.Add(Current);
					const int32 Next = Neighbors[Current][0] == Previous
						? Neighbors[Current][1] : Neighbors[Current][0];
					Previous = Current;
					Current = Next;
				}
				if (Gateway == INDEX_NONE)
				{
					continue;
				}
				if (ReversePath.Num() == 1)
				{
					++InOutResult.OneCellTerminalSpurCount;
				}
				if (ReversePath.Num() < Input.MinimumRewardBranchLengthTiles)
				{
					continue;
				}

				bool bAllOrdinary = true;
				for (const int32 Cell : ReversePath)
				{
					bAllOrdinary &= Input.StructureWalkableByCell.IsValidIndex(Cell)
						&& Input.StructureWalkableByCell[Cell] == 0
						&& Cell != Enter && Cell != Leave;
				}
				if (!bAllOrdinary)
				{
					continue;
				}

				TArray<int32> ForwardPath = ReversePath;
				Algo::Reverse(ForwardPath);
				bool bHasTurn = false;
				uint8 IncomingDirection = DirectionBetween(
					Gateway, ForwardPath[0], Input.GridSize);
				for (int32 PathIndex = 0; PathIndex + 1 < ForwardPath.Num(); ++PathIndex)
				{
					const uint8 Direction = DirectionBetween(
						ForwardPath[PathIndex], ForwardPath[PathIndex + 1], Input.GridSize);
					bHasTurn |= Direction != IncomingDirection;
					IncomingDirection = Direction;
				}
				if (!bHasTurn)
				{
					continue;
				}

				FZeroEscapeGeneratedRewardBranch& Branch =
					InOutResult.CandidateRewardBranches.AddDefaulted_GetRef();
				Branch.GatewayCoordinate = FIntVector(
					Gateway % Input.GridSize.X,
					Gateway / Input.GridSize.X,
					Input.FloorIndex);
				for (const int32 Cell : ForwardPath)
				{
					Branch.PathCoordinates.Add(FIntVector(
						Cell % Input.GridSize.X,
						Cell / Input.GridSize.X,
						Input.FloorIndex));
				}
				Branch.EndpointCoordinate = Branch.PathCoordinates.Last();
				CandidateBranchCells += Branch.PathCoordinates.Num();
			}
			InOutResult.CandidateRewardBranches.Sort(RewardBranchLess);
			InOutResult.CandidateRewardBranchCellRatio =
				InOutResult.OrdinaryWalkableCellCount > 0
					? static_cast<double>(CandidateBranchCells)
						/ InOutResult.OrdinaryWalkableCellCount
					: 0.0;

			int32 BypassableEdges = 0;
			for (int32 PathIndex = 0; PathIndex + 1 < MainPath.Num(); ++PathIndex)
			{
				BypassableEdges += CanReachWithoutEdge(
					Neighbors, Enter, Leave,
					MainPath[PathIndex], MainPath[PathIndex + 1]) ? 1 : 0;
			}
			InOutResult.AlternativeRouteCoverageRatio =
				InOutResult.StableMainRouteEdgeCount > 0
					? static_cast<double>(BypassableEdges)
						/ InOutResult.StableMainRouteEdgeCount
					: 0.0;
			InOutResult.bSoftRouteAnalysisSucceeded = true;
			return true;
		}

		FFloorSoftQualityScore ScoreCandidate(
			const FZeroEscapeConstrainedFloorInput& Input,
			const FZeroEscapeConstrainedFloorResult& Candidate)
		{
			FFloorSoftQualityScore Score;
			Score.OrdinaryDeficit = FMath::Max(
				0,
				Input.PreferredOrdinaryWalkableCellCount
					- Candidate.OrdinaryWalkableCellCount);
			Score.TotalDeviation = FMath::Abs(
				Candidate.TotalWalkableCellCount
					- Input.PreferredTotalWalkableCellCount);
			const int32 Excess = Input.PreferredMaxConsecutiveStraightTiles > 0
				? FMath::Max(
					0, Candidate.LongestStraightRunTiles
						- Input.PreferredMaxConsecutiveStraightTiles)
				: 0;
			Score.LongStraightExcessSquared = Excess * Excess;
			Score.OneCellTerminalSpurCount = Candidate.OneCellTerminalSpurCount;
			const int32 MaxBranchesByCapacity = FMath::FloorToInt(
				static_cast<double>(Input.PreferredOrdinaryWalkableCellCount)
					* Input.PreferredRewardBranchCellRatio
					/ FMath::Max(1, Input.MinimumRewardBranchLengthTiles));
			const int32 EffectiveBranchTarget = FMath::Min(
				Input.PreferredRewardBranchCount,
				FMath::Max(0, MaxBranchesByCapacity));
			Score.RewardBranchDeviation = FMath::Abs(
				Candidate.CandidateRewardBranches.Num() - EffectiveBranchTarget);
			Score.AlternativeCoverageDeficitMicros = FMath::Max(
				0, FMath::RoundToInt(
					(Input.PreferredAlternativeRouteCoverageRatio
						- Candidate.AlternativeRouteCoverageRatio) * 1000000.0));

			if (!Candidate.bSoftRouteAnalysisSucceeded)
			{
				return Score;
			}
			double WeightedQuality = 0.0;
			double ActiveWeight = 0.0;
			const auto AddChannel = [&WeightedQuality, &ActiveWeight](
				const double Weight, const double Fit)
			{
				WeightedQuality += Weight * FMath::Clamp(Fit, 0.0, 1.0);
				ActiveWeight += Weight;
			};
			const double DensityFit = 1.0 - FMath::Clamp(
				static_cast<double>(Score.TotalDeviation)
					/ FMath::Max(1, Input.PreferredTotalWalkableCellCount),
				0.0, 1.0);
			AddChannel(0.15, DensityFit);
			const double CoverageFit = Input.PreferredRouteCoverageRatio > 0.0
				? FMath::Clamp(
					Candidate.RouteCoverageRatio / Input.PreferredRouteCoverageRatio,
					0.0, 1.0)
				: 1.0;
			const int32 DesiredTurns = Candidate.StableMainRouteEdgeCount
				> Input.PreferredMaxConsecutiveStraightTiles
					? FMath::Max(1, Candidate.StableMainRouteEdgeCount
						/ FMath::Max(2, Input.PreferredMaxConsecutiveStraightTiles + 1))
					: 0;
			const double TurnFit = DesiredTurns > 0
				? FMath::Clamp(
					static_cast<double>(Candidate.ReadableTurnCount) / DesiredTurns,
					0.0, 1.0)
				: 1.0;
			const double MainRouteFit = 0.50 * CoverageFit
				+ 0.30 * TurnFit
				+ 0.20 * (1.0 - Candidate.OneTileRunRatio);
			AddChannel(0.25, MainRouteFit);
			if (EffectiveBranchTarget > 0)
			{
				const double RelativeDeviation =
					static_cast<double>(Score.RewardBranchDeviation)
						/ EffectiveBranchTarget;
				AddChannel(0.25, 1.0 - FMath::Clamp(
					RelativeDeviation * RelativeDeviation, 0.0, 1.0));
			}
			if (Input.PreferredAlternativeRouteCoverageRatio > 0.0)
			{
				AddChannel(0.20, Candidate.AlternativeRouteCoverageRatio
					/ Input.PreferredAlternativeRouteCoverageRatio);
			}
			if (Candidate.HighCeilingWalkableCellCount > 0)
			{
				AddChannel(0.15, Candidate.HighCeilingMainRouteCoverageRatio);
			}
			const double ArtifactFit = 1.0 / (
				1.0 + 0.5 * Candidate.OneCellTerminalSpurCount
				+ Score.LongStraightExcessSquared
				+ Candidate.OneTileRunRatio * Candidate.OneTileRunRatio);
			AddChannel(0.15, ArtifactFit);
			const double Quality = ActiveWeight > 0.0
				? FMath::Clamp(WeightedQuality / ActiveWeight, 0.0, 1.0) : 0.0;
			Score.QualityPenaltyMicros = FMath::RoundToInt(
				(1.0 - Quality) * 1000000.0);
			return Score;
		}

		FZeroEscapeWfcShapeWeights BuildDensityBiasedWeights(
			const FZeroEscapeConstrainedFloorInput& Input,
			const FZeroEscapeWfcShapeWeights& Weights,
			const int32 FixedStructureWalkableCount,
			const int32 PossibleWalkableCount)
		{
			FZeroEscapeWfcShapeWeights Result = Weights;
			const int32 OptionalCapacity = FMath::Max(
				1, PossibleWalkableCount - FixedStructureWalkableCount);
			const int32 PreferredOptional = FMath::Clamp(
				FMath::Max(
					Input.PreferredOrdinaryWalkableCellCount,
					Input.PreferredTotalWalkableCellCount
						- FixedStructureWalkableCount),
				Input.MinOrdinaryWalkableCellCount,
				OptionalCapacity);
			const int64 NonEmptyWeight = Weights.GetTotalNonEmptyVariantWeight();
			const int64 EmptyWeight = FMath::DivideAndRoundNearest<int64>(
				NonEmptyWeight * (OptionalCapacity - PreferredOptional),
				FMath::Max(1, PreferredOptional));
			const int64 MaxEmptyWeight = FMath::Max<int64>(
				1, static_cast<int64>(MAX_int32) - NonEmptyWeight);
			Result.EmptyWeight = static_cast<int32>(FMath::Clamp<int64>(
				EmptyWeight, 1, MaxEmptyWeight));
			return Result;
		}

		bool BuildDeterministicFallbackFloor(
			const FZeroEscapeConstrainedFloorInput& Input,
			const TConstArrayView<FWfcCellOpeningPreference> OpeningPreferences,
			FRandomStream& Random,
			FZeroEscapeConstrainedFloorResult& OutResult,
			FString& OutError)
		{
			const int32 CellCount = Input.Constraints.Num();
			const int32 StartIndex = Grid::ToIndex(
				Input.RequiredEnterCoordinate, Input.GridSize);
			TArray<int32> Parent;
			Parent.Init(INDEX_NONE, CellCount);
			TArray<uint8> Reachable;
			Reachable.Init(0, CellCount);
			TArray<int32> ReachableDistance;
			ReachableDistance.Init(INDEX_NONE, CellCount);

			const auto IsAllowedEdge = [&Input](
				const int32 FromIndex,
				const uint8 Direction,
				int32& OutNeighborIndex)
			{
				return IsAllowedPreferenceEdge(
					Input, FromIndex, Direction, OutNeighborIndex);
			};
			const auto IsPreferredEdge = [&Input, OpeningPreferences](
				const int32 FromIndex, const int32 ToIndex)
			{
				if (!OpeningPreferences.IsValidIndex(FromIndex)
					|| !OpeningPreferences.IsValidIndex(ToIndex))
				{
					return false;
				}
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if (Grid::Step(Input.Constraints[FromIndex].Coordinate, Direction)
						== Input.Constraints[ToIndex].Coordinate)
					{
						return (OpeningPreferences[FromIndex].PreferredOpenMask
							& Grid::DirectionBit(Direction)) != 0;
					}
				}
				return false;
			};

			TQueue<int32> Queue;
			Reachable[StartIndex] = 1;
			ReachableDistance[StartIndex] = 0;
			Queue.Enqueue(StartIndex);
			int32 CurrentIndex = INDEX_NONE;
			while (Queue.Dequeue(CurrentIndex))
			{
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					int32 NeighborIndex = INDEX_NONE;
					if (!IsAllowedEdge(CurrentIndex, Direction, NeighborIndex))
					{
						continue;
					}
					if (Reachable[NeighborIndex] == 0)
					{
						Reachable[NeighborIndex] = 1;
						ReachableDistance[NeighborIndex] =
							ReachableDistance[CurrentIndex] + 1;
						Parent[NeighborIndex] = CurrentIndex;
						Queue.Enqueue(NeighborIndex);
					}
					else if (NeighborIndex != StartIndex
						&& ReachableDistance[NeighborIndex]
							== ReachableDistance[CurrentIndex] + 1
						&& IsPreferredEdge(CurrentIndex, NeighborIndex)
						&& !IsPreferredEdge(Parent[NeighborIndex], NeighborIndex))
					{
						Parent[NeighborIndex] = CurrentIndex;
					}
				}
			}

			TArray<uint8> Selected;
			Selected.Init(0, CellCount);
			TArray<uint8> OpeningMasks;
			OpeningMasks.Init(0, CellCount);
			const auto OpenEdge = [&Input, &OpeningMasks](
				const int32 FirstIndex,
				const int32 SecondIndex)
			{
				const FIntPoint Delta = Input.Constraints[SecondIndex].Coordinate
					- Input.Constraints[FirstIndex].Coordinate;
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if (Grid::Step(FIntPoint::ZeroValue, Direction) != Delta)
					{
						continue;
					}
					OpeningMasks[FirstIndex] |= Grid::DirectionBit(Direction);
					OpeningMasks[SecondIndex] |= Grid::DirectionBit(
						Grid::OppositeDirectionIndex(Direction));
					return true;
				}
				return false;
			};
			const auto AddPathToRoot = [&Parent, &Selected, &OpenEdge, StartIndex](
				int32 Index)
			{
				while (Index != StartIndex)
				{
					const int32 ParentIndex = Parent[Index];
					if (ParentIndex == INDEX_NONE || !OpenEdge(Index, ParentIndex))
					{
						return false;
					}
					Selected[Index] = 1;
					Index = ParentIndex;
				}
				Selected[StartIndex] = 1;
				return true;
			};

			for (int32 Index = 0; Index < CellCount; ++Index)
			{
				const FGridCellConstraint& Cell = Input.Constraints[Index];
				if ((Cell.RequiredOpenMask & Cell.RequiredClosedMask) != 0)
				{
					OutError = TEXT("楼层兜底发现同一公共边同时被要求开启和关闭。");
					return false;
				}
				if (Cell.Domain == EGridCellDomain::Required
					&& (Reachable[Index] == 0 || !AddPathToRoot(Index)))
				{
					OutError = TEXT("结构投影的必需格在允许边图中不连通。");
					return false;
				}
			}

			for (int32 Index = 0; Index < CellCount; ++Index)
			{
				const FGridCellConstraint& Cell = Input.Constraints[Index];
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if ((Cell.RequiredOpenMask & Grid::DirectionBit(Direction)) == 0)
					{
						continue;
					}
					int32 NeighborIndex = INDEX_NONE;
					if (!IsAllowedEdge(Index, Direction, NeighborIndex)
						|| Reachable[NeighborIndex] == 0
						|| !AddPathToRoot(NeighborIndex)
						|| !OpenEdge(Index, NeighborIndex))
					{
						OutError = TEXT("结构投影的必开边无法并入兜底连通图。");
						return false;
					}
				}
			}

			int32 FixedStructureWalkableCount = 0;
			int32 SelectedCount = 0;
			int32 ReachableCount = 0;
			for (int32 Index = 0; Index < CellCount; ++Index)
			{
				FixedStructureWalkableCount +=
					Input.StructureWalkableByCell[Index] != 0 ? 1 : 0;
				SelectedCount += Selected[Index] != 0 ? 1 : 0;
				ReachableCount += Reachable[Index] != 0 ? 1 : 0;
			}
			const int32 TargetCount = FMath::Clamp(
				FMath::Max3(
					Input.PreferredTotalWalkableCellCount,
					FixedStructureWalkableCount
						+ Input.PreferredOrdinaryWalkableCellCount,
					Input.MinTotalWalkableCellCount),
				SelectedCount,
				FMath::Min(Input.MaxTotalWalkableCellCount, ReachableCount));

			struct FFrontierEdge
			{
				int32 FromIndex = INDEX_NONE;
				int32 ToIndex = INDEX_NONE;
				int32 Weight = 1;
			};
			const int32 PreferredEdgeWeight = FMath::Max(
				1, FMath::RoundToInt(64.0 * FMath::Pow(
					2.0, static_cast<double>(Input.RouteOpeningPreferenceLog2Strength))));
			while (SelectedCount < TargetCount)
			{
				TArray<FFrontierEdge> Frontier;
				int32 TotalWeight = 0;
				for (int32 Index = 0; Index < CellCount; ++Index)
				{
					if (Selected[Index] == 0)
					{
						continue;
					}
					for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
					{
						int32 NeighborIndex = INDEX_NONE;
						if (IsAllowedEdge(Index, Direction, NeighborIndex)
							&& Selected[NeighborIndex] == 0)
						{
							const int32 Weight = IsPreferredEdge(Index, NeighborIndex)
								? PreferredEdgeWeight : 64;
							Frontier.Add({Index, NeighborIndex, Weight});
							TotalWeight += Weight;
						}
					}
				}
				if (Frontier.IsEmpty())
				{
					break;
				}
				int32 Roll = Random.RandHelper(TotalWeight);
				const FFrontierEdge* Chosen = &Frontier.Last();
				for (const FFrontierEdge& Edge : Frontier)
				{
					Roll -= Edge.Weight;
					if (Roll < 0)
					{
						Chosen = &Edge;
						break;
					}
				}
				Selected[Chosen->ToIndex] = 1;
				++SelectedCount;
				if (!OpenEdge(Chosen->FromIndex, Chosen->ToIndex))
				{
					OutError = TEXT("楼层兜底无法提交前沿公共边。");
					return false;
				}
			}

			return BuildRouteMetrics(Input, OpeningMasks, OutResult, OutError);
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

		int32 FixedStructureWalkableCount = 0;
		int32 PossibleWalkableCount = 0;
		for (int32 DenseIndex = 0; DenseIndex < Input.Constraints.Num(); ++DenseIndex)
		{
			FixedStructureWalkableCount +=
				Input.StructureWalkableByCell[DenseIndex] != 0 ? 1 : 0;
			PossibleWalkableCount +=
				Input.Constraints[DenseIndex].Domain != EGridCellDomain::Outside ? 1 : 0;
		}

		const FZeroEscapeWfcShapeWeights EffectiveWeights = BuildDensityBiasedWeights(
			Input,
			Weights,
			FixedStructureWalkableCount,
			PossibleWalkableCount);
		TStaticArray<FTileVariant, 16> StaticVariants;
		FWfcSolver::BuildCanonicalVariants(EffectiveWeights, StaticVariants);
		TArray<FTileVariant> Variants;
		Variants.Reserve(StaticVariants.Num());
		for (const FTileVariant& Variant : StaticVariants)
		{
			Variants.Add(Variant);
		}
		TArray<FWfcCellOpeningPreference> OpeningPreferences;
		if (!BuildRouteOpeningPreferences(Input, OpeningPreferences))
		{
			OpeningPreferences.Reset();
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
		Settings.PreferredMaxConsecutiveStraightTiles =
			Input.PreferredMaxConsecutiveStraightTiles;
		Settings.OpeningPreferencesByCell = OpeningPreferences;
		Settings.OpeningPreferenceLog2Strength =
			OpeningPreferences.IsEmpty()
				? 0.0f : Input.RouteOpeningPreferenceLog2Strength;

		FZeroEscapeGenerationMetrics AggregateMetrics;
		TOptional<FZeroEscapeConstrainedFloorResult> BestCandidate;
		FFloorSoftQualityScore BestScore;
		const int32 MaxTreesThisCall = FMath::Min(
			Input.MaxSolveAttemptsForThisFloor,
			InOutBudget.RemainingSolveAttempts);
		for (int32 LocalAttempt = 0; LocalAttempt < MaxTreesThisCall; ++LocalAttempt)
		{
			if (InOutBudget.RemainingSolveAttempts <= 0
				|| InOutBudget.RemainingCandidateAttempts <= 0
				|| InOutBudget.RemainingBacktracks <= 0)
			{
				break;
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
				AnalyzeCollapsedRouteStructure(
					Input, AcceptedCandidate.OpeningMaskByCell, AcceptedCandidate);

				const FFloorSoftQualityScore Score = ScoreCandidate(
					Input, AcceptedCandidate);
				if (!BestCandidate.IsSet() || Score.IsBetterThan(BestScore))
				{
					BestCandidate = MoveTemp(AcceptedCandidate);
					BestScore = Score;
				}
				if (Score.OrdinaryDeficit == 0
					&& Score.TotalDeviation == 0
					&& Score.QualityPenaltyMicros <= FMath::RoundToInt(
						(1.0 - Input.RouteQualityEarlyAcceptThreshold)
							* 1000000.0))
				{
					break;
				}
				continue;
			}

			OutReport = MoveTemp(AttemptReport);
			OutReport.Metrics = AggregateMetrics;
			if (!FatalCandidateError.IsEmpty()
				|| (OutReport.Failure != EZeroEscapeGenerationFailure::SolverBudgetExhausted
					&& OutReport.Failure
						!= EZeroEscapeGenerationFailure::NoValidWfcSolution))
			{
				return false;
			}
		}

		if (BestCandidate.IsSet())
		{
			OutResult = MoveTemp(BestCandidate.GetValue());
			OutReport = {};
			OutReport.Metrics = AggregateMetrics;
			return true;
		}

		const int32 FallbackSalt = MakeFloorFallbackSalt(
			Input.WholeLayoutAttemptIndex,
			Input.FloorIndex);
		FRandomStream FallbackRandom = FGenerationCore::MakeRandomStream(
			Input.Signature.Seed,
			ERandomDomain::WfcLayout,
			FallbackSalt);
		FString FallbackError;
		if (BuildDeterministicFallbackFloor(
				Input, OpeningPreferences, FallbackRandom, OutResult, FallbackError))
		{
			AnalyzeCollapsedRouteStructure(
				Input, OutResult.OpeningMaskByCell, OutResult);
			OutReport = {};
			OutReport.Metrics = AggregateMetrics;
			return true;
		}

		OutReport.Metrics = AggregateMetrics;
		return FailFloor(
			OutReport,
			EZeroEscapeGenerationStage::WfcLayout,
			EZeroEscapeGenerationFailure::NoValidWfcSolution,
			FString::Printf(
				TEXT("WFC 搜索未得到硬合法候选，确定性楼层兜底也无法连接结构端点：%s"),
				*FallbackError),
			AggregateMetrics.WfcSolveAttemptCount,
			Input.MaxSolveAttemptsForThisFloor);
	}

#if WITH_DEV_AUTOMATION_TESTS
	int64 Testing::MeasurePreferredPathDirectionCost(
		const TConstArrayView<uint8> Directions,
		const int32 PreferredStraightTiles)
	{
		const int32 MaxRun = FMath::Max(1, PreferredStraightTiles + 8);
		uint8 Incoming = Grid::DirectionCount;
		uint8 Run = 0;
		int64 Total = 0;
		for (const uint8 Direction : Directions)
		{
			uint8 NextRun = 1;
			Total += GridLayoutPrivate::CalculatePreferredPathStepCost(
				FMath::Max(1, PreferredStraightTiles),
				MaxRun,
				Incoming,
				Run,
				Direction,
				NextRun);
			Incoming = Direction;
			Run = NextRun;
		}
		return Total;
	}

	bool Testing::AnalyzeCollapsedRouteStructureForTesting(
		const FZeroEscapeConstrainedFloorInput& Input,
		const TConstArrayView<uint8> OpeningMasks,
		FZeroEscapeConstrainedFloorResult& OutResult)
	{
		OutResult = {};
		OutResult.OpeningMaskByCell.Append(
			OpeningMasks.GetData(), OpeningMasks.Num());
		for (int32 Cell = 0; Cell < OpeningMasks.Num(); ++Cell)
		{
			OutResult.TotalWalkableCellCount += OpeningMasks[Cell] != 0 ? 1 : 0;
			OutResult.OrdinaryWalkableCellCount += OpeningMasks[Cell] != 0
				&& Input.StructureWalkableByCell.IsValidIndex(Cell)
				&& Input.StructureWalkableByCell[Cell] == 0 ? 1 : 0;
		}
		return GridLayoutPrivate::AnalyzeCollapsedRouteStructure(
			Input, OpeningMasks, OutResult);
	}

#endif
}
