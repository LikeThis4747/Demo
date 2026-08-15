// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeMultiFloorLayoutPlanner.cpp
 * 职责：实现受预算约束的多层完整结构放置、逐层二维 WFC 与一次整栋 BFS 验收。
 * 边界：所有状态都属于一次 Solve 调用；失败不提交半张地图，也不按墙钟时间改变结果。
 */

#include "PCG/Layout/ZeroEscapeMultiFloorLayoutPlanner.h"

#include "Algo/Sort.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "HAL/PlatformTime.h"

#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeGridLayoutSolver.h"

namespace ZeroEscape::LevelGeneration
{
	namespace MultiFloorLayoutPrivate
	{
		enum class EReservedCellUse : uint8
		{
			Free,
			PursuerSpawn,
			Exit,
			StructureWalkable,
			StructureSolid,
			Clearance
		};

		struct FReservedCell
		{
			EReservedCellUse Use = EReservedCellUse::Free;
			int32 OwnerStableStructureId = INDEX_NONE;
		};

		struct FFloorEndpoints
		{
			FIntVector Enter = FIntVector::ZeroValue;
			FIntVector Leave = FIntVector::ZeroValue;
			bool bHasEnter = false;
			bool bHasLeave = false;
		};

		struct FPlacementState
		{
			int32 FloorCount = 0;
			FIntPoint GridSize = FIntPoint::ZeroValue;
			TArray<FReservedCell> Cells;
			TArray<FZeroEscapeGeneratedStructure> Structures;
			TArray<int32> RequiredTwoFloorStairStableIdByLowerFloor;
			TArray<FFloorEndpoints> FloorEndpoints;
			TArray<FIntVector> ProtectedEndpoints;
			TMap<FIntVector, int32> ProtectedOpeningOrdinaryCells;
			TArray<int32> HighCeilingRoomCountByFloor;
			FIntVector PursuerSpawn = FIntVector::ZeroValue;
			FIntVector Exit = FIntVector::ZeroValue;
		};

		struct FStructureCandidate
		{
			FName DefinitionId;
			EZeroEscapeStructureKind Kind = EZeroEscapeStructureKind::TwoFloorStair;
			FIntVector BaseCoordinate = FIntVector::ZeroValue;
			uint8 QuarterTurnCount = 0;
			FName ActiveOpeningSetId;
			TArray<FIntVector> WalkableCells;
			TArray<FIntVector> SolidCells;
			TArray<FIntVector> ClearanceCells;
			TArray<FZeroEscapeGeneratedCellConnection> InternalConnections;
			TArray<FZeroEscapeGeneratedStructureOpening> Openings;
			TArray<FZeroEscapeGeneratedStructureLanding> Landings;
			double PrimaryDistanceSquared = 0.0;
			int32 EdgeDepth = 0;
			uint64 StableTieBreak = 0;
		};

		struct FWholeLayoutBudget
		{
			int32 MaxStructureCandidateEvaluations = 0;
			int32 RemainingStructureCandidateEvaluations = 0;
			int32 ConsumedStructureCandidateEvaluations = 0;
			FZeroEscapeSharedWfcBudget Wfc;
		};

		void RestoreBudgetAvailability(
			const FWholeLayoutBudget& Snapshot,
			FWholeLayoutBudget& InOutBudget)
		{
			InOutBudget.RemainingStructureCandidateEvaluations =
				Snapshot.RemainingStructureCandidateEvaluations;
			InOutBudget.Wfc.RemainingSolveAttempts =
				Snapshot.Wfc.RemainingSolveAttempts;
			InOutBudget.Wfc.RemainingCandidateAttempts =
				Snapshot.Wfc.RemainingCandidateAttempts;
			InOutBudget.Wfc.RemainingBacktracks =
				Snapshot.Wfc.RemainingBacktracks;
		}

		struct FFloorSolveRecord
		{
			FZeroEscapeConstrainedFloorInput Input;
			FZeroEscapeConstrainedFloorResult Result;
		};

		struct FWholeGraphResult
		{
			TArray<TArray<int32>> Neighbors;
			TArray<int32> DistanceFromPursuerByNode;
			int32 TotalWalkableCellCount = 0;
		};

		bool FailLayout(
			FZeroEscapeGenerationReport& OutReport,
			const EZeroEscapeGenerationStage Stage,
			const EZeroEscapeGenerationFailure Failure,
			const FString& Message,
			const int32 ActualValue = 0,
			const int32 LimitValue = 0,
			const int32 RelatedStableId = INDEX_NONE)
		{
			OutReport.Stage = Stage;
			OutReport.Failure = Failure;
			OutReport.Message = Message;
			OutReport.ActualValue = ActualValue;
			OutReport.LimitValue = LimitValue;
			OutReport.RelatedStableId = RelatedStableId;
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

		bool AccumulateFloorWfcMetrics(
			FZeroEscapeGenerationMetrics& InOutTotal,
			const int32 FloorIndex,
			const FZeroEscapeGenerationMetrics& Source,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (!InOutTotal.FloorWfcMetrics.IsValidIndex(FloorIndex)
				|| InOutTotal.FloorWfcMetrics[FloorIndex].FloorIndex != FloorIndex)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("每层 WFC 指标数组没有按 FloorIndex 固定初始化。"),
					FloorIndex,
					InOutTotal.FloorWfcMetrics.Num());
			}
			FZeroEscapeFloorWfcMetrics& Floor =
				InOutTotal.FloorWfcMetrics[FloorIndex];
			Floor.WfcObservationCount += Source.WfcObservationCount;
			Floor.WfcSolveAttemptCount += Source.WfcSolveAttemptCount;
			Floor.WfcCandidateAttemptCount += Source.WfcCandidateAttemptCount;
			Floor.WfcPropagationCount += Source.WfcPropagationCount;
			Floor.WfcContradictionCount += Source.WfcContradictionCount;
			Floor.WfcBacktrackCount += Source.WfcBacktrackCount;
			return true;
		}

		bool ConsumeStructureCandidate(
			FWholeLayoutBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (Budget.RemainingStructureCandidateEvaluations <= 0)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::SolverBudgetExhausted,
					TEXT("整栋结构候选检查预算已耗尽。"),
					Budget.ConsumedStructureCandidateEvaluations,
					Budget.MaxStructureCandidateEvaluations);
			}
			--Budget.RemainingStructureCandidateEvaluations;
			++Budget.ConsumedStructureCandidateEvaluations;
			return true;
		}

		// —— 保留格索引、占用所有权与完整结构候选构造 ——
		int32 ToBuildingIndex(
			const FIntVector Address,
			const FIntPoint GridSize)
		{
			return Address.Z * GridSize.X * GridSize.Y
				+ Address.Y * GridSize.X
				+ Address.X;
		}

		bool IsInsideBuilding(
			const FIntVector Address,
			const FPlacementState& State)
		{
			return Address.Z >= 0
				&& Address.Z < State.FloorCount
				&& Grid::IsInside(FIntPoint(Address.X, Address.Y), State.GridSize);
		}

		FReservedCell* FindReservation(
			FPlacementState& State,
			const FIntVector Address)
		{
			return IsInsideBuilding(Address, State)
				? &State.Cells[ToBuildingIndex(Address, State.GridSize)]
				: nullptr;
		}

		const FReservedCell* FindReservation(
			const FPlacementState& State,
			const FIntVector Address)
		{
			return IsInsideBuilding(Address, State)
				? &State.Cells[ToBuildingIndex(Address, State.GridSize)]
				: nullptr;
		}

		FIntVector RotateLocalAddress(
			const FIntVector Local,
			const uint8 QuarterTurnCount)
		{
			switch (QuarterTurnCount & 3u)
			{
			case 0: return Local;
			case 1: return FIntVector(Local.Y, -Local.X, Local.Z);
			case 2: return FIntVector(-Local.X, -Local.Y, Local.Z);
			default: return FIntVector(-Local.Y, Local.X, Local.Z);
			}
		}

		FIntVector ResolveAddress(
			const FIntVector Base,
			const FIntVector Local,
			const uint8 QuarterTurnCount)
		{
			return Base + RotateLocalAddress(Local, QuarterTurnCount);
		}

		int32 DirectionIndexFromEdge(const EZeroEscapeOpenEdge Edge)
		{
			const uint8 Bits = static_cast<uint8>(Edge);
			for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
			{
				if (Bits == Grid::DirectionBit(Direction))
				{
					return Direction;
				}
			}
			return INDEX_NONE;
		}

		FIntVector StepAddress(
			const FIntVector Address,
			const uint8 Direction)
		{
			const FIntPoint Stepped = Grid::Step(
				FIntPoint(Address.X, Address.Y),
				Direction);
			return FIntVector(Stepped.X, Stepped.Y, Address.Z);
		}

		uint64 HashStableString(uint64 Hash, const FString& Text)
		{
			for (const TCHAR Character : Text)
			{
				Hash ^= static_cast<uint64>(Character);
				Hash *= 1099511628211ull;
			}
			return Hash;
		}

		uint64 HashStableInteger(uint64 Hash, const int64 Value)
		{
			uint64 Unsigned = static_cast<uint64>(Value);
			for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				Hash ^= (Unsigned >> (ByteIndex * 8)) & 0xFFu;
				Hash *= 1099511628211ull;
			}
			return Hash;
		}

		uint64 MakeCandidateTieBreak(
			const FResolvedGenerationInput& Input,
			const int32 WholeAttempt,
			const int32 SequenceIndex,
			const FName DefinitionId,
			const FIntVector Base,
			const uint8 QuarterTurnCount,
			const FName OpeningSetId)
		{
			uint64 Hash = 1469598103934665603ull;
			Hash = HashStableInteger(Hash, Input.Signature.Seed);
			// 保留删除 AlgorithmVersion 前 V7 对结构 tie-break 的固定混合贡献。
			Hash = HashStableInteger(Hash, 7);
			Hash = HashStableInteger(Hash, WholeAttempt);
			Hash = HashStableInteger(Hash, SequenceIndex);
			Hash = HashStableString(Hash, DefinitionId.ToString());
			Hash = HashStableInteger(Hash, Base.X);
			Hash = HashStableInteger(Hash, Base.Y);
			Hash = HashStableInteger(Hash, Base.Z);
			Hash = HashStableInteger(Hash, QuarterTurnCount);
			return HashStableString(Hash, OpeningSetId.ToString());
		}

		int32 MakeCandidateSalt(
			const FResolvedGenerationInput& Input,
			const int32 WholeAttempt,
			const int32 SequenceIndex,
			const FName DefinitionId,
			const FIntVector Base,
			const uint8 QuarterTurnCount)
		{
			const uint64 Hash = MakeCandidateTieBreak(
				Input,
				WholeAttempt,
				SequenceIndex,
				DefinitionId,
				Base,
				QuarterTurnCount,
				NAME_None);
			return static_cast<int32>(Hash ^ (Hash >> 32));
		}

		double DistanceSquared2D(const FIntVector A, const FIntVector B)
		{
			const double DeltaX = static_cast<double>(A.X - B.X);
			const double DeltaY = static_cast<double>(A.Y - B.Y);
			return DeltaX * DeltaX + DeltaY * DeltaY;
		}

		double ComputeWalkableFootprintDistanceSquared(
			const TConstArrayView<FIntVector> FirstWalkableCells,
			const TConstArrayView<FIntVector> SecondWalkableCells)
		{
			double Minimum = TNumericLimits<double>::Max();
			bool bCompared = false;
			for (const FIntVector First : FirstWalkableCells)
			{
				for (const FIntVector Second : SecondWalkableCells)
				{
					if (First.Z != Second.Z)
					{
						continue;
					}
					Minimum = FMath::Min(
						Minimum,
						DistanceSquared2D(First, Second));
					bCompared = true;
				}
			}
			return bCompared ? Minimum : -1.0;
		}

		double GridDiagonalSquared(const FIntPoint GridSize)
		{
			return FMath::Square(static_cast<double>(GridSize.X - 1))
				+ FMath::Square(static_cast<double>(GridSize.Y - 1));
		}

		int32 EdgeDepth(const FIntVector Address, const FIntPoint GridSize)
		{
			return FMath::Min(
				FMath::Min(Address.X, GridSize.X - 1 - Address.X),
				FMath::Min(Address.Y, GridSize.Y - 1 - Address.Y));
		}

		TArray<int32> GetSortedDefinitionIndices(
			const FResolvedGenerationInput& Input,
			const EZeroEscapeStructureKind Kind)
		{
			TArray<int32> Indices;
			for (int32 Index = 0; Index < Input.StructureDefinitions.Num(); ++Index)
			{
				if (Input.StructureDefinitions[Index].Kind == Kind)
				{
					Indices.Add(Index);
				}
			}
			Indices.Sort([&Input](const int32 A, const int32 B)
			{
				return Input.StructureDefinitions[A].DefinitionId.LexicalLess(
					Input.StructureDefinitions[B].DefinitionId);
			});
			return Indices;
		}

		const FZeroEscapeStructureOpeningDefinition* FindOpeningDefinition(
			const FZeroEscapeStructureDefinition& Definition,
			const FName OpeningId)
		{
			return Definition.Openings.FindByPredicate(
				[OpeningId](const FZeroEscapeStructureOpeningDefinition& Candidate)
				{
					return Candidate.OpeningId == OpeningId;
				});
		}

		bool IsCandidateCellFree(
			const FPlacementState& State,
			const FIntVector Address)
		{
			const FReservedCell* Cell = FindReservation(State, Address);
			return Cell != nullptr && Cell->Use == EReservedCellUse::Free;
		}

		bool BuildCandidateCells(
			const FZeroEscapeStructureDefinition& Definition,
			const FPlacementState& State,
			const FIntVector Base,
			const uint8 QuarterTurnCount,
			FStructureCandidate& OutCandidate)
		{
			TSet<FIntVector> SeenCells;
			const auto AddCells =
				[&](
					const TArray<FIntVector>& LocalCells,
					const bool bClearance,
					TArray<FIntVector>& OutCells)
				{
					for (const FIntVector Local : LocalCells)
					{
						const FIntVector Resolved = ResolveAddress(
							Base, Local, QuarterTurnCount);
						if (!IsInsideBuilding(Resolved, State))
						{
							const bool bMayTrimAboveTop = bClearance
								&& Definition.Kind
									== EZeroEscapeStructureKind::HighCeilingRoom
								&& Definition.bAllowClearanceAboveGeneratedTopFloor
								&& Resolved.Z >= State.FloorCount
								&& Grid::IsInside(
									FIntPoint(Resolved.X, Resolved.Y),
									State.GridSize);
							if (bMayTrimAboveTop)
							{
								continue;
							}
							return false;
						}
						if (SeenCells.Contains(Resolved))
						{
							return false;
						}
						SeenCells.Add(Resolved);
						OutCells.Add(Resolved);
					}
					return true;
				};

			if (!AddCells(Definition.WalkableCells, false, OutCandidate.WalkableCells)
				|| !AddCells(Definition.SolidCells, false, OutCandidate.SolidCells)
				|| !AddCells(Definition.ClearanceCells, true, OutCandidate.ClearanceCells))
			{
				return false;
			}

			TSet<FIntVector> WalkableSet;
			for (const FIntVector Address : OutCandidate.WalkableCells)
			{
				WalkableSet.Add(Address);
			}

			const auto CoordinateLess = [](const FIntVector& A, const FIntVector& B)
			{
				return A.Z != B.Z ? A.Z < B.Z
					: A.Y != B.Y ? A.Y < B.Y
					: A.X < B.X;
			};

			for (const FZeroEscapeLocalCellConnection& LocalConnection :
				Definition.InternalConnections)
			{
				FZeroEscapeGeneratedCellConnection Connection;
				Connection.FirstCoordinate = ResolveAddress(
					Base, LocalConnection.FirstCell, QuarterTurnCount);
				Connection.SecondCoordinate = ResolveAddress(
					Base, LocalConnection.SecondCell, QuarterTurnCount);
				if (Connection.FirstCoordinate == Connection.SecondCoordinate
					|| !WalkableSet.Contains(Connection.FirstCoordinate)
					|| !WalkableSet.Contains(Connection.SecondCoordinate))
				{
					return false;
				}
				if (CoordinateLess(
						Connection.SecondCoordinate,
						Connection.FirstCoordinate))
				{
					Swap(
						Connection.FirstCoordinate,
						Connection.SecondCoordinate);
				}
				OutCandidate.InternalConnections.Add(Connection);
			}

			for (const FZeroEscapeStructureLandingDefinition& LocalLanding :
				Definition.Landings)
			{
				FZeroEscapeGeneratedStructureLanding Landing;
				Landing.LandingId = LocalLanding.LandingId;
				Landing.Coordinate = ResolveAddress(
					Base, LocalLanding.LocalCoordinate, QuarterTurnCount);
				if (!WalkableSet.Contains(Landing.Coordinate))
				{
					return false;
				}
				OutCandidate.Landings.Add(Landing);
			}

			OutCandidate.Landings.Sort(
				[CoordinateLess](const FZeroEscapeGeneratedStructureLanding& A,
					const FZeroEscapeGeneratedStructureLanding& B)
				{
					return A.LandingId != B.LandingId
						? A.LandingId.LexicalLess(B.LandingId)
						: CoordinateLess(A.Coordinate, B.Coordinate);
				});
			OutCandidate.WalkableCells.Sort(CoordinateLess);
			OutCandidate.SolidCells.Sort(CoordinateLess);
			OutCandidate.ClearanceCells.Sort(CoordinateLess);
			OutCandidate.InternalConnections.Sort(
				[CoordinateLess](
					const FZeroEscapeGeneratedCellConnection& A,
					const FZeroEscapeGeneratedCellConnection& B)
				{
					return A.FirstCoordinate != B.FirstCoordinate
						? CoordinateLess(A.FirstCoordinate, B.FirstCoordinate)
						: CoordinateLess(A.SecondCoordinate, B.SecondCoordinate);
				});
			return true;
		}

		bool TryBuildOpeningsForSet(
			const FZeroEscapeStructureDefinition& Definition,
			const FZeroEscapeStructureOpeningSetDefinition& OpeningSet,
			const FPlacementState& State,
			const FIntVector Base,
			const uint8 QuarterTurnCount,
			const TSet<FIntVector>& CandidateWalkable,
			const TSet<FIntVector>& CandidateFootprint,
			TArray<FZeroEscapeGeneratedStructureOpening>& OutOpenings)
		{
			OutOpenings.Reset();
			TSet<FName> SeenOpeningIds;
			TSet<FIntVector> SeenConnectedOrdinaryCells;
			for (const FName OpeningId : OpeningSet.OpenOpeningIds)
			{
				if (SeenOpeningIds.Contains(OpeningId))
				{
					return false;
				}
				SeenOpeningIds.Add(OpeningId);
				const FZeroEscapeStructureOpeningDefinition* OpeningDefinition =
					FindOpeningDefinition(Definition, OpeningId);
				if (OpeningDefinition == nullptr)
				{
					return false;
				}

				const int32 LocalDirection =
					DirectionIndexFromEdge(OpeningDefinition->OutwardEdge);
				if (LocalDirection == INDEX_NONE)
				{
					return false;
				}
				const uint8 Direction = static_cast<uint8>(
					(LocalDirection + QuarterTurnCount) & 3);
				FZeroEscapeGeneratedStructureOpening Opening;
				Opening.OpeningId = OpeningId;
				Opening.StructureCoordinate = ResolveAddress(
					Base,
					OpeningDefinition->LocalWalkableCell,
					QuarterTurnCount);
				Opening.ConnectedOrdinaryCoordinate = StepAddress(
					Opening.StructureCoordinate,
					Direction);
				if (!CandidateWalkable.Contains(Opening.StructureCoordinate)
					|| !IsInsideBuilding(Opening.ConnectedOrdinaryCoordinate, State)
					|| CandidateFootprint.Contains(Opening.ConnectedOrdinaryCoordinate)
					|| SeenConnectedOrdinaryCells.Contains(
						Opening.ConnectedOrdinaryCoordinate)
					|| State.ProtectedOpeningOrdinaryCells.Contains(
						Opening.ConnectedOrdinaryCoordinate))
				{
					return false;
				}
				const FReservedCell* ConnectedCell = FindReservation(
					State, Opening.ConnectedOrdinaryCoordinate);
				if (ConnectedCell == nullptr)
				{
					return false;
				}
				const EReservedCellUse ConnectedUse = ConnectedCell->Use;
				if (ConnectedUse != EReservedCellUse::Free
					&& ConnectedUse != EReservedCellUse::PursuerSpawn
					&& ConnectedUse != EReservedCellUse::Exit)
				{
					return false;
				}
				SeenConnectedOrdinaryCells.Add(
					Opening.ConnectedOrdinaryCoordinate);
				OutOpenings.Add(Opening);
			}

			OutOpenings.Sort(
				[](const FZeroEscapeGeneratedStructureOpening& A,
					const FZeroEscapeGeneratedStructureOpening& B)
				{
					if (A.OpeningId != B.OpeningId)
					{
						return A.OpeningId.LexicalLess(B.OpeningId);
					}
					if (A.StructureCoordinate != B.StructureCoordinate)
					{
						return A.StructureCoordinate.Z != B.StructureCoordinate.Z
							? A.StructureCoordinate.Z < B.StructureCoordinate.Z
							: A.StructureCoordinate.Y != B.StructureCoordinate.Y
							? A.StructureCoordinate.Y < B.StructureCoordinate.Y
							: A.StructureCoordinate.X < B.StructureCoordinate.X;
					}
					return A.ConnectedOrdinaryCoordinate.Z
						!= B.ConnectedOrdinaryCoordinate.Z
						? A.ConnectedOrdinaryCoordinate.Z
							< B.ConnectedOrdinaryCoordinate.Z
						: A.ConnectedOrdinaryCoordinate.Y
							!= B.ConnectedOrdinaryCoordinate.Y
						? A.ConnectedOrdinaryCoordinate.Y
							< B.ConnectedOrdinaryCoordinate.Y
						: A.ConnectedOrdinaryCoordinate.X
							< B.ConnectedOrdinaryCoordinate.X;
				});
			return !OutOpenings.IsEmpty();
		}

		bool SelectLegalOpeningSet(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeStructureDefinition& Definition,
			const FPlacementState& State,
			const FIntVector Base,
			const uint8 QuarterTurnCount,
			const int32 WholeAttempt,
			const int32 SequenceIndex,
			const ERandomDomain Domain,
			const FStructureCandidate& CellCandidate,
			FName& OutSetId,
			TArray<FZeroEscapeGeneratedStructureOpening>& OutOpenings)
		{
			TSet<FIntVector> CandidateFootprint;
			TSet<FIntVector> CandidateWalkable;
			for (const FIntVector Address : CellCandidate.WalkableCells)
			{
				CandidateWalkable.Add(Address);
				CandidateFootprint.Add(Address);
			}
			for (const FIntVector Address : CellCandidate.SolidCells)
			{
				CandidateFootprint.Add(Address);
			}
			for (const FIntVector Address : CellCandidate.ClearanceCells)
			{
				CandidateFootprint.Add(Address);
			}

			struct FLegalSet
			{
				int32 SetIndex = INDEX_NONE;
				TArray<FZeroEscapeGeneratedStructureOpening> Openings;
			};
			TArray<int32> SortedSetIndices;
			for (int32 Index = 0; Index < Definition.AllowedOpeningSets.Num(); ++Index)
			{
				SortedSetIndices.Add(Index);
			}
			SortedSetIndices.Sort([&Definition](const int32 A, const int32 B)
			{
				return Definition.AllowedOpeningSets[A].SetId.LexicalLess(
					Definition.AllowedOpeningSets[B].SetId);
			});

			TArray<FLegalSet> LegalSets;
			int64 TotalWeight = 0;
			for (const int32 SetIndex : SortedSetIndices)
			{
				const FZeroEscapeStructureOpeningSetDefinition& Set =
					Definition.AllowedOpeningSets[SetIndex];
				if (Set.SelectionWeight <= 0)
				{
					continue;
				}
				FLegalSet Legal;
				Legal.SetIndex = SetIndex;
				if (!TryBuildOpeningsForSet(
						Definition,
						Set,
						State,
						Base,
						QuarterTurnCount,
						CandidateWalkable,
						CandidateFootprint,
						Legal.Openings))
				{
					continue;
				}
				TotalWeight += Set.SelectionWeight;
				if (TotalWeight > MAX_int32)
				{
					return false;
				}
				LegalSets.Add(MoveTemp(Legal));
			}

			if (LegalSets.IsEmpty() || TotalWeight <= 0)
			{
				return false;
			}

			FRandomStream Random = FGenerationCore::MakeRandomStream(
				Input.Signature.Seed,
				Domain,
				MakeCandidateSalt(
					Input,
					WholeAttempt,
					SequenceIndex,
					Definition.DefinitionId,
					Base,
					QuarterTurnCount));
			int32 Roll = Random.RandHelper(static_cast<int32>(TotalWeight));
			for (FLegalSet& Legal : LegalSets)
			{
				const FZeroEscapeStructureOpeningSetDefinition& Set =
					Definition.AllowedOpeningSets[Legal.SetIndex];
				Roll -= Set.SelectionWeight;
				if (Roll < 0)
				{
					OutSetId = Set.SetId;
					OutOpenings = MoveTemp(Legal.Openings);
					return true;
				}
			}
			return false;
		}

		bool TryResolveCandidate(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeStructureDefinition& Definition,
			const FPlacementState& State,
			const FIntVector Base,
			const uint8 QuarterTurnCount,
			const int32 WholeAttempt,
			const int32 SequenceIndex,
			const ERandomDomain Domain,
			FWholeLayoutBudget& Budget,
			FStructureCandidate& OutCandidate,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (!ConsumeStructureCandidate(Budget, OutReport))
			{
				return false;
			}
			OutCandidate = {};
			OutCandidate.DefinitionId = Definition.DefinitionId;
			OutCandidate.Kind = Definition.Kind;
			OutCandidate.BaseCoordinate = Base;
			OutCandidate.QuarterTurnCount = QuarterTurnCount;
			if (!BuildCandidateCells(
					Definition,
					State,
					Base,
					QuarterTurnCount,
					OutCandidate))
			{
				return false;
			}

			if (!SelectLegalOpeningSet(
					Input,
					Definition,
					State,
					Base,
					QuarterTurnCount,
					WholeAttempt,
					SequenceIndex,
					Domain,
					OutCandidate,
					OutCandidate.ActiveOpeningSetId,
					OutCandidate.Openings))
			{
				return false;
			}

			OutCandidate.StableTieBreak = MakeCandidateTieBreak(
				Input,
				WholeAttempt,
				SequenceIndex,
				Definition.DefinitionId,
				Base,
				QuarterTurnCount,
				OutCandidate.ActiveOpeningSetId);
			return true;
		}

		bool PreservesOrdinaryCapacity(
			const FStructureCandidate& Candidate,
			const FPlacementState& State)
		{
			TArray<int32> Available;
			Available.Init(0, State.FloorCount);
			for (int32 Floor = 0; Floor < State.FloorCount; ++Floor)
			{
				for (int32 Y = 0; Y < State.GridSize.Y; ++Y)
				{
					for (int32 X = 0; X < State.GridSize.X; ++X)
					{
						const FReservedCell* Cell = FindReservation(
							State, FIntVector(X, Y, Floor));
						if (Cell == nullptr)
						{
							return false;
						}
						const EReservedCellUse Use = Cell->Use;
						Available[Floor] += Use == EReservedCellUse::Free
							|| Use == EReservedCellUse::PursuerSpawn
							|| Use == EReservedCellUse::Exit
							? 1 : 0;
					}
				}
			}

			const auto SubtractCells = [&Available](const TArray<FIntVector>& Cells)
			{
				for (const FIntVector Address : Cells)
				{
					--Available[Address.Z];
				}
			};
			SubtractCells(Candidate.WalkableCells);
			SubtractCells(Candidate.SolidCells);
			SubtractCells(Candidate.ClearanceCells);
			for (int32 Floor = 0; Floor < Available.Num(); ++Floor)
			{
				const int32 HardMinimum = Floor == 0 ? 2 : 1;
				if (Available[Floor] < HardMinimum)
				{
					return false;
				}
			}
			return true;
		}

		bool CanPlaceCandidate(
			const FStructureCandidate& Candidate,
			const FPlacementState& State)
		{
			const auto AllFree = [&State](const TArray<FIntVector>& Cells)
			{
				for (const FIntVector Address : Cells)
				{
					if (!IsCandidateCellFree(State, Address)
						|| State.ProtectedOpeningOrdinaryCells.Contains(Address))
					{
						return false;
					}
				}
				return true;
			};
			if (!AllFree(Candidate.WalkableCells)
				|| !AllFree(Candidate.SolidCells)
				|| !AllFree(Candidate.ClearanceCells))
			{
				return false;
			}
			for (const FZeroEscapeGeneratedStructureOpening& Opening : Candidate.Openings)
			{
				const FReservedCell* Cell = FindReservation(
					State, Opening.ConnectedOrdinaryCoordinate);
				if (Cell == nullptr)
				{
					return false;
				}
				const EReservedCellUse Use = Cell->Use;
				if (Use != EReservedCellUse::Free
					&& Use != EReservedCellUse::PursuerSpawn
					&& Use != EReservedCellUse::Exit)
				{
					return false;
				}
			}
			return PreservesOrdinaryCapacity(Candidate, State);
		}

		bool CommitCandidate(
			const FStructureCandidate& Candidate,
			FPlacementState& State,
			int32& OutStableStructureId,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutStableStructureId = INDEX_NONE;
			const auto CanReserve = [&State](const TArray<FIntVector>& Addresses)
			{
				for (const FIntVector Address : Addresses)
				{
					const FReservedCell* Cell = FindReservation(State, Address);
					if (Cell == nullptr
						|| Cell->Use != EReservedCellUse::Free
						|| State.ProtectedOpeningOrdinaryCells.Contains(Address))
					{
						return false;
					}
				}
				return true;
			};
			if (!CanReserve(Candidate.WalkableCells)
				|| !CanReserve(Candidate.SolidCells)
				|| !CanReserve(Candidate.ClearanceCells))
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("结构候选在提交前与已保留格发生了冲突。"));
			}

			TSet<FIntVector> CandidateFootprint;
			CandidateFootprint.Append(Candidate.WalkableCells);
			CandidateFootprint.Append(Candidate.SolidCells);
			CandidateFootprint.Append(Candidate.ClearanceCells);
			TSet<FIntVector> OpeningOrdinaryCells;
			for (const FZeroEscapeGeneratedStructureOpening& Opening : Candidate.Openings)
			{
				const FReservedCell* ConnectedCell = FindReservation(
					State, Opening.ConnectedOrdinaryCoordinate);
				const bool bConnectedUseIsOrdinary = ConnectedCell != nullptr
					&& (ConnectedCell->Use == EReservedCellUse::Free
						|| ConnectedCell->Use == EReservedCellUse::PursuerSpawn
						|| ConnectedCell->Use == EReservedCellUse::Exit);
				if (!Candidate.WalkableCells.Contains(Opening.StructureCoordinate)
					|| !bConnectedUseIsOrdinary
					|| CandidateFootprint.Contains(
						Opening.ConnectedOrdinaryCoordinate)
					|| OpeningOrdinaryCells.Contains(
						Opening.ConnectedOrdinaryCoordinate)
					|| State.ProtectedOpeningOrdinaryCells.Contains(
						Opening.ConnectedOrdinaryCoordinate))
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("结构开口外的普通格在提交前已失去唯一保护所有权。"));
				}
				OpeningOrdinaryCells.Add(Opening.ConnectedOrdinaryCoordinate);
			}

			FZeroEscapeGeneratedStructure& Structure =
				State.Structures.AddDefaulted_GetRef();
			Structure.StableStructureId = State.Structures.Num() - 1;
			Structure.DefinitionId = Candidate.DefinitionId;
			Structure.Kind = Candidate.Kind;
			Structure.BaseCoordinate = Candidate.BaseCoordinate;
			Structure.QuarterTurnCount = Candidate.QuarterTurnCount;
			Structure.ActiveOpeningSetId = Candidate.ActiveOpeningSetId;
			Structure.WalkableCells = Candidate.WalkableCells;
			Structure.SolidCells = Candidate.SolidCells;
			Structure.ClearanceCells = Candidate.ClearanceCells;
			Structure.InternalConnections = Candidate.InternalConnections;
			Structure.Openings = Candidate.Openings;
			Structure.Landings = Candidate.Landings;

			const auto Reserve =
				[&State, &Structure](
					const TArray<FIntVector>& Addresses,
					const EReservedCellUse Use)
				{
					for (const FIntVector Address : Addresses)
					{
						FReservedCell* Cell = FindReservation(State, Address);
						if (Cell == nullptr || Cell->Use != EReservedCellUse::Free)
						{
							return false;
						}
						Cell->Use = Use;
						Cell->OwnerStableStructureId = Structure.StableStructureId;
					}
					return true;
				};
			if (!Reserve(Structure.WalkableCells, EReservedCellUse::StructureWalkable)
				|| !Reserve(Structure.SolidCells, EReservedCellUse::StructureSolid)
				|| !Reserve(Structure.ClearanceCells, EReservedCellUse::Clearance))
			{
				State.Structures.Pop(EAllowShrinking::No);
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("结构候选的预检查与提交结果不一致。"));
			}
			for (const FIntVector Address : OpeningOrdinaryCells)
			{
				State.ProtectedOpeningOrdinaryCells.Add(
					Address, Structure.StableStructureId);
			}

			if (Structure.Kind == EZeroEscapeStructureKind::TwoFloorStair
				|| Structure.Kind == EZeroEscapeStructureKind::ThreeFloorStairwell)
			{
				for (const FZeroEscapeGeneratedStructureLanding& Landing :
					Structure.Landings)
				{
					State.ProtectedEndpoints.Add(Landing.Coordinate);
				}
			}
			if (Structure.Kind == EZeroEscapeStructureKind::HighCeilingRoom)
			{
				++State.HighCeilingRoomCountByFloor[Structure.BaseCoordinate.Z];
			}
			OutStableStructureId = Structure.StableStructureId;
			return true;
		}

		bool RollbackLastStructure(
			FPlacementState& State,
			const int32 PreviousProtectedEndpointCount,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (State.Structures.IsEmpty()
				|| PreviousProtectedEndpointCount < 0
				|| PreviousProtectedEndpointCount > State.ProtectedEndpoints.Num())
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("结构回滚栈或端点边界无效。"));
			}
			const FZeroEscapeGeneratedStructure& Structure = State.Structures.Last();
			TSet<FIntVector> OpeningOrdinaryCells;
			for (const FZeroEscapeGeneratedStructureOpening& Opening :
				Structure.Openings)
			{
				const FIntVector Address = Opening.ConnectedOrdinaryCoordinate;
				const int32* OwnerStableStructureId =
					State.ProtectedOpeningOrdinaryCells.Find(Address);
				if (OpeningOrdinaryCells.Contains(Address)
					|| OwnerStableStructureId == nullptr
					|| *OwnerStableStructureId != Structure.StableStructureId)
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("Opening ordinary-cell ownership did not match the rollback stack."),
						0,
						0,
						Structure.StableStructureId);
				}
				OpeningOrdinaryCells.Add(Address);
			}
			const auto Release = [&State, &Structure](const TArray<FIntVector>& Addresses)
			{
				for (const FIntVector Address : Addresses)
				{
					FReservedCell* Cell = FindReservation(State, Address);
					if (Cell == nullptr
						|| Cell->OwnerStableStructureId != Structure.StableStructureId)
					{
						return false;
					}
					*Cell = {};
				}
				return true;
			};
			if (!Release(Structure.WalkableCells)
				|| !Release(Structure.SolidCells)
				|| !Release(Structure.ClearanceCells))
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("结构回滚时格子所有者与回滚栈不一致。"),
					0,
					0,
					Structure.StableStructureId);
			}
			for (const FIntVector Address : OpeningOrdinaryCells)
			{
				if (State.ProtectedOpeningOrdinaryCells.Remove(Address) != 1)
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("Opening ordinary-cell ownership changed during rollback."),
						0,
						0,
						Structure.StableStructureId);
				}
			}
			if (Structure.Kind == EZeroEscapeStructureKind::HighCeilingRoom)
			{
				--State.HighCeilingRoomCountByFloor[Structure.BaseCoordinate.Z];
			}
			State.ProtectedEndpoints.SetNum(
				PreviousProtectedEndpointCount,
				EAllowShrinking::No);
			State.Structures.Pop(EAllowShrinking::No);
			return true;
		}

		bool FindUniqueLandingOnFloor(
			const FStructureCandidate& Candidate,
			const int32 Floor,
			FIntVector& OutLanding)
		{
			bool bFound = false;
			for (const FZeroEscapeGeneratedStructureLanding& Landing : Candidate.Landings)
			{
				if (Landing.Coordinate.Z != Floor)
				{
					continue;
				}
				if (bFound)
				{
					return false;
				}
				OutLanding = Landing.Coordinate;
				bFound = true;
			}
			return bFound;
		}

		bool EnumerateStructureCandidates(
			const FResolvedGenerationInput& Input,
			const EZeroEscapeStructureKind Kind,
			const int32 RequiredBaseFloor,
			const int32 WholeAttempt,
			const int32 SequenceIndex,
			const ERandomDomain Domain,
			const FZeroEscapeFloorCountOption& FloorOption,
			FPlacementState& State,
			FWholeLayoutBudget& Budget,
			TArray<FStructureCandidate>& OutCandidates,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutCandidates.Reset();
			const TArray<int32> DefinitionIndices =
				GetSortedDefinitionIndices(Input, Kind);
			for (const int32 DefinitionIndex : DefinitionIndices)
			{
				const FZeroEscapeStructureDefinition& Definition =
					Input.StructureDefinitions[DefinitionIndex];
				const int32 FirstBaseFloor = RequiredBaseFloor >= 0
					? RequiredBaseFloor : 0;
				const int32 LastBaseFloor = RequiredBaseFloor >= 0
					? RequiredBaseFloor
					: State.FloorCount - Definition.RequiredFloorCount;
				if (FirstBaseFloor < 0 || LastBaseFloor < FirstBaseFloor)
				{
					continue;
				}

				for (int32 BaseFloor = FirstBaseFloor;
					BaseFloor <= LastBaseFloor;
					++BaseFloor)
				{
					if (BaseFloor + Definition.RequiredFloorCount > State.FloorCount)
					{
						continue;
					}
					for (int32 Y = 0; Y < State.GridSize.Y; ++Y)
					{
						for (int32 X = 0; X < State.GridSize.X; ++X)
						{
							for (uint8 QuarterTurn = 0; QuarterTurn < 4; ++QuarterTurn)
							{
								FStructureCandidate Candidate;
								if (!TryResolveCandidate(
										Input,
										Definition,
										State,
										FIntVector(X, Y, BaseFloor),
										QuarterTurn,
										WholeAttempt,
										SequenceIndex,
										Domain,
										Budget,
										Candidate,
										OutReport))
								{
									if (OutReport.Failure
										== EZeroEscapeGenerationFailure::SolverBudgetExhausted)
									{
										return false;
									}
									continue;
								}
								if (CanPlaceCandidate(Candidate, State))
								{
									OutCandidates.Add(MoveTemp(Candidate));
								}
							}
						}
					}
				}
			}
			return true;
		}

		// —— 玩家出生点、必需双层楼梯回溯链与顶层终点 ——
		bool TryChooseTopExit(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			const int32 WholeAttempt,
			FPlacementState& State,
			FWholeLayoutBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int32 TopFloor = State.FloorCount - 1;
			const FIntVector Enter = State.FloorEndpoints[TopFloor].Enter;
			struct FExitCandidate
			{
				FIntVector Address = FIntVector::ZeroValue;
				double DistanceSquared = 0.0;
				int32 EdgeDepth = 0;
				uint64 TieBreak = 0;
			};
			TArray<FExitCandidate> Candidates;
			for (int32 Y = 0; Y < State.GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < State.GridSize.X; ++X)
				{
					if (!ConsumeStructureCandidate(Budget, OutReport))
					{
						return false;
					}
					const FIntVector Address(X, Y, TopFloor);
					if (!IsCandidateCellFree(State, Address))
					{
						continue;
					}
					FExitCandidate Candidate;
					Candidate.Address = Address;
					Candidate.DistanceSquared = DistanceSquared2D(Enter, Address);
					Candidate.EdgeDepth = EdgeDepth(Address, State.GridSize);
					Candidate.TieBreak = MakeCandidateTieBreak(
						Input,
						WholeAttempt,
						TopFloor,
						FName(TEXT("Exit")),
						Address,
						0,
						NAME_None);
					Candidates.Add(Candidate);
				}
			}

			Candidates.Sort([](const FExitCandidate& A, const FExitCandidate& B)
			{
				if (A.DistanceSquared != B.DistanceSquared)
				{
					return A.DistanceSquared > B.DistanceSquared;
				}
				if (A.EdgeDepth != B.EdgeDepth)
				{
					return A.EdgeDepth < B.EdgeDepth;
				}
				return A.TieBreak < B.TieBreak;
			});
			if (Candidates.IsEmpty())
			{
				return false;
			}

			State.Exit = Candidates[0].Address;
			FReservedCell* ExitCell = FindReservation(State, State.Exit);
			if (ExitCell == nullptr || ExitCell->Use != EReservedCellUse::Free)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("选定顶层终点后对应格已不可保留。"));
			}
			ExitCell->Use = EReservedCellUse::Exit;
			State.FloorEndpoints[TopFloor].Leave = State.Exit;
			State.FloorEndpoints[TopFloor].bHasLeave = true;
			State.ProtectedEndpoints.Add(State.Exit);
			return true;
		}

		bool PlaceRequiredStairsRecursive(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			const int32 WholeAttempt,
			const int32 LowerFloor,
			FPlacementState& State,
			FWholeLayoutBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (LowerFloor == State.FloorCount - 1)
			{
				return TryChooseTopExit(
					Input, FloorOption, WholeAttempt, State, Budget, OutReport);
			}

			TArray<FStructureCandidate> Candidates;
			if (!EnumerateStructureCandidates(
					Input,
					EZeroEscapeStructureKind::TwoFloorStair,
					LowerFloor,
					WholeAttempt,
					LowerFloor,
					ERandomDomain::RequiredTwoFloorStairPlacement,
					FloorOption,
					State,
					Budget,
					Candidates,
					OutReport))
			{
				return false;
			}

			const FIntVector CurrentEnter = State.FloorEndpoints[LowerFloor].Enter;
			for (FStructureCandidate& Candidate : Candidates)
			{
				FIntVector LowerLanding;
				FIntVector UpperLanding;
				if (!FindUniqueLandingOnFloor(Candidate, LowerFloor, LowerLanding)
					|| !FindUniqueLandingOnFloor(Candidate, LowerFloor + 1, UpperLanding))
				{
					Candidate.PrimaryDistanceSquared = -1.0;
					continue;
				}
				Candidate.PrimaryDistanceSquared =
					DistanceSquared2D(CurrentEnter, LowerLanding);
				Candidate.EdgeDepth = FMath::Max(
					EdgeDepth(LowerLanding, State.GridSize),
					EdgeDepth(UpperLanding, State.GridSize));
			}
			Candidates.Sort([](const FStructureCandidate& A, const FStructureCandidate& B)
			{
				if (A.PrimaryDistanceSquared != B.PrimaryDistanceSquared)
				{
					return A.PrimaryDistanceSquared > B.PrimaryDistanceSquared;
				}
				if (A.EdgeDepth != B.EdgeDepth)
				{
					return A.EdgeDepth < B.EdgeDepth;
				}
				return A.StableTieBreak < B.StableTieBreak;
			});

			for (const FStructureCandidate& Candidate : Candidates)
			{
				if (Candidate.PrimaryDistanceSquared < 0.0)
				{
					continue;
				}
				FIntVector LowerLanding;
				FIntVector UpperLanding;
				if (!FindUniqueLandingOnFloor(Candidate, LowerFloor, LowerLanding)
					|| !FindUniqueLandingOnFloor(
						Candidate, LowerFloor + 1, UpperLanding))
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("排序后的必需楼梯候选丢失了楼层落脚点。"));
				}
				const int32 PreviousProtectedCount = State.ProtectedEndpoints.Num();
				int32 StableId = INDEX_NONE;
				if (!CommitCandidate(
						Candidate, State, StableId, OutReport))
				{
					return false;
				}
				State.RequiredTwoFloorStairStableIdByLowerFloor[LowerFloor] = StableId;
				State.FloorEndpoints[LowerFloor].Leave = LowerLanding;
				State.FloorEndpoints[LowerFloor].bHasLeave = true;
				State.FloorEndpoints[LowerFloor + 1].Enter = UpperLanding;
				State.FloorEndpoints[LowerFloor + 1].bHasEnter = true;

				if (PlaceRequiredStairsRecursive(
						Input,
						FloorOption,
						WholeAttempt,
						LowerFloor + 1,
						State,
						Budget,
						OutReport))
				{
					return true;
				}

				if (!RollbackLastStructure(
						State, PreviousProtectedCount, OutReport))
				{
					return false;
				}
				State.RequiredTwoFloorStairStableIdByLowerFloor[LowerFloor] = INDEX_NONE;
				State.FloorEndpoints[LowerFloor].Leave = FIntVector::ZeroValue;
				State.FloorEndpoints[LowerFloor].bHasLeave = false;
				State.FloorEndpoints[LowerFloor + 1].Enter = FIntVector::ZeroValue;
				State.FloorEndpoints[LowerFloor + 1].bHasEnter = false;
				if (OutReport.Failure
					== EZeroEscapeGenerationFailure::SolverBudgetExhausted)
				{
					return false;
				}
			}
			return false;
		}

		bool PlaceRequiredRoute(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			const int32 WholeAttempt,
			FPlacementState& State,
			FWholeLayoutBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			struct FStartCandidate
			{
				FIntVector Address = FIntVector::ZeroValue;
				int32 EdgeDepth = 0;
				uint64 TieBreak = 0;
			};
			TArray<FStartCandidate> Starts;
			for (int32 Y = 0; Y < State.GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < State.GridSize.X; ++X)
				{
					FStartCandidate Candidate;
					Candidate.Address = FIntVector(X, Y, 0);
					Candidate.EdgeDepth = EdgeDepth(Candidate.Address, State.GridSize);
					Candidate.TieBreak = MakeCandidateTieBreak(
						Input,
						WholeAttempt,
						0,
						FName(TEXT("PursuerSpawn")),
						Candidate.Address,
						0,
						NAME_None);
					Starts.Add(Candidate);
				}
			}
			Starts.Sort([](const FStartCandidate& A, const FStartCandidate& B)
			{
				if (A.EdgeDepth != B.EdgeDepth)
				{
					return A.EdgeDepth < B.EdgeDepth;
				}
				return A.TieBreak < B.TieBreak;
			});

			for (const FStartCandidate& Start : Starts)
			{
				if (!ConsumeStructureCandidate(Budget, OutReport))
				{
					return false;
				}
				FReservedCell* Cell = FindReservation(State, Start.Address);
				if (Cell == nullptr || Cell->Use != EReservedCellUse::Free)
				{
					continue;
				}
				Cell->Use = EReservedCellUse::PursuerSpawn;
				State.PursuerSpawn = Start.Address;
				State.FloorEndpoints[0].Enter = Start.Address;
				State.FloorEndpoints[0].bHasEnter = true;
				State.ProtectedEndpoints.Add(Start.Address);

				if (PlaceRequiredStairsRecursive(
						Input,
						FloorOption,
						WholeAttempt,
						0,
						State,
						Budget,
						OutReport))
				{
					return true;
				}

				*Cell = {};
				State.PursuerSpawn = FIntVector::ZeroValue;
				State.FloorEndpoints[0].Enter = FIntVector::ZeroValue;
				State.FloorEndpoints[0].bHasEnter = false;
				State.ProtectedEndpoints.Reset();
				if (OutReport.Failure
					== EZeroEscapeGenerationFailure::SolverBudgetExhausted)
				{
					return false;
				}
			}
			return false;
		}

		int32 DrawAdditionalTwoFloorStairCount(
			const FResolvedGenerationInput& Input,
			const int32 LowerFloor)
		{
			const FZeroEscapeAdditionalTwoFloorStairWeights& Weights =
				Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair;
			const int64 TotalWeight64 =
				static_cast<int64>(Weights.ZeroAdditionalWeight)
				+ Weights.OneAdditionalWeight
				+ Weights.TwoAdditionalWeight;
			if (TotalWeight64 <= 0 || TotalWeight64 > MAX_int32)
			{
				return 0;
			}
			const int32 TotalWeight = static_cast<int32>(TotalWeight64);
			FRandomStream Random = FGenerationCore::MakeRandomStream(
				Input.Signature.Seed,
				ERandomDomain::AdditionalTwoFloorStairCount,
				LowerFloor);
			int32 Roll = Random.RandHelper(TotalWeight);
			Roll -= Weights.ZeroAdditionalWeight;
			if (Roll < 0)
			{
				return 0;
			}
			Roll -= Weights.OneAdditionalWeight;
			return Roll < 0 ? 1 : 2;
		}

		void BuildAdditionalTwoFloorStairPairOrder(
			const FResolvedGenerationInput& Input,
			const int32 FloorCount,
			const int32 WholeAttempt,
			const TConstArrayView<int32> TargetByLowerFloor,
			TArray<int32>& OutLowerFloorOrder)
		{
			OutLowerFloorOrder.Reset();
			const int32 FloorPairCount = FloorCount - 1;
			if (FloorPairCount <= 0
				|| TargetByLowerFloor.Num() != FloorPairCount)
			{
				return;
			}

			for (int32 AdditionalRound = 0; AdditionalRound < 2; ++AdditionalRound)
			{
				uint32 Salt = 0x41B9A11Du;
				Salt ^= static_cast<uint32>(WholeAttempt) * 0x9E3779B9u;
				Salt ^= static_cast<uint32>(AdditionalRound) * 0x85EBCA6Bu;
				FRandomStream Random = FGenerationCore::MakeRandomStream(
					Input.Signature.Seed,
					ERandomDomain::AdditionalTwoFloorStairPlacement,
					static_cast<int32>(Salt));
				const int32 FirstLowerFloor = Random.RandHelper(FloorPairCount);
				for (int32 Offset = 0; Offset < FloorPairCount; ++Offset)
				{
					const int32 LowerFloor =
						(FirstLowerFloor + Offset) % FloorPairCount;
					if (TargetByLowerFloor[LowerFloor] > AdditionalRound)
					{
						OutLowerFloorOrder.Add(LowerFloor);
					}
				}
			}
		}

		double ComputeMinimumLandingDistanceSquared(
			const FStructureCandidate& Candidate,
			const TConstArrayView<FIntVector> ExistingEndpoints)
		{
			double Minimum = TNumericLimits<double>::Max();
			bool bCompared = false;
			for (const FZeroEscapeGeneratedStructureLanding& Landing : Candidate.Landings)
			{
				for (const FIntVector Endpoint : ExistingEndpoints)
				{
					if (Landing.Coordinate.Z != Endpoint.Z)
					{
						continue;
					}
					Minimum = FMath::Min(
						Minimum,
						DistanceSquared2D(Landing.Coordinate, Endpoint));
					bCompared = true;
				}
			}
			return bCompared ? Minimum : -1.0;
		}

		enum class EOptionalPlacementResult : uint8
		{
			Placed,
			Skipped,
			Fatal
		};

		EOptionalPlacementResult TryPlaceOptionalStair(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			const EZeroEscapeStructureKind Kind,
			const int32 RequiredBaseFloor,
			const int32 WholeAttempt,
			const int32 SequenceIndex,
			FPlacementState& State,
			FWholeLayoutBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			const ERandomDomain Domain = Kind == EZeroEscapeStructureKind::TwoFloorStair
				? ERandomDomain::AdditionalTwoFloorStairPlacement
				: ERandomDomain::ThreeFloorStairwellPlacement;
			TArray<FStructureCandidate> Candidates;
			if (!EnumerateStructureCandidates(
					Input,
					Kind,
					RequiredBaseFloor,
					WholeAttempt,
					SequenceIndex,
					Domain,
					FloorOption,
					State,
					Budget,
					Candidates,
					OutReport))
			{
				if (OutReport.Failure == EZeroEscapeGenerationFailure::None
					|| OutReport.Failure
						== EZeroEscapeGenerationFailure::SolverBudgetExhausted)
				{
					OutReport = {};
					return EOptionalPlacementResult::Skipped;
				}
				return EOptionalPlacementResult::Fatal;
			}

			for (FStructureCandidate& Candidate : Candidates)
			{
				Candidate.PrimaryDistanceSquared = ComputeMinimumLandingDistanceSquared(
					Candidate,
					State.ProtectedEndpoints);
				Candidate.EdgeDepth = 0;
				for (const FZeroEscapeGeneratedStructureLanding& Landing : Candidate.Landings)
				{
					Candidate.EdgeDepth = FMath::Max(
						Candidate.EdgeDepth,
						EdgeDepth(Landing.Coordinate, State.GridSize));
				}
			}
			Candidates.Sort([](const FStructureCandidate& A, const FStructureCandidate& B)
			{
				if (A.PrimaryDistanceSquared != B.PrimaryDistanceSquared)
				{
					return A.PrimaryDistanceSquared > B.PrimaryDistanceSquared;
				}
				if (A.EdgeDepth != B.EdgeDepth)
				{
					return A.EdgeDepth < B.EdgeDepth;
				}
				return A.StableTieBreak < B.StableTieBreak;
			});
			if (Candidates.IsEmpty() || Candidates[0].PrimaryDistanceSquared < 0.0)
			{
				return EOptionalPlacementResult::Skipped;
			}

			const double SeparationRatio = FMath::Sqrt(
				Candidates[0].PrimaryDistanceSquared
				/ GridDiagonalSquared(State.GridSize));
			if (SeparationRatio + UE_DOUBLE_SMALL_NUMBER
				< Input.Difficulty.MinAdditionalStairSeparationRatio)
			{
				return EOptionalPlacementResult::Skipped;
			}

			int32 StableId = INDEX_NONE;
			if (!CommitCandidate(Candidates[0], State, StableId, OutReport))
			{
				return EOptionalPlacementResult::Fatal;
			}
			return EOptionalPlacementResult::Placed;
		}

		bool PlaceOptionalStairs(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			const int32 WholeAttempt,
			FPlacementState& State,
			FWholeLayoutBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int32 FloorPairCount = State.FloorCount - 1;
			TArray<int32> TargetByLowerFloor;
			TargetByLowerFloor.Reserve(FloorPairCount);
			for (int32 LowerFloor = 0; LowerFloor < FloorPairCount; ++LowerFloor)
			{
				TargetByLowerFloor.Add(
					DrawAdditionalTwoFloorStairCount(Input, LowerFloor));
			}
			TArray<int32> LowerFloorOrder;
			BuildAdditionalTwoFloorStairPairOrder(
				Input,
				State.FloorCount,
				WholeAttempt,
				TargetByLowerFloor,
				LowerFloorOrder);
			TArray<int32> AttemptCountByLowerFloor;
			AttemptCountByLowerFloor.Init(0, FloorPairCount);
			TArray<bool> bPairCannotFit;
			bPairCannotFit.Init(false, FloorPairCount);
			int32 AdditionalPlaced = 0;
			for (const int32 LowerFloor : LowerFloorOrder)
			{
				if (AdditionalPlaced >= FloorOption.MaxAdditionalTwoFloorStairCount)
				{
					break;
				}
				if (bPairCannotFit[LowerFloor])
				{
					continue;
				}
				const int32 AdditionalIndex =
					AttemptCountByLowerFloor[LowerFloor]++;
				const EOptionalPlacementResult Result = TryPlaceOptionalStair(
					Input,
					FloorOption,
					EZeroEscapeStructureKind::TwoFloorStair,
					LowerFloor,
					WholeAttempt,
					LowerFloor * 2 + AdditionalIndex,
					State,
					Budget,
					OutReport);
				if (Result == EOptionalPlacementResult::Fatal)
				{
					return false;
				}
				if (Result == EOptionalPlacementResult::Skipped)
				{
					bPairCannotFit[LowerFloor] = true;
					continue;
				}
				++AdditionalPlaced;
			}

			if (State.FloorCount >= 3
				&& Input.Difficulty.ThreeFloorStairwellChancePercent > 0)
			{
				FRandomStream Random = FGenerationCore::MakeRandomStream(
					Input.Signature.Seed,
					ERandomDomain::ThreeFloorStairwellPlacement,
					-1);
				if (Random.RandRange(1, 100)
					<= Input.Difficulty.ThreeFloorStairwellChancePercent)
				{
					const EOptionalPlacementResult Result = TryPlaceOptionalStair(
						Input,
						FloorOption,
						EZeroEscapeStructureKind::ThreeFloorStairwell,
						INDEX_NONE,
						WholeAttempt,
						0,
						State,
						Budget,
						OutReport);
					if (Result == EOptionalPlacementResult::Fatal)
					{
						return false;
					}
				}
			}
			return true;
		}

		// —— 高天花板房间数量抽取与完整占地放置 ——
		int32 DrawHighCeilingRoomTarget(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption)
		{
			int64 TotalWeight = 0;
			for (const FZeroEscapeWeightedCount& Option :
				FloorOption.HighCeilingRoomTargetCounts)
			{
				if (Option.Weight > 0)
				{
					TotalWeight += Option.Weight;
				}
			}
			if (TotalWeight <= 0 || TotalWeight > MAX_int32)
			{
				return Input.Difficulty.HighCeilingRooms.MinimumTotalCount;
			}

			FRandomStream Random = FGenerationCore::MakeRandomStream(
				Input.Signature.Seed,
				ERandomDomain::HighCeilingRoomCount,
				FloorOption.FloorCount);
			int32 Roll = Random.RandHelper(static_cast<int32>(TotalWeight));
			for (const FZeroEscapeWeightedCount& Option :
				FloorOption.HighCeilingRoomTargetCounts)
			{
				if (Option.Weight <= 0)
				{
					continue;
				}
				Roll -= Option.Weight;
				if (Roll < 0)
				{
					return Option.Count;
				}
			}
			return Input.Difficulty.HighCeilingRooms.MinimumTotalCount;
		}

		double ComputeHighRoomSeparationSquared(
			const FStructureCandidate& Candidate,
			const FPlacementState& State)
		{
			double Minimum = GridDiagonalSquared(State.GridSize);
			for (const FZeroEscapeGeneratedStructure& Structure : State.Structures)
			{
				if (Structure.Kind != EZeroEscapeStructureKind::HighCeilingRoom
					|| Structure.BaseCoordinate.Z != Candidate.BaseCoordinate.Z)
				{
					continue;
				}
				const double FootprintDistanceSquared =
					ComputeWalkableFootprintDistanceSquared(
						Candidate.WalkableCells,
						Structure.WalkableCells);
				if (FootprintDistanceSquared < 0.0)
				{
					return -1.0;
				}
				Minimum = FMath::Min(Minimum, FootprintDistanceSquared);
			}
			return Minimum;
		}

		bool TryPlaceExactHighCeilingRoomCount(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			const int32 WholeAttempt,
			const int32 RemainingCount,
			FPlacementState& State,
			FWholeLayoutBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (RemainingCount == 0)
			{
				return true;
			}

			TArray<FStructureCandidate> Candidates;
			if (!EnumerateStructureCandidates(
					Input,
					EZeroEscapeStructureKind::HighCeilingRoom,
					INDEX_NONE,
					WholeAttempt,
					RemainingCount,
					ERandomDomain::HighCeilingRoomPlacement,
					FloorOption,
					State,
					Budget,
					Candidates,
					OutReport))
			{
				return false;
			}

			bool bHasPlacedHighRoom = false;
			for (const FZeroEscapeGeneratedStructure& Structure : State.Structures)
			{
				bHasPlacedHighRoom |=
					Structure.Kind == EZeroEscapeStructureKind::HighCeilingRoom;
			}
			for (FStructureCandidate& Candidate : Candidates)
			{
				if (!bHasPlacedHighRoom
					&& Candidate.BaseCoordinate.Z == State.FloorCount - 1)
				{
					Candidate.PrimaryDistanceSquared = -1.0;
					continue;
				}
				if (State.HighCeilingRoomCountByFloor[Candidate.BaseCoordinate.Z]
					>= Input.Difficulty.HighCeilingRooms.MaxCountPerFloor)
				{
					Candidate.PrimaryDistanceSquared = -1.0;
					continue;
				}
				Candidate.PrimaryDistanceSquared =
					ComputeHighRoomSeparationSquared(Candidate, State);
				Candidate.EdgeDepth = EdgeDepth(
					Candidate.BaseCoordinate, State.GridSize);
			}
			Candidates.Sort([](const FStructureCandidate& A, const FStructureCandidate& B)
			{
				if (A.PrimaryDistanceSquared != B.PrimaryDistanceSquared)
				{
					return A.PrimaryDistanceSquared > B.PrimaryDistanceSquared;
				}
				return A.StableTieBreak < B.StableTieBreak;
			});

			for (const FStructureCandidate& Candidate : Candidates)
			{
				if (Candidate.PrimaryDistanceSquared < 0.0)
				{
					continue;
				}
				const int32 PreviousProtectedCount = State.ProtectedEndpoints.Num();
				int32 StableId = INDEX_NONE;
				if (!CommitCandidate(Candidate, State, StableId, OutReport))
				{
					return false;
				}
				if (TryPlaceExactHighCeilingRoomCount(
						Input,
						FloorOption,
						WholeAttempt,
						RemainingCount - 1,
						State,
						Budget,
						OutReport))
				{
					return true;
				}
				if (!RollbackLastStructure(
						State, PreviousProtectedCount, OutReport))
				{
					return false;
				}
				if (OutReport.Failure
					== EZeroEscapeGenerationFailure::SolverBudgetExhausted)
				{
					return false;
				}
			}
			return false;
		}

		bool PlaceHighCeilingRooms(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			const int32 WholeAttempt,
			FPlacementState& State,
			FWholeLayoutBudget& Budget,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int32 Minimum = FMath::Max(
				2,
				Input.Difficulty.HighCeilingRooms.MinimumTotalCount);
			const int32 Target = FMath::Max(
				Minimum,
				DrawHighCeilingRoomTarget(Input, FloorOption));
			if (Minimum < 0 || Target < Minimum)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("高天花板房间目标数量低于整栋最低数量。"),
					Target,
					Minimum);
			}

			const int32 BaselineStructureCount = State.Structures.Num();
			const int32 BaselineProtectedCount = State.ProtectedEndpoints.Num();
			for (int32 Desired = Target; Desired >= Minimum; --Desired)
			{
				while (State.Structures.Num() > BaselineStructureCount)
				{
					if (!RollbackLastStructure(
							State, BaselineProtectedCount, OutReport))
					{
						return false;
					}
				}
				if (TryPlaceExactHighCeilingRoomCount(
						Input,
						FloorOption,
						WholeAttempt,
						Desired,
						State,
						Budget,
						OutReport))
				{
					return true;
				}
				if (OutReport.Failure
					== EZeroEscapeGenerationFailure::SolverBudgetExhausted)
				{
					return false;
				}
			}

			while (State.Structures.Num() > BaselineStructureCount)
			{
				if (!RollbackLastStructure(
						State, BaselineProtectedCount, OutReport))
				{
					return false;
				}
			}
			return false;
		}

		// —— 各层二维约束投影与现有 WFC 求解器复用 ——
		bool FindHorizontalDirection(
			const FIntVector From,
			const FIntVector To,
			uint8& OutDirection)
		{
			if (From.Z != To.Z)
			{
				return false;
			}
			for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
			{
				if (StepAddress(From, Direction) == To)
				{
					OutDirection = Direction;
					return true;
				}
			}
			return false;
		}

		bool RequireHorizontalOpening(
			const FIntVector First,
			const FIntVector Second,
			const int32 FloorIndex,
			const FIntPoint GridSize,
			TArray<FGridCellConstraint>& Constraints,
			FZeroEscapeGenerationReport& OutReport)
		{
			uint8 Direction = 0;
			if (First.Z != FloorIndex
				|| Second.Z != FloorIndex
				|| !Grid::IsInside(FIntPoint(First.X, First.Y), GridSize)
				|| !Grid::IsInside(FIntPoint(Second.X, Second.Y), GridSize)
				|| !FindHorizontalDirection(First, Second, Direction))
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("结构投影的同层连接不是四邻格边。"));
			}

			const int32 FirstIndex = Grid::ToIndex(
				FIntPoint(First.X, First.Y), GridSize);
			const int32 SecondIndex = Grid::ToIndex(
				FIntPoint(Second.X, Second.Y), GridSize);
			FGridCellConstraint& FirstCell = Constraints[FirstIndex];
			FGridCellConstraint& SecondCell = Constraints[SecondIndex];
			if (FirstCell.Domain == EGridCellDomain::Outside
				|| SecondCell.Domain == EGridCellDomain::Outside)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::StructurePlacement,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("结构必开边指向了 Solid 或 Clearance 格。"));
			}

			const uint8 FirstBit = Grid::DirectionBit(Direction);
			const uint8 SecondBit = Grid::DirectionBit(
				Grid::OppositeDirectionIndex(Direction));
			FirstCell.Domain = EGridCellDomain::Required;
			SecondCell.Domain = EGridCellDomain::Required;
			FirstCell.RequiredClosedMask &= ~FirstBit;
			SecondCell.RequiredClosedMask &= ~SecondBit;
			FirstCell.RequiredOpenMask |= FirstBit;
			SecondCell.RequiredOpenMask |= SecondBit;
			return true;
		}

		bool BuildFloorProjection(
			const FResolvedGenerationInput& Input,
			const int32 WholeAttempt,
			const FPlacementState& State,
			const int32 FloorIndex,
			FFloorSolveRecord& OutRecord,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutRecord = {};
			FZeroEscapeConstrainedFloorInput& FloorInput = OutRecord.Input;
			FloorInput.Signature = Input.Signature;
			FloorInput.WholeLayoutAttemptIndex = WholeAttempt;
			FloorInput.FloorIndex = FloorIndex;
			FloorInput.GridSize = State.GridSize;
			FloorInput.RequiredEnterCoordinate = FIntPoint(
				State.FloorEndpoints[FloorIndex].Enter.X,
				State.FloorEndpoints[FloorIndex].Enter.Y);
			FloorInput.RequiredLeaveCoordinate = FIntPoint(
				State.FloorEndpoints[FloorIndex].Leave.X,
				State.FloorEndpoints[FloorIndex].Leave.Y);
			FloorInput.MinTotalWalkableCellCount = 1;
			FloorInput.MaxTotalWalkableCellCount =
				State.GridSize.X * State.GridSize.Y;
			FloorInput.MinOrdinaryWalkableCellCount = FloorIndex == 0 ? 2 : 1;
			FloorInput.MaxConsecutiveStraightTiles =
				FMath::Max(State.GridSize.X, State.GridSize.Y);
			FloorInput.PreferredMaxConsecutiveStraightTiles =
				Input.SharedRules.MaxConsecutiveStraightTiles;
			FloorInput.MaxSolveAttemptsForThisFloor =
				FMath::Min(
					Input.Budget.MaxWfcSolveAttemptsPerFloor,
					GenerationLimits::MaxWfcSolveAttemptsPerFloor);
			FloorInput.PreferredRouteCoverageRatio =
				Input.Difficulty.MinRequiredRouteCoverageRatio;
			FloorInput.PreferredRewardBranchCellRatio =
				Input.Difficulty.PreferredRewardBranchCellRatio;
			FloorInput.PreferredAlternativeRouteCoverageRatio =
				Input.Difficulty.PreferredAlternativeRouteCoverageRatio;
			FloorInput.RouteQualityEarlyAcceptThreshold =
				Input.SharedRules.RouteQualityEarlyAcceptThreshold;
			FloorInput.RouteOpeningPreferenceLog2Strength =
				Input.SharedRules.RouteOpeningPreferenceLog2Strength;
			FloorInput.MinimumRewardBranchLengthTiles =
				Input.SharedRules.MinimumRewardBranchLengthTiles;
			FloorInput.MaximumPreferredRewardBranchLengthTiles =
				Input.SharedRules.MaximumPreferredRewardBranchLengthTiles;
			FloorInput.PreferredRewardBranchCount =
				Input.Difficulty.PreferredRewardBranchCount;

			const int32 CellCount = State.GridSize.X * State.GridSize.Y;
			FloorInput.Constraints.SetNum(CellCount);
			FloorInput.StructureWalkableByCell.Init(0, CellCount);
			for (int32 Y = 0; Y < State.GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < State.GridSize.X; ++X)
				{
					const FIntPoint Coordinate(X, Y);
					const int32 DenseIndex = Grid::ToIndex(Coordinate, State.GridSize);
					FGridCellConstraint& Constraint =
						FloorInput.Constraints[DenseIndex];
					Constraint.Coordinate = Coordinate;
					Constraint.Domain = EGridCellDomain::Optional;
					for (uint8 Direction = 0;
						Direction < Grid::DirectionCount;
						++Direction)
					{
						if (!Grid::IsInside(
								Grid::Step(Coordinate, Direction), State.GridSize))
						{
							Constraint.RequiredClosedMask |=
								Grid::DirectionBit(Direction);
						}
					}

					const FReservedCell* Reservation = FindReservation(
						State, FIntVector(X, Y, FloorIndex));
					if (Reservation == nullptr)
					{
						return FailLayout(
							OutReport,
							EZeroEscapeGenerationStage::StructurePlacement,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("构建单层投影时访问了整栋索引外的格。"));
					}
					switch (Reservation->Use)
					{
					case EReservedCellUse::StructureWalkable:
						Constraint.Domain = EGridCellDomain::Required;
						FloorInput.StructureWalkableByCell[DenseIndex] = 1;
						break;
					case EReservedCellUse::StructureSolid:
					case EReservedCellUse::Clearance:
						Constraint.Domain = EGridCellDomain::Outside;
						Constraint.RequiredClosedMask = Grid::AllOpenEdges;
						break;
					default:
						break;
					}
				}
			}

			for (const FZeroEscapeGeneratedStructure& Structure : State.Structures)
			{
				for (const FZeroEscapeGeneratedCellConnection& Connection :
					Structure.InternalConnections)
				{
					if (Connection.FirstCoordinate.Z == FloorIndex
						&& Connection.SecondCoordinate.Z == FloorIndex
						&& !RequireHorizontalOpening(
							Connection.FirstCoordinate,
							Connection.SecondCoordinate,
							FloorIndex,
							State.GridSize,
							FloorInput.Constraints,
							OutReport))
					{
						return false;
					}
				}
				for (const FZeroEscapeGeneratedStructureOpening& Opening :
					Structure.Openings)
				{
					if (Opening.StructureCoordinate.Z == FloorIndex
						&& !RequireHorizontalOpening(
							Opening.StructureCoordinate,
							Opening.ConnectedOrdinaryCoordinate,
							FloorIndex,
							State.GridSize,
							FloorInput.Constraints,
							OutReport))
					{
						return false;
					}
				}
			}

			const FIntPoint Endpoints[] = {
				FloorInput.RequiredEnterCoordinate,
				FloorInput.RequiredLeaveCoordinate };
			for (const FIntPoint Endpoint : Endpoints)
			{
				FGridCellConstraint& Constraint = FloorInput.Constraints[
					Grid::ToIndex(Endpoint, State.GridSize)];
				if (Constraint.Domain == EGridCellDomain::Outside)
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("本层必经进入点或离开点被投影成了禁用格。"));
				}
				Constraint.Domain = EGridCellDomain::Required;
			}

			for (int32 DenseIndex = 0; DenseIndex < CellCount; ++DenseIndex)
			{
				FGridCellConstraint& Constraint =
					FloorInput.Constraints[DenseIndex];
				if (FloorInput.StructureWalkableByCell[DenseIndex] != 0)
				{
					Constraint.RequiredClosedMask |= static_cast<uint8>(
						Grid::AllOpenEdges & ~Constraint.RequiredOpenMask);
				}
				if ((Constraint.RequiredOpenMask
						& Constraint.RequiredClosedMask) != 0)
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("单层投影在同一方向同时要求开放和封闭。"));
				}
			}
			return true;
		}

		int32 CountProjectionConstraints(const FFloorSolveRecord& Record)
		{
			int32 Count = 0;
			for (const FGridCellConstraint& Constraint : Record.Input.Constraints)
			{
				Count += Constraint.Domain != EGridCellDomain::Optional ? 1 : 0;
				Count += FMath::CountBits(
					static_cast<uint32>(Constraint.RequiredOpenMask));
				Count += FMath::CountBits(
					static_cast<uint32>(Constraint.RequiredClosedMask));
			}
			return Count;
		}

		bool SolveFloors(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			const int32 WholeAttempt,
			const FPlacementState& State,
			FWholeLayoutBudget& Budget,
			TArray<FFloorSolveRecord>& OutRecords,
			FZeroEscapeGenerationMetrics& InOutMetrics,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutRecords.SetNum(State.FloorCount);
			TArray<int32> SolveOrder;
			for (int32 Floor = 0; Floor < State.FloorCount; ++Floor)
			{
				if (!State.FloorEndpoints[Floor].bHasEnter
					|| !State.FloorEndpoints[Floor].bHasLeave
					|| !BuildFloorProjection(
						Input,
						WholeAttempt,
						State,
						Floor,
						OutRecords[Floor],
						OutReport))
				{
					return false;
				}
				SolveOrder.Add(Floor);
			}

			const int32 PreferredBuildingTotal =
				(FloorOption.MinTotalWalkableCellCount
					+ FloorOption.MaxTotalWalkableCellCount) / 2;
			const int32 PreferredPerFloor = FMath::Max(
				1, PreferredBuildingTotal / State.FloorCount);
			for (int32 Floor = 0; Floor < State.FloorCount; ++Floor)
			{
				FZeroEscapeConstrainedFloorInput& FloorInput = OutRecords[Floor].Input;
				int32 FixedStructureWalkableCount = 0;
				int32 PossibleWalkableCount = 0;
				for (int32 DenseIndex = 0;
					DenseIndex < FloorInput.Constraints.Num();
					++DenseIndex)
				{
					FixedStructureWalkableCount +=
						FloorInput.StructureWalkableByCell[DenseIndex] != 0 ? 1 : 0;
					PossibleWalkableCount +=
						FloorInput.Constraints[DenseIndex].Domain
							!= EGridCellDomain::Outside ? 1 : 0;
				}
				FloorInput.PreferredOrdinaryWalkableCellCount = FMath::Clamp(
					FloorOption.MinOrdinaryWalkableCellCountPerFloor,
					FloorInput.MinOrdinaryWalkableCellCount,
					FMath::Max(
						FloorInput.MinOrdinaryWalkableCellCount,
						PossibleWalkableCount - FixedStructureWalkableCount));
				FloorInput.PreferredTotalWalkableCellCount = FMath::Clamp(
					FMath::Max(
						PreferredPerFloor,
						FixedStructureWalkableCount
							+ FloorInput.PreferredOrdinaryWalkableCellCount),
					FixedStructureWalkableCount
						+ FloorInput.MinOrdinaryWalkableCellCount,
					PossibleWalkableCount);
			}

			SolveOrder.Sort([&OutRecords](const int32 A, const int32 B)
			{
				const int32 ACount = CountProjectionConstraints(OutRecords[A]);
				const int32 BCount = CountProjectionConstraints(OutRecords[B]);
				return ACount != BCount ? ACount > BCount : A < B;
			});

			TArray<int32> BaseMinimumByFloor;
			TArray<int32> PossibleMaximumByFloor;
			BaseMinimumByFloor.Init(0, State.FloorCount);
			PossibleMaximumByFloor.Init(0, State.FloorCount);
			for (int32 Floor = 0; Floor < State.FloorCount; ++Floor)
			{
				int32 FixedStructureWalkableCount = 0;
				for (int32 DenseIndex = 0;
					DenseIndex < OutRecords[Floor].Input.Constraints.Num();
					++DenseIndex)
				{
					FixedStructureWalkableCount +=
						OutRecords[Floor].Input.StructureWalkableByCell[DenseIndex]
						!= 0 ? 1 : 0;
					PossibleMaximumByFloor[Floor] +=
						OutRecords[Floor].Input.Constraints[DenseIndex].Domain
						!= EGridCellDomain::Outside ? 1 : 0;
				}
				BaseMinimumByFloor[Floor] = FixedStructureWalkableCount
					+ OutRecords[Floor].Input.MinOrdinaryWalkableCellCount;
				if (BaseMinimumByFloor[Floor] > PossibleMaximumByFloor[Floor])
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::CapacityInsufficient,
						TEXT("结构投影后某层无法保留每层普通内容下限。"),
						PossibleMaximumByFloor[Floor],
						BaseMinimumByFloor[Floor]);
				}
			}

			for (const int32 Floor : SolveOrder)
			{
				FZeroEscapeConstrainedFloorInput& FloorInput =
					OutRecords[Floor].Input;
				FloorInput.MinTotalWalkableCellCount = BaseMinimumByFloor[Floor];
				FloorInput.MaxTotalWalkableCellCount = PossibleMaximumByFloor[Floor];
				if (FloorInput.MaxTotalWalkableCellCount
					< FloorInput.MinTotalWalkableCellCount)
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::CapacityInsufficient,
						TEXT("已解楼层数量与剩余楼层容量无法同时满足整栋总可走量范围。"),
						FloorInput.MinTotalWalkableCellCount,
						FloorInput.MaxTotalWalkableCellCount);
				}
				FZeroEscapeGenerationReport FloorReport;
				if (!FGridLayoutSolver::SolveConstrainedFloor(
						OutRecords[Floor].Input,
						Input.WfcShapeWeights,
						Budget.Wfc,
						OutRecords[Floor].Result,
						FloorReport))
				{
					AccumulateWfcMetrics(InOutMetrics, FloorReport.Metrics);
					if (!AccumulateFloorWfcMetrics(
							InOutMetrics, Floor, FloorReport.Metrics, FloorReport))
					{
						OutReport = MoveTemp(FloorReport);
						return false;
					}
					FloorReport.Metrics = InOutMetrics;
					OutReport = MoveTemp(FloorReport);
					return false;
				}
				AccumulateWfcMetrics(InOutMetrics, FloorReport.Metrics);
				if (!AccumulateFloorWfcMetrics(
						InOutMetrics, Floor, FloorReport.Metrics, FloorReport))
				{
					OutReport = MoveTemp(FloorReport);
					return false;
				}
			}
			return true;
		}

		FZeroEscapeJunctionMetrics MeasureJunctions(
			const TConstArrayView<uint8> OpeningMasks)
		{
			FZeroEscapeJunctionMetrics Metrics;
			for (const uint8 Mask : OpeningMasks)
			{
				const int32 Degree = FMath::CountBits(static_cast<uint32>(Mask));
				switch (Degree)
				{
				case 1:
					++Metrics.DeadEndCount;
					break;
				case 2:
					if (Mask == (Grid::DirectionBit(0) | Grid::DirectionBit(2))
						|| Mask == (Grid::DirectionBit(1) | Grid::DirectionBit(3)))
					{
						++Metrics.StraightCount;
					}
					else
					{
						++Metrics.CornerCount;
					}
					break;
				case 3:
					++Metrics.TJunctionCount;
					break;
				case 4:
					++Metrics.CrossJunctionCount;
					break;
				default:
					break;
				}
			}
			return Metrics;
		}

		// —— 合并逐层结果并验收整栋无向通行图 ——
		void AddJunctionMetrics(
			FZeroEscapeJunctionMetrics& InOutTotal,
			const FZeroEscapeJunctionMetrics& Source)
		{
			InOutTotal.DeadEndCount += Source.DeadEndCount;
			InOutTotal.StraightCount += Source.StraightCount;
			InOutTotal.CornerCount += Source.CornerCount;
			InOutTotal.TJunctionCount += Source.TJunctionCount;
			InOutTotal.CrossJunctionCount += Source.CrossJunctionCount;
		}

		int32 CountHorizontalEdges(
			const TConstArrayView<uint8> OpeningMasks)
		{
			int32 EdgeCount = 0;
			for (int32 DenseIndex = 0; DenseIndex < OpeningMasks.Num(); ++DenseIndex)
			{
				const uint8 Mask = OpeningMasks[DenseIndex];
				EdgeCount += (Mask & Grid::DirectionBit(0)) != 0 ? 1 : 0;
				EdgeCount += (Mask & Grid::DirectionBit(1)) != 0 ? 1 : 0;
			}
			return EdgeCount;
		}

		bool BuildPlanFloorData(
			const FResolvedGenerationInput& Input,
			const FPlacementState& State,
			const TArray<FFloorSolveRecord>& Records,
			FZeroEscapeGeneratedLevelPlan& InOutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			InOutPlan.OrdinaryCells.Reset();
			InOutPlan.Floors.SetNum(State.FloorCount);
			const double DiagonalSquared = GridDiagonalSquared(State.GridSize);
			if (DiagonalSquared <= 0.0 || Records.Num() != State.FloorCount)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("单层结果数量或网格对角线无效。"));
			}

			for (int32 Floor = 0; Floor < State.FloorCount; ++Floor)
			{
				const FFloorSolveRecord& Record = Records[Floor];
				if (Record.Result.OpeningMaskByCell.Num()
						!= State.GridSize.X * State.GridSize.Y
					|| Record.Input.StructureWalkableByCell.Num()
						!= Record.Result.OpeningMaskByCell.Num())
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("单层 WFC 结果与结构投影数量不一致。"));
				}

				for (int32 DenseIndex = 0;
					DenseIndex < Record.Result.OpeningMaskByCell.Num();
					++DenseIndex)
				{
					const uint8 Mask = Record.Result.OpeningMaskByCell[DenseIndex];
					if (Mask == 0
						|| Record.Input.StructureWalkableByCell[DenseIndex] != 0)
					{
						continue;
					}
					FZeroEscapeGeneratedOrdinaryCell& Cell =
						InOutPlan.OrdinaryCells.AddDefaulted_GetRef();
					Cell.Coordinate = FIntVector(
						DenseIndex % State.GridSize.X,
						DenseIndex / State.GridSize.X,
						Floor);
					Cell.OpeningMask = Mask;
				}

				FZeroEscapeGeneratedFloorSummary& Summary = InOutPlan.Floors[Floor];
				Summary.FloorIndex = Floor;
				Summary.RequiredEnterCoordinate = State.FloorEndpoints[Floor].Enter;
				Summary.RequiredLeaveCoordinate = State.FloorEndpoints[Floor].Leave;
				Summary.OrdinaryWalkableCellCount =
					Record.Result.OrdinaryWalkableCellCount;
				Summary.TotalWalkableCellCount =
					Record.Result.TotalWalkableCellCount;
				Summary.RequiredRouteLengthTiles =
					Record.Result.RequiredRouteLengthTiles;
				Summary.FarthestRouteLengthTiles =
					Record.Result.FarthestRouteLengthTiles;
				Summary.SpatialSeparationRatio = FMath::Sqrt(
					DistanceSquared2D(
						Summary.RequiredEnterCoordinate,
						Summary.RequiredLeaveCoordinate)
					/ DiagonalSquared);
				Summary.RouteCoverageRatio = Record.Result.RouteCoverageRatio;
				Summary.RewardBranchCount = 0;
				Summary.OneCellTerminalSpurCount =
					Record.Result.OneCellTerminalSpurCount;
				Summary.RewardBranchCellRatio = 0.0;
				Summary.AlternativeRouteCoverageRatio =
					Record.Result.AlternativeRouteCoverageRatio;
				Summary.JunctionMetrics = MeasureJunctions(
					Record.Result.OpeningMaskByCell);
				Summary.CycleRank = CountHorizontalEdges(
						Record.Result.OpeningMaskByCell)
					- Summary.TotalWalkableCellCount + 1;
				if (Summary.CycleRank < 0)
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("单层图的环路数与连通图不变量不一致。"));
				}
				AddJunctionMetrics(InOutPlan.JunctionMetrics, Summary.JunctionMetrics);
			}
			return true;
		}

		uint64 MakeUndirectedEdgeKey(const int32 FirstNode, const int32 SecondNode)
		{
			const uint32 Lower = static_cast<uint32>(FMath::Min(FirstNode, SecondNode));
			const uint32 Upper = static_cast<uint32>(FMath::Max(FirstNode, SecondNode));
			return (static_cast<uint64>(Lower) << 32) | Upper;
		}

		bool AddGraphEdge(
			const int32 FirstNode,
			const int32 SecondNode,
			const TConstArrayView<uint8> WalkableByNode,
			TSet<uint64>& InOutEdgeKeys,
			TArray<TArray<int32>>& InOutNeighbors,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (FirstNode < 0 || SecondNode < 0
				|| FirstNode >= WalkableByNode.Num()
				|| SecondNode >= WalkableByNode.Num()
				|| FirstNode == SecondNode
				|| WalkableByNode[FirstNode] == 0
				|| WalkableByNode[SecondNode] == 0)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("整栋通行图尝试连接无效或不可走节点。"));
			}
			const uint64 Key = MakeUndirectedEdgeKey(FirstNode, SecondNode);
			if (!InOutEdgeKeys.Contains(Key))
			{
				InOutEdgeKeys.Add(Key);
				InOutNeighbors[FirstNode].Add(SecondNode);
				InOutNeighbors[SecondNode].Add(FirstNode);
			}
			return true;
		}

		bool ComputeShortestRoutes(
			const TArray<TArray<int32>>& Neighbors,
			const int32 StartNode,
			const int32 CellsPerFloor,
			TArray<int32>& OutDistanceByNode,
			TArray<int32>& OutVerticalTransitionsByNode)
		{
			if (!Neighbors.IsValidIndex(StartNode) || CellsPerFloor <= 0)
			{
				OutDistanceByNode.Reset();
				OutVerticalTransitionsByNode.Reset();
				return false;
			}

			OutDistanceByNode.Init(INDEX_NONE, Neighbors.Num());
			OutVerticalTransitionsByNode.Init(MAX_int32, Neighbors.Num());
			TQueue<int32> Queue;
			OutDistanceByNode[StartNode] = 0;
			OutVerticalTransitionsByNode[StartNode] = 0;
			Queue.Enqueue(StartNode);
			int32 Current = INDEX_NONE;
			while (Queue.Dequeue(Current))
			{
				for (const int32 Neighbor : Neighbors[Current])
				{
					const int32 CandidateDistance = OutDistanceByNode[Current] + 1;
					const bool bCrossFloor =
						Current / CellsPerFloor != Neighbor / CellsPerFloor;
					const int32 CandidateVertical =
						OutVerticalTransitionsByNode[Current] + (bCrossFloor ? 1 : 0);
					if (OutDistanceByNode[Neighbor] == INDEX_NONE
						|| CandidateDistance < OutDistanceByNode[Neighbor]
						|| (CandidateDistance == OutDistanceByNode[Neighbor]
							&& CandidateVertical
								< OutVerticalTransitionsByNode[Neighbor]))
					{
						OutDistanceByNode[Neighbor] = CandidateDistance;
						OutVerticalTransitionsByNode[Neighbor] = CandidateVertical;
						Queue.Enqueue(Neighbor);
					}
				}
			}
			return true;
		}

		bool BuildAndValidateWholeGraph(
			const FPlacementState& State,
			const TArray<FFloorSolveRecord>& Records,
			FWholeGraphResult& OutGraph,
			FZeroEscapeGeneratedLevelPlan& InOutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutGraph = {};
			const int32 CellsPerFloor = State.GridSize.X * State.GridSize.Y;
			const int32 NodeCount = CellsPerFloor * State.FloorCount;
			TArray<uint8> WalkableByNode;
			WalkableByNode.Init(0, NodeCount);
			TArray<TArray<int32>> Neighbors;
			Neighbors.SetNum(NodeCount);
			TSet<uint64> EdgeKeys;
			for (int32 Floor = 0; Floor < State.FloorCount; ++Floor)
			{
				if (!Records.IsValidIndex(Floor)
					|| Records[Floor].Result.OpeningMaskByCell.Num() != CellsPerFloor)
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("整栋图缺少某一层的稠密 WFC 结果。"));
				}
				for (int32 DenseIndex = 0; DenseIndex < CellsPerFloor; ++DenseIndex)
				{
					WalkableByNode[Floor * CellsPerFloor + DenseIndex] =
						Records[Floor].Result.OpeningMaskByCell[DenseIndex] != 0 ? 1 : 0;
				}
			}

			for (int32 Floor = 0; Floor < State.FloorCount; ++Floor)
			{
				for (int32 DenseIndex = 0; DenseIndex < CellsPerFloor; ++DenseIndex)
				{
					const uint8 Mask = Records[Floor].Result.OpeningMaskByCell[DenseIndex];
					const int32 Node = Floor * CellsPerFloor + DenseIndex;
					if (Mask == 0)
					{
						continue;
					}
					const FIntPoint Coordinate(
						DenseIndex % State.GridSize.X,
						DenseIndex / State.GridSize.X);
					for (uint8 Direction = 0;
						Direction < Grid::DirectionCount;
						++Direction)
					{
						const uint8 Bit = Grid::DirectionBit(Direction);
						if ((Mask & Bit) == 0)
						{
							continue;
						}
						const FIntPoint NeighborCoordinate =
							Grid::Step(Coordinate, Direction);
						if (!Grid::IsInside(NeighborCoordinate, State.GridSize))
						{
							return FailLayout(
								OutReport,
								EZeroEscapeGenerationStage::GlobalValidation,
								EZeroEscapeGenerationFailure::SolverInvariantViolation,
								TEXT("整栋图发现指向网格外的单层开口。"));
						}
						const int32 NeighborDense = Grid::ToIndex(
							NeighborCoordinate, State.GridSize);
						const uint8 OppositeBit = Grid::DirectionBit(
							Grid::OppositeDirectionIndex(Direction));
						if ((Records[Floor].Result.OpeningMaskByCell[NeighborDense]
								& OppositeBit) == 0)
						{
							return FailLayout(
								OutReport,
								EZeroEscapeGenerationStage::GlobalValidation,
								EZeroEscapeGenerationFailure::SolverInvariantViolation,
								TEXT("整栋图发现不对称的单层开口。"));
						}
						if (!AddGraphEdge(
								Node,
								Floor * CellsPerFloor + NeighborDense,
								WalkableByNode,
								EdgeKeys,
								Neighbors,
								OutReport))
						{
							return false;
						}
					}
				}
			}

			for (const FZeroEscapeGeneratedStructure& Structure : State.Structures)
			{
				for (const FZeroEscapeGeneratedCellConnection& Connection :
					Structure.InternalConnections)
				{
					if (Connection.FirstCoordinate.Z == Connection.SecondCoordinate.Z)
					{
						continue;
					}
					if (!IsInsideBuilding(Connection.FirstCoordinate, State)
						|| !IsInsideBuilding(Connection.SecondCoordinate, State)
						|| !AddGraphEdge(
							ToBuildingIndex(Connection.FirstCoordinate, State.GridSize),
							ToBuildingIndex(Connection.SecondCoordinate, State.GridSize),
							WalkableByNode,
							EdgeKeys,
							Neighbors,
							OutReport))
					{
						return false;
					}
				}
			}

			const int32 StartNode = ToBuildingIndex(State.PursuerSpawn, State.GridSize);
			const int32 ExitNode = ToBuildingIndex(State.Exit, State.GridSize);
			if (StartNode < 0 || StartNode >= NodeCount
				|| ExitNode < 0 || ExitNode >= NodeCount
				|| WalkableByNode[StartNode] == 0
				|| WalkableByNode[ExitNode] == 0)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("追猎者起点或顶层终点没有落在整栋可走图上。"));
			}

			TArray<int32> VerticalTransitions;
			if (!ComputeShortestRoutes(
					Neighbors,
					StartNode,
					CellsPerFloor,
					OutGraph.DistanceFromPursuerByNode,
					VerticalTransitions))
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("整栋通行图无法从追猎者起点计算最短路线。"));
			}

			int32 ReachableCount = 0;
			for (int32 Node = 0; Node < NodeCount; ++Node)
			{
				if (WalkableByNode[Node] == 0)
				{
					continue;
				}
				++OutGraph.TotalWalkableCellCount;
				ReachableCount +=
					OutGraph.DistanceFromPursuerByNode[Node] != INDEX_NONE ? 1 : 0;
			}
			if (ReachableCount != OutGraph.TotalWalkableCellCount)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::GlobalConnectivityFailed,
					TEXT("整栋可走图存在从追猎者起点无法到达的节点。"),
					ReachableCount,
					OutGraph.TotalWalkableCellCount);
			}
			for (const FZeroEscapeGeneratedStructure& Structure : State.Structures)
			{
				for (const FZeroEscapeGeneratedStructureLanding& Landing :
					Structure.Landings)
				{
					const int32 Node = ToBuildingIndex(
						Landing.Coordinate, State.GridSize);
					if (!OutGraph.DistanceFromPursuerByNode.IsValidIndex(Node)
						|| OutGraph.DistanceFromPursuerByNode[Node] == INDEX_NONE)
					{
						return FailLayout(
							OutReport,
							EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::GlobalConnectivityFailed,
							TEXT("至少一个楼梯落脚点不在整栋连通分量内。"),
							0,
							0,
							Structure.StableStructureId);
					}
				}
			}

			if (State.RequiredTwoFloorStairStableIdByLowerFloor.Num()
					!= State.FloorCount - 1)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("必需双层楼梯映射数量与相邻楼层对数不一致。"));
			}
			for (int32 LowerFloor = 0;
				LowerFloor + 1 < State.FloorCount;
				++LowerFloor)
			{
				const int32 StableId =
					State.RequiredTwoFloorStairStableIdByLowerFloor[LowerFloor];
				if (!State.Structures.IsValidIndex(StableId))
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("必需双层楼梯索引不存在。"),
						StableId,
						State.Structures.Num());
				}
				const FZeroEscapeGeneratedStructure& Stair = State.Structures[StableId];
				bool bConnectsPair = false;
				for (const FZeroEscapeGeneratedCellConnection& Connection :
					Stair.InternalConnections)
				{
					const int32 FirstFloor = Connection.FirstCoordinate.Z;
					const int32 SecondFloor = Connection.SecondCoordinate.Z;
					bConnectsPair |=
						(FirstFloor == LowerFloor && SecondFloor == LowerFloor + 1)
						|| (SecondFloor == LowerFloor && FirstFloor == LowerFloor + 1);
				}
				if (Stair.Kind != EZeroEscapeStructureKind::TwoFloorStair
					|| Stair.BaseCoordinate.Z != LowerFloor
					|| !bConnectsPair)
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("必需映射中的结构不是真正连接该相邻楼层对的双层楼梯。"),
						0,
						0,
						StableId);
				}
			}

			InOutPlan.CycleRank = EdgeKeys.Num() - OutGraph.TotalWalkableCellCount + 1;
			if (InOutPlan.CycleRank < 0)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("整栋已连通无向图的 E-V+1 为负数。"));
			}
			OutGraph.Neighbors = MoveTemp(Neighbors);
			return true;
		}

		// —— 追猎者占一层主路线起点；玩家选择满足安全距离的最近普通格 ——
		bool SelectPlayerSpawn(
			const FResolvedGenerationInput& Input,
			const FPlacementState& State,
			const TArray<FFloorSolveRecord>& Records,
			const FWholeGraphResult& Graph,
			FZeroEscapeGeneratedLevelPlan& InOutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (Records.IsEmpty()
				|| Input.SharedRules.LogicalTileSizeCm <= 0.0)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("玩家出生点选择缺少一层结果或逻辑格边长无效。"));
			}

			const FFloorSolveRecord& GroundFloor = Records[0];
			TSet<int32> CandidateRewardBranchCells;
			for (const FZeroEscapeGeneratedRewardBranch& Branch :
				GroundFloor.Result.CandidateRewardBranches)
			{
				for (const FIntVector Coordinate : Branch.PathCoordinates)
				{
					if (Coordinate.Z == 0)
					{
						CandidateRewardBranchCells.Add(Grid::ToIndex(
							FIntPoint(Coordinate.X, Coordinate.Y), State.GridSize));
					}
				}
			}

			const auto FindBestSpawn = [&](const bool bProtectRewardBranches)
			{
				int32 BestDenseIndex = INDEX_NONE;
				int32 BestRouteDistance = MAX_int32;
				uint32 BestTieBreak = MAX_uint32;
				int32 FallbackDenseIndex = INDEX_NONE;
				int32 FallbackRouteDistance = INDEX_NONE;
				uint32 FallbackTieBreak = MAX_uint32;
				for (int32 DenseIndex = 0;
					DenseIndex < GroundFloor.Result.OpeningMaskByCell.Num();
					++DenseIndex)
				{
					if (GroundFloor.Result.OpeningMaskByCell[DenseIndex] == 0
						|| GroundFloor.Input.StructureWalkableByCell[DenseIndex] != 0
						|| !Graph.DistanceFromPursuerByNode.IsValidIndex(DenseIndex)
						|| Graph.DistanceFromPursuerByNode[DenseIndex] == INDEX_NONE
						|| (bProtectRewardBranches
							&& CandidateRewardBranchCells.Contains(DenseIndex)))
					{
						continue;
					}
					const FIntVector Address(
						DenseIndex % State.GridSize.X,
						DenseIndex / State.GridSize.X,
						0);
					if (Address == State.PursuerSpawn || Address == State.Exit)
					{
						continue;
					}
					const int32 RouteDistance =
						Graph.DistanceFromPursuerByNode[DenseIndex];
					FRandomStream TieRandom = FGenerationCore::MakeRandomStream(
						Input.Signature.Seed,
						ERandomDomain::PlayerPursuerSpawn,
						DenseIndex);
					const uint32 TieBreak = TieRandom.GetUnsignedInt();
					if (RouteDistance > FallbackRouteDistance
						|| (RouteDistance == FallbackRouteDistance
							&& TieBreak < FallbackTieBreak))
					{
						FallbackDenseIndex = DenseIndex;
						FallbackRouteDistance = RouteDistance;
						FallbackTieBreak = TieBreak;
					}
					const double RouteDistanceCm =
						RouteDistance * Input.SharedRules.LogicalTileSizeCm;
					if (RouteDistanceCm + UE_DOUBLE_SMALL_NUMBER
						< Input.Difficulty.MinPlayerPursuerRouteDistanceCm)
					{
						continue;
					}
					if (RouteDistance < BestRouteDistance
						|| (RouteDistance == BestRouteDistance
							&& TieBreak < BestTieBreak))
					{
						BestDenseIndex = DenseIndex;
						BestRouteDistance = RouteDistance;
						BestTieBreak = TieBreak;
					}
				}
				return BestDenseIndex != INDEX_NONE
					? BestDenseIndex : FallbackDenseIndex;
			};

			int32 BestDenseIndex = FindBestSpawn(true);
			if (BestDenseIndex == INDEX_NONE && !CandidateRewardBranchCells.IsEmpty())
			{
				BestDenseIndex = FindBestSpawn(false);
			}
			if (BestDenseIndex == INDEX_NONE)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::GlobalConnectivityFailed,
					TEXT("一层没有可用于玩家出生点的普通可走格。"));
			}

			InOutPlan.PlayerSpawnCoordinate = FIntVector(
				BestDenseIndex % State.GridSize.X,
				BestDenseIndex / State.GridSize.X,
				0);
			return true;
		}

		bool FinalizeRewardBranches(
			const FPlacementState& State,
			const TArray<FFloorSolveRecord>& Records,
			FZeroEscapeGeneratedLevelPlan& InOutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			InOutPlan.RewardBranches.Reset();
			for (FZeroEscapeGeneratedFloorSummary& Floor : InOutPlan.Floors)
			{
				Floor.RewardBranchCount = 0;
				Floor.RewardBranchCellRatio = 0.0;
			}
			if (Records.Num() != State.FloorCount
				|| InOutPlan.Floors.Num() != State.FloorCount)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("奖励支线定稿缺少逐层结果。"));
			}

			const auto CoordinateLess = [](const FIntVector A, const FIntVector B)
			{
				return A.Z != B.Z ? A.Z < B.Z
					: A.Y != B.Y ? A.Y < B.Y
					: A.X < B.X;
			};
			const auto BranchLess = [CoordinateLess](
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
			};

			for (int32 FloorIndex = 0; FloorIndex < Records.Num(); ++FloorIndex)
			{
				for (const FZeroEscapeGeneratedRewardBranch& Branch :
					Records[FloorIndex].Result.CandidateRewardBranches)
				{
					if (Branch.PathCoordinates.IsEmpty()
						|| Branch.EndpointCoordinate != Branch.PathCoordinates.Last()
						|| Branch.EndpointCoordinate.Z != FloorIndex
						|| Branch.GatewayCoordinate.Z != FloorIndex)
					{
						return FailLayout(
							OutReport,
							EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("楼层路线分析输出了非法奖励支线。"));
					}
					const bool bConflictsWithFlowAnchor =
						Branch.PathCoordinates.Contains(InOutPlan.PlayerSpawnCoordinate)
						|| Branch.PathCoordinates.Contains(State.PursuerSpawn)
						|| Branch.PathCoordinates.Contains(State.Exit);
					if (!bConflictsWithFlowAnchor)
					{
						InOutPlan.RewardBranches.Add(Branch);
					}
				}
			}
			InOutPlan.RewardBranches.Sort(BranchLess);

			TArray<int32> BranchCellCountByFloor;
			BranchCellCountByFloor.Init(0, State.FloorCount);
			TSet<FIntVector> SeenEndpoints;
			for (const FZeroEscapeGeneratedRewardBranch& Branch :
				InOutPlan.RewardBranches)
			{
				const int32 FloorIndex = Branch.EndpointCoordinate.Z;
				if (!InOutPlan.Floors.IsValidIndex(FloorIndex)
					|| SeenEndpoints.Contains(Branch.EndpointCoordinate))
				{
					return FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("奖励支线端点重复或楼层越界。"));
				}
				SeenEndpoints.Add(Branch.EndpointCoordinate);
				++InOutPlan.Floors[FloorIndex].RewardBranchCount;
				BranchCellCountByFloor[FloorIndex] += Branch.PathCoordinates.Num();
			}
			for (int32 FloorIndex = 0; FloorIndex < State.FloorCount; ++FloorIndex)
			{
				FZeroEscapeGeneratedFloorSummary& Floor = InOutPlan.Floors[FloorIndex];
				Floor.RewardBranchCellRatio = Floor.OrdinaryWalkableCellCount > 0
					? static_cast<double>(BranchCellCountByFloor[FloorIndex])
						/ Floor.OrdinaryWalkableCellCount
					: 0.0;
			}
			return true;
		}

		bool ValidatePlayerToExitRoute(
			const FPlacementState& State,
			const FWholeGraphResult& Graph,
			FZeroEscapeGeneratedLevelPlan& InOutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int32 CellsPerFloor = State.GridSize.X * State.GridSize.Y;
			const int32 PlayerNode = ToBuildingIndex(
				InOutPlan.PlayerSpawnCoordinate, State.GridSize);
			const int32 ExitNode = ToBuildingIndex(State.Exit, State.GridSize);
			TArray<int32> DistanceByNode;
			TArray<int32> VerticalTransitionsByNode;
			if (!ComputeShortestRoutes(
					Graph.Neighbors,
					PlayerNode,
					CellsPerFloor,
					DistanceByNode,
					VerticalTransitionsByNode)
				|| !DistanceByNode.IsValidIndex(ExitNode)
				|| DistanceByNode[ExitNode] == INDEX_NONE)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::GlobalConnectivityFailed,
					TEXT("玩家出生点无法到达顶层终点。"));
			}

			const int32 RouteLength = DistanceByNode[ExitNode];
			InOutPlan.PlayerToExitRouteLengthTiles = RouteLength;
			InOutPlan.VerticalTransitionCountOnShortestRoute =
				VerticalTransitionsByNode[ExitNode];
			return true;
		}

		const FZeroEscapeFloorCountOption* SelectFloorCountOption(
			const FResolvedGenerationInput& Input,
			FZeroEscapeGenerationReport& OutReport)
		{
			TArray<int32> Indices;
			for (int32 Index = 0; Index < Input.Difficulty.FloorCountOptions.Num(); ++Index)
			{
				Indices.Add(Index);
			}
			Indices.Sort([&Input](const int32 A, const int32 B)
			{
				return Input.Difficulty.FloorCountOptions[A].FloorCount
					< Input.Difficulty.FloorCountOptions[B].FloorCount;
			});

			int64 TotalWeight = 0;
			for (const int32 Index : Indices)
			{
				const FZeroEscapeFloorCountOption& Option =
					Input.Difficulty.FloorCountOptions[Index];
				if (Option.SelectionWeight < 0)
				{
					FailLayout(
						OutReport,
						EZeroEscapeGenerationStage::Configuration,
						EZeroEscapeGenerationFailure::InvalidConfiguration,
						TEXT("楼层数选项权重不得为负数。"));
					return nullptr;
				}
				if (Option.SelectionWeight > 0)
				{
					TotalWeight += Option.SelectionWeight;
				}
			}
			if (TotalWeight <= 0 || TotalWeight > MAX_int32)
			{
				FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("当前难度没有正权重的楼层数选项。"));
				return nullptr;
			}

			FRandomStream Random = FGenerationCore::MakeRandomStream(
				Input.Signature.Seed,
				ERandomDomain::FloorCount,
				static_cast<int32>(Input.Signature.Difficulty));
			int32 Roll = Random.RandHelper(static_cast<int32>(TotalWeight));
			for (const int32 Index : Indices)
			{
				const FZeroEscapeFloorCountOption& Option =
					Input.Difficulty.FloorCountOptions[Index];
				if (Option.SelectionWeight <= 0)
				{
					continue;
				}
				Roll -= Option.SelectionWeight;
				if (Roll < 0)
				{
					return &Option;
				}
			}
			FailLayout(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::SolverInvariantViolation,
				TEXT("楼层数加权抽取未命中任何选项。"));
			return nullptr;
		}

		bool ValidatePlannerInput(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int64 CellCount = static_cast<int64>(Input.SharedRules.GridSize.X)
				* Input.SharedRules.GridSize.Y;
			const int64 TotalCandidateBudget =
				static_cast<int64>(Input.Budget.MaxWfcCandidateAttemptsPerFloor)
				* FloorOption.FloorCount;
			const int64 TotalBacktrackBudget =
				static_cast<int64>(Input.Budget.MaxWfcBacktrackCountPerFloor)
				* FloorOption.FloorCount;
			const int64 TotalSolveBudget =
				static_cast<int64>(Input.Budget.MaxWfcSolveAttemptsPerFloor)
				* FloorOption.FloorCount;
			const int64 AdditionalWeightTotal = static_cast<int64>(
				Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.ZeroAdditionalWeight)
				+ Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.OneAdditionalWeight
				+ Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.TwoAdditionalWeight;
			int64 HighRoomTargetWeightTotal = 0;
			bool bInvalidHighRoomTargetWeight = false;
			for (const FZeroEscapeWeightedCount& Option :
				FloorOption.HighCeilingRoomTargetCounts)
			{
				bInvalidHighRoomTargetWeight |= Option.Count < 0 || Option.Weight < 0;
				if (Option.Weight > 0)
				{
					HighRoomTargetWeightTotal += Option.Weight;
				}
			}
			bInvalidHighRoomTargetWeight |= HighRoomTargetWeightTotal <= 0
				|| HighRoomTargetWeightTotal > MAX_int32;
			bool bInvalidOpeningSetWeight = false;
			for (const FZeroEscapeStructureDefinition& Definition :
				Input.StructureDefinitions)
			{
				int64 OpeningSetWeightTotal = 0;
				for (const FZeroEscapeStructureOpeningSetDefinition& Set :
					Definition.AllowedOpeningSets)
				{
					bInvalidOpeningSetWeight |= Set.SelectionWeight < 0;
					if (Set.SelectionWeight > 0)
					{
						OpeningSetWeightTotal += Set.SelectionWeight;
					}
				}
				bInvalidOpeningSetWeight |= OpeningSetWeightTotal <= 0
					|| OpeningSetWeightTotal > MAX_int32;
			}
			FString WeightError;
			if (Input.SharedRules.GridSize.X < GenerationLimits::MinGridAxis
				|| Input.SharedRules.GridSize.Y < GenerationLimits::MinGridAxis
				|| Input.SharedRules.GridSize.X > GenerationLimits::MaxGridAxis
				|| Input.SharedRules.GridSize.Y > GenerationLimits::MaxGridAxis
				|| CellCount <= 0 || CellCount > GenerationLimits::MaxGridCells
				|| FloorOption.FloorCount < GenerationLimits::MinFloorCount
				|| FloorOption.FloorCount > GenerationLimits::MaxFloorCount
				|| FloorOption.MinTotalWalkableCellCount <= 0
				|| FloorOption.MaxTotalWalkableCellCount
					< FloorOption.MinTotalWalkableCellCount
				|| FloorOption.MaxTotalWalkableCellCount
					> CellCount * FloorOption.FloorCount
				|| FloorOption.MinOrdinaryWalkableCellCountPerFloor <= 0
				|| FloorOption.MinOrdinaryWalkableCellCountPerFloor
					> CellCount
				|| FloorOption.MaxTotalWalkableCellCount
					< static_cast<int64>(
						FloorOption.MinOrdinaryWalkableCellCountPerFloor)
						* FloorOption.FloorCount
				|| FloorOption.MaxAdditionalTwoFloorStairCount < 0
				|| Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.ZeroAdditionalWeight < 0
				|| Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.OneAdditionalWeight < 0
				|| Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.TwoAdditionalWeight < 0
				|| AdditionalWeightTotal <= 0 || AdditionalWeightTotal > MAX_int32
				|| bInvalidHighRoomTargetWeight
				|| bInvalidOpeningSetWeight
				|| Input.Difficulty.ThreeFloorStairwellChancePercent < 0
				|| Input.Difficulty.ThreeFloorStairwellChancePercent > 100
				|| Input.Difficulty.HighCeilingRooms.MinimumTotalCount < 2
				|| Input.Difficulty.HighCeilingRooms.MaxCountPerFloor < 0
				|| Input.Difficulty.HighCeilingRooms.MinimumTotalCount
					> Input.Difficulty.HighCeilingRooms.MaxCountPerFloor
						* FloorOption.FloorCount
				|| !FMath::IsFinite(
					Input.Difficulty.HighCeilingRooms.MinimumSeparationRatio)
				|| Input.Difficulty.HighCeilingRooms.MinimumSeparationRatio < 0.0
				|| Input.Difficulty.HighCeilingRooms.MinimumSeparationRatio > 1.0
				|| Input.SharedRules.LogicalTileSizeCm <= 0.0
				|| Input.SharedRules.FloorHeightCm <= 0.0
				|| Input.SharedRules.AnchorHeightCm < 0.0
				|| Input.SharedRules.MaxConsecutiveStraightTiles <= 0
				|| Input.Budget.MaxWholeLayoutAttempts <= 0
				|| Input.Budget.MaxStructureCandidateEvaluations <= 0
				|| Input.Budget.MaxWfcCandidateAttemptsPerFloor <= 0
				|| Input.Budget.MaxWfcBacktrackCountPerFloor <= 0
				|| Input.Budget.MaxWfcSolveAttemptsPerFloor <= 0
				|| Input.Budget.MaxWfcSolveAttemptsPerFloor
					> GenerationLimits::MaxWfcSolveAttemptsPerFloor
				|| TotalCandidateBudget > MAX_int32
				|| TotalBacktrackBudget > MAX_int32
				|| TotalSolveBudget > MAX_int32
				|| !FMath::IsFinite(
					Input.Difficulty.MinRequiredEndpointSpatialSeparationRatio)
				|| Input.Difficulty.MinRequiredEndpointSpatialSeparationRatio < 0.0
				|| Input.Difficulty.MinRequiredEndpointSpatialSeparationRatio > 1.0
				|| !FMath::IsFinite(
					Input.Difficulty.MinRequiredRouteCoverageRatio)
				|| Input.Difficulty.MinRequiredRouteCoverageRatio < 0.0
				|| Input.Difficulty.MinRequiredRouteCoverageRatio > 1.0
				|| !FMath::IsFinite(
					Input.Difficulty.MinAdditionalStairSeparationRatio)
				|| Input.Difficulty.MinAdditionalStairSeparationRatio < 0.0
				|| Input.Difficulty.MinAdditionalStairSeparationRatio > 1.0
				|| !FMath::IsFinite(
					Input.Difficulty.MinPlayerPursuerRouteDistanceCm)
				|| Input.Difficulty.MinPlayerPursuerRouteDistanceCm < 0.0
				|| !Input.WfcShapeWeights.IsConfigured(WeightError))
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					WeightError.IsEmpty()
						? TEXT("多层布局输入、楼层选项或整栋共享预算非法。")
						: WeightError);
			}

			bool bHasTwoFloorDefinition = false;
			bool bHasHighCeilingDefinition = false;
			for (const FZeroEscapeStructureDefinition& Definition :
				Input.StructureDefinitions)
			{
				bHasTwoFloorDefinition |=
					Definition.Kind == EZeroEscapeStructureKind::TwoFloorStair;
				bHasHighCeilingDefinition |=
					Definition.Kind == EZeroEscapeStructureKind::HighCeilingRoom;
			}
			if (!bHasTwoFloorDefinition || !bHasHighCeilingDefinition)
			{
				return FailLayout(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("配置必须同时提供必需双层楼梯和高厅定义。"));
			}
			return true;
		}

		void InitializePlacementState(
			const FResolvedGenerationInput& Input,
			const FZeroEscapeFloorCountOption& FloorOption,
			FPlacementState& OutState)
		{
			OutState = {};
			OutState.FloorCount = FloorOption.FloorCount;
			OutState.GridSize = Input.SharedRules.GridSize;
			OutState.Cells.SetNum(
				OutState.FloorCount * OutState.GridSize.X * OutState.GridSize.Y);
			OutState.RequiredTwoFloorStairStableIdByLowerFloor.Init(
				INDEX_NONE, OutState.FloorCount - 1);
			OutState.FloorEndpoints.SetNum(OutState.FloorCount);
			OutState.HighCeilingRoomCountByFloor.Init(0, OutState.FloorCount);
		}

		bool IsRecoverableWholeAttemptFailure(
			const FZeroEscapeGenerationReport& Report)
		{
			return Report.Failure == EZeroEscapeGenerationFailure::None
				|| Report.Failure == EZeroEscapeGenerationFailure::StructurePlacementFailed
				|| Report.Failure == EZeroEscapeGenerationFailure::NoValidWfcSolution
				|| Report.Failure == EZeroEscapeGenerationFailure::GlobalConnectivityFailed;
		}

		void PopulateSuccessMetrics(
			const FPlacementState& State,
			const FWholeLayoutBudget& Budget,
			const int32 WholeAttemptCount,
			const FWholeGraphResult& Graph,
			FZeroEscapeGenerationMetrics& InOutMetrics)
		{
			InOutMetrics.WholeLayoutAttemptCount = WholeAttemptCount;
			InOutMetrics.StructureCandidateEvaluationCount =
				Budget.ConsumedStructureCandidateEvaluations;
			InOutMetrics.GeneratedFloorCount = State.FloorCount;
			InOutMetrics.RequiredTwoFloorStairCount = State.FloorCount - 1;
			int32 TotalTwoFloorStairs = 0;
			for (const FZeroEscapeGeneratedStructure& Structure : State.Structures)
			{
				switch (Structure.Kind)
				{
				case EZeroEscapeStructureKind::TwoFloorStair:
					++TotalTwoFloorStairs;
					break;
				case EZeroEscapeStructureKind::ThreeFloorStairwell:
					++InOutMetrics.ThreeFloorStairwellCount;
					break;
				case EZeroEscapeStructureKind::HighCeilingRoom:
					++InOutMetrics.HighCeilingRoomCount;
					break;
				default:
					break;
				}
			}
			InOutMetrics.AdditionalTwoFloorStairCount =
				FMath::Max(0, TotalTwoFloorStairs - (State.FloorCount - 1));
			InOutMetrics.WalkableCellCount = Graph.TotalWalkableCellCount;
		}
	}

	// —— 公开求解入口与共享预算下的整栋有限重试 ——
	bool FMultiFloorLayoutPlanner::Solve(
		const FResolvedGenerationInput& Input,
		FZeroEscapeGeneratedLevelPlan& OutPlan,
		FZeroEscapeGenerationReport& OutReport)
	{
		using namespace MultiFloorLayoutPrivate;
		const double StartSeconds = FPlatformTime::Seconds();
		OutPlan = {};
		OutReport = {};

		const FZeroEscapeFloorCountOption* FloorOption =
			SelectFloorCountOption(Input, OutReport);
		if (FloorOption == nullptr
			|| !ValidatePlannerInput(Input, *FloorOption, OutReport))
		{
			OutReport.Metrics.PlanningMilliseconds =
				(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			return false;
		}

		FWholeLayoutBudget Budget;
		Budget.MaxStructureCandidateEvaluations =
			Input.Budget.MaxStructureCandidateEvaluations;

		FZeroEscapeGenerationMetrics AggregateMetrics;
		AggregateMetrics.FloorWfcMetrics.SetNum(FloorOption->FloorCount);
		for (int32 Floor = 0; Floor < FloorOption->FloorCount; ++Floor)
		{
			AggregateMetrics.FloorWfcMetrics[Floor].FloorIndex = Floor;
		}
		FZeroEscapeGenerationReport LastAttemptReport;
		int32 AttemptsExecuted = 0;
		for (int32 WholeAttempt = 0;
			WholeAttempt < Input.Budget.MaxWholeLayoutAttempts;
			++WholeAttempt)
		{
			AttemptsExecuted = WholeAttempt + 1;
			Budget.RemainingStructureCandidateEvaluations =
				Input.Budget.MaxStructureCandidateEvaluations;
			Budget.Wfc.RemainingCandidateAttempts =
				Input.Budget.MaxWfcCandidateAttemptsPerFloor * FloorOption->FloorCount;
			Budget.Wfc.RemainingBacktracks =
				Input.Budget.MaxWfcBacktrackCountPerFloor * FloorOption->FloorCount;
			Budget.Wfc.RemainingSolveAttempts =
				Input.Budget.MaxWfcSolveAttemptsPerFloor * FloorOption->FloorCount;
			FPlacementState State;
			InitializePlacementState(Input, *FloorOption, State);
			FZeroEscapeGenerationReport AttemptReport;
			if (!PlaceRequiredRoute(
					Input,
					*FloorOption,
					WholeAttempt,
					State,
					Budget,
					AttemptReport))
			{
				if (AttemptReport.Failure == EZeroEscapeGenerationFailure::None)
				{
					FailLayout(
						AttemptReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::StructurePlacementFailed,
						TEXT("必需双层楼梯链无法在当前整栋尝试中满足端点距离。"));
				}
				LastAttemptReport = MoveTemp(AttemptReport);
				if (!IsRecoverableWholeAttemptFailure(LastAttemptReport))
				{
					break;
				}
				continue;
			}

			if (!PlaceHighCeilingRooms(
					Input,
					*FloorOption,
					WholeAttempt,
					State,
					Budget,
					AttemptReport))
			{
				if (AttemptReport.Failure == EZeroEscapeGenerationFailure::None)
				{
					FailLayout(
						AttemptReport,
						EZeroEscapeGenerationStage::StructurePlacement,
						EZeroEscapeGenerationFailure::StructurePlacementFailed,
						TEXT("高天花板房间无法在不破坏必需楼梯和数量下限的前提下放置。"));
				}
				LastAttemptReport = MoveTemp(AttemptReport);
				if (!IsRecoverableWholeAttemptFailure(LastAttemptReport))
				{
					break;
				}
				continue;
			}

			const FPlacementState RequiredStructureState = State;
			const FWholeLayoutBudget RequiredStructureBudget = Budget;
			if (!PlaceOptionalStairs(
					Input,
					*FloorOption,
					WholeAttempt,
					State,
					Budget,
					AttemptReport))
			{
				LastAttemptReport = MoveTemp(AttemptReport);
				if (!IsRecoverableWholeAttemptFailure(LastAttemptReport))
				{
					break;
				}
				State = RequiredStructureState;
				RestoreBudgetAvailability(RequiredStructureBudget, Budget);
				AttemptReport = {};
			}

			TArray<FFloorSolveRecord> Records;
			bool bFloorsSolved = SolveFloors(
					Input,
					*FloorOption,
					WholeAttempt,
					State,
					Budget,
					Records,
					AggregateMetrics,
					AttemptReport);
			if (!bFloorsSolved
				&& State.Structures.Num() > RequiredStructureState.Structures.Num()
				&& IsRecoverableWholeAttemptFailure(AttemptReport))
			{
				State = RequiredStructureState;
				RestoreBudgetAvailability(RequiredStructureBudget, Budget);
				Records.Reset();
				AttemptReport = {};
				bFloorsSolved = SolveFloors(
					Input,
					*FloorOption,
					WholeAttempt,
					State,
					Budget,
					Records,
					AggregateMetrics,
					AttemptReport);
			}
			if (!bFloorsSolved)
			{
				LastAttemptReport = MoveTemp(AttemptReport);
				if (!IsRecoverableWholeAttemptFailure(LastAttemptReport))
				{
					break;
				}
				continue;
			}

			FZeroEscapeGeneratedLevelPlan CandidatePlan;
			CandidatePlan.Signature = Input.Signature;
			CandidatePlan.FloorCount = State.FloorCount;
			CandidatePlan.GridSize = State.GridSize;
			CandidatePlan.LogicalTileSizeCm = Input.SharedRules.LogicalTileSizeCm;
			CandidatePlan.FloorHeightCm = Input.SharedRules.FloorHeightCm;
			CandidatePlan.AnchorHeightCm = Input.SharedRules.AnchorHeightCm;
			CandidatePlan.Structures = State.Structures;
			CandidatePlan.PursuerSpawnCoordinate = State.PursuerSpawn;
			CandidatePlan.ExitCoordinate = State.Exit;
			CandidatePlan.RequiredTwoFloorStairStableIdByLowerFloor =
				State.RequiredTwoFloorStairStableIdByLowerFloor;
			if (!BuildPlanFloorData(
					Input,
					State,
					Records,
					CandidatePlan,
					AttemptReport))
			{
				LastAttemptReport = MoveTemp(AttemptReport);
				if (!IsRecoverableWholeAttemptFailure(LastAttemptReport))
				{
					break;
				}
				continue;
			}

			FWholeGraphResult Graph;
			if (!BuildAndValidateWholeGraph(
					State,
					Records,
					Graph,
					CandidatePlan,
					AttemptReport)
				|| !SelectPlayerSpawn(
					Input,
					State,
					Records,
					Graph,
					CandidatePlan,
					AttemptReport)
				|| !FinalizeRewardBranches(
					State,
					Records,
					CandidatePlan,
					AttemptReport)
				|| !ValidatePlayerToExitRoute(
					State,
					Graph,
					CandidatePlan,
					AttemptReport))
			{
				LastAttemptReport = MoveTemp(AttemptReport);
				if (!IsRecoverableWholeAttemptFailure(LastAttemptReport))
				{
					break;
				}
				continue;
			}

			CandidatePlan.CanonicalLayoutHash =
				FGenerationCore::ComputeCanonicalLayoutHash(CandidatePlan);
			if (CandidatePlan.CanonicalLayoutHash == 0)
			{
				FailLayout(
					LastAttemptReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("成功候选没有通过规范布局 Hash 的结构合同检查。"));
				break;
			}

			OutPlan = MoveTemp(CandidatePlan);
			OutReport = {};
			OutReport.Metrics = AggregateMetrics;
			PopulateSuccessMetrics(
				State,
				Budget,
				WholeAttempt + 1,
				Graph,
				OutReport.Metrics);
			OutReport.Metrics.PlanningMilliseconds =
				(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			return true;
		}

		OutPlan = {};
		OutReport = MoveTemp(LastAttemptReport);
		if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
		{
			FailLayout(
				OutReport,
				EZeroEscapeGenerationStage::StructurePlacement,
				EZeroEscapeGenerationFailure::StructurePlacementFailed,
				TEXT("已用完整栋布局尝试次数，仍无法生成可提交的多层 Plan。"));
		}
		OutReport.Metrics = AggregateMetrics;
		OutReport.Metrics.WholeLayoutAttemptCount = AttemptsExecuted;
		OutReport.Metrics.StructureCandidateEvaluationCount =
			Budget.ConsumedStructureCandidateEvaluations;
		OutReport.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return false;
	}
}
