// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeLayoutSolver.cpp
 * 职责：实现有界的 Graph-to-Grid、Socket 模块放置、确定性整数 A*、Support-count WFC 与 Plan 全局验证。
 * 边界：全程只操作值类型；不读取 UObject，不 Spawn/注册组件，不修改 World。
 * 状态 Owner：FLayoutSolver::Solve 的局部 FPlacementState 拥有单次 Attempt；失败时 OutPlan 始终为空。
 */

#include "PCG/ZeroEscapeLayoutSolver.h"

#include "Algo/Reverse.h"
#include "Algo/Sort.h"
#include "Containers/Queue.h"
#include "Math/RotationMatrix.h"

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		constexpr int32 PlanarDirectionCount = 4;
		constexpr int32 MaxAlternatePathProbesPerSocketPair = 4;
		constexpr double PortalAlignmentToleranceCm = 0.1;
		constexpr float DirectionDotTolerance = 0.9999f;

		const TStaticArray<FIntVector, PlanarDirectionCount> DirectionDeltas = {
			FIntVector(0, 1, 0),
			FIntVector(1, 0, 0),
			FIntVector(0, -1, 0),
			FIntVector(-1, 0, 0) };

		/**
		 * 求解器内部统一 Placement 表示。Strong Socket、WFC 单格与 Closure 都先写入这里，
		 * Finalize 再一次性投影为公开 Plan；三个布尔/Id 字段用于区分它们的导出和验证规则。
		 */
		struct FInternalPlacement
		{
			int32 StablePlacementId = INDEX_NONE;
			int32 ModuleIndex = INDEX_NONE;
			int32 StableVariantId = INDEX_NONE;
			uint8 QuarterTurns = 0;
			int32 AbstractNodeId = INDEX_NONE;
			FIntVector GridOrigin = FIntVector::ZeroValue;
			FTransform LocalTransform = FTransform::Identity;
			bool bWfcPlacement = false;
			bool bClosurePlacement = false;
		};

		/** Strong Anchor 的一个“模块 + 旋转 + 占格”候选；RandomKey 只负责稳定打散尝试顺序。 */
		struct FSocketPlacementCandidate
		{
			int32 AnchorIndex = INDEX_NONE;
			int32 ModuleIndex = INDEX_NONE;
			uint8 QuarterTurns = 0;
			FIntVector GridOrigin = FIntVector::ZeroValue;
			FTransform LocalTransform = FTransform::Identity;
			uint32 RandomKey = 0;
		};

		/**
		 * 一条抽象 Edge 在某端可以使用的路由端点。
		 * Strong Anchor 的 RouteCell 位于实体模块 Portal 外一格并携带真实 Socket Id；Weak Anchor
		 * 直接使用自己的 GridCoordinate，Socket Id 保持 INDEX_NONE，等待 WFC 回填。
		 */
		struct FRouteEndpointOption
		{
			FIntVector RouteCell = FIntVector::ZeroValue;
			int32 StableSocketId = INDEX_NONE;
			EZeroEscapeCardinalDirection OutwardDirection = EZeroEscapeCardinalDirection::North;
			EZeroEscapeSocketPolicy SocketPolicy = EZeroEscapeSocketPolicy::Required;
			FConnectorSignature Signature;
		};

		/** Finalize 前的无 Id Portal 连接；完成去重和“一口只连一端”检查后才编号导出。 */
		struct FConnectionDraft
		{
			int32 AbstractEdgeId = INDEX_NONE;
			int32 PlacementA = INDEX_NONE;
			int32 SocketA = INDEX_NONE;
			int32 PlacementB = INDEX_NONE;
			int32 SocketB = INDEX_NONE;
		};

		enum class EPropagationResult : uint8
		{
			Stable,
			Contradiction,
			BudgetExceeded,
			InvariantViolation
		};

		enum class ESnapshotCaptureResult : uint8
		{
			Success,
			BudgetExceeded,
			InvariantViolation
		};

		/** 返回 N/E/S/W 的 0..3 索引；非平面方向返回 INDEX_NONE。 */
		int32 ToPlanarDirectionIndex(const EZeroEscapeCardinalDirection Direction)
		{
			const int32 Value = static_cast<int32>(Direction);
			return Value >= 0 && Value < PlanarDirectionCount ? Value : INDEX_NONE;
		}

		/** 从相邻 Cell 差值解析 N/E/S/W；非单步平面差值返回 INDEX_NONE。 */
		int32 DirectionIndexFromDelta(const FIntVector& Delta)
		{
			for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
			{
				if (DirectionDeltas[Direction] == Delta)
				{
					return Direction;
				}
			}
			return INDEX_NONE;
		}

		int32 OppositeDirectionIndex(const int32 Direction)
		{
			return Direction >= 0 && Direction < PlanarDirectionCount
				? (Direction + 2) % PlanarDirectionCount
				: INDEX_NONE;
		}

		bool IsInsideGrid(const FIntVector& Coordinate, const FIntVector& GridExtent)
		{
			return Coordinate.X >= 0 && Coordinate.X < GridExtent.X
				&& Coordinate.Y >= 0 && Coordinate.Y < GridExtent.Y
				&& Coordinate.Z >= 0 && Coordinate.Z < GridExtent.Z;
		}

		void Fail(
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
		}

		bool FailBudget(
			FZeroEscapeGenerationReport& OutReport,
			const EZeroEscapeGenerationStage Stage,
			const EZeroEscapeGenerationFailure Failure,
			const TCHAR* Message)
		{
			Fail(OutReport, Stage, Failure, Message);
			return false;
		}

		void ResetWfcAttemptMetrics(FZeroEscapeGenerationMetrics& Metrics)
		{
			Metrics.WfcActiveCellCount = 0;
			Metrics.WfcVariantCount = 0;
			Metrics.WfcInitialChoiceCellCount = 0;
			Metrics.WfcInitialAlternativeCount = 0;
			Metrics.WfcInitialMaxDomainSize = 0;
			Metrics.WfcDecisionFrameCount = 0;
			Metrics.WfcObservationCount = 0;
			Metrics.WfcContradictionCount = 0;
			Metrics.WfcBacktrackCount = 0;
			Metrics.WfcPeakDecisionDepth = 0;
			Metrics.WfcVariantRemovalCount = 0;
			Metrics.WfcSupportUpdateCount = 0;
			Metrics.WfcPeakSnapshotBytes = 0;
			Metrics.WfcCumulativeSnapshotCopyBytes = 0;
			Metrics.bHadEffectiveWfcChoice = false;
		}

		void RecordAttemptFailure(const int32 AttemptIndex, FZeroEscapeGenerationReport& Report)
		{
			Report.LastAttemptFailure = Report.Failure;
			FZeroEscapeFailureCount* Existing = Report.AttemptFailureCounts.FindByPredicate(
				[&Report](const FZeroEscapeFailureCount& Entry)
				{
					return Entry.Stage == Report.Stage && Entry.Failure == Report.Failure;
				});
			if (Existing == nullptr)
			{
				FZeroEscapeFailureCount& Added = Report.AttemptFailureCounts.AddDefaulted_GetRef();
				Added.Stage = Report.Stage;
				Added.Failure = Report.Failure;
				Added.Count = 1;
				Added.FirstAttemptIndex = AttemptIndex;
				Added.LastAttemptIndex = AttemptIndex;
			}
			else
			{
				++Existing->Count;
				Existing->LastAttemptIndex = AttemptIndex;
			}

			Report.AttemptFailureCounts.Sort(
				[](const FZeroEscapeFailureCount& A, const FZeroEscapeFailureCount& B)
				{
					if (A.Stage != B.Stage)
					{
						return static_cast<uint8>(A.Stage) < static_cast<uint8>(B.Stage);
					}
					return static_cast<uint8>(A.Failure) < static_cast<uint8>(B.Failure);
				});
		}

		bool AreConnectorSignaturesCompatible(
			const FConnectorSignature& A,
			const FConnectorSignature& B)
		{
			if (A.bOpen != B.bOpen)
			{
				return false;
			}
			return !A.bOpen
				|| (A.ConnectorTypeId == B.ConnectorTypeId
					&& A.WidthClass == B.WidthClass
					&& A.HeightLayer == B.HeightLayer);
		}

		FConnectorSignature MakeOpenSignature(const FZeroEscapeModulePortal& Portal)
		{
			FConnectorSignature Result;
			Result.bOpen = true;
			Result.ConnectorTypeId = Portal.ConnectorTypeId;
			Result.WidthClass = Portal.WidthClass;
			Result.HeightLayer = Portal.HeightLayer;
			return Result;
		}

		FTransform MakeGridModuleTransform(
			const FIntVector& GridOrigin,
			const FIntVector& RotatedFootprint,
			const FVector& CellSize,
			const uint8 QuarterTurns)
		{
			const FVector Translation(
				(static_cast<double>(GridOrigin.X) + static_cast<double>(RotatedFootprint.X) * 0.5) * CellSize.X,
				(static_cast<double>(GridOrigin.Y) + static_cast<double>(RotatedFootprint.Y) * 0.5) * CellSize.Y,
				static_cast<double>(GridOrigin.Z) * CellSize.Z);
			return FTransform(
				FRotator(0.0, static_cast<double>(QuarterTurns) * 90.0, 0.0),
				Translation,
				FVector::OneVector);
		}

		bool EnumerateFootprintCells(
			const FIntVector& Origin,
			const FIntVector& Footprint,
			const FIntVector& GridExtent,
			FDeterministicWorkBudget& WorkBudget,
			TArray<FIntVector>& OutCells)
		{
			OutCells.Reset();
			for (int32 Z = 0; Z < Footprint.Z; ++Z)
			{
				for (int32 Y = 0; Y < Footprint.Y; ++Y)
				{
					for (int32 X = 0; X < Footprint.X; ++X)
					{
						if (!WorkBudget.TryConsume(1))
						{
							return false;
						}
						const FIntVector Cell = Origin + FIntVector(X, Y, Z);
						if (!IsInsideGrid(Cell, GridExtent))
						{
							return false;
						}
						OutCells.Add(Cell);
					}
				}
			}
			return true;
		}

		uint64 MakeStablePairKey(const int32 A, const int32 B)
		{
			return (static_cast<uint64>(static_cast<uint32>(A)) << 32)
				| static_cast<uint32>(B);
		}

		bool ModuleHasAnchorType(
			const FModuleSnapshot& Module,
			const EZeroEscapeGameplayAnchorType Type)
		{
			return Module.GameplayAnchors.ContainsByPredicate(
				[Type](const FZeroEscapeModuleAnchor& Anchor)
				{
					return Anchor.Type == Type;
				});
		}

		/**
		 * 为 Strong Anchor 选择能满足角色、度数和 GameplayAnchor 的最小旋转占地。
		 * 这里只预估空间而不冻结具体模块；后续 Socket DFS 的真实候选不得超过该占地，所以 Embed
		 * 阶段预留的净空不会被更大的实际模块突破。
		 */
		bool FindMinimumPlanningFootprint(
			const FSpatialNode& Node,
			const int32 RequiredDegree,
			const bool bObjectiveNode,
			const FModuleCatalogSnapshot& Catalog,
			FDeterministicWorkBudget& WorkBudget,
			FIntVector& OutFootprint)
		{
			bool bFound = false;
			int64 BestArea = MAX_int64;
			int32 BestLongestAxis = MAX_int32;
			int32 BestModuleId = MAX_int32;
			uint8 BestQuarterTurns = MAX_uint8;
			for (const FModuleSnapshot& Module : Catalog.Modules)
			{
				if (!WorkBudget.TryConsume(1))
				{
					return false;
				}
				if (Module.LayoutPolicy != EZeroEscapeLayoutPolicy::SocketModule
					|| !Module.AllowedRoles.Contains(Node.Role))
				{
					continue;
				}

				int32 RequiredPortalCount = 0;
				for (const FZeroEscapeModulePortal& Portal : Module.Portals)
				{
					RequiredPortalCount += Portal.Policy == EZeroEscapeSocketPolicy::Required ? 1 : 0;
				}
				if (Module.Portals.Num() < RequiredDegree || RequiredPortalCount > RequiredDegree
					|| (Node.Role == EZeroEscapeTopologyRole::Start
						&& !ModuleHasAnchorType(Module, EZeroEscapeGameplayAnchorType::PlayerSpawn))
					|| (Node.Role == EZeroEscapeTopologyRole::Exit
						&& !ModuleHasAnchorType(Module, EZeroEscapeGameplayAnchorType::Exit))
					|| (bObjectiveNode
						&& !ModuleHasAnchorType(Module, EZeroEscapeGameplayAnchorType::Objective)))
				{
					continue;
				}

				for (uint8 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
				{
					if ((Module.AllowedQuarterTurnsMask & (1 << QuarterTurns)) == 0)
					{
						continue;
					}
					if (!WorkBudget.TryConsume(1))
					{
						return false;
					}
					const FIntVector Footprint = RotateFootprint(Module.Footprint, QuarterTurns);
					const int64 Area = static_cast<int64>(Footprint.X) * Footprint.Y;
					const int32 LongestAxis = FMath::Max(Footprint.X, Footprint.Y);
					const bool bBetter = !bFound
						|| Area < BestArea
						|| (Area == BestArea && LongestAxis < BestLongestAxis)
						|| (Area == BestArea && LongestAxis == BestLongestAxis
							&& Footprint.X < OutFootprint.X)
						|| (Area == BestArea && LongestAxis == BestLongestAxis
							&& Footprint.X == OutFootprint.X
							&& Module.StableModuleId < BestModuleId)
						|| (Area == BestArea && LongestAxis == BestLongestAxis
							&& Footprint.X == OutFootprint.X
							&& Module.StableModuleId == BestModuleId
							&& QuarterTurns < BestQuarterTurns);
					if (bBetter)
					{
						bFound = true;
						BestArea = Area;
						BestLongestAxis = LongestAxis;
						BestModuleId = Module.StableModuleId;
						BestQuarterTurns = QuarterTurns;
						OutFootprint = Footprint;
					}
				}
			}
			return bFound;
		}

		const FSpatialNode* FindNodeByStableId(
			const FAbstractLevelPlan& Plan,
			const int32 StableNodeId)
		{
			return Plan.Nodes.FindByPredicate(
				[StableNodeId](const FSpatialNode& Node)
				{
					return Node.StableNodeId == StableNodeId;
				});
		}

		const FModuleSnapshot* FindModuleByStableId(
			const FModuleCatalogSnapshot& Catalog,
			const int32 StableModuleId,
			int32* OutModuleIndex = nullptr)
		{
			for (int32 Index = 0; Index < Catalog.Modules.Num(); ++Index)
			{
				if (Catalog.Modules[Index].StableModuleId == StableModuleId)
				{
					if (OutModuleIndex != nullptr)
					{
						*OutModuleIndex = Index;
					}
					return &Catalog.Modules[Index];
				}
			}
			return nullptr;
		}

		int32 FindPortalIndexByStableId(
			const FModuleSnapshot& Module,
			const int32 StableSocketId)
		{
			return Module.Portals.IndexOfByPredicate(
				[StableSocketId](const FZeroEscapeModulePortal& Portal)
				{
					return Portal.StableSocketId == StableSocketId;
				});
		}

		bool IsSameCoordinateOrder(const FIntVector& A, const FIntVector& B)
		{
			if (A.Z != B.Z)
			{
				return A.Z < B.Z;
			}
			if (A.Y != B.Y)
			{
				return A.Y < B.Y;
			}
			return A.X < B.X;
		}

		FTransform MakeOpposedPortalFrame(const FTransform& TargetPortalInGenerator)
		{
			checkSlow(IsFiniteUnitScaleTransform(TargetPortalInGenerator));
			const FQuat TargetRotation = TargetPortalInGenerator.GetRotation();
			const FVector OpposedForward = -TargetRotation.GetAxisX();
			const FVector PreservedUp = TargetRotation.GetAxisZ();
			const FQuat OpposedRotation = FRotationMatrix::MakeFromXZ(OpposedForward, PreservedUp).ToQuat();
			return FTransform(OpposedRotation, TargetPortalInGenerator.GetTranslation(), FVector::OneVector);
		}
	}

	/**
	 * 一次 Layout Attempt 的唯一可变状态 Owner。
	 *
	 * 关键不变量：
	 * - Strong Anchor 的 StablePlacementId 指向 Placements 中的 SocketModule；Weak Anchor 暂不绑定；
	 * - OccupiedPlacementByCell 只记录已实体占用的格，A* 路线本身记录在 Constraints；
	 * - UsedSocketKeys、SocketByNodeAndEdge、AssignedSocketIds 与 RoutedEdges 必须同步提交/回滚；
	 * - StablePlacementId 只在候选排列稳定后分配，避免 DFS 尝试顺序泄漏进最终身份。
	 *
	 * State 从不跨 Attempt 复用，因此后期 Closure/Finalize 失败时丢弃整个 State 即可恢复原子性。
	 */
	struct FPlacementState
	{
		FIntVector GridExtent = FIntVector::ZeroValue;
		FVector CellSize = FVector::ZeroVector;
		TArray<FGraphAnchorPlacement> Anchors;
		TMap<int32, int32> AnchorIndexByNodeId;
		TSet<FIntVector> ReservedAnchorCoordinates;
		/** 已选规划 Footprint 及其一格四邻域；后续 Anchor 的 Footprint 不得占用。 */
		TSet<FIntVector> ReservedAnchorClearanceCells;
		TArray<FInternalPlacement> Placements;
		TMap<int32, int32> PlacementIndexByStableId;
		TMap<FIntVector, int32> OccupiedPlacementByCell;
		TSet<uint64> UsedSocketKeys;
		TMap<uint64, int32> SocketByNodeAndEdge;
		int32 NextStablePlacementId = 0;
	};

	namespace
	{
		bool AreOpposedPortalFrames(const FTransform& A, const FTransform& B);

		const FInternalPlacement* FindInternalPlacement(
			const FPlacementState& State,
			const int32 StablePlacementId)
		{
			const int32* Index = State.PlacementIndexByStableId.Find(StablePlacementId);
			return Index != nullptr && State.Placements.IsValidIndex(*Index)
				? &State.Placements[*Index]
				: nullptr;
		}

		FInternalPlacement* FindInternalPlacement(
			FPlacementState& State,
			const int32 StablePlacementId)
		{
			const int32* Index = State.PlacementIndexByStableId.Find(StablePlacementId);
			return Index != nullptr && State.Placements.IsValidIndex(*Index)
				? &State.Placements[*Index]
				: nullptr;
		}

		void RebuildPlacementIndices(FPlacementState& State)
		{
			State.PlacementIndexByStableId.Reset();
			for (int32 Index = 0; Index < State.Placements.Num(); ++Index)
			{
				State.PlacementIndexByStableId.Add(State.Placements[Index].StablePlacementId, Index);
			}
		}

		/**
		 * 从期望位置按 Chebyshev 环逐圈搜索；环内 Y/X 次序固定，因此没有容器遍历不确定性。
		 * 对 PlanningFootprint 的每一格都检查边界和已预留净空，返回的是占地中心锚点而非左下角。
		 */
		bool FindNearestFreeAnchorCoordinate(
			const FIntVector& Preferred,
			const FIntVector& PlanningFootprint,
			const FIntVector& GridExtent,
			const TSet<FIntVector>& ReservedClearanceCells,
			FDeterministicWorkBudget& WorkBudget,
			FIntVector& OutCoordinate)
		{
			const int32 MaxRadius = FMath::Max(GridExtent.X, GridExtent.Y);
			for (int32 Radius = 0; Radius <= MaxRadius; ++Radius)
			{
				for (int32 DeltaY = -Radius; DeltaY <= Radius; ++DeltaY)
				{
					for (int32 DeltaX = -Radius; DeltaX <= Radius; ++DeltaX)
					{
						if (FMath::Max(FMath::Abs(DeltaX), FMath::Abs(DeltaY)) != Radius)
						{
							continue;
						}
						if (!WorkBudget.TryConsume(1))
						{
							return false;
						}
						const FIntVector Candidate = Preferred + FIntVector(DeltaX, DeltaY, 0);
						const FIntVector Origin = Candidate - FIntVector(
							PlanningFootprint.X / 2,
							PlanningFootprint.Y / 2,
							0);
						bool bFits = true;
						for (int32 Y = 0; bFits && Y < PlanningFootprint.Y; ++Y)
						{
							for (int32 X = 0; X < PlanningFootprint.X; ++X)
							{
								if (!WorkBudget.TryConsume(1))
								{
									return false;
								}
								const FIntVector Cell = Origin + FIntVector(X, Y, 0);
								if (!IsInsideGrid(Cell, GridExtent)
									|| ReservedClearanceCells.Contains(Cell))
								{
									bFits = false;
									break;
								}
							}
						}
						if (bFits)
						{
							OutCoordinate = Candidate;
							return true;
						}
					}
				}
			}
			return false;
		}

		/**
		 * 原子式计算并预留 PlanningFootprint 及其四邻域一格净空。
		 * 写集合前先把所有格收集到局部数组；越界或预算失败不会留下半份 Reservation。
		 */
		bool ReserveAnchorFootprintAndClearance(
			const FIntVector& Coordinate,
			const FIntVector& PlanningFootprint,
			const FIntVector& GridExtent,
			FDeterministicWorkBudget& WorkBudget,
			TSet<FIntVector>& InOutReservedClearanceCells)
		{
			const FIntVector Origin = Coordinate - FIntVector(
				PlanningFootprint.X / 2,
				PlanningFootprint.Y / 2,
				0);
			TArray<FIntVector> CellsToReserve;
			for (int32 Y = 0; Y < PlanningFootprint.Y; ++Y)
			{
				for (int32 X = 0; X < PlanningFootprint.X; ++X)
				{
					if (!WorkBudget.TryConsume(1))
					{
						return false;
					}
					const FIntVector Cell = Origin + FIntVector(X, Y, 0);
					if (!IsInsideGrid(Cell, GridExtent))
					{
						return false;
					}
					CellsToReserve.Add(Cell);
					for (const FIntVector& Delta : DirectionDeltas)
					{
						if (!WorkBudget.TryConsume(1))
						{
							return false;
						}
						const FIntVector Neighbor = Cell + Delta;
						if (IsInsideGrid(Neighbor, GridExtent))
						{
							CellsToReserve.Add(Neighbor);
						}
					}
				}
			}
			for (const FIntVector& Cell : CellsToReserve)
			{
				InOutReservedClearanceCells.Add(Cell);
			}
			return true;
		}

		/** 检查实际旋转 Footprint 不越界、不重叠，也不会吞掉其他抽象 Anchor 的中心格。 */
		bool CanOccupyCandidate(
			const FSocketPlacementCandidate& Candidate,
			const FModuleCatalogSnapshot& Catalog,
			const FPlacementState& State,
			FDeterministicWorkBudget& WorkBudget,
			TArray<FIntVector>& OutCells)
		{
			if (!Catalog.Modules.IsValidIndex(Candidate.ModuleIndex))
			{
				return false;
			}
			const FIntVector Footprint = RotateFootprint(
				Catalog.Modules[Candidate.ModuleIndex].Footprint,
				Candidate.QuarterTurns);
			if (!EnumerateFootprintCells(
					Candidate.GridOrigin,
					Footprint,
					State.GridExtent,
					WorkBudget,
					OutCells))
			{
				return false;
			}

			const int32 OwnNodeId = State.Anchors[Candidate.AnchorIndex].AbstractNodeId;
			for (const FIntVector& Cell : OutCells)
			{
				if (State.OccupiedPlacementByCell.Contains(Cell))
				{
					return false;
				}
				for (const FGraphAnchorPlacement& Anchor : State.Anchors)
				{
					if (Anchor.AbstractNodeId != OwnNodeId && Anchor.GridCoordinate == Cell)
					{
						return false;
					}
				}
			}
			return true;
		}

		/**
		 * 提交一层 Socket DFS 候选。此时不分配 StablePlacementId；数组尾部和占格表构成一个
		 * 可由 RemoveLastCandidatePlacement 完整撤销的栈帧。
		 */
		void AddCandidatePlacement(
			const FSocketPlacementCandidate& Candidate,
			const TArray<FIntVector>& Cells,
			FPlacementState& State)
		{
			FInternalPlacement& Placement = State.Placements.AddDefaulted_GetRef();
			Placement.ModuleIndex = Candidate.ModuleIndex;
			Placement.QuarterTurns = Candidate.QuarterTurns;
			Placement.AbstractNodeId = State.Anchors[Candidate.AnchorIndex].AbstractNodeId;
			Placement.GridOrigin = Candidate.GridOrigin;
			Placement.LocalTransform = Candidate.LocalTransform;
			const int32 PlacementArrayIndex = State.Placements.Num() - 1;
			for (const FIntVector& Cell : Cells)
			{
				State.OccupiedPlacementByCell.Add(Cell, PlacementArrayIndex);
			}
		}

		/** 严格逆操作 AddCandidatePlacement；调用者必须传入同一候选枚举出的完整 Cells。 */
		void RemoveLastCandidatePlacement(
			const TArray<FIntVector>& Cells,
			FPlacementState& State)
		{
			for (const FIntVector& Cell : Cells)
			{
				State.OccupiedPlacementByCell.Remove(Cell);
			}
			State.Placements.Pop(EAllowShrinking::No);
		}

		/**
		 * 为 Strong Anchor 放置结构模块的有界 DFS。
		 * 每层先重新统计可占用候选数，选择 MRV Anchor 以尽早暴露无解；随后按已经由独立随机流
		 * 稳定打散的候选顺序尝试。递归失败会同时撤销 OccupiedPlacementByCell、Placements 尾项
		 * 和 PlacedAnchor 标记，保证兄弟分支看到完全相同的父状态。
		 */
		bool PlaceSocketCandidatesRecursive(
			const FModuleCatalogSnapshot& Catalog,
			const TArray<TArray<FSocketPlacementCandidate>>& CandidatesByAnchor,
			const int32 RequiredStrongCount,
			const FZeroEscapeSolverBudgets& Budgets,
			FDeterministicWorkBudget& WorkBudget,
			TArray<bool>& InOutPlacedAnchor,
			int32& InOutCandidateChecks,
			int32& InOutBacktracks,
			FPlacementState& State,
			FZeroEscapeGenerationReport& OutReport)
		{
			int32 PlacedCount = 0;
			for (int32 Index = 0; Index < State.Anchors.Num(); ++Index)
			{
				PlacedCount += State.Anchors[Index].bStrongAnchor && InOutPlacedAnchor[Index] ? 1 : 0;
			}
			if (PlacedCount == RequiredStrongCount)
			{
				return true;
			}

			int32 BestAnchor = INDEX_NONE;
			int32 BestAvailableCount = MAX_int32;
			for (int32 AnchorIndex = 0; AnchorIndex < State.Anchors.Num(); ++AnchorIndex)
			{
				if (!State.Anchors[AnchorIndex].bStrongAnchor || InOutPlacedAnchor[AnchorIndex])
				{
					continue;
				}
				int32 AvailableCount = 0;
				for (const FSocketPlacementCandidate& Candidate : CandidatesByAnchor[AnchorIndex])
				{
					if (!WorkBudget.TryConsume(1)
						|| ++InOutCandidateChecks > Budgets.MaxSocketCandidateChecks)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::SocketLayout,
							EZeroEscapeGenerationFailure::SearchBudgetExceeded,
							TEXT("Socket MRV 候选过滤预算耗尽。"),
							InOutCandidateChecks,
							Budgets.MaxSocketCandidateChecks,
							State.Anchors[AnchorIndex].AbstractNodeId);
						return false;
					}
					TArray<FIntVector> Cells;
					if (CanOccupyCandidate(Candidate, Catalog, State, WorkBudget, Cells))
					{
						++AvailableCount;
					}
					else if (WorkBudget.GetRemainingUnits() == 0)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::SocketLayout,
							EZeroEscapeGenerationFailure::SearchBudgetExceeded,
							TEXT("Socket 占格检查耗尽全局工作预算。"),
							InOutCandidateChecks,
							Budgets.MaxSocketCandidateChecks,
							State.Anchors[AnchorIndex].AbstractNodeId);
						return false;
					}
				}
				if (AvailableCount < BestAvailableCount)
				{
					BestAvailableCount = AvailableCount;
					BestAnchor = AnchorIndex;
				}
			}

			if (BestAnchor == INDEX_NONE || BestAvailableCount == 0)
			{
				return false;
			}

			for (const FSocketPlacementCandidate& Candidate : CandidatesByAnchor[BestAnchor])
			{
				if (!WorkBudget.TryConsume(1)
					|| ++InOutCandidateChecks > Budgets.MaxSocketCandidateChecks)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::SocketLayout,
						EZeroEscapeGenerationFailure::SearchBudgetExceeded,
						TEXT("Socket 候选检查或全局工作预算耗尽。"),
						InOutCandidateChecks,
						Budgets.MaxSocketCandidateChecks,
						State.Anchors[BestAnchor].AbstractNodeId);
					return false;
				}

				TArray<FIntVector> Cells;
				if (!CanOccupyCandidate(Candidate, Catalog, State, WorkBudget, Cells))
				{
					if (WorkBudget.GetRemainingUnits() == 0)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::SocketLayout,
							EZeroEscapeGenerationFailure::SearchBudgetExceeded,
							TEXT("Socket 占格检查耗尽全局工作预算。"),
							InOutCandidateChecks,
							Budgets.MaxSocketCandidateChecks,
							State.Anchors[BestAnchor].AbstractNodeId);
						return false;
					}
					continue;
				}
				AddCandidatePlacement(Candidate, Cells, State);
				InOutPlacedAnchor[BestAnchor] = true;
				if (PlaceSocketCandidatesRecursive(
						Catalog,
						CandidatesByAnchor,
						RequiredStrongCount,
						Budgets,
						WorkBudget,
						InOutPlacedAnchor,
						InOutCandidateChecks,
						InOutBacktracks,
						State,
						OutReport))
				{
					return true;
				}

				InOutPlacedAnchor[BestAnchor] = false;
				RemoveLastCandidatePlacement(Cells, State);
				if (++InOutBacktracks > Budgets.MaxSocketBacktracks)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::SocketLayout,
						EZeroEscapeGenerationFailure::SearchBudgetExceeded,
						TEXT("Socket 有限回溯预算耗尽。"),
						InOutBacktracks,
						Budgets.MaxSocketBacktracks,
						State.Anchors[BestAnchor].AbstractNodeId);
					return false;
				}
			}
			return false;
		}

		/**
		 * 合并同一 Cell 面的结构要求。相同要求可重复写入；Closed 与 Open 或不同签名冲突时立即
		 * 拒绝，使路线 DFS 能换端点/换路径，而不是把矛盾推迟到 WFC 才发现。
		 */
		bool MergeDirectionalConstraint(
			FGridConstraint& Constraint,
			const int32 Direction,
			const EConnectorConstraintRule Rule,
			const FConnectorSignature& Signature,
			FZeroEscapeGenerationReport& OutReport)
		{
			FDirectionalConnectorConstraint& Existing = Constraint.Directions[Direction];
			if (Existing.Rule == EConnectorConstraintRule::Unconstrained)
			{
				Existing.Rule = Rule;
				Existing.RequiredSignature = Signature;
				return true;
			}
			if (Existing.Rule != Rule
				|| (Rule == EConnectorConstraintRule::MustMatchSignature
					&& Existing.RequiredSignature != Signature))
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::PortalMismatch,
					TEXT("同一 Grid Cell 面收到了相互冲突的 Connector 约束。"));
				return false;
			}
			return true;
		}

		FGridConstraint& FindOrAddActiveConstraint(
			const FIntVector& Coordinate,
			TArray<FGridConstraint>& Constraints,
			TMap<FIntVector, int32>& ConstraintIndexByCoordinate)
		{
			if (const int32* Existing = ConstraintIndexByCoordinate.Find(Coordinate))
			{
				return Constraints[*Existing];
			}
			FGridConstraint& Added = Constraints.AddDefaulted_GetRef();
			Added.Coordinate = Coordinate;
			Added.Participation = EGridCellParticipation::ActiveWfc;
			ConstraintIndexByCoordinate.Add(Coordinate, Constraints.Num() - 1);
			return Added;
		}

		bool FindCanonicalRouteSignature(
			const FModuleCatalogSnapshot& Catalog,
			FConnectorSignature& OutSignature)
		{
			bool bFound = false;
			for (const FModuleSnapshot& Module : Catalog.Modules)
			{
				if (Module.LayoutPolicy != EZeroEscapeLayoutPolicy::WfcSingleCell)
				{
					continue;
				}
				for (const FZeroEscapeModulePortal& Portal : Module.Portals)
				{
					const FConnectorSignature Candidate = MakeOpenSignature(Portal);
					if (!bFound
						|| Candidate.ConnectorTypeId < OutSignature.ConnectorTypeId
						|| (Candidate.ConnectorTypeId == OutSignature.ConnectorTypeId
							&& Candidate.WidthClass < OutSignature.WidthClass)
						|| (Candidate.ConnectorTypeId == OutSignature.ConnectorTypeId
							&& Candidate.WidthClass == OutSignature.WidthClass
							&& Candidate.HeightLayer < OutSignature.HeightLayer))
					{
						OutSignature = Candidate;
						bFound = true;
					}
				}
			}
			return bFound;
		}

		/**
		 * 构建 Edge 端点候选。Strong Anchor 按 Policy、StableSocketId 稳定排序未使用 Portal；
		 * Weak Anchor 只生成一个逻辑端点。bRequireFreeRouteCell=false 仅用于检测两个实体 Portal
		 * 直接相接的特殊情况。
		 */
		bool BuildEndpointOptions(
			const FGraphAnchorPlacement& Anchor,
			const FModuleCatalogSnapshot& Catalog,
			const FPlacementState& State,
			const FConnectorSignature& DefaultSignature,
			const bool bRequireFreeRouteCell,
			TArray<FRouteEndpointOption>& OutOptions)
		{
			OutOptions.Reset();
			if (!Anchor.bStrongAnchor)
			{
				FRouteEndpointOption& Option = OutOptions.AddDefaulted_GetRef();
				Option.RouteCell = Anchor.GridCoordinate;
				Option.Signature = DefaultSignature;
				return true;
			}

			const FInternalPlacement* Placement = FindInternalPlacement(State, Anchor.StablePlacementId);
			if (Placement == nullptr || !Catalog.Modules.IsValidIndex(Placement->ModuleIndex))
			{
				return false;
			}
			const FModuleSnapshot& Module = Catalog.Modules[Placement->ModuleIndex];
			TArray<const FZeroEscapeModulePortal*> SortedPortals;
			for (const FZeroEscapeModulePortal& Portal : Module.Portals)
			{
				SortedPortals.Add(&Portal);
			}
			SortedPortals.Sort(
				[](const FZeroEscapeModulePortal& A, const FZeroEscapeModulePortal& B)
				{
					return A.StableSocketId < B.StableSocketId;
				});

			for (const FZeroEscapeModulePortal* Portal : SortedPortals)
			{
				if (Portal == nullptr
					|| State.UsedSocketKeys.Contains(
						MakeStablePairKey(Anchor.StablePlacementId, Portal->StableSocketId)))
				{
					continue;
				}
				const EZeroEscapeCardinalDirection RotatedDirection = RotateDirection(
					Portal->Direction, Placement->QuarterTurns);
				const int32 DirectionIndex = ToPlanarDirectionIndex(RotatedDirection);
				if (DirectionIndex == INDEX_NONE)
				{
					continue;
				}
				const FIntVector PortalCell = Placement->GridOrigin
					+ RotateCellOffset(Portal->CellOffset, Module.Footprint, Placement->QuarterTurns);
				const FIntVector RouteCell = PortalCell + DirectionDeltas[DirectionIndex];
				if (!IsInsideGrid(RouteCell, State.GridExtent)
					|| (bRequireFreeRouteCell
						&& State.OccupiedPlacementByCell.Contains(RouteCell)))
				{
					continue;
				}
				FRouteEndpointOption& Option = OutOptions.AddDefaulted_GetRef();
				Option.RouteCell = RouteCell;
				Option.StableSocketId = Portal->StableSocketId;
				Option.OutwardDirection = RotatedDirection;
				Option.SocketPolicy = Portal->Policy;
				Option.Signature = MakeOpenSignature(*Portal);
			}
			OutOptions.Sort(
				[](const FRouteEndpointOption& A, const FRouteEndpointOption& B)
				{
					if (A.SocketPolicy != B.SocketPolicy)
					{
						return static_cast<uint8>(A.SocketPolicy) < static_cast<uint8>(B.SocketPolicy);
					}
					return A.StableSocketId < B.StableSocketId;
				});
			return !OutOptions.IsEmpty();
		}

		/**
		 * 把一条 A* Cell 序列翻译为 WFC 一元开口约束：相邻格的相对方向必须成对开放，Strong
		 * 端点还要求首/尾格朝实体 Portal 开放。调用方在局部 Constraints 副本上执行本函数，
		 * 因此任何中途冲突都不会污染已提交的路线集合。
		 */
		bool ApplyRoutedEdgeConstraints(
			const FGraphAnchorPlacement& SourceAnchor,
			const FGraphAnchorPlacement& TargetAnchor,
			const FRouteEndpointOption& SourceOption,
			const FRouteEndpointOption& TargetOption,
			const TArray<FIntVector>& Path,
			TArray<FGridConstraint>& Constraints,
			TMap<FIntVector, int32>& ConstraintIndexByCoordinate,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (Path.IsEmpty())
			{
				return false;
			}
			for (const FIntVector& Cell : Path)
			{
				FindOrAddActiveConstraint(Cell, Constraints, ConstraintIndexByCoordinate);
			}

			for (int32 Index = 1; Index < Path.Num(); ++Index)
			{
				const int32 Direction = DirectionIndexFromDelta(Path[Index] - Path[Index - 1]);
				if (Direction == INDEX_NONE)
				{
					return false;
				}
				FGridConstraint& A = Constraints[*ConstraintIndexByCoordinate.Find(Path[Index - 1])];
				FGridConstraint& B = Constraints[*ConstraintIndexByCoordinate.Find(Path[Index])];
				if (!MergeDirectionalConstraint(
						A, Direction, EConnectorConstraintRule::MustMatchSignature, SourceOption.Signature, OutReport)
					|| !MergeDirectionalConstraint(
						B,
						OppositeDirectionIndex(Direction),
						EConnectorConstraintRule::MustMatchSignature,
						SourceOption.Signature,
						OutReport))
				{
					return false;
				}
			}

			if (SourceAnchor.bStrongAnchor)
			{
				const int32 SourceDirection = ToPlanarDirectionIndex(SourceOption.OutwardDirection);
				FGridConstraint& First = Constraints[*ConstraintIndexByCoordinate.Find(Path[0])];
				if (SourceDirection == INDEX_NONE
					|| !MergeDirectionalConstraint(
						First,
						OppositeDirectionIndex(SourceDirection),
						EConnectorConstraintRule::MustMatchSignature,
						SourceOption.Signature,
						OutReport))
				{
					return false;
				}
			}

			if (TargetAnchor.bStrongAnchor)
			{
				const int32 TargetDirection = ToPlanarDirectionIndex(TargetOption.OutwardDirection);
				FGridConstraint& Last = Constraints[*ConstraintIndexByCoordinate.Find(Path.Last())];
				if (TargetDirection == INDEX_NONE
					|| !MergeDirectionalConstraint(
						Last,
						OppositeDirectionIndex(TargetDirection),
						EConnectorConstraintRule::MustMatchSignature,
						TargetOption.Signature,
						OutReport))
				{
					return false;
				}
			}
			return true;
		}

		bool ConstraintAllowsVariant(
			const FGridConstraint& Constraint,
			const FTileVariant& Variant)
		{
			for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
			{
				const FDirectionalConnectorConstraint& Required = Constraint.Directions[Direction];
				const FConnectorSignature& Actual = Variant.Connectors[Direction];
				switch (Required.Rule)
				{
				case EConnectorConstraintRule::Unconstrained:
					break;
				case EConnectorConstraintRule::MustBeClosed:
					if (Actual.bOpen)
					{
						return false;
					}
					break;
				case EConnectorConstraintRule::MustMatchSignature:
					if (!AreConnectorSignaturesCompatible(Actual, Required.RequiredSignature))
					{
						return false;
					}
					break;
				default:
					return false;
				}
			}
			return true;
		}

		/**
		 * WFC 的唯一 Domain 删除入口：位清零、计数递减并排入传播队列必须作为同一操作发生。
		 * 已删除候选重复删除是幂等的；删除最后一个候选返回 false 表示产生 Contradiction。
		 */
		bool RemoveVariant(
			FWfcState& State,
			const int32 CellIndex,
			const int32 VariantIndex,
			TQueue<FRemovedVariantEvent>& Removed,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (!State.Cells.IsValidIndex(CellIndex)
				|| !State.Cells[CellIndex].AllowedVariants.IsValidIndex(VariantIndex))
			{
				return false;
			}
			FWfcCell& Cell = State.Cells[CellIndex];
			if (!Cell.AllowedVariants[VariantIndex])
			{
				return Cell.RemainingCandidateCount > 0;
			}
			Cell.AllowedVariants[VariantIndex] = false;
			--Cell.RemainingCandidateCount;
			++OutReport.Metrics.WfcVariantRemovalCount;
			Removed.Enqueue({ CellIndex, VariantIndex });
			return Cell.RemainingCandidateCount > 0;
		}

		void EmptyRemovedQueue(TQueue<FRemovedVariantEvent>& Removed)
		{
			FRemovedVariantEvent Ignored;
			while (Removed.Dequeue(Ignored))
			{
			}
		}

		/**
		 * AC-4 风格的增量传播。删除 Source Variant 后，只访问兼容表中曾由它支持的邻格 Variant，
		 * 将邻格反方向 Support Count 减一；减到零时级联删除。NoActiveNeighbor 和零下溢都被视为
		 * 内部不变量破坏，而不是普通无解。
		 */
		EPropagationResult PropagateRemovedVariants(
			FWfcState& State,
			const FCompatibilityTable& Compatibility,
			TQueue<FRemovedVariantEvent>& Removed,
			const FZeroEscapeSolverBudgets& Budgets,
			FDeterministicWorkBudget& WorkBudget,
			FZeroEscapeGenerationReport& OutReport)
		{
			FRemovedVariantEvent Event;
			while (Removed.Dequeue(Event))
			{
				if (!State.Cells.IsValidIndex(Event.CellIndex))
				{
					return EPropagationResult::InvariantViolation;
				}
				const FWfcCell& Source = State.Cells[Event.CellIndex];
				for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
				{
					const int32 NeighborIndex = Source.NeighborCellIndices[Direction];
					if (NeighborIndex == INDEX_NONE)
					{
						continue;
					}
					if (!State.Cells.IsValidIndex(NeighborIndex))
					{
						return EPropagationResult::InvariantViolation;
					}
					FWfcCell& Neighbor = State.Cells[NeighborIndex];
					const int32 Opposite = OppositeDirectionIndex(Direction);
					for (const int32 NeighborVariant :
						Compatibility.GetSupportedNeighbors(Event.VariantIndex, Direction))
					{
						if (!WorkBudget.TryConsume(1))
						{
							return EPropagationResult::BudgetExceeded;
						}
						if (!Neighbor.AllowedVariants.IsValidIndex(NeighborVariant)
							|| !Neighbor.AllowedVariants[NeighborVariant])
						{
							continue;
						}
						const int32 Slot = NeighborVariant * PlanarDirectionCount + Opposite;
						if (!Neighbor.SupportCountByVariantDirection.IsValidIndex(Slot))
						{
							return EPropagationResult::InvariantViolation;
						}
						FWfcSupportCount& Support = Neighbor.SupportCountByVariantDirection[Slot];
						if (Support == NoActiveNeighbor || Support == 0)
						{
							return EPropagationResult::InvariantViolation;
						}
						if (!WorkBudget.TryConsume(1)
							|| OutReport.Metrics.WfcSupportUpdateCount >= Budgets.MaxWfcSupportUpdates)
						{
							return EPropagationResult::BudgetExceeded;
						}
						--Support;
						++OutReport.Metrics.WfcSupportUpdateCount;
						if (Support == 0)
						{
							if (!WorkBudget.TryConsume(1))
							{
								return EPropagationResult::BudgetExceeded;
							}
							if (!RemoveVariant(State, NeighborIndex, NeighborVariant, Removed, OutReport))
							{
								return EPropagationResult::Contradiction;
							}
						}
					}
				}
			}
			return EPropagationResult::Stable;
		}

		/**
		 * 从当前完整 Domains 重新计算全部 Support Count，并传播初始零支持候选。
		 * 初始化和快照恢复共用此路径，确保回溯后没有残留的增量计数或删除事件。
		 */
		EPropagationResult BuildOrRebuildSupportCounts(
			FWfcState& State,
			const FCompatibilityTable& Compatibility,
			TQueue<FRemovedVariantEvent>& Removed,
			const FZeroEscapeSolverBudgets& Budgets,
			FDeterministicWorkBudget& WorkBudget,
			FZeroEscapeGenerationReport& OutReport)
		{
			EmptyRemovedQueue(Removed);
			TArray<FRemovedVariantEvent> InitiallyUnsupported;
			for (int32 CellIndex = 0; CellIndex < State.Cells.Num(); ++CellIndex)
			{
				FWfcCell& Cell = State.Cells[CellIndex];
				Cell.RemainingCandidateCount = Cell.AllowedVariants.CountSetBits();
				if (Cell.RemainingCandidateCount <= 0)
				{
					return EPropagationResult::Contradiction;
				}
				const int32 VariantCount = Cell.AllowedVariants.Num();
				Cell.SupportCountByVariantDirection.Init(0, VariantCount * PlanarDirectionCount);
				for (int32 VariantIndex = 0; VariantIndex < VariantCount; ++VariantIndex)
				{
					for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
					{
						const int32 Slot = VariantIndex * PlanarDirectionCount + Direction;
						const int32 NeighborIndex = Cell.NeighborCellIndices[Direction];
						if (NeighborIndex == INDEX_NONE)
						{
							Cell.SupportCountByVariantDirection[Slot] = NoActiveNeighbor;
							continue;
						}
						if (!Cell.AllowedVariants[VariantIndex])
						{
							continue;
						}
						if (!State.Cells.IsValidIndex(NeighborIndex))
						{
							return EPropagationResult::InvariantViolation;
						}
						const FWfcCell& Neighbor = State.Cells[NeighborIndex];
						int32 SupportCount = 0;
						for (const int32 NeighborVariant :
							Compatibility.GetSupportedNeighbors(VariantIndex, Direction))
						{
							if (!WorkBudget.TryConsume(1))
							{
								return EPropagationResult::BudgetExceeded;
							}
							if (Neighbor.AllowedVariants.IsValidIndex(NeighborVariant)
								&& Neighbor.AllowedVariants[NeighborVariant])
							{
								++SupportCount;
							}
						}
						if (SupportCount >= NoActiveNeighbor)
						{
							return EPropagationResult::InvariantViolation;
						}
						Cell.SupportCountByVariantDirection[Slot] = static_cast<FWfcSupportCount>(SupportCount);
						if (SupportCount == 0)
						{
							InitiallyUnsupported.Add({ CellIndex, VariantIndex });
						}
					}
				}
			}

			for (const FRemovedVariantEvent& Unsupported : InitiallyUnsupported)
			{
				if (!WorkBudget.TryConsume(1))
				{
					return EPropagationResult::BudgetExceeded;
				}
				if (!RemoveVariant(
						State, Unsupported.CellIndex, Unsupported.VariantIndex, Removed, OutReport))
				{
					return EPropagationResult::Contradiction;
				}
			}
			return PropagateRemovedVariants(State, Compatibility, Removed, Budgets, WorkBudget, OutReport);
		}

		/**
		 * 仅抽取 ActiveWfc Constraints，按 Z/Y/X 排序后建立 Cell 数组和四向邻接索引。
		 * 输出顺序也是 SolveWfc 的 VariantByActiveCell 顺序，Finalize 会用相同排序还原坐标绑定。
		 */
		bool InitializeDomainsAndNeighbors(
			const TArray<FGridConstraint>& Constraints,
			const TArray<FTileVariant>& Variants,
			FDeterministicWorkBudget& WorkBudget,
			FWfcState& OutState,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutState.Cells.Reset();
			TArray<FGridConstraint> ActiveConstraints;
			for (const FGridConstraint& Constraint : Constraints)
			{
				if (!WorkBudget.TryConsume(1))
				{
					return false;
				}
				if (Constraint.Participation == EGridCellParticipation::ActiveWfc)
				{
					ActiveConstraints.Add(Constraint);
				}
			}
			ActiveConstraints.Sort(
				[](const FGridConstraint& A, const FGridConstraint& B)
				{
					return IsSameCoordinateOrder(A.Coordinate, B.Coordinate);
				});

			TMap<FIntVector, int32> CellIndexByCoordinate;
			for (const FGridConstraint& Constraint : ActiveConstraints)
			{
				if (CellIndexByCoordinate.Contains(Constraint.Coordinate))
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::InvalidConfiguration,
						TEXT("WFC Constraints 含有重复 Active Cell。"));
					return false;
				}
				FWfcCell& Cell = OutState.Cells.AddDefaulted_GetRef();
				Cell.Constraint = Constraint;
				Cell.AllowedVariants.Init(false, Variants.Num());
				for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
				{
					if (!WorkBudget.TryConsume(1))
					{
						return false;
					}
					Cell.AllowedVariants[VariantIndex] = ConstraintAllowsVariant(Constraint, Variants[VariantIndex]);
				}
				Cell.RemainingCandidateCount = Cell.AllowedVariants.CountSetBits();
				if (Cell.RemainingCandidateCount == 0)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::WfcNoSolution,
						TEXT("WFC 一元 Connector 约束使 Cell Domain 为空。"));
					return false;
				}
				CellIndexByCoordinate.Add(Constraint.Coordinate, OutState.Cells.Num() - 1);
			}

			for (int32 CellIndex = 0; CellIndex < OutState.Cells.Num(); ++CellIndex)
			{
				for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
				{
					const FIntVector NeighborCoordinate = OutState.Cells[CellIndex].Constraint.Coordinate
						+ DirectionDeltas[Direction];
					if (const int32* NeighborIndex = CellIndexByCoordinate.Find(NeighborCoordinate))
					{
						OutState.Cells[CellIndex].NeighborCellIndices[Direction] = *NeighborIndex;
					}
				}
			}
			return true;
		}

		/** 使用 64 位随机样本对整数权重取模；候选按 StableVariantId 顺序扫描，结果可复现。 */
		int32 ChooseWeightedVariant(
			const TBitArray<>& Candidates,
			const TArray<FTileVariant>& Variants,
			FRandomStream& Random)
		{
			uint64 TotalWeight = 0;
			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				if (Candidates[Index])
				{
					TotalWeight += static_cast<uint64>(FMath::Max(1, Variants[Index].Weight));
				}
			}
			if (TotalWeight == 0)
			{
				return INDEX_NONE;
			}
			const uint64 Random64 = (static_cast<uint64>(Random.GetUnsignedInt()) << 32)
				| static_cast<uint64>(Random.GetUnsignedInt());
			uint64 Pick = Random64 % TotalWeight;
			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				if (!Candidates[Index])
				{
					continue;
				}
				const uint64 Weight = static_cast<uint64>(FMath::Max(1, Variants[Index].Weight));
				if (Pick < Weight)
				{
					return Index;
				}
				Pick -= Weight;
			}
			return INDEX_NONE;
		}

		/** 选择候选数最少且尚未坍缩的 Cell；仅 MRV 平局消耗 WFC 专用随机流。 */
		int32 ChooseMinimumRemainingValueCell(const TArray<FWfcCell>& Cells, FRandomStream& Random)
		{
			int32 Minimum = MAX_int32;
			TArray<int32> Tied;
			for (int32 Index = 0; Index < Cells.Num(); ++Index)
			{
				const int32 Count = Cells[Index].RemainingCandidateCount;
				if (Count <= 1)
				{
					continue;
				}
				if (Count < Minimum)
				{
					Minimum = Count;
					Tied.Reset();
					Tied.Add(Index);
				}
				else if (Count == Minimum)
				{
					Tied.Add(Index);
				}
			}
			return Tied.IsEmpty() ? INDEX_NONE : Tied[Random.RandRange(0, Tied.Num() - 1)];
		}

		void MeasureInitialChoiceSpace(
			const FWfcState& State,
			FZeroEscapeGenerationMetrics& Metrics)
		{
			for (const FWfcCell& Cell : State.Cells)
			{
				Metrics.WfcInitialMaxDomainSize = FMath::Max(
					Metrics.WfcInitialMaxDomainSize, Cell.RemainingCandidateCount);
				if (Cell.RemainingCandidateCount > 1)
				{
					++Metrics.WfcInitialChoiceCellCount;
					Metrics.WfcInitialAlternativeCount += Cell.RemainingCandidateCount - 1;
				}
			}
		}

		/**
		 * Observation 的执行阶段：删除该 Cell 中除 ChosenVariant 外的所有候选，再把删除队列传播
		 * 到稳定点。这里不创建快照，调用者必须在进入本函数前保存可回溯状态。
		 */
		EPropagationResult CollapseCell(
			FWfcState& State,
			const int32 CellIndex,
			const int32 ChosenVariant,
			TQueue<FRemovedVariantEvent>& Removed,
			const FCompatibilityTable& Compatibility,
			const FZeroEscapeSolverBudgets& Budgets,
			FDeterministicWorkBudget& WorkBudget,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (!State.Cells.IsValidIndex(CellIndex)
				|| !State.Cells[CellIndex].AllowedVariants.IsValidIndex(ChosenVariant)
				|| !State.Cells[CellIndex].AllowedVariants[ChosenVariant])
			{
				return EPropagationResult::InvariantViolation;
			}
			for (int32 VariantIndex = 0;
				VariantIndex < State.Cells[CellIndex].AllowedVariants.Num();
				++VariantIndex)
			{
				if (VariantIndex == ChosenVariant
					|| !State.Cells[CellIndex].AllowedVariants[VariantIndex])
				{
					continue;
				}
				if (!WorkBudget.TryConsume(1))
				{
					return EPropagationResult::BudgetExceeded;
				}
				if (!RemoveVariant(State, CellIndex, VariantIndex, Removed, OutReport))
				{
					return EPropagationResult::Contradiction;
				}
			}
			return PropagateRemovedVariants(State, Compatibility, Removed, Budgets, WorkBudget, OutReport);
		}

		int64 MeasureSnapshotBytes(const FWfcDecision& Decision)
		{
			int64 Bytes = Decision.StableState.Domains.GetAllocatedSize();
			for (const TBitArray<>& Domain : Decision.StableState.Domains)
			{
				Bytes += Domain.GetAllocatedSize();
			}
			Bytes += Decision.UntriedVariants.GetAllocatedSize();
			return Bytes;
		}

		/**
		 * 在实际分配前做偏保守的快照大小估算，避免先占用内存再发现超限。
		 * 固定余量覆盖容器对齐/小对象开销；Capture 后还会以真实 GetAllocatedSize 二次校验。
		 */
		int64 EstimateSnapshotBytes(const FWfcState& State, const int32 CellIndex)
		{
			int64 Bytes = Align(
				static_cast<int64>(State.Cells.Num()) * static_cast<int64>(sizeof(TBitArray<>)),
				static_cast<int64>(16));
			for (const FWfcCell& Cell : State.Cells)
			{
				Bytes += Align(static_cast<int64>(Cell.AllowedVariants.GetAllocatedSize()), static_cast<int64>(16));
			}
			Bytes += Align(
				static_cast<int64>(State.Cells[CellIndex].AllowedVariants.GetAllocatedSize()),
				static_cast<int64>(16));
			return Bytes + 256;
		}

		/**
		 * 只允许在 Removed 队列为空的传播稳定点捕获完整 Domains。
		 * 同时执行两种内存门槛：当前 Decision Stack 的驻留量，以及整个 Solve 中所有捕获/恢复的
		 * 累计拷贝量；二者分别防止峰值爆内存和频繁回溯造成不可控的实时开销。
		 */
		ESnapshotCaptureResult CaptureDecision(
			const FWfcState& State,
			const TQueue<FRemovedVariantEvent>& Removed,
			const int32 CellIndex,
			const FZeroEscapeSolverBudgets& Budgets,
			int64& InOutLiveBytes,
			FWfcDecision& OutDecision,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (!Removed.IsEmpty() || !State.Cells.IsValidIndex(CellIndex))
			{
				return ESnapshotCaptureResult::InvariantViolation;
			}
			const int64 MaxLive = static_cast<int64>(Budgets.MaxWfcSnapshotMemoryMB) * 1024LL * 1024LL;
			const int64 MaxCopied = static_cast<int64>(Budgets.MaxWfcCumulativeSnapshotCopyMB) * 1024LL * 1024LL;
			const int64 Estimated = EstimateSnapshotBytes(State, CellIndex);
			if (InOutLiveBytes + Estimated > MaxLive
				|| OutReport.Metrics.WfcCumulativeSnapshotCopyBytes + Estimated > MaxCopied)
			{
				return ESnapshotCaptureResult::BudgetExceeded;
			}

			OutDecision = {};
			OutDecision.CellIndex = CellIndex;
			OutDecision.StableState.Domains.Reserve(State.Cells.Num());
			for (const FWfcCell& Cell : State.Cells)
			{
				OutDecision.StableState.Domains.Add(Cell.AllowedVariants);
			}
			OutDecision.UntriedVariants = State.Cells[CellIndex].AllowedVariants;
			OutDecision.NestedResidentBytes = MeasureSnapshotBytes(OutDecision);
			if (OutDecision.NestedResidentBytes > Estimated)
			{
				OutDecision = {};
				return ESnapshotCaptureResult::InvariantViolation;
			}
			const int64 NextLive = InOutLiveBytes + OutDecision.NestedResidentBytes;
			const int64 NextCopied = OutReport.Metrics.WfcCumulativeSnapshotCopyBytes
				+ OutDecision.NestedResidentBytes;
			if (NextLive > MaxLive || NextCopied > MaxCopied)
			{
				OutDecision = {};
				return ESnapshotCaptureResult::InvariantViolation;
			}
			InOutLiveBytes = NextLive;
			OutReport.Metrics.WfcCumulativeSnapshotCopyBytes = NextCopied;
			OutReport.Metrics.WfcPeakSnapshotBytes = FMath::Max(
				OutReport.Metrics.WfcPeakSnapshotBytes, NextLive);
			return ESnapshotCaptureResult::Success;
		}

		/**
		 * 恢复选择前的完整 Domains，然后从零重建 RemainingCandidateCount、Support Count 和传播
		 * 队列。Support Count 不在快照中做增量撤销，这是用可预测的重建成本换取更简单可靠的
		 * 回溯不变量。
		 */
		EPropagationResult RestoreDecisionAndRebuild(
			const FWfcDomainSnapshot& Snapshot,
			FWfcState& State,
			const FCompatibilityTable& Compatibility,
			TQueue<FRemovedVariantEvent>& Removed,
			const FZeroEscapeSolverBudgets& Budgets,
			FDeterministicWorkBudget& WorkBudget,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (Snapshot.Domains.Num() != State.Cells.Num())
			{
				return EPropagationResult::InvariantViolation;
			}
			int64 CopyBytes = Snapshot.Domains.GetAllocatedSize();
			for (const TBitArray<>& Domain : Snapshot.Domains)
			{
				CopyBytes += Domain.GetAllocatedSize();
			}
			const int64 MaxCopied = static_cast<int64>(Budgets.MaxWfcCumulativeSnapshotCopyMB) * 1024LL * 1024LL;
			if (OutReport.Metrics.WfcCumulativeSnapshotCopyBytes + CopyBytes > MaxCopied)
			{
				return EPropagationResult::BudgetExceeded;
			}
			EmptyRemovedQueue(Removed);
			for (int32 Index = 0; Index < State.Cells.Num(); ++Index)
			{
				if (!WorkBudget.TryConsume(1))
				{
					return EPropagationResult::BudgetExceeded;
				}
				if (Snapshot.Domains[Index].Num() != State.Cells[Index].AllowedVariants.Num())
				{
					return EPropagationResult::InvariantViolation;
				}
				State.Cells[Index].AllowedVariants = Snapshot.Domains[Index];
				State.Cells[Index].RemainingCandidateCount = Snapshot.Domains[Index].CountSetBits();
			}
			OutReport.Metrics.WfcCumulativeSnapshotCopyBytes += CopyBytes;
			return BuildOrRebuildSupportCounts(
				State, Compatibility, Removed, Budgets, WorkBudget, OutReport);
		}

		/** 终态导出门：每个 Active Cell 必须恰有一个候选，否则说明求解器状态不完整。 */
		bool ExportCollapsedVariants(
			const FWfcState& State,
			TArray<int32>& OutVariants,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutVariants.Reset(State.Cells.Num());
			for (const FWfcCell& Cell : State.Cells)
			{
				if (Cell.RemainingCandidateCount != 1)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("WFC 导出时存在非单候选 Domain。"));
					return false;
				}
				int32 Chosen = INDEX_NONE;
				for (int32 VariantIndex = 0; VariantIndex < Cell.AllowedVariants.Num(); ++VariantIndex)
				{
					if (Cell.AllowedVariants[VariantIndex])
					{
						Chosen = VariantIndex;
						break;
					}
				}
				if (Chosen == INDEX_NONE)
				{
					return false;
				}
				OutVariants.Add(Chosen);
			}
			return true;
		}
	}

	EZeroEscapeCardinalDirection OppositeDirection(const EZeroEscapeCardinalDirection Direction)
	{
		switch (Direction)
		{
		case EZeroEscapeCardinalDirection::North:
			return EZeroEscapeCardinalDirection::South;
		case EZeroEscapeCardinalDirection::East:
			return EZeroEscapeCardinalDirection::West;
		case EZeroEscapeCardinalDirection::South:
			return EZeroEscapeCardinalDirection::North;
		case EZeroEscapeCardinalDirection::West:
			return EZeroEscapeCardinalDirection::East;
		case EZeroEscapeCardinalDirection::Up:
			return EZeroEscapeCardinalDirection::Down;
		case EZeroEscapeCardinalDirection::Down:
			return EZeroEscapeCardinalDirection::Up;
		default:
			checkNoEntry();
			return Direction;
		}
	}

	EZeroEscapeCardinalDirection RotateDirection(
		const EZeroEscapeCardinalDirection Direction,
		const uint8 QuarterTurns)
	{
		checkSlow(QuarterTurns < 4);
		if (Direction == EZeroEscapeCardinalDirection::Up
			|| Direction == EZeroEscapeCardinalDirection::Down)
		{
			return Direction;
		}
		checkSlow(ToPlanarDirectionIndex(Direction) != INDEX_NONE);
		const int32 Rotated = (static_cast<int32>(Direction) - static_cast<int32>(QuarterTurns) + 4) % 4;
		return static_cast<EZeroEscapeCardinalDirection>(Rotated);
	}

	FIntVector RotateFootprint(const FIntVector& Footprint, const uint8 QuarterTurns)
	{
		checkSlow(QuarterTurns < 4);
		checkSlow(Footprint.X > 0 && Footprint.Y > 0 && Footprint.Z > 0);
		return (QuarterTurns & 1u) != 0
			? FIntVector(Footprint.Y, Footprint.X, Footprint.Z)
			: Footprint;
	}

	FIntVector RotateCellOffset(
		const FIntVector& CellOffset,
		const FIntVector& Footprint,
		const uint8 QuarterTurns)
	{
		checkSlow(QuarterTurns < 4);
		checkSlow(Footprint.X > 0 && Footprint.Y > 0 && Footprint.Z > 0);
		checkSlow(CellOffset.X >= 0 && CellOffset.X < Footprint.X);
		checkSlow(CellOffset.Y >= 0 && CellOffset.Y < Footprint.Y);
		checkSlow(CellOffset.Z >= 0 && CellOffset.Z < Footprint.Z);
		switch (QuarterTurns)
		{
		case 0:
			return CellOffset;
		case 1:
			return FIntVector(Footprint.Y - 1 - CellOffset.Y, CellOffset.X, CellOffset.Z);
		case 2:
			return FIntVector(
				Footprint.X - 1 - CellOffset.X,
				Footprint.Y - 1 - CellOffset.Y,
				CellOffset.Z);
		case 3:
			return FIntVector(CellOffset.Y, Footprint.X - 1 - CellOffset.X, CellOffset.Z);
		default:
			checkNoEntry();
			return CellOffset;
		}
	}

	FTransform SolveModuleLocalTransform(
		const FTransform& TargetPortalInGenerator,
		const FTransform& SourcePortalInModule)
	{
		checkSlow(IsFiniteUnitScaleTransform(TargetPortalInGenerator));
		checkSlow(IsFiniteUnitScaleTransform(SourcePortalInModule));
		return SourcePortalInModule.Inverse() * MakeOpposedPortalFrame(TargetPortalInGenerator);
	}

	FTransform MakePresentationLocalTransform(
		const FTransform& PivotCorrection,
		const FTransform& ModuleLocalTransform)
	{
		checkSlow(IsFiniteUnitScaleTransform(PivotCorrection));
		checkSlow(IsFiniteUnitScaleTransform(ModuleLocalTransform));
		return PivotCorrection * ModuleLocalTransform;
	}

	FTransform MakePresentationWorldTransform(
		const FTransform& PivotCorrection,
		const FTransform& ModuleLocalTransform,
		const FTransform& GeneratedRootWorldTransform)
	{
		checkSlow(IsFiniteUnitScaleTransform(GeneratedRootWorldTransform));
		return MakePresentationLocalTransform(PivotCorrection, ModuleLocalTransform)
			* GeneratedRootWorldTransform;
	}

	bool FCompatibilityTable::IsConfigured(
		const TArray<FTileVariant>& Variants,
		FString& OutError) const
	{
		OutError.Reset();
		if (Variants.IsEmpty() || SupportedNeighbors.Num() != Variants.Num())
		{
			OutError = TEXT("WFC 兼容表与 Variant 数量不一致。");
			return false;
		}
		for (int32 Source = 0; Source < Variants.Num(); ++Source)
		{
			for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
			{
				int32 PreviousStableId = INDEX_NONE;
				for (const int32 Neighbor : SupportedNeighbors[Source][Direction])
				{
					if (!Variants.IsValidIndex(Neighbor)
						|| Variants[Neighbor].StableVariantId <= PreviousStableId)
					{
						OutError = TEXT("WFC 兼容行含越界、重复或非稳定排序索引。");
						return false;
					}
					PreviousStableId = Variants[Neighbor].StableVariantId;
					const int32 Opposite = OppositeDirectionIndex(Direction);
					if (!SupportedNeighbors[Neighbor][Opposite].Contains(Source))
					{
						OutError = TEXT("WFC 兼容表不满足反向对称性。");
						return false;
					}
				}
			}
		}
		return true;
	}

	/**
	 * 将 Catalog 的 WfcSingleCell 模块展开为“模块 × 允许旋转”的稳定 Variant 集合。
	 * 每个 Variant 的四面都显式表示：缺少 Portal 即 Closed。兼容表随后做 O(V²×4) 的严格
	 * 签名比较，并在投入求解前验证排序、索引和双向对称性。
	 */
	bool FLayoutSolver::BuildWfcVariantsAndCompatibility(
		const FModuleCatalogSnapshot& Catalog,
		FDeterministicWorkBudget& WorkBudget,
		TArray<FTileVariant>& OutVariants,
		FCompatibilityTable& OutCompatibility,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutVariants.Reset();
		OutCompatibility.SupportedNeighbors.Reset();
		TArray<int32> ModuleIndices;
		for (int32 Index = 0; Index < Catalog.Modules.Num(); ++Index)
		{
			if (!WorkBudget.TryConsume(1))
			{
				return FailBudget(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::WfcBudgetExceeded,
					TEXT("WFC Variant 展开耗尽全局工作预算。"));
			}
			if (Catalog.Modules[Index].LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell)
			{
				ModuleIndices.Add(Index);
			}
		}
		ModuleIndices.Sort(
			[&Catalog](const int32 A, const int32 B)
			{
				return Catalog.Modules[A].StableModuleId < Catalog.Modules[B].StableModuleId;
			});

		for (const int32 ModuleIndex : ModuleIndices)
		{
			const FModuleSnapshot& Module = Catalog.Modules[ModuleIndex];
			for (uint8 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
			{
				if ((Module.AllowedQuarterTurnsMask & (1 << QuarterTurns)) == 0)
				{
					continue;
				}
				if (!WorkBudget.TryConsume(1))
				{
					return FailBudget(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::WfcBudgetExceeded,
						TEXT("WFC Variant 展开耗尽全局工作预算。"));
				}
				FTileVariant& Variant = OutVariants.AddDefaulted_GetRef();
				Variant.StableVariantId = OutVariants.Num() - 1;
				Variant.ModuleIndex = ModuleIndex;
				Variant.QuarterTurns = QuarterTurns;
				Variant.Weight = Module.Weight;
				for (const FZeroEscapeModulePortal& Portal : Module.Portals)
				{
					const int32 Direction = ToPlanarDirectionIndex(
						RotateDirection(Portal.Direction, QuarterTurns));
					if (Direction == INDEX_NONE || Variant.Connectors[Direction].bOpen)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::WfcLayout,
							EZeroEscapeGenerationFailure::InvalidModulePortal,
							TEXT("WFC 模块旋转后包含非平面或重复方向 Portal。"),
							0,
							0,
							Module.StableModuleId);
						return false;
					}
					Variant.Connectors[Direction] = MakeOpenSignature(Portal);
					Variant.StableSocketIds[Direction] = Portal.StableSocketId;
					Variant.OpenPortalMask |= static_cast<uint8>(1u << Direction);
				}
				if (OutVariants.Num() > ZeroEscape::GenerationLimits::FirstPassMaxWfcVariants)
				{
					return FailBudget(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::WfcBudgetExceeded,
						TEXT("WFC Variant 超过首版硬上限。"));
				}
			}
		}

		if (OutVariants.IsEmpty())
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::MissingModuleRole,
				TEXT("Catalog 没有可展开的 WfcSingleCell Variant。"));
			return false;
		}

		OutCompatibility.SupportedNeighbors.SetNum(OutVariants.Num());
		for (int32 Source = 0; Source < OutVariants.Num(); ++Source)
		{
			for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
			{
				const int32 Opposite = OppositeDirectionIndex(Direction);
				for (int32 Neighbor = 0; Neighbor < OutVariants.Num(); ++Neighbor)
				{
					if (!WorkBudget.TryConsume(1))
					{
						return FailBudget(
							OutReport,
							EZeroEscapeGenerationStage::WfcLayout,
							EZeroEscapeGenerationFailure::WfcBudgetExceeded,
							TEXT("WFC 兼容表构建耗尽全局工作预算。"));
					}
					if (AreConnectorSignaturesCompatible(
							OutVariants[Source].Connectors[Direction],
							OutVariants[Neighbor].Connectors[Opposite]))
					{
						OutCompatibility.SupportedNeighbors[Source][Direction].Add(Neighbor);
					}
				}
			}
		}

		FString Error;
		if (!OutCompatibility.IsConfigured(OutVariants, Error))
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				MoveTemp(Error));
			return false;
		}
		return true;
	}

	/**
	 * 确定性整数 A*。
	 *
	 * State Key 包含来向，所以转弯罚分不会被错误地折叠到同一 Coordinate。启发式只计算
	 * Manhattan 直行成本，是可采纳下界；Open 集合以 F、G、转弯数、来向、Z/Y/X 逐级破平，
	 * 不依赖 TMap/TSet 的遍历顺序。每次状态展开和邻居检查都消费确定性预算，并受独立展开数
	 * 上限约束。OutOrderedCells 在入口清空，只有到达 Goal 后才写入完整 Start->Goal 链。
	 */
	bool FLayoutSolver::FindGridPath(
		const FIntVector& GridExtent,
		const TSet<FIntVector>& BlockedCells,
		const FIntVector& Start,
		const FIntVector& Goal,
		const int32 StraightStepCost,
		const int32 TurnPenalty,
		const int32 MaxExpandedStates,
		FDeterministicWorkBudget& WorkBudget,
		TArray<FIntVector>& OutOrderedCells,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutOrderedCells.Reset();
		if (GridExtent.X <= 0 || GridExtent.Y <= 0 || GridExtent.Z != 1
			|| GridExtent.X > 64 || GridExtent.Y > 64
			|| !IsInsideGrid(Start, GridExtent)
			|| !IsInsideGrid(Goal, GridExtent)
			|| StraightStepCost <= 0
			|| TurnPenalty < 0
			|| MaxExpandedStates <= 0
			|| MaxExpandedStates > ZeroEscape::GenerationLimits::FirstPassMaxAStarExpandedStates
			|| (BlockedCells.Contains(Start) && Start != Goal)
			|| (BlockedCells.Contains(Goal) && Start != Goal))
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::SocketLayout,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("A* 输入越界、被占用或成本非法。"));
			return false;
		}
		if (Start == Goal)
		{
			OutOrderedCells.Add(Start);
			return true;
		}

		auto Heuristic = [StraightStepCost, &Goal](const FIntVector& Coordinate) -> int64
		{
			const int64 Manhattan = static_cast<int64>(FMath::Abs(Coordinate.X - Goal.X))
				+ static_cast<int64>(FMath::Abs(Coordinate.Y - Goal.Y));
			return Manhattan * static_cast<int64>(StraightStepCost);
		};

		TArray<FAStarNodeRecord> Records;
		TMap<FAStarStateKey, int32> RecordIndexByKey;
		TArray<int32> OpenRecordIndices;
		TSet<FAStarStateKey> Closed;
		FAStarNodeRecord& StartRecord = Records.AddDefaulted_GetRef();
		StartRecord.Key.Coordinate = Start;
		StartRecord.Key.IncomingDirection = INDEX_NONE;
		StartRecord.GScore = 0;
		StartRecord.FScore = Heuristic(Start);
		StartRecord.TurnCount = 0;
		RecordIndexByKey.Add(StartRecord.Key, 0);
		OpenRecordIndices.Add(0);

		int32 ExpandedStates = 0;
		while (!OpenRecordIndices.IsEmpty())
		{
			int32 BestOpenArrayIndex = 0;
			for (int32 Index = 1; Index < OpenRecordIndices.Num(); ++Index)
			{
				const FAStarNodeRecord& Candidate = Records[OpenRecordIndices[Index]];
				const FAStarNodeRecord& Best = Records[OpenRecordIndices[BestOpenArrayIndex]];
				const bool bBetter = Candidate.FScore < Best.FScore
					|| (Candidate.FScore == Best.FScore && Candidate.GScore < Best.GScore)
					|| (Candidate.FScore == Best.FScore && Candidate.GScore == Best.GScore
						&& Candidate.TurnCount < Best.TurnCount)
					|| (Candidate.FScore == Best.FScore && Candidate.GScore == Best.GScore
						&& Candidate.TurnCount == Best.TurnCount
						&& Candidate.Key.IncomingDirection < Best.Key.IncomingDirection)
					|| (Candidate.FScore == Best.FScore && Candidate.GScore == Best.GScore
						&& Candidate.TurnCount == Best.TurnCount
						&& Candidate.Key.IncomingDirection == Best.Key.IncomingDirection
						&& IsSameCoordinateOrder(Candidate.Key.Coordinate, Best.Key.Coordinate));
				if (bBetter)
				{
					BestOpenArrayIndex = Index;
				}
			}

			const int32 CurrentRecordIndex = OpenRecordIndices[BestOpenArrayIndex];
			OpenRecordIndices.RemoveAt(BestOpenArrayIndex, 1, EAllowShrinking::No);
			const FAStarNodeRecord Current = Records[CurrentRecordIndex];
			if (Closed.Contains(Current.Key))
			{
				continue;
			}
			if (!WorkBudget.TryConsume(1) || ++ExpandedStates > MaxExpandedStates)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::SearchBudgetExceeded,
					TEXT("A* 展开或全局工作预算耗尽。"),
					ExpandedStates,
					MaxExpandedStates);
				return false;
			}
			Closed.Add(Current.Key);

			if (Current.Key.Coordinate == Goal)
			{
				int32 TraceIndex = CurrentRecordIndex;
				while (TraceIndex != INDEX_NONE)
				{
					OutOrderedCells.Add(Records[TraceIndex].Key.Coordinate);
					TraceIndex = Records[TraceIndex].ParentRecordIndex;
				}
				Algo::Reverse(OutOrderedCells);
				return true;
			}

			for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
			{
				if (!WorkBudget.TryConsume(1))
				{
					return FailBudget(
						OutReport,
						EZeroEscapeGenerationStage::SocketLayout,
						EZeroEscapeGenerationFailure::SearchBudgetExceeded,
						TEXT("A* 邻居检查耗尽全局工作预算。"));
				}
				const FIntVector NextCoordinate = Current.Key.Coordinate + DirectionDeltas[Direction];
				if (!IsInsideGrid(NextCoordinate, GridExtent)
					|| (BlockedCells.Contains(NextCoordinate) && NextCoordinate != Goal))
				{
					continue;
				}
				const bool bTurns = Current.Key.IncomingDirection != INDEX_NONE
					&& Current.Key.IncomingDirection != Direction;
				const int64 Step = static_cast<int64>(StraightStepCost)
					+ (bTurns ? static_cast<int64>(TurnPenalty) : 0LL);
				if (Current.GScore > MAX_int64 - Step)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::SocketLayout,
						EZeroEscapeGenerationFailure::InvalidConfiguration,
						TEXT("A* 整数 GScore 溢出。"));
					return false;
				}
				const int64 TentativeG = Current.GScore + Step;
				const int32 TentativeTurns = Current.TurnCount + (bTurns ? 1 : 0);
				FAStarStateKey NextKey;
				NextKey.Coordinate = NextCoordinate;
				NextKey.IncomingDirection = static_cast<int8>(Direction);
				if (Closed.Contains(NextKey))
				{
					continue;
				}

				int32* ExistingIndex = RecordIndexByKey.Find(NextKey);
				if (ExistingIndex == nullptr)
				{
					FAStarNodeRecord& Added = Records.AddDefaulted_GetRef();
					Added.Key = NextKey;
					Added.GScore = TentativeG;
					Added.FScore = TentativeG + Heuristic(NextCoordinate);
					Added.TurnCount = TentativeTurns;
					Added.ParentRecordIndex = CurrentRecordIndex;
					const int32 AddedIndex = Records.Num() - 1;
					RecordIndexByKey.Add(NextKey, AddedIndex);
					OpenRecordIndices.Add(AddedIndex);
				}
				else
				{
					FAStarNodeRecord& Existing = Records[*ExistingIndex];
					if (TentativeG < Existing.GScore
						|| (TentativeG == Existing.GScore && TentativeTurns < Existing.TurnCount))
					{
						Existing.GScore = TentativeG;
						Existing.FScore = TentativeG + Heuristic(NextCoordinate);
						Existing.TurnCount = TentativeTurns;
						Existing.ParentRecordIndex = CurrentRecordIndex;
						if (!OpenRecordIndices.Contains(*ExistingIndex))
						{
							OpenRecordIndices.Add(*ExistingIndex);
						}
					}
				}
			}
		}

		Fail(
			OutReport,
			EZeroEscapeGenerationStage::SocketLayout,
			EZeroEscapeGenerationFailure::SocketPlacementNoSolution,
			TEXT("A* 在当前占格与端点 Socket 下无解。"));
		return false;
	}

	/**
	 * 有界 Simple-Tiled WFC 主循环。
	 *
	 * 先由路线一元约束裁剪 Domain，再构建 AC-4 Support Count 并传播到稳定点。之后反复选择
	 * MRV Cell、保存完整 Domain 快照、按权重 Observation 和增量传播。矛盾时恢复最近仍有候选
	 * 的 Decision；恢复后统一重建 Support Count。所有分支成本均被配置预算和全局 WorkBudget
	 * 限制，且只有完全坍缩后才填充 OutVariantByActiveCell。
	 */
	bool FLayoutSolver::SolveWfc(
		const TArray<FGridConstraint>& Constraints,
		const TArray<FTileVariant>& Variants,
		const FCompatibilityTable& Compatibility,
		FRandomStream& Random,
		const FZeroEscapeSolverBudgets& Budgets,
		const bool bRequireEffectiveWfcChoice,
		FDeterministicWorkBudget& WorkBudget,
		TArray<int32>& OutVariantByActiveCell,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutVariantByActiveCell.Reset();
		ResetWfcAttemptMetrics(OutReport.Metrics);
		int32 ActiveCellCount = 0;
		for (const FGridConstraint& Constraint : Constraints)
		{
			ActiveCellCount += Constraint.Participation == EGridCellParticipation::ActiveWfc ? 1 : 0;
		}
		const int64 SupportSlotCount = static_cast<int64>(ActiveCellCount)
			* static_cast<int64>(Variants.Num()) * PlanarDirectionCount;
		if (ActiveCellCount <= 0 || Variants.IsEmpty()
			|| Budgets.MaxWfcBacktracks < 0
			|| Budgets.MaxWfcObservationCount <= 0
			|| Budgets.MaxWfcSupportUpdates <= 0
			|| Budgets.MaxWfcActiveCells <= 0
			|| Budgets.MaxWfcVariants <= 0
			|| Budgets.MaxWfcSnapshotMemoryMB <= 0
			|| Budgets.MaxWfcCumulativeSnapshotCopyMB < Budgets.MaxWfcSnapshotMemoryMB
			|| Budgets.MaxWfcBacktracks > ZeroEscape::GenerationLimits::FirstPassMaxWfcBacktracks
			|| Budgets.MaxWfcObservationCount > ZeroEscape::GenerationLimits::FirstPassMaxWfcObservationCount
			|| Budgets.MaxWfcSupportUpdates > ZeroEscape::GenerationLimits::FirstPassMaxWfcSupportUpdates
			|| Budgets.MaxWfcActiveCells > ZeroEscape::GenerationLimits::FirstPassMaxWfcActiveCells
			|| Budgets.MaxWfcVariants > ZeroEscape::GenerationLimits::FirstPassMaxWfcVariants
			|| Budgets.MaxWfcSnapshotMemoryMB > ZeroEscape::GenerationLimits::FirstPassMaxWfcSnapshotMemoryMB
			|| Budgets.MaxWfcCumulativeSnapshotCopyMB
				> ZeroEscape::GenerationLimits::FirstPassMaxWfcCumulativeSnapshotCopyMB)
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("WFC 输入或预算非法。"));
			return false;
		}
		if (ActiveCellCount > FMath::Min(
				Budgets.MaxWfcActiveCells,
				ZeroEscape::GenerationLimits::FirstPassMaxWfcActiveCells)
			|| Variants.Num() > FMath::Min(
				Budgets.MaxWfcVariants,
				ZeroEscape::GenerationLimits::FirstPassMaxWfcVariants)
			|| SupportSlotCount > MAX_int32)
		{
			return FailBudget(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::WfcBudgetExceeded,
				TEXT("WFC 在分配 Domain/Support 前超过 Active Cell、Variant 或索引硬上限。"));
		}
		FString CompatibilityError;
		if (!Compatibility.IsConfigured(Variants, CompatibilityError))
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				MoveTemp(CompatibilityError));
			return false;
		}
		OutReport.Metrics.WfcActiveCellCount = ActiveCellCount;
		OutReport.Metrics.WfcVariantCount = Variants.Num();

		// 从一元 Connector 约束得到初始 Domains；此时尚未利用邻格二元兼容关系。
		FWfcState State;
		TQueue<FRemovedVariantEvent> Removed;
		if (!InitializeDomainsAndNeighbors(Constraints, Variants, WorkBudget, State, OutReport))
		{
			if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
			{
				return FailBudget(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::WfcBudgetExceeded,
					TEXT("WFC Domain 初始化耗尽全局工作预算。"));
			}
			return false;
		}
		// 初始化 Support Count 并删尽零支持候选；返回 Stable 才是合法的 Observation 起点。
		EPropagationResult Result = BuildOrRebuildSupportCounts(
			State, Compatibility, Removed, Budgets, WorkBudget, OutReport);
		if (Result == EPropagationResult::BudgetExceeded)
		{
			return FailBudget(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::WfcBudgetExceeded,
				TEXT("WFC Support Count 初始化耗尽预算。"));
		}
		if (Result == EPropagationResult::InvariantViolation)
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::SolverInvariantViolation,
				TEXT("WFC Support Count 初始化违反内部不变量。"));
			return false;
		}
		if (Result == EPropagationResult::Contradiction)
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::WfcNoSolution,
				TEXT("WFC 初始传播证明无解。"));
			return false;
		}
		MeasureInitialChoiceSpace(State, OutReport.Metrics);

		const int32 MaxDepth = FMath::Min(ActiveCellCount, Budgets.MaxWfcObservationCount);
		const int64 MaxLiveBytes = static_cast<int64>(Budgets.MaxWfcSnapshotMemoryMB) * 1024LL * 1024LL;
		if (static_cast<int64>(MaxDepth) * static_cast<int64>(sizeof(FWfcDecision)) > MaxLiveBytes)
		{
			return FailBudget(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::WfcBudgetExceeded,
				TEXT("WFC Decision Stack 在 Reserve 前已超过快照驻留上限。"));
		}
		TArray<FWfcDecision> Decisions;
		Decisions.Reserve(MaxDepth);
		int64 LiveSnapshotBytes = Decisions.GetAllocatedSize();
		if (LiveSnapshotBytes > MaxLiveBytes)
		{
			return FailBudget(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::WfcBudgetExceeded,
				TEXT("WFC Decision Stack 容量超过快照驻留上限。"));
		}
		OutReport.Metrics.WfcPeakSnapshotBytes = LiveSnapshotBytes;

		for (;;)
		{
			// 当前分支矛盾时，丢弃已穷尽帧，恢复最近仍有未试 Variant 的稳定快照。
			while (Result == EPropagationResult::Contradiction)
			{
				++OutReport.Metrics.WfcContradictionCount;
				while (!Decisions.IsEmpty()
					&& Decisions.Last().UntriedVariants.CountSetBits() == 0)
				{
					LiveSnapshotBytes -= Decisions.Last().NestedResidentBytes;
					Decisions.Pop(EAllowShrinking::No);
				}
				if (Decisions.IsEmpty())
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::WfcNoSolution,
						TEXT("WFC 穷尽全部有界分支后证明无解。"));
					return false;
				}
				if (OutReport.Metrics.WfcObservationCount >= Budgets.MaxWfcObservationCount
					|| OutReport.Metrics.WfcBacktrackCount >= Budgets.MaxWfcBacktracks)
				{
					return FailBudget(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::WfcBudgetExceeded,
						TEXT("WFC Observation/Backtrack 预算耗尽。"));
				}
				++OutReport.Metrics.WfcBacktrackCount;
				FWfcDecision& Retry = Decisions.Last();
				Result = RestoreDecisionAndRebuild(
					Retry.StableState,
					State,
					Compatibility,
					Removed,
					Budgets,
					WorkBudget,
					OutReport);
				if (Result == EPropagationResult::BudgetExceeded)
				{
					return FailBudget(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::WfcBudgetExceeded,
						TEXT("WFC 快照恢复或 Support 重建耗尽预算。"));
				}
				if (Result != EPropagationResult::Stable)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("WFC 恢复稳定 Domain 后重建出矛盾或非法支持计数。"));
					return false;
				}
				const int32 Alternative = ChooseWeightedVariant(Retry.UntriedVariants, Variants, Random);
				if (Alternative == INDEX_NONE)
				{
					Result = EPropagationResult::Contradiction;
					continue;
				}
				Retry.UntriedVariants[Alternative] = false;
				++OutReport.Metrics.WfcObservationCount;
				Result = CollapseCell(
					State,
					Retry.CellIndex,
					Alternative,
					Removed,
					Compatibility,
					Budgets,
					WorkBudget,
					OutReport);
			}

			if (Result == EPropagationResult::BudgetExceeded)
			{
				return FailBudget(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::WfcBudgetExceeded,
					TEXT("WFC 传播耗尽预算。"));
			}
			if (Result == EPropagationResult::InvariantViolation)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("WFC 传播违反 Support/Domain 不变量。"));
				return false;
			}

			// 没有 Domain 大于一的 Cell 表示全部坍缩；否则对 MRV Cell 创建下一层 Decision。
			const int32 CellIndex = ChooseMinimumRemainingValueCell(State.Cells, Random);
			if (CellIndex == INDEX_NONE)
			{
				OutReport.Metrics.bHadEffectiveWfcChoice =
					OutReport.Metrics.WfcInitialChoiceCellCount > 0
					&& OutReport.Metrics.WfcInitialAlternativeCount > 0
					&& OutReport.Metrics.WfcObservationCount > 0;
				if (bRequireEffectiveWfcChoice && !OutReport.Metrics.bHadEffectiveWfcChoice)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::WfcLayout,
						EZeroEscapeGenerationFailure::WfcNoEffectiveChoice,
						TEXT("验收 Profile 的 WFC 在初始传播后没有真实选择或 Observation。"));
					return false;
				}
				return ExportCollapsedVariants(State, OutVariantByActiveCell, OutReport);
			}

			if (OutReport.Metrics.WfcObservationCount >= Budgets.MaxWfcObservationCount)
			{
				return FailBudget(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::WfcBudgetExceeded,
					TEXT("WFC Observation 预算在复制快照前耗尽。"));
			}

			FWfcDecision Decision;
			const ESnapshotCaptureResult CaptureResult = CaptureDecision(
				State,
				Removed,
				CellIndex,
				Budgets,
				LiveSnapshotBytes,
				Decision,
				OutReport);
			if (CaptureResult == ESnapshotCaptureResult::BudgetExceeded)
			{
				return FailBudget(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::WfcBudgetExceeded,
					TEXT("WFC 实时或累计快照复制预算耗尽。"));
			}
			if (CaptureResult == ESnapshotCaptureResult::InvariantViolation)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("WFC 快照实测大小超过保守预估或在非稳定点创建。"));
				return false;
			}
			Decisions.Add(MoveTemp(Decision));
			++OutReport.Metrics.WfcDecisionFrameCount;
			OutReport.Metrics.WfcPeakDecisionDepth = FMath::Max(
				OutReport.Metrics.WfcPeakDecisionDepth, Decisions.Num());
			FWfcDecision& Current = Decisions.Last();
			const int32 Chosen = ChooseWeightedVariant(Current.UntriedVariants, Variants, Random);
			if (Chosen == INDEX_NONE)
			{
				Result = EPropagationResult::InvariantViolation;
				continue;
			}
			Current.UntriedVariants[Chosen] = false;
			++OutReport.Metrics.WfcObservationCount;
			Result = CollapseCell(
				State,
				CellIndex,
				Chosen,
				Removed,
				Compatibility,
				Budgets,
				WorkBudget,
				OutReport);
		}
	}

	/**
	 * 将抽象 Progression Graph 映射为二维 Grid Anchor。
	 * 主进度沿 X 推进，短支路/前向汇合交替分配到中轴两侧，降低长距离回头和路线交叉概率。
	 * Start/Exit/Objective、非二度节点与非普通主路节点属于 Strong Anchor；普通二度主路节点是
	 * Weak Anchor。Strong Anchor 先用最小合法模块 Footprint 规划，并与其他 Anchor 保留一格
	 * 四邻域净空，为后续实体房间和 A* 出口留下空间。
	 */
	bool FLayoutSolver::EmbedAbstractNodes(
		const FLayoutRequest& Request,
		const FModuleCatalogSnapshot& Catalog,
		FDeterministicWorkBudget& WorkBudget,
		FPlacementState& State,
		FZeroEscapeGenerationReport& OutReport)
	{
		State = {};
		State.GridExtent = Request.GridExtent;
		State.CellSize = Request.CellSize;
		const FAbstractLevelPlan& Plan = *Request.AbstractPlan;
		if (Plan.Nodes.IsEmpty() || Plan.Edges.IsEmpty())
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Topology,
				EZeroEscapeGenerationFailure::InvalidGraph,
				TEXT("Graph-to-Grid 需要非空 Nodes 与 Edges。"));
			return false;
		}

		TMap<int32, int32> DegreeByNode;
		TSet<int32> NodeIds;
		int32 MaxProgressIndex = 0;
		for (const FSpatialNode& Node : Plan.Nodes)
		{
			if (!WorkBudget.TryConsume(1)
				|| Node.StableNodeId < 0
				|| NodeIds.Contains(Node.StableNodeId))
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					WorkBudget.GetRemainingUnits() == 0
						? EZeroEscapeGenerationFailure::SearchBudgetExceeded
						: EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("Graph-to-Grid 遇到重复/非法 Node Id 或工作预算耗尽。"),
					0,
					0,
					Node.StableNodeId);
				return false;
			}
			NodeIds.Add(Node.StableNodeId);
			DegreeByNode.Add(Node.StableNodeId, 0);
			MaxProgressIndex = FMath::Max(MaxProgressIndex, Node.ProgressIndex);
		}
		for (const FSpatialEdge& Edge : Plan.Edges)
		{
			int32* DegreeA = DegreeByNode.Find(Edge.StableNodeA);
			int32* DegreeB = DegreeByNode.Find(Edge.StableNodeB);
			if (!WorkBudget.TryConsume(1)
				|| Edge.StableEdgeId < 0
				|| DegreeA == nullptr || DegreeB == nullptr
				|| Edge.StableNodeA == Edge.StableNodeB)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("Graph-to-Grid 遇到端点缺失或自环 Edge。"),
					0,
					0,
					Edge.StableEdgeId);
				return false;
			}
			++*DegreeA;
			++*DegreeB;
		}

		TSet<int32> ObjectiveNodeIds;
		for (const FObjectivePlacement& Objective : Plan.Objectives)
		{
			ObjectiveNodeIds.Add(Objective.StableNodeId);
		}
		TArray<const FSpatialNode*> SortedNodes;
		for (const FSpatialNode& Node : Plan.Nodes)
		{
			SortedNodes.Add(&Node);
		}
		SortedNodes.Sort(
			[](const FSpatialNode& A, const FSpatialNode& B)
			{
				return A.StableNodeId < B.StableNodeId;
			});

		int32 BranchOrdinal = 0;
		for (const FSpatialNode* Node : SortedNodes)
		{
			const int32 Degree = DegreeByNode.FindRef(Node->StableNodeId);
			// Weak Anchor 可以直接由走廊 WFC Cell 表示；任何具有语义/分支责任的 Node 必须实体化。
			const bool bStrongAnchor = Node->Role != EZeroEscapeTopologyRole::MainPath
				|| Degree != 2
				|| ObjectiveNodeIds.Contains(Node->StableNodeId)
				|| Node->StableNodeId == Plan.StartStableNodeId
				|| Node->StableNodeId == Plan.ExitStableNodeId;
			FIntVector PlanningFootprint(1, 1, 1);
			if (bStrongAnchor
				&& !FindMinimumPlanningFootprint(
					*Node,
					Degree,
					ObjectiveNodeIds.Contains(Node->StableNodeId),
					Catalog,
					WorkBudget,
					PlanningFootprint))
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::SocketLayout,
					WorkBudget.GetRemainingUnits() == 0
						? EZeroEscapeGenerationFailure::SearchBudgetExceeded
						: EZeroEscapeGenerationFailure::MissingModuleRole,
					TEXT("没有可用于规划 Strong Anchor Footprint 的 SocketModule。"),
					0,
					0,
					Node->StableNodeId);
				return false;
			}

			const int32 Progress = FMath::Max(0, Node->ProgressIndex);
			const int32 Span = FMath::Max(1, Request.GridExtent.X - 3);
			int32 PreferredX = 1 + (MaxProgressIndex > 0
				? static_cast<int32>((static_cast<int64>(Progress) * Span) / MaxProgressIndex)
				: 0);
			int32 PreferredY = Request.GridExtent.Y / 2;
			if (Node->Role == EZeroEscapeTopologyRole::ShortLeaf
				|| Node->Role == EZeroEscapeTopologyRole::ForwardRejoin)
			{
				const int32 AnchorProgress = FMath::Max(0, Node->AnchorProgressIndex);
				const int32 RejoinProgress = FMath::Max(AnchorProgress, Node->RejoinProgressIndex);
				const int32 EffectiveProgress = Node->Role == EZeroEscapeTopologyRole::ForwardRejoin
					? (AnchorProgress + RejoinProgress) / 2
					: AnchorProgress;
				PreferredX = 1 + (MaxProgressIndex > 0
					? static_cast<int32>((static_cast<int64>(EffectiveProgress) * Span) / MaxProgressIndex)
					: 0);
				const int32 MinimumRouteLane = FMath::Max(
					2, FMath::DivideAndRoundUp(PlanningFootprint.Y, 2) + 1);
				const int32 Lane = MinimumRouteLane + BranchOrdinal / 2;
				PreferredY += (BranchOrdinal & 1) == 0 ? Lane : -Lane;
				++BranchOrdinal;
			}
			PreferredX = FMath::Clamp(PreferredX, 0, Request.GridExtent.X - 1);
			PreferredY = FMath::Clamp(PreferredY, 0, Request.GridExtent.Y - 1);

			FIntVector Coordinate;
			if (!FindNearestFreeAnchorCoordinate(
					FIntVector(PreferredX, PreferredY, 0),
					PlanningFootprint,
					Request.GridExtent,
					State.ReservedAnchorClearanceCells,
					WorkBudget,
					Coordinate))
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					WorkBudget.GetRemainingUnits() == 0
						? EZeroEscapeGenerationFailure::SearchBudgetExceeded
						: EZeroEscapeGenerationFailure::TopologyCapacityInsufficient,
					TEXT("Grid 无法为所有抽象 Node 分配稳定 Anchor Cell。"),
					0,
					0,
					Node->StableNodeId);
				return false;
			}
			if (!ReserveAnchorFootprintAndClearance(
					Coordinate,
					PlanningFootprint,
					Request.GridExtent,
					WorkBudget,
					State.ReservedAnchorClearanceCells))
			{
				return FailBudget(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					EZeroEscapeGenerationFailure::SearchBudgetExceeded,
					TEXT("Graph-to-Grid 记录 Anchor Footprint/净空时耗尽预算。"));
			}
			FGraphAnchorPlacement& Anchor = State.Anchors.AddDefaulted_GetRef();
			Anchor.AbstractNodeId = Node->StableNodeId;
			Anchor.GridCoordinate = Coordinate;
			Anchor.PlanningFootprint = PlanningFootprint;
			Anchor.bStrongAnchor = bStrongAnchor;
			State.AnchorIndexByNodeId.Add(Node->StableNodeId, State.Anchors.Num() - 1);
			State.ReservedAnchorCoordinates.Add(Coordinate);
		}
		return true;
	}

	/**
	 * 为所有 Strong Anchor 选择真实 SocketModule。
	 * 候选必须满足拓扑角色、节点度数、Required Portal 数和对应 GameplayAnchor；旋转后的真实
	 * Footprint 不能超过 Embed 阶段承诺的 PlanningFootprint。候选使用 SocketLayout 专用随机流
	 * 生成 RandomKey 后稳定排序，再交给 MRV/DFS 做全局无重叠选择。成功后才按稳定 Node/Module
	 * 顺序分配 Placement Id 并重建 OccupiedPlacementByCell。
	 */
	bool FLayoutSolver::PlaceRequiredSocketModules(
		const FLayoutRequest& Request,
		const FModuleCatalogSnapshot& Catalog,
		FRandomStream& Random,
		const FZeroEscapeSolverBudgets& Budgets,
		FDeterministicWorkBudget& WorkBudget,
		FPlacementState& State,
		FZeroEscapeGenerationReport& OutReport)
	{
		const FAbstractLevelPlan& Plan = *Request.AbstractPlan;
		TMap<int32, int32> DegreeByNode;
		for (const FSpatialNode& Node : Plan.Nodes)
		{
			DegreeByNode.Add(Node.StableNodeId, 0);
		}
		for (const FSpatialEdge& Edge : Plan.Edges)
		{
			++DegreeByNode.FindChecked(Edge.StableNodeA);
			++DegreeByNode.FindChecked(Edge.StableNodeB);
		}
		TSet<int32> ObjectiveNodeIds;
		for (const FObjectivePlacement& Objective : Plan.Objectives)
		{
			ObjectiveNodeIds.Add(Objective.StableNodeId);
		}

		TArray<TArray<FSocketPlacementCandidate>> CandidatesByAnchor;
		CandidatesByAnchor.SetNum(State.Anchors.Num());
		int32 StrongCount = 0;
		for (int32 AnchorIndex = 0; AnchorIndex < State.Anchors.Num(); ++AnchorIndex)
		{
			const FGraphAnchorPlacement& Anchor = State.Anchors[AnchorIndex];
			if (!Anchor.bStrongAnchor)
			{
				continue;
			}
			++StrongCount;
			const FSpatialNode* Node = FindNodeByStableId(Plan, Anchor.AbstractNodeId);
			if (Node == nullptr)
			{
				return false;
			}
			const int32 RequiredDegree = DegreeByNode.FindRef(Node->StableNodeId);
			for (int32 ModuleIndex = 0; ModuleIndex < Catalog.Modules.Num(); ++ModuleIndex)
			{
				if (!WorkBudget.TryConsume(1))
				{
					return FailBudget(
						OutReport,
						EZeroEscapeGenerationStage::SocketLayout,
						EZeroEscapeGenerationFailure::SocketPlacementNoSolution,
						TEXT("Socket 候选构建耗尽全局工作预算。"));
				}
				const FModuleSnapshot& Module = Catalog.Modules[ModuleIndex];
				if (Module.LayoutPolicy != EZeroEscapeLayoutPolicy::SocketModule
					|| !Module.AllowedRoles.Contains(Node->Role))
				{
					continue;
				}
				int32 RequiredPortalCount = 0;
				for (const FZeroEscapeModulePortal& Portal : Module.Portals)
				{
					RequiredPortalCount += Portal.Policy == EZeroEscapeSocketPolicy::Required ? 1 : 0;
				}
				if (Module.Portals.Num() < RequiredDegree || RequiredPortalCount > RequiredDegree
					|| (Node->Role == EZeroEscapeTopologyRole::Start
						&& !ModuleHasAnchorType(Module, EZeroEscapeGameplayAnchorType::PlayerSpawn))
					|| (Node->Role == EZeroEscapeTopologyRole::Exit
						&& !ModuleHasAnchorType(Module, EZeroEscapeGameplayAnchorType::Exit))
					|| (ObjectiveNodeIds.Contains(Node->StableNodeId)
						&& !ModuleHasAnchorType(Module, EZeroEscapeGameplayAnchorType::Objective)))
				{
					continue;
				}

				for (uint8 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
				{
					if ((Module.AllowedQuarterTurnsMask & (1 << QuarterTurns)) == 0)
					{
						continue;
					}
					const FIntVector RotatedFootprint = RotateFootprint(Module.Footprint, QuarterTurns);
					if (RotatedFootprint.X > Anchor.PlanningFootprint.X
						|| RotatedFootprint.Y > Anchor.PlanningFootprint.Y
						|| RotatedFootprint.Z > Anchor.PlanningFootprint.Z)
					{
						continue;
					}
					const FIntVector Origin = Anchor.GridCoordinate
						- FIntVector(RotatedFootprint.X / 2, RotatedFootprint.Y / 2, 0);
					FSocketPlacementCandidate& Candidate = CandidatesByAnchor[AnchorIndex].AddDefaulted_GetRef();
					Candidate.AnchorIndex = AnchorIndex;
					Candidate.ModuleIndex = ModuleIndex;
					Candidate.QuarterTurns = QuarterTurns;
					Candidate.GridOrigin = Origin;
					Candidate.LocalTransform = MakeGridModuleTransform(
						Origin, RotatedFootprint, Request.CellSize, QuarterTurns);
					Candidate.RandomKey = Random.GetUnsignedInt();
				}
			}
			CandidatesByAnchor[AnchorIndex].Sort(
				[&Catalog](const FSocketPlacementCandidate& A, const FSocketPlacementCandidate& B)
				{
					if (A.RandomKey != B.RandomKey)
					{
						return A.RandomKey < B.RandomKey;
					}
					const int32 ModuleA = Catalog.Modules[A.ModuleIndex].StableModuleId;
					const int32 ModuleB = Catalog.Modules[B.ModuleIndex].StableModuleId;
					return ModuleA != ModuleB ? ModuleA < ModuleB : A.QuarterTurns < B.QuarterTurns;
				});
			if (CandidatesByAnchor[AnchorIndex].IsEmpty())
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::MissingModuleRole,
					TEXT("没有 SocketModule 同时满足 Node Role、度数与 Gameplay Anchor。"),
					0,
					0,
					Node->StableNodeId);
				return false;
			}
		}

		TArray<bool> PlacedAnchor;
		PlacedAnchor.Init(false, State.Anchors.Num());
		int32 CandidateChecks = 0;
		int32 Backtracks = 0;
		if (!PlaceSocketCandidatesRecursive(
				Catalog,
				CandidatesByAnchor,
				StrongCount,
				Budgets,
				WorkBudget,
				PlacedAnchor,
				CandidateChecks,
				Backtracks,
				State,
				OutReport))
		{
			if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::SocketPlacementNoSolution,
					TEXT("Socket MRV/回溯在当前 Grid 内无解。"));
			}
			return false;
		}

		// DFS 中的数组顺序是搜索细节；先规范排序再发号，确保同一结果拥有同一稳定身份。
		State.Placements.Sort(
			[&Catalog](const FInternalPlacement& A, const FInternalPlacement& B)
			{
				if (A.AbstractNodeId != B.AbstractNodeId)
				{
					return A.AbstractNodeId < B.AbstractNodeId;
				}
				const int32 ModuleA = Catalog.Modules[A.ModuleIndex].StableModuleId;
				const int32 ModuleB = Catalog.Modules[B.ModuleIndex].StableModuleId;
				return ModuleA != ModuleB ? ModuleA < ModuleB : A.QuarterTurns < B.QuarterTurns;
			});
		State.OccupiedPlacementByCell.Reset();
		for (int32 PlacementIndex = 0; PlacementIndex < State.Placements.Num(); ++PlacementIndex)
		{
			FInternalPlacement& Placement = State.Placements[PlacementIndex];
			Placement.StablePlacementId = State.NextStablePlacementId++;
			const FModuleSnapshot& Module = Catalog.Modules[Placement.ModuleIndex];
			TArray<FIntVector> Cells;
			if (!EnumerateFootprintCells(
					Placement.GridOrigin,
					RotateFootprint(Module.Footprint, Placement.QuarterTurns),
					State.GridExtent,
					WorkBudget,
					Cells))
			{
				return FailBudget(
					OutReport,
					EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::SocketPlacementNoSolution,
					TEXT("Socket 占格重建失败或耗尽预算。"));
			}
			for (const FIntVector& Cell : Cells)
			{
				State.OccupiedPlacementByCell.Add(Cell, Placement.StablePlacementId);
			}
			const int32 AnchorIndex = State.AnchorIndexByNodeId.FindChecked(Placement.AbstractNodeId);
			State.Anchors[AnchorIndex].StablePlacementId = Placement.StablePlacementId;
		}
		RebuildPlacementIndices(State);
		return true;
	}

	namespace
	{
		bool IsHardRouteFailure(const FZeroEscapeGenerationReport& Report)
		{
			// 普通候选无解只通过 false 回溯；写入 Report 的失败都是不可吞掉的硬失败。
			return Report.Failure != EZeroEscapeGenerationFailure::None;
		}

		bool ConsumeRouteAttempt(
			const FSpatialEdge& Edge,
			const FZeroEscapeSolverBudgets& Budgets,
			FDeterministicWorkBudget& WorkBudget,
			int32& InOutRouteAttempts,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (!WorkBudget.TryConsume(1)
				|| InOutRouteAttempts >= Budgets.MaxAStarRouteAttempts)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::SearchBudgetExceeded,
					TEXT("Socket/A* Route Attempt 或全局工作预算耗尽。"),
					InOutRouteAttempts + 1,
					Budgets.MaxAStarRouteAttempts,
					Edge.StableEdgeId);
				return false;
			}
			++InOutRouteAttempts;
			return true;
		}

		/**
		 * DFS 叶节点验收：每个 Required Strong Socket 必须恰好被某条抽象 Edge 消费。
		 * Sealable Socket 可以留到 Closure；Required Socket 不能靠封口掩盖拓扑缺边。
		 */
		bool CheckAllRequiredStrongSocketsUsed(
			const FModuleCatalogSnapshot& Catalog,
			const FPlacementState& State,
			FDeterministicWorkBudget& WorkBudget,
			bool& bOutAllUsed,
			FZeroEscapeGenerationReport& OutReport)
		{
			bOutAllUsed = true;
			for (const FGraphAnchorPlacement& Anchor : State.Anchors)
			{
				if (!Anchor.bStrongAnchor)
				{
					continue;
				}
				const FInternalPlacement* Placement = FindInternalPlacement(
					State, Anchor.StablePlacementId);
				if (Placement == nullptr || !Catalog.Modules.IsValidIndex(Placement->ModuleIndex))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::SocketLayout,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("Required Socket 终态检查无法解析 Strong Placement。"));
					return false;
				}
				for (const FZeroEscapeModulePortal& Portal : Catalog.Modules[Placement->ModuleIndex].Portals)
				{
					if (!WorkBudget.TryConsume(1))
					{
						return FailBudget(OutReport, EZeroEscapeGenerationStage::SocketLayout,
							EZeroEscapeGenerationFailure::SearchBudgetExceeded,
							TEXT("Required Socket 终态检查耗尽全局工作预算。"));
					}
					if (Portal.Policy == EZeroEscapeSocketPolicy::Required
						&& !State.UsedSocketKeys.Contains(MakeStablePairKey(
							Anchor.StablePlacementId, Portal.StableSocketId)))
					{
						bOutAllUsed = false;
					}
				}
			}
			return true;
		}

		/**
		 * 防止某条 Edge 借用第三个 Weak Anchor 的 Cell 作为端点。只有当前 Edge 自己的两个 Weak
		 * Anchor 可以从 Blocked 集合移除，否则 Node->Placement 绑定会悄悄落到错误路线。
		 */
		bool CheckForeignWeakAnchorCell(
			const FIntVector& Coordinate,
			const int32 SourceNodeId,
			const int32 TargetNodeId,
			const FPlacementState& State,
			FDeterministicWorkBudget& WorkBudget,
			bool& bOutIsForeign,
			FZeroEscapeGenerationReport& OutReport)
		{
			bOutIsForeign = false;
			for (const FGraphAnchorPlacement& Anchor : State.Anchors)
			{
				if (!WorkBudget.TryConsume(1))
				{
					return FailBudget(OutReport, EZeroEscapeGenerationStage::SocketLayout,
						EZeroEscapeGenerationFailure::SearchBudgetExceeded,
						TEXT("A* Foreign Anchor 检查耗尽全局工作预算。"));
				}
				if (!Anchor.bStrongAnchor
					&& Anchor.AbstractNodeId != SourceNodeId
					&& Anchor.AbstractNodeId != TargetNodeId
					&& Anchor.GridCoordinate == Coordinate)
				{
					bOutIsForeign = true;
					return true;
				}
			}
			return true;
		}

		/**
		 * 检测两个 Strong 模块是否已通过实体 Portal 直接贴合。
		 * 不仅检查“Portal 外一格落入对方占地”，还验证 Catalog Portal 的世界内位置、Forward
		 * 反向和 Up 同向，避免仅凭整数占格误判为可连接。
		 */
		bool IsDirectStrongConnection(
			const FGraphAnchorPlacement& SourceAnchor,
			const FGraphAnchorPlacement& TargetAnchor,
			const FRouteEndpointOption& SourceOption,
			const FRouteEndpointOption& TargetOption,
			const FModuleCatalogSnapshot& Catalog,
			const FPlacementState& State)
		{
			if (!SourceAnchor.bStrongAnchor || !TargetAnchor.bStrongAnchor)
			{
				return false;
			}
			const int32* SourceRouteOwner = State.OccupiedPlacementByCell.Find(SourceOption.RouteCell);
			const int32* TargetRouteOwner = State.OccupiedPlacementByCell.Find(TargetOption.RouteCell);
			if (SourceRouteOwner == nullptr || TargetRouteOwner == nullptr
				|| *SourceRouteOwner != TargetAnchor.StablePlacementId
				|| *TargetRouteOwner != SourceAnchor.StablePlacementId)
			{
				return false;
			}

			const FInternalPlacement* SourcePlacement = FindInternalPlacement(
				State, SourceAnchor.StablePlacementId);
			const FInternalPlacement* TargetPlacement = FindInternalPlacement(
				State, TargetAnchor.StablePlacementId);
			if (SourcePlacement == nullptr || TargetPlacement == nullptr
				|| !Catalog.Modules.IsValidIndex(SourcePlacement->ModuleIndex)
				|| !Catalog.Modules.IsValidIndex(TargetPlacement->ModuleIndex))
			{
				return false;
			}
			const FModuleSnapshot& SourceModule = Catalog.Modules[SourcePlacement->ModuleIndex];
			const FModuleSnapshot& TargetModule = Catalog.Modules[TargetPlacement->ModuleIndex];
			const int32 SourcePortalIndex = FindPortalIndexByStableId(
				SourceModule, SourceOption.StableSocketId);
			const int32 TargetPortalIndex = FindPortalIndexByStableId(
				TargetModule, TargetOption.StableSocketId);
			return SourcePortalIndex != INDEX_NONE && TargetPortalIndex != INDEX_NONE
				&& AreOpposedPortalFrames(
					SourceModule.Portals[SourcePortalIndex].LocalTransform
						* SourcePlacement->LocalTransform,
					TargetModule.Portals[TargetPortalIndex].LocalTransform
						* TargetPlacement->LocalTransform);
		}

		/**
		 * 原子提交当前 DFS Edge 的逻辑状态：路由记录、Strong Socket 占用、Node+Edge->Socket
		 * 映射和 Anchor 已分配列表必须同步增加，供后续 Edge 看到一致的父状态。
		 */
		void CommitRoutedEdge(
			const FSpatialEdge& Edge,
			const FGraphAnchorPlacement& SourceAnchor,
			const FGraphAnchorPlacement& TargetAnchor,
			const FRouteEndpointOption& SourceOption,
			const FRouteEndpointOption& TargetOption,
			const TArray<FIntVector>& Path,
			FPlacementState& State,
			TArray<FRoutedGraphEdge>& InOutRoutedEdges)
		{
			FRoutedGraphEdge& Routed = InOutRoutedEdges.AddDefaulted_GetRef();
			Routed.AbstractEdgeId = Edge.StableEdgeId;
			Routed.SourceNodeId = Edge.StableNodeA;
			Routed.TargetNodeId = Edge.StableNodeB;
			Routed.SourceSocketId = SourceOption.StableSocketId;
			Routed.TargetSocketId = TargetOption.StableSocketId;
			Routed.RouteSignature = SourceOption.Signature;
			Routed.OrderedCells = Path;
			if (SourceAnchor.bStrongAnchor)
			{
				State.UsedSocketKeys.Add(MakeStablePairKey(
					SourceAnchor.StablePlacementId, SourceOption.StableSocketId));
				State.SocketByNodeAndEdge.Add(
					MakeStablePairKey(SourceAnchor.AbstractNodeId, Edge.StableEdgeId),
					SourceOption.StableSocketId);
				State.Anchors[State.AnchorIndexByNodeId.FindChecked(SourceAnchor.AbstractNodeId)]
					.AssignedSocketIds.Add(SourceOption.StableSocketId);
			}
			if (TargetAnchor.bStrongAnchor)
			{
				State.UsedSocketKeys.Add(MakeStablePairKey(
					TargetAnchor.StablePlacementId, TargetOption.StableSocketId));
				State.SocketByNodeAndEdge.Add(
					MakeStablePairKey(TargetAnchor.AbstractNodeId, Edge.StableEdgeId),
					TargetOption.StableSocketId);
				State.Anchors[State.AnchorIndexByNodeId.FindChecked(TargetAnchor.AbstractNodeId)]
					.AssignedSocketIds.Add(TargetOption.StableSocketId);
			}
		}

		/**
		 * CommitRoutedEdge 的严格逆操作。因为 DFS 按栈式提交，同一 Anchor 的 AssignedSocketIds
		 * 可以安全 Pop；任何新增字段都必须同时加入 Commit/Rollback，维持失败原子性。
		 */
		void RollbackRoutedEdge(
			const FSpatialEdge& Edge,
			const FGraphAnchorPlacement& SourceAnchor,
			const FGraphAnchorPlacement& TargetAnchor,
			const FRouteEndpointOption& SourceOption,
			const FRouteEndpointOption& TargetOption,
			FPlacementState& State,
			TArray<FRoutedGraphEdge>& InOutRoutedEdges)
		{
			if (SourceAnchor.bStrongAnchor)
			{
				State.UsedSocketKeys.Remove(MakeStablePairKey(
					SourceAnchor.StablePlacementId, SourceOption.StableSocketId));
				State.SocketByNodeAndEdge.Remove(
					MakeStablePairKey(SourceAnchor.AbstractNodeId, Edge.StableEdgeId));
				State.Anchors[State.AnchorIndexByNodeId.FindChecked(SourceAnchor.AbstractNodeId)]
					.AssignedSocketIds.Pop(EAllowShrinking::No);
			}
			if (TargetAnchor.bStrongAnchor)
			{
				State.UsedSocketKeys.Remove(MakeStablePairKey(
					TargetAnchor.StablePlacementId, TargetOption.StableSocketId));
				State.SocketByNodeAndEdge.Remove(
					MakeStablePairKey(TargetAnchor.AbstractNodeId, Edge.StableEdgeId));
				State.Anchors[State.AnchorIndexByNodeId.FindChecked(TargetAnchor.AbstractNodeId)]
					.AssignedSocketIds.Pop(EAllowShrinking::No);
			}
			InOutRoutedEdges.Pop(EAllowShrinking::No);
		}

		bool PathsEqual(const TArray<FIntVector>& A, const TArray<FIntVector>& B)
		{
			if (A.Num() != B.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < A.Num(); ++Index)
			{
				if (A[Index] != B[Index])
				{
					return false;
				}
			}
			return true;
		}

		/**
		 * Socket 选择与 A* 路线的外层有界 DFS。
		 *
		 * 单独为每条 Edge 取局部最短路是贪心的：早期路线可能占掉后续 Edge 的唯一通道。因此本
		 * 函数按 StableEdgeId 深度优先，枚举两端 Socket 对、规范 A* 路和少量确定性替代路；若后续
		 * Edge 无解，就完整撤销本 Edge 的 Constraints、RoutedEdges 与所有 Socket 状态再试兄弟分支。
		 * 只有预算/配置/不变量错误写入 OutReport 并作为硬失败向上传播；普通候选无解仅返回 false。
		 */
		bool RouteGraphEdgesRecursive(
			const FLayoutRequest& Request,
			const FModuleCatalogSnapshot& Catalog,
			const FZeroEscapeSolverBudgets& Budgets,
			const FConnectorSignature& DefaultSignature,
			const TArray<const FSpatialEdge*>& SortedEdges,
			const int32 EdgeIndex,
			FDeterministicWorkBudget& WorkBudget,
			int32& InOutRouteAttempts,
			int32& InOutLastFailedEdgeId,
			FPlacementState& State,
			TArray<FGridConstraint>& InOutConstraints,
			TArray<FRoutedGraphEdge>& InOutRoutedEdges,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (EdgeIndex == SortedEdges.Num())
			{
				bool bAllRequiredUsed = false;
				return CheckAllRequiredStrongSocketsUsed(
					Catalog, State, WorkBudget, bAllRequiredUsed, OutReport)
					&& bAllRequiredUsed;
			}
			if (!SortedEdges.IsValidIndex(EdgeIndex) || SortedEdges[EdgeIndex] == nullptr)
			{
				Fail(OutReport, EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("Socket/A* DFS 遇到非法 Edge Index。"));
				return false;
			}

			const FSpatialEdge& Edge = *SortedEdges[EdgeIndex];
			InOutLastFailedEdgeId = Edge.StableEdgeId;
			const int32* SourceAnchorIndex = State.AnchorIndexByNodeId.Find(Edge.StableNodeA);
			const int32* TargetAnchorIndex = State.AnchorIndexByNodeId.Find(Edge.StableNodeB);
			if (SourceAnchorIndex == nullptr || TargetAnchorIndex == nullptr)
			{
				Fail(OutReport, EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("Socket/A* Edge 缺少 Graph Anchor。"), 0, 0, Edge.StableEdgeId);
				return false;
			}
			const FGraphAnchorPlacement SourceAnchor = State.Anchors[*SourceAnchorIndex];
			const FGraphAnchorPlacement TargetAnchor = State.Anchors[*TargetAnchorIndex];

			// Direct Strong↔Strong 不需要虚构中间 WFC Cell；离散相邻与实际 Portal Frame 都必须成立。
			if (SourceAnchor.bStrongAnchor && TargetAnchor.bStrongAnchor)
			{
				TArray<FRouteEndpointOption> DirectSourceOptions;
				TArray<FRouteEndpointOption> DirectTargetOptions;
				BuildEndpointOptions(SourceAnchor, Catalog, State, DefaultSignature, false, DirectSourceOptions);
				BuildEndpointOptions(TargetAnchor, Catalog, State, DefaultSignature, false, DirectTargetOptions);
				for (const FRouteEndpointOption& SourceOption : DirectSourceOptions)
				{
					for (const FRouteEndpointOption& TargetOption : DirectTargetOptions)
					{
						if (!AreConnectorSignaturesCompatible(SourceOption.Signature, TargetOption.Signature)
							|| !IsDirectStrongConnection(
								SourceAnchor, TargetAnchor, SourceOption, TargetOption, Catalog, State))
						{
							continue;
						}
						if (!ConsumeRouteAttempt(
								Edge, Budgets, WorkBudget, InOutRouteAttempts, OutReport))
						{
							return false;
						}
						const TArray<FIntVector> EmptyPath;
						CommitRoutedEdge(Edge, SourceAnchor, TargetAnchor,
							SourceOption, TargetOption, EmptyPath, State, InOutRoutedEdges);
						if (RouteGraphEdgesRecursive(
								Request, Catalog, Budgets, DefaultSignature, SortedEdges,
								EdgeIndex + 1, WorkBudget, InOutRouteAttempts,
								InOutLastFailedEdgeId, State, InOutConstraints,
								InOutRoutedEdges, OutReport))
						{
							return true;
						}
						RollbackRoutedEdge(Edge, SourceAnchor, TargetAnchor,
							SourceOption, TargetOption, State, InOutRoutedEdges);
						if (IsHardRouteFailure(OutReport))
						{
							return false;
						}
					}
				}
			}

			TArray<FRouteEndpointOption> SourceOptions;
			TArray<FRouteEndpointOption> TargetOptions;
			if (!BuildEndpointOptions(
					SourceAnchor, Catalog, State, DefaultSignature, true, SourceOptions)
				|| !BuildEndpointOptions(
					TargetAnchor, Catalog, State, DefaultSignature, true, TargetOptions))
			{
				return false;
			}

			for (const FRouteEndpointOption& SourceOption : SourceOptions)
			{
				for (const FRouteEndpointOption& TargetOption : TargetOptions)
				{
					if (!AreConnectorSignaturesCompatible(SourceOption.Signature, TargetOption.Signature))
					{
						continue;
					}
					bool bForeignSource = false;
					bool bForeignTarget = false;
					if (!CheckForeignWeakAnchorCell(
							SourceOption.RouteCell, Edge.StableNodeA, Edge.StableNodeB,
							State, WorkBudget, bForeignSource, OutReport)
						|| !CheckForeignWeakAnchorCell(
							TargetOption.RouteCell, Edge.StableNodeA, Edge.StableNodeB,
							State, WorkBudget, bForeignTarget, OutReport))
					{
						return false;
					}
					if (bForeignSource || bForeignTarget)
					{
						continue;
					}

					TSet<FIntVector> Blocked;
					for (const TPair<FIntVector, int32>& Pair : State.OccupiedPlacementByCell)
					{
						Blocked.Add(Pair.Key);
					}
					for (const FGraphAnchorPlacement& OtherAnchor : State.Anchors)
					{
						if (OtherAnchor.AbstractNodeId != Edge.StableNodeA
							&& OtherAnchor.AbstractNodeId != Edge.StableNodeB
							&& !OtherAnchor.bStrongAnchor)
						{
							Blocked.Add(OtherAnchor.GridCoordinate);
						}
					}
					for (const FGridConstraint& ExistingConstraint : InOutConstraints)
					{
						if (ExistingConstraint.Participation == EGridCellParticipation::ActiveWfc)
						{
							Blocked.Add(ExistingConstraint.Coordinate);
						}
					}
					// 上面的 foreign-anchor 检查通过后，才允许显式端点共享既有 Route Cell。
					Blocked.Remove(SourceOption.RouteCell);
					Blocked.Remove(TargetOption.RouteCell);

					/**
					 * 小型事务边界：先在 Constraints 副本合并路径要求，成功后才交换进共享状态；递归失败
					 * 时按相反顺序撤销 Socket/Route 并恢复原 Constraints。这样路径约束冲突不会泄漏到
					 * 下一个 Socket 对或替代路径。
					 */
					auto TryPathAndDescend = [&](const TArray<FIntVector>& Path) -> bool
					{
						TArray<FGridConstraint> CandidateConstraints = InOutConstraints;
						TMap<FIntVector, int32> CandidateIndex;
						for (int32 Index = 0; Index < CandidateConstraints.Num(); ++Index)
						{
							CandidateIndex.Add(CandidateConstraints[Index].Coordinate, Index);
						}
						FZeroEscapeGenerationReport CandidateReport = OutReport;
						if (!ApplyRoutedEdgeConstraints(
								SourceAnchor, TargetAnchor, SourceOption, TargetOption, Path,
								CandidateConstraints, CandidateIndex, CandidateReport))
						{
							return false;
						}

						TArray<FGridConstraint> SavedConstraints = MoveTemp(InOutConstraints);
						InOutConstraints = MoveTemp(CandidateConstraints);
						CommitRoutedEdge(Edge, SourceAnchor, TargetAnchor,
							SourceOption, TargetOption, Path, State, InOutRoutedEdges);
						if (RouteGraphEdgesRecursive(
								Request, Catalog, Budgets, DefaultSignature, SortedEdges,
								EdgeIndex + 1, WorkBudget, InOutRouteAttempts,
								InOutLastFailedEdgeId, State, InOutConstraints,
								InOutRoutedEdges, OutReport))
						{
							return true;
						}
						RollbackRoutedEdge(Edge, SourceAnchor, TargetAnchor,
							SourceOption, TargetOption, State, InOutRoutedEdges);
						InOutConstraints = MoveTemp(SavedConstraints);
						return false;
					};

					if (!ConsumeRouteAttempt(
							Edge, Budgets, WorkBudget, InOutRouteAttempts, OutReport))
					{
						return false;
					}
					TArray<FIntVector> CanonicalPath;
					FZeroEscapeGenerationReport PathReport = OutReport;
					if (!FLayoutSolver::FindGridPath(
							Request.GridExtent, Blocked,
							SourceOption.RouteCell, TargetOption.RouteCell,
							Request.AStarStraightStepCost, Request.AStarTurnPenalty,
							Budgets.MaxAStarExpandedStates, WorkBudget,
							CanonicalPath, PathReport))
					{
						if (PathReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded)
						{
							OutReport = MoveTemp(PathReport);
							return false;
						}
						continue;
					}

					TArray<TArray<FIntVector>> SeenPaths;
					SeenPaths.Add(CanonicalPath);
					if (TryPathAndDescend(CanonicalPath))
					{
						return true;
					}
					if (IsHardRouteFailure(OutReport))
					{
						return false;
					}

					// 有界地避开规范路径的每个内部 Cell，给上层 DFS 提供确定性的替代路径。
					int32 AlternateProbeCount = 0;
					for (int32 BlockIndex = 1;
						BlockIndex + 1 < CanonicalPath.Num()
							&& AlternateProbeCount < MaxAlternatePathProbesPerSocketPair;
						++BlockIndex)
					{
						++AlternateProbeCount;
						if (!ConsumeRouteAttempt(
								Edge, Budgets, WorkBudget, InOutRouteAttempts, OutReport))
						{
							return false;
						}
						TSet<FIntVector> AlternateBlocked = Blocked;
						AlternateBlocked.Add(CanonicalPath[BlockIndex]);
						TArray<FIntVector> AlternatePath;
						FZeroEscapeGenerationReport AlternateReport = OutReport;
						if (!FLayoutSolver::FindGridPath(
								Request.GridExtent, AlternateBlocked,
								SourceOption.RouteCell, TargetOption.RouteCell,
								Request.AStarStraightStepCost, Request.AStarTurnPenalty,
								Budgets.MaxAStarExpandedStates, WorkBudget,
								AlternatePath, AlternateReport))
						{
							if (AlternateReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded)
							{
								OutReport = MoveTemp(AlternateReport);
								return false;
							}
							continue;
						}
						if (SeenPaths.ContainsByPredicate(
								[&AlternatePath](const TArray<FIntVector>& Existing)
								{
									return PathsEqual(Existing, AlternatePath);
								}))
						{
							continue;
						}
						SeenPaths.Add(AlternatePath);
						if (TryPathAndDescend(AlternatePath))
						{
							return true;
						}
						if (IsHardRouteFailure(OutReport))
						{
							return false;
						}
					}
				}
			}
			return false;
		}
	}

	/**
	 * 路由阶段入口。选择 Catalog 中稳定最小的 WFC 开口签名作为 Weak Anchor 默认签名，随后
	 * 对排序后的全部抽象 Edge 运行有界 DFS。成功后把 Active 区域外露且尚无要求的面封闭，
	 * 再附加 ReservedSocket Cell 供后续重叠验证；WFC 只消费 ActiveWfc 项。
	 */
	bool FLayoutSolver::RouteGraphEdgesWithAStar(
		const FLayoutRequest& Request,
		const FModuleCatalogSnapshot& Catalog,
		const FZeroEscapeSolverBudgets& Budgets,
		FDeterministicWorkBudget& WorkBudget,
		FPlacementState& State,
		TArray<FGridConstraint>& OutConstraints,
		TArray<FRoutedGraphEdge>& OutRoutedEdges,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutConstraints.Reset();
		OutRoutedEdges.Reset();
		FConnectorSignature DefaultSignature;
		if (!FindCanonicalRouteSignature(Catalog, DefaultSignature))
		{
			Fail(OutReport, EZeroEscapeGenerationStage::SocketLayout,
				EZeroEscapeGenerationFailure::MissingModuleRole,
				TEXT("WFC Catalog 没有可用的平面开口 Connector。"));
			return false;
		}

		TArray<const FSpatialEdge*> SortedEdges;
		for (const FSpatialEdge& Edge : Request.AbstractPlan->Edges)
		{
			SortedEdges.Add(&Edge);
		}
		SortedEdges.Sort(
			[](const FSpatialEdge& A, const FSpatialEdge& B)
			{
				return A.StableEdgeId < B.StableEdgeId;
			});
		if (SortedEdges.IsEmpty() || SortedEdges.Num() > Budgets.MaxAStarRouteAttempts)
		{
			Fail(OutReport, EZeroEscapeGenerationStage::SocketLayout,
				EZeroEscapeGenerationFailure::SearchBudgetExceeded,
				TEXT("抽象 Edge 数超过 Socket/A* Route Attempt 预算。"),
				SortedEdges.Num(), Budgets.MaxAStarRouteAttempts);
			return false;
		}

		int32 RouteAttempts = 0;
		int32 LastFailedEdgeId = INDEX_NONE;
		if (!RouteGraphEdgesRecursive(
				Request, Catalog, Budgets, DefaultSignature, SortedEdges, 0,
				WorkBudget, RouteAttempts, LastFailedEdgeId, State,
				OutConstraints, OutRoutedEdges, OutReport))
		{
			if (!IsHardRouteFailure(OutReport))
			{
				Fail(OutReport, EZeroEscapeGenerationStage::SocketLayout,
					EZeroEscapeGenerationFailure::SocketPlacementNoSolution,
					TEXT("有界 Socket/A* DFS 穷尽候选后无解。"),
					RouteAttempts, Budgets.MaxAStarRouteAttempts, LastFailedEdgeId);
			}
			return false;
		}

		TSet<FIntVector> ActiveCoordinates;
		for (const FGridConstraint& Constraint : OutConstraints)
		{
			ActiveCoordinates.Add(Constraint.Coordinate);
		}
		for (FGridConstraint& Constraint : OutConstraints)
		{
			for (int32 Direction = 0; Direction < PlanarDirectionCount; ++Direction)
			{
				if (!ActiveCoordinates.Contains(Constraint.Coordinate + DirectionDeltas[Direction])
					&& Constraint.Directions[Direction].Rule == EConnectorConstraintRule::Unconstrained)
				{
					FConnectorSignature Closed;
					if (!MergeDirectionalConstraint(
							Constraint,
							Direction,
							EConnectorConstraintRule::MustBeClosed,
							Closed,
							OutReport))
					{
						return false;
					}
				}
			}
		}

		for (const TPair<FIntVector, int32>& Occupied : State.OccupiedPlacementByCell)
		{
			FGridConstraint Reserved;
			Reserved.Coordinate = Occupied.Key;
			Reserved.Participation = EGridCellParticipation::ReservedSocket;
			OutConstraints.Add(MoveTemp(Reserved));
		}
		OutConstraints.Sort(
			[](const FGridConstraint& A, const FGridConstraint& B)
			{
				if (A.Coordinate == B.Coordinate)
				{
					return static_cast<uint8>(A.Participation) < static_cast<uint8>(B.Participation);
				}
				return IsSameCoordinateOrder(A.Coordinate, B.Coordinate);
			});
		return true;
	}

	namespace
	{
		/**
		 * 将完全坍缩的 WFC 结果回填为 Placement。
		 * Active Constraints 与 SolveWfc 都使用相同 Z/Y/X 排序，这是 Variant 数组与 GridCoordinate
		 * 的位置契约；任何数量、索引或占格不一致都在写入后续 Plan 前失败。
		 */
		bool AppendWfcPlacements(
			const FLayoutRequest& Request,
			const FModuleCatalogSnapshot& Catalog,
			const TArray<FGridConstraint>& Constraints,
			const TArray<FTileVariant>& Variants,
			const TArray<int32>& VariantByActiveCell,
			FPlacementState& State,
			FZeroEscapeGenerationReport& OutReport)
		{
			TArray<FGridConstraint> Active;
			for (const FGridConstraint& Constraint : Constraints)
			{
				if (Constraint.Participation == EGridCellParticipation::ActiveWfc)
				{
					Active.Add(Constraint);
				}
			}
			Active.Sort(
				[](const FGridConstraint& A, const FGridConstraint& B)
				{
					return IsSameCoordinateOrder(A.Coordinate, B.Coordinate);
				});
			if (Active.Num() != VariantByActiveCell.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < Active.Num(); ++Index)
			{
				if (!Variants.IsValidIndex(VariantByActiveCell[Index]))
				{
					return false;
				}
				const FTileVariant& Variant = Variants[VariantByActiveCell[Index]];
				if (!Catalog.Modules.IsValidIndex(Variant.ModuleIndex)
					|| State.OccupiedPlacementByCell.Contains(Active[Index].Coordinate))
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::GeometryOverlap,
						TEXT("WFC 回填 Cell 与 Socket 占格重叠或 Variant 越界。"));
					return false;
				}
				FInternalPlacement& Placement = State.Placements.AddDefaulted_GetRef();
				Placement.StablePlacementId = State.NextStablePlacementId++;
				Placement.ModuleIndex = Variant.ModuleIndex;
				Placement.StableVariantId = Variant.StableVariantId;
				Placement.QuarterTurns = Variant.QuarterTurns;
				Placement.GridOrigin = Active[Index].Coordinate;
				Placement.LocalTransform = MakeGridModuleTransform(
					Placement.GridOrigin,
					FIntVector(1, 1, 1),
					Request.CellSize,
					Placement.QuarterTurns);
				Placement.bWfcPlacement = true;
				State.OccupiedPlacementByCell.Add(Placement.GridOrigin, Placement.StablePlacementId);
			}
			RebuildPlacementIndices(State);
			return true;
		}

		int32 FindWfcSocketForDirection(
			const FInternalPlacement& Placement,
			const TArray<FTileVariant>& Variants,
			const int32 Direction)
		{
			const FTileVariant* Variant = Variants.FindByPredicate(
				[&Placement](const FTileVariant& Item)
				{
					return Item.StableVariantId == Placement.StableVariantId;
				});
			return Variant != nullptr ? Variant->StableSocketIds[Direction] : INDEX_NONE;
		}

		/**
		 * 添加 Portal 连接并维护“一条 Portal 最多一个对端”的集合不变量。
		 * 完全相同的无向连接重复发现是幂等的；同一端点指向另一对端则是结构错误。
		 */
		bool AddConnectionDraft(
			const FConnectionDraft& Draft,
			TArray<FConnectionDraft>& Connections,
			TSet<uint64>& ConnectedPortalKeys,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (Draft.PlacementA < 0 || Draft.PlacementB < 0
				|| Draft.SocketA < 0 || Draft.SocketB < 0
				|| Draft.PlacementA == Draft.PlacementB)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::PortalMismatch,
					TEXT("导出 PortalConnection 时端点或 Socket Id 非法。"));
				return false;
			}
			const uint64 KeyA = MakeStablePairKey(Draft.PlacementA, Draft.SocketA);
			const uint64 KeyB = MakeStablePairKey(Draft.PlacementB, Draft.SocketB);
			const bool bAUsed = ConnectedPortalKeys.Contains(KeyA);
			const bool bBUsed = ConnectedPortalKeys.Contains(KeyB);
			if (bAUsed || bBUsed)
			{
				const bool bSameExisting = Connections.ContainsByPredicate(
					[&Draft](const FConnectionDraft& Existing)
					{
						return (Existing.PlacementA == Draft.PlacementA
							&& Existing.SocketA == Draft.SocketA
							&& Existing.PlacementB == Draft.PlacementB
							&& Existing.SocketB == Draft.SocketB)
							|| (Existing.PlacementA == Draft.PlacementB
								&& Existing.SocketA == Draft.SocketB
								&& Existing.PlacementB == Draft.PlacementA
								&& Existing.SocketB == Draft.SocketA);
					});
				if (!bSameExisting)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::PortalMismatch,
						TEXT("同一 Portal 被尝试连接到多个不同对端。"));
					return false;
				}
				return true;
			}
			Connections.Add(Draft);
			ConnectedPortalKeys.Add(KeyA);
			ConnectedPortalKeys.Add(KeyB);
			return true;
		}

		/**
		 * Finalize 第一步：把每条抽象 Edge 的 Strong 端点和顺序 WFC Cell 转换成 Placement 链，
		 * 再从相邻 Placement 的方向解析两端 Socket。额外扫描 WFC 邻格，把兼容坍缩自然形成的
		 * 局部环也导出为真实 PortalConnection，而不改变抽象必需路线绑定。
		 */
		bool BuildConnectionsAndEdgeRoutes(
			const FModuleCatalogSnapshot& Catalog,
			const TArray<FTileVariant>& Variants,
			const TArray<FRoutedGraphEdge>& RoutedEdges,
			FPlacementState& State,
			TArray<FConnectionDraft>& OutConnections,
			TSet<uint64>& OutConnectedPortalKeys,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			TMap<FIntVector, int32> WfcPlacementByCoordinate;
			for (const FInternalPlacement& Placement : State.Placements)
			{
				if (Placement.bWfcPlacement)
				{
					WfcPlacementByCoordinate.Add(Placement.GridOrigin, Placement.StablePlacementId);
				}
			}

			for (const FRoutedGraphEdge& Routed : RoutedEdges)
			{
				const int32 SourceAnchorIndex = State.AnchorIndexByNodeId.FindChecked(Routed.SourceNodeId);
				const int32 TargetAnchorIndex = State.AnchorIndexByNodeId.FindChecked(Routed.TargetNodeId);
				const FGraphAnchorPlacement& SourceAnchor = State.Anchors[SourceAnchorIndex];
				const FGraphAnchorPlacement& TargetAnchor = State.Anchors[TargetAnchorIndex];
				FZeroEscapeEdgeRouteBinding& EdgeBinding = OutPlan.EdgeRoutes.AddDefaulted_GetRef();
				EdgeBinding.AbstractEdgeId = Routed.AbstractEdgeId;
				EdgeBinding.FromNodeId = Routed.SourceNodeId;
				EdgeBinding.ToNodeId = Routed.TargetNodeId;
				if (SourceAnchor.bStrongAnchor)
				{
					EdgeBinding.OrderedStablePlacementIds.Add(SourceAnchor.StablePlacementId);
				}
				for (const FIntVector& Cell : Routed.OrderedCells)
				{
					const int32* PlacementId = WfcPlacementByCoordinate.Find(Cell);
					if (PlacementId == nullptr)
					{
						return false;
					}
					if (EdgeBinding.OrderedStablePlacementIds.IsEmpty()
						|| EdgeBinding.OrderedStablePlacementIds.Last() != *PlacementId)
					{
						EdgeBinding.OrderedStablePlacementIds.Add(*PlacementId);
					}
				}
				if (TargetAnchor.bStrongAnchor
					&& (EdgeBinding.OrderedStablePlacementIds.IsEmpty()
						|| EdgeBinding.OrderedStablePlacementIds.Last() != TargetAnchor.StablePlacementId))
				{
					EdgeBinding.OrderedStablePlacementIds.Add(TargetAnchor.StablePlacementId);
				}

				for (int32 LinkIndex = 1;
					LinkIndex < EdgeBinding.OrderedStablePlacementIds.Num();
					++LinkIndex)
				{
					const int32 PlacementAId = EdgeBinding.OrderedStablePlacementIds[LinkIndex - 1];
					const int32 PlacementBId = EdgeBinding.OrderedStablePlacementIds[LinkIndex];
					const FInternalPlacement* PlacementA = FindInternalPlacement(State, PlacementAId);
					const FInternalPlacement* PlacementB = FindInternalPlacement(State, PlacementBId);
					if (PlacementA == nullptr || PlacementB == nullptr)
					{
						return false;
					}
					FConnectionDraft Draft;
					Draft.AbstractEdgeId = Routed.AbstractEdgeId;
					Draft.PlacementA = PlacementAId;
					Draft.PlacementB = PlacementBId;
					if (!PlacementA->bWfcPlacement && !PlacementB->bWfcPlacement)
					{
						// Strong↔Strong 直连没有中间 WFC Placement，Socket 已在路由 DFS 中成对冻结。
						Draft.SocketA = Routed.SourceSocketId;
						Draft.SocketB = Routed.TargetSocketId;
					}
					else if (!PlacementA->bWfcPlacement)
					{
						Draft.SocketA = Routed.SourceSocketId;
						const FModuleSnapshot& SourceModule = Catalog.Modules[PlacementA->ModuleIndex];
						const int32 PortalIndex = FindPortalIndexByStableId(SourceModule, Draft.SocketA);
						if (PortalIndex == INDEX_NONE)
						{
							return false;
						}
						const int32 Outward = ToPlanarDirectionIndex(RotateDirection(
							SourceModule.Portals[PortalIndex].Direction, PlacementA->QuarterTurns));
						Draft.SocketB = FindWfcSocketForDirection(
							*PlacementB, Variants, OppositeDirectionIndex(Outward));
					}
					else if (!PlacementB->bWfcPlacement)
					{
						Draft.SocketB = Routed.TargetSocketId;
						const FModuleSnapshot& TargetModule = Catalog.Modules[PlacementB->ModuleIndex];
						const int32 PortalIndex = FindPortalIndexByStableId(TargetModule, Draft.SocketB);
						if (PortalIndex == INDEX_NONE)
						{
							return false;
						}
						const int32 TargetOutward = ToPlanarDirectionIndex(RotateDirection(
							TargetModule.Portals[PortalIndex].Direction, PlacementB->QuarterTurns));
						Draft.SocketA = FindWfcSocketForDirection(
							*PlacementA, Variants, OppositeDirectionIndex(TargetOutward));
					}
					else
					{
						const int32 Direction = DirectionIndexFromDelta(
							PlacementB->GridOrigin - PlacementA->GridOrigin);
						if (Direction == INDEX_NONE)
						{
							return false;
						}
						Draft.SocketA = FindWfcSocketForDirection(*PlacementA, Variants, Direction);
						Draft.SocketB = FindWfcSocketForDirection(
							*PlacementB, Variants, OppositeDirectionIndex(Direction));
					}
					if (!AddConnectionDraft(Draft, OutConnections, OutConnectedPortalKeys, OutReport))
					{
						return false;
					}
				}
			}

			// WFC 可以在必需路线外形成局部环；把所有双向开口邻格也导出为连接。
			for (const FInternalPlacement& A : State.Placements)
			{
				if (!A.bWfcPlacement)
				{
					continue;
				}
				for (const int32 Direction : { 0, 1 })
				{
					const int32* BId = WfcPlacementByCoordinate.Find(A.GridOrigin + DirectionDeltas[Direction]);
					if (BId == nullptr)
					{
						continue;
					}
					const FInternalPlacement* B = FindInternalPlacement(State, *BId);
					if (B == nullptr)
					{
						return false;
					}
					const int32 SocketA = FindWfcSocketForDirection(A, Variants, Direction);
					const int32 SocketB = FindWfcSocketForDirection(
						*B, Variants, OppositeDirectionIndex(Direction));
					if (SocketA == INDEX_NONE && SocketB == INDEX_NONE)
					{
						continue;
					}
					FConnectionDraft Draft;
					Draft.PlacementA = A.StablePlacementId;
					Draft.SocketA = SocketA;
					Draft.PlacementB = B->StablePlacementId;
					Draft.SocketB = SocketB;
					if (!AddConnectionDraft(Draft, OutConnections, OutConnectedPortalKeys, OutReport))
					{
						return false;
					}
				}
			}
			return true;
		}

		/**
		 * Closure 阶段只处理 Strong SocketModule 上未连接的特殊 Portal。
		 * Required Portal 留空直接失败；Sealable Portal 必须引用一个单 Portal ClosureModule，并用
		 * Portal Frame 精确求出封口 Transform。Closure 是独立 Placement，但不占用额外逻辑路由格。
		 * 本函数只修改本 Attempt 的 State/CandidatePlan，后续失败会由 Solve 整体丢弃。
		 */
		bool CloseUnusedSpecialPortals(
			const FModuleCatalogSnapshot& Catalog,
			const TSet<uint64>& ConnectedPortalKeys,
			FPlacementState& State,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int32 OriginalPlacementCount = State.Placements.Num();
			for (int32 PlacementIndex = 0; PlacementIndex < OriginalPlacementCount; ++PlacementIndex)
			{
				// 添加封口可能触发 Placements 重分配，因此复制父 Placement，避免 Portal 循环中引用失效。
				const FInternalPlacement Placement = State.Placements[PlacementIndex];
				if (Placement.bWfcPlacement || Placement.bClosurePlacement
					|| !Catalog.Modules.IsValidIndex(Placement.ModuleIndex))
				{
					continue;
				}
				const FModuleSnapshot& Module = Catalog.Modules[Placement.ModuleIndex];
				for (const FZeroEscapeModulePortal& Portal : Module.Portals)
				{
					const uint64 PortalKey = MakeStablePairKey(
						Placement.StablePlacementId, Portal.StableSocketId);
					if (ConnectedPortalKeys.Contains(PortalKey))
					{
						continue;
					}
					if (Portal.Policy == EZeroEscapeSocketPolicy::Required)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::PortalMismatch,
							TEXT("未使用的 Required Socket 不能被封口。"),
							0,
							0,
							Module.StableModuleId);
						return false;
					}
					if (Portal.Policy != EZeroEscapeSocketPolicy::Sealable)
					{
						return false;
					}
					int32 CapModuleIndex = INDEX_NONE;
					const FModuleSnapshot* Cap = FindModuleByStableId(
						Catalog, Portal.ClosureModuleId, &CapModuleIndex);
					if (Cap == nullptr || Cap->Portals.Num() != 1)
					{
						return false;
					}
					uint8 CapQuarterTurns = 0;
					bool bFoundRotation = false;
					const EZeroEscapeCardinalDirection ParentDirection = RotateDirection(
						Portal.Direction, Placement.QuarterTurns);
					for (uint8 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
					{
						if ((Cap->AllowedQuarterTurnsMask & (1 << QuarterTurns)) != 0
							&& RotateDirection(Cap->Portals[0].Direction, QuarterTurns)
								== OppositeDirection(ParentDirection))
						{
							CapQuarterTurns = QuarterTurns;
							bFoundRotation = true;
							break;
						}
					}
					if (!bFoundRotation)
					{
						return false;
					}
					const FTransform TargetPortal = Portal.LocalTransform * Placement.LocalTransform;
					const FTransform CapTransform = SolveModuleLocalTransform(
						TargetPortal, Cap->Portals[0].LocalTransform);
					if (!IsFiniteUnitScaleTransform(CapTransform))
					{
						return false;
					}
					FInternalPlacement& CapPlacement = State.Placements.AddDefaulted_GetRef();
					CapPlacement.StablePlacementId = State.NextStablePlacementId++;
					CapPlacement.ModuleIndex = CapModuleIndex;
					CapPlacement.QuarterTurns = CapQuarterTurns;
					CapPlacement.GridOrigin = Placement.GridOrigin;
					CapPlacement.LocalTransform = CapTransform;
					CapPlacement.bClosurePlacement = true;
					FZeroEscapeClosedPortal& Closed = OutPlan.ClosedPortals.AddDefaulted_GetRef();
					Closed.StablePlacementId = Placement.StablePlacementId;
					Closed.StableSocketId = Portal.StableSocketId;
					Closed.StableClosurePlacementId = CapPlacement.StablePlacementId;
				}
			}
			RebuildPlacementIndices(State);
			return true;
		}

		/**
		 * Finalize 第二步：按 StablePlacementId 导出模块与 GameplayAnchor，再建立抽象 Node/Objective
		 * 绑定。Strong Anchor 绑定预放置房间；Weak Anchor 必须绑定自己坐标上的 WFC Placement。
		 * Start/Exit 最终还必须能解析到各自唯一的 PlayerSpawn/Exit Anchor 实例。
		 */
		bool ExportModulesAndAnchors(
			const FLayoutRequest& Request,
			const FModuleCatalogSnapshot& Catalog,
			FPlacementState& State,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			State.Placements.Sort(
				[](const FInternalPlacement& A, const FInternalPlacement& B)
				{
					return A.StablePlacementId < B.StablePlacementId;
				});
			RebuildPlacementIndices(State);
			for (const FInternalPlacement& Internal : State.Placements)
			{
				if (!Catalog.Modules.IsValidIndex(Internal.ModuleIndex))
				{
					return false;
				}
				const FModuleSnapshot& Module = Catalog.Modules[Internal.ModuleIndex];
				FZeroEscapePlacedModule& Exported = OutPlan.Modules.AddDefaulted_GetRef();
				Exported.StablePlacementId = Internal.StablePlacementId;
				Exported.StableModuleId = Module.StableModuleId;
				Exported.StableVariantId = Internal.StableVariantId;
				Exported.QuarterTurns = Internal.QuarterTurns;
				Exported.AbstractNodeId = Internal.AbstractNodeId;
				Exported.GridOrigin = Internal.GridOrigin;
				Exported.LocalTransform = Internal.LocalTransform;
				for (const FZeroEscapeModuleAnchor& Anchor : Module.GameplayAnchors)
				{
					FZeroEscapeGeneratedAnchor& Generated = OutPlan.GameplayAnchors.AddDefaulted_GetRef();
					Generated.StableAnchorInstanceId = OutPlan.GameplayAnchors.Num() - 1;
					Generated.Type = Anchor.Type;
					Generated.StablePlacementId = Internal.StablePlacementId;
					Generated.StableModuleAnchorId = Anchor.StableAnchorId;
					Generated.LocalTransform = Anchor.LocalTransform * Internal.LocalTransform;
				}
			}

			TMap<FIntVector, int32> WfcPlacementByCoordinate;
			for (const FInternalPlacement& Placement : State.Placements)
			{
				if (Placement.bWfcPlacement)
				{
					WfcPlacementByCoordinate.Add(Placement.GridOrigin, Placement.StablePlacementId);
				}
			}
			for (const FGraphAnchorPlacement& Anchor : State.Anchors)
			{
				int32 PlacementId = Anchor.StablePlacementId;
				if (!Anchor.bStrongAnchor)
				{
					const int32* RoutePlacementId = WfcPlacementByCoordinate.Find(Anchor.GridCoordinate);
					if (RoutePlacementId == nullptr)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::InvalidGraph,
							TEXT("Route Anchor 没有被 WFC Placement 回填。"),
							0,
							0,
							Anchor.AbstractNodeId);
						return false;
					}
					PlacementId = *RoutePlacementId;
				}
				if (PlacementId < 0)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("Route Anchor 没有被 WFC Placement 回填。"),
						0,
						0,
						Anchor.AbstractNodeId);
					return false;
				}
				FZeroEscapeNodePlacementBinding& Binding = OutPlan.NodeBindings.AddDefaulted_GetRef();
				Binding.AbstractNodeId = Anchor.AbstractNodeId;
				Binding.StablePlacementId = PlacementId;
			}

			const FZeroEscapeNodePlacementBinding* StartBinding = OutPlan.NodeBindings.FindByPredicate(
				[&Request](const FZeroEscapeNodePlacementBinding& Binding)
				{
					return Binding.AbstractNodeId == Request.AbstractPlan->StartStableNodeId;
				});
			const FZeroEscapeNodePlacementBinding* ExitBinding = OutPlan.NodeBindings.FindByPredicate(
				[&Request](const FZeroEscapeNodePlacementBinding& Binding)
				{
					return Binding.AbstractNodeId == Request.AbstractPlan->ExitStableNodeId;
				});
			if (StartBinding == nullptr || ExitBinding == nullptr)
			{
				return false;
			}
			OutPlan.StartPlacementId = StartBinding->StablePlacementId;
			OutPlan.ExitPlacementId = ExitBinding->StablePlacementId;
			for (const FZeroEscapeGeneratedAnchor& Anchor : OutPlan.GameplayAnchors)
			{
				if (Anchor.StablePlacementId == OutPlan.StartPlacementId
					&& Anchor.Type == EZeroEscapeGameplayAnchorType::PlayerSpawn)
				{
					OutPlan.PlayerSpawnAnchorInstanceId = Anchor.StableAnchorInstanceId;
				}
				if (Anchor.StablePlacementId == OutPlan.ExitPlacementId
					&& Anchor.Type == EZeroEscapeGameplayAnchorType::Exit)
				{
					OutPlan.ExitAnchorInstanceId = Anchor.StableAnchorInstanceId;
				}
			}

			for (const FObjectivePlacement& Objective : Request.AbstractPlan->Objectives)
			{
				const FZeroEscapeNodePlacementBinding* NodeBinding = OutPlan.NodeBindings.FindByPredicate(
					[&Objective](const FZeroEscapeNodePlacementBinding& Binding)
					{
						return Binding.AbstractNodeId == Objective.StableNodeId;
					});
				if (NodeBinding == nullptr)
				{
					return false;
				}
				const FZeroEscapeGeneratedAnchor* ObjectiveAnchor = OutPlan.GameplayAnchors.FindByPredicate(
					[NodeBinding](const FZeroEscapeGeneratedAnchor& Anchor)
					{
						return Anchor.StablePlacementId == NodeBinding->StablePlacementId
							&& Anchor.Type == EZeroEscapeGameplayAnchorType::Objective;
					});
				if (ObjectiveAnchor == nullptr)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::MissingModuleRole,
						TEXT("Objective Node 的最终模块缺少 Objective Anchor。"),
						0,
						0,
						Objective.StableNodeId);
					return false;
				}
				FZeroEscapeObjectiveBinding& ObjectiveBinding = OutPlan.ObjectiveBindings.AddDefaulted_GetRef();
				ObjectiveBinding.StableObjectiveId = Objective.StableObjectiveId;
				ObjectiveBinding.AbstractNodeId = Objective.StableNodeId;
				ObjectiveBinding.StablePlacementId = NodeBinding->StablePlacementId;
				ObjectiveBinding.StableAnchorInstanceId = ObjectiveAnchor->StableAnchorInstanceId;
			}
			return OutPlan.PlayerSpawnAnchorInstanceId >= 0 && OutPlan.ExitAnchorInstanceId >= 0;
		}

		struct FValidatedPortalEndpoint
		{
			const FZeroEscapePlacedModule* Placement = nullptr;
			const FModuleSnapshot* Module = nullptr;
			const FZeroEscapeModulePortal* Portal = nullptr;
		};

		struct FPlacementProgressionState
		{
			int32 StablePlacementId = INDEX_NONE;
			uint32 CollectedMask = 0;

			friend bool operator==(
				const FPlacementProgressionState& A,
				const FPlacementProgressionState& B)
			{
				return A.StablePlacementId == B.StablePlacementId
					&& A.CollectedMask == B.CollectedMask;
			}

			friend uint32 GetTypeHash(const FPlacementProgressionState& State)
			{
				return HashCombineFast(
					GetTypeHash(State.StablePlacementId),
					GetTypeHash(State.CollectedMask));
			}
		};

		uint64 MakeUndirectedPlacementKey(const int32 A, const int32 B)
		{
			return A < B ? MakeStablePairKey(A, B) : MakeStablePairKey(B, A);
		}

		bool AreOpposedPortalFrames(const FTransform& A, const FTransform& B)
		{
			return IsFiniteUnitScaleTransform(A)
				&& IsFiniteUnitScaleTransform(B)
				&& A.GetTranslation().Equals(B.GetTranslation(), PortalAlignmentToleranceCm)
				&& FMath::IsNearlyEqual(
					A.GetRotation().GetAxisX().Dot(B.GetRotation().GetAxisX()),
					-1.0f,
					DirectionDotTolerance)
				&& FMath::IsNearlyEqual(
					A.GetRotation().GetAxisZ().Dot(B.GetRotation().GetAxisZ()),
					1.0f,
					DirectionDotTolerance);
		}

		bool AreEquivalentTransforms(const FTransform& A, const FTransform& B)
		{
			return IsFiniteUnitScaleTransform(A)
				&& IsFiniteUnitScaleTransform(B)
				&& A.GetTranslation().Equals(B.GetTranslation(), PortalAlignmentToleranceCm)
				&& FMath::Abs(A.GetRotation() | B.GetRotation()) >= DirectionDotTolerance;
		}

		/**
		 * 对“最终实际 Placement/Portal 图”做权威验证，而不是信任抽象图或中间路由。
		 * 验证覆盖稳定 Id、模块/Variant 身份、占格、Portal 一对一连接与 Frame 对齐、Closure、
		 * Node/Edge/Objective/GameplayAnchor 映射、Start->Exit 可达性，以及携带 collected-mask 的
		 * K-of-N 状态搜索。验证本身也消费全局预算，防止恶劣输入让收尾阶段失去实时上界。
		 */
		bool ValidateGlobalPlan(
			const FLayoutRequest& Request,
			const FModuleCatalogSnapshot& Catalog,
			const FZeroEscapeGeneratedLevelPlan& Plan,
			FDeterministicWorkBudget& WorkBudget,
			FZeroEscapeGenerationReport& OutReport)
		{
			auto ConsumeValidationWork = [&WorkBudget, &OutReport](
				const TCHAR* Message,
				const int32 Units = 1) -> bool
			{
				if (WorkBudget.TryConsume(Units))
				{
					return true;
				}
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SearchBudgetExceeded,
					Message);
				return false;
			};

			if (Plan.Modules.IsEmpty()
				|| Plan.NodeBindings.Num() != Request.AbstractPlan->Nodes.Num()
				|| Plan.EdgeRoutes.Num() != Request.AbstractPlan->Edges.Num()
				|| Plan.ObjectiveBindings.Num() != Request.AbstractPlan->Objectives.Num()
				|| Plan.StartPlacementId < 0 || Plan.ExitPlacementId < 0
				|| Plan.PlayerSpawnAnchorInstanceId < 0 || Plan.ExitAnchorInstanceId < 0)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("最终 Plan 没有完整覆盖 Node/Edge/Objective/Start/Exit 契约。"));
				return false;
			}

			TMap<int32, const FSpatialNode*> AbstractNodeById;
			TMap<int32, const FSpatialEdge*> AbstractEdgeById;
			TMap<int32, const FObjectivePlacement*> AbstractObjectiveById;
			for (const FSpatialNode& Node : Request.AbstractPlan->Nodes)
			{
				if (!ConsumeValidationWork(TEXT("全局验证构建抽象 Node 索引时耗尽预算。"))
					|| Node.StableNodeId < 0 || AbstractNodeById.Contains(Node.StableNodeId))
				{
					if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
					{
						Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::InvalidGraph,
							TEXT("最终验证发现非法或重复 Abstract Node Id。"));
					}
					return false;
				}
				AbstractNodeById.Add(Node.StableNodeId, &Node);
			}
			for (const FSpatialEdge& Edge : Request.AbstractPlan->Edges)
			{
				if (!ConsumeValidationWork(TEXT("全局验证构建抽象 Edge 索引时耗尽预算。"))
					|| Edge.StableEdgeId < 0 || AbstractEdgeById.Contains(Edge.StableEdgeId))
				{
					if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
					{
						Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::InvalidGraph,
							TEXT("最终验证发现非法或重复 Abstract Edge Id。"));
					}
					return false;
				}
				AbstractEdgeById.Add(Edge.StableEdgeId, &Edge);
			}
			for (const FObjectivePlacement& Objective : Request.AbstractPlan->Objectives)
			{
				if (!ConsumeValidationWork(TEXT("全局验证构建 Objective 索引时耗尽预算。"))
					|| Objective.StableObjectiveId < 0
					|| AbstractObjectiveById.Contains(Objective.StableObjectiveId))
				{
					if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
					{
						Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::InvalidGraph,
							TEXT("最终验证发现非法或重复 Objective Id。"));
					}
					return false;
				}
				AbstractObjectiveById.Add(Objective.StableObjectiveId, &Objective);
			}

			TMap<int32, int32> PlacementIndexById;
			TMap<int32, int32> CatalogIndexByPlacementId;
			TMap<FIntVector, int32> StructuralOccupancy;
			TMap<int32, TArray<int32>> PlacementGraph;
			for (int32 PlacementIndex = 0; PlacementIndex < Plan.Modules.Num(); ++PlacementIndex)
			{
				if (!ConsumeValidationWork(TEXT("全局验证检查 Placement 时耗尽预算。")))
				{
					return false;
				}
				const FZeroEscapePlacedModule& Placement = Plan.Modules[PlacementIndex];
				int32 CatalogIndex = INDEX_NONE;
				const FModuleSnapshot* Module = FindModuleByStableId(
					Catalog, Placement.StableModuleId, &CatalogIndex);
				if (Placement.StablePlacementId < 0
					|| PlacementIndexById.Contains(Placement.StablePlacementId)
					|| Module == nullptr
					|| Placement.QuarterTurns >= 4
					|| (Module->AllowedQuarterTurnsMask & (1 << Placement.QuarterTurns)) == 0
					|| !IsFiniteUnitScaleTransform(Placement.LocalTransform)
					|| (Module->LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell
						&& Placement.StableVariantId < 0)
					|| Module->LayoutPolicy == EZeroEscapeLayoutPolicy::DecorationOnly)
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::InvalidConfiguration,
						TEXT("最终 Plan 含非法、重复或无法映射 Catalog 的 Placement。"),
						0, 0, Placement.StablePlacementId);
					return false;
				}
				PlacementIndexById.Add(Placement.StablePlacementId, PlacementIndex);
				CatalogIndexByPlacementId.Add(Placement.StablePlacementId, CatalogIndex);
				PlacementGraph.FindOrAdd(Placement.StablePlacementId);

				if (Module->LayoutPolicy != EZeroEscapeLayoutPolicy::Cap)
				{
					const FIntVector Footprint = RotateFootprint(
						Module->Footprint, Placement.QuarterTurns);
					if (!AreEquivalentTransforms(
							Placement.LocalTransform,
							MakeGridModuleTransform(
								Placement.GridOrigin,
								Footprint,
								Request.CellSize,
								Placement.QuarterTurns)))
					{
						Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::GeometryOverlap,
							TEXT("结构 Placement Transform 与 GridOrigin/Footprint/QuarterTurns 不一致。"),
							0, 0, Placement.StablePlacementId);
						return false;
					}
					for (int32 Z = 0; Z < Footprint.Z; ++Z)
					{
						for (int32 Y = 0; Y < Footprint.Y; ++Y)
						{
							for (int32 X = 0; X < Footprint.X; ++X)
							{
								if (!ConsumeValidationWork(TEXT("全局几何占格验证耗尽预算。")))
								{
									return false;
								}
								const FIntVector Cell = Placement.GridOrigin + FIntVector(X, Y, Z);
								if (!IsInsideGrid(Cell, Request.GridExtent)
									|| StructuralOccupancy.Contains(Cell))
								{
									Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
										EZeroEscapeGenerationFailure::GeometryOverlap,
										TEXT("最终结构 Placement 越界或占用同一 Grid Cell。"),
										0, 0, Placement.StablePlacementId);
									return false;
								}
								StructuralOccupancy.Add(Cell, Placement.StablePlacementId);
							}
						}
					}
				}
			}

			TMap<uint64, FValidatedPortalEndpoint> PortalEndpointByKey;
			for (const FZeroEscapePlacedModule& Placement : Plan.Modules)
			{
				const int32 CatalogIndex = CatalogIndexByPlacementId.FindChecked(Placement.StablePlacementId);
				const FModuleSnapshot& Module = Catalog.Modules[CatalogIndex];
				for (const FZeroEscapeModulePortal& Portal : Module.Portals)
				{
					if (!ConsumeValidationWork(TEXT("全局验证构建 Portal 索引时耗尽预算。")))
					{
						return false;
					}
					const uint64 Key = MakeStablePairKey(
						Placement.StablePlacementId, Portal.StableSocketId);
					if (PortalEndpointByKey.Contains(Key))
					{
						Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::PortalMismatch,
							TEXT("最终 Plan 出现重复 Placement/Socket 端点。"));
						return false;
					}
					FValidatedPortalEndpoint Endpoint;
					Endpoint.Placement = &Placement;
					Endpoint.Module = &Module;
					Endpoint.Portal = &Portal;
					PortalEndpointByKey.Add(Key, Endpoint);
				}
			}

			TSet<int32> ConnectionIds;
			TSet<uint64> ConnectedPortalKeys;
			TSet<uint64> ConnectedPlacementPairs;
			for (const FZeroEscapePortalConnection& Connection : Plan.PortalConnections)
			{
				if (!ConsumeValidationWork(TEXT("全局验证检查 PortalConnection 时耗尽预算。")))
				{
					return false;
				}
				const uint64 KeyA = MakeStablePairKey(
					Connection.StablePlacementAId, Connection.StableSocketAId);
				const uint64 KeyB = MakeStablePairKey(
					Connection.StablePlacementBId, Connection.StableSocketBId);
				const FValidatedPortalEndpoint* A = PortalEndpointByKey.Find(KeyA);
				const FValidatedPortalEndpoint* B = PortalEndpointByKey.Find(KeyB);
				if (Connection.StableConnectionId < 0
					|| ConnectionIds.Contains(Connection.StableConnectionId)
					|| Connection.StablePlacementAId == Connection.StablePlacementBId
					|| A == nullptr || B == nullptr
					|| ConnectedPortalKeys.Contains(KeyA) || ConnectedPortalKeys.Contains(KeyB)
					|| (Connection.AbstractEdgeId >= 0
						&& !AbstractEdgeById.Contains(Connection.AbstractEdgeId))
					|| !AreConnectorSignaturesCompatible(
						MakeOpenSignature(*A->Portal), MakeOpenSignature(*B->Portal))
					|| !AreOpposedPortalFrames(
						A->Portal->LocalTransform * A->Placement->LocalTransform,
						B->Portal->LocalTransform * B->Placement->LocalTransform))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::PortalMismatch,
						TEXT("PortalConnection 端点、签名、唯一性或 Frame 对齐非法。"),
						0, 0, Connection.StableConnectionId);
					return false;
				}
				ConnectionIds.Add(Connection.StableConnectionId);
				ConnectedPortalKeys.Add(KeyA);
				ConnectedPortalKeys.Add(KeyB);
				ConnectedPlacementPairs.Add(MakeUndirectedPlacementKey(
					Connection.StablePlacementAId, Connection.StablePlacementBId));
				PlacementGraph.FindOrAdd(Connection.StablePlacementAId).AddUnique(
					Connection.StablePlacementBId);
				PlacementGraph.FindOrAdd(Connection.StablePlacementBId).AddUnique(
					Connection.StablePlacementAId);
			}
			for (TPair<int32, TArray<int32>>& Pair : PlacementGraph)
			{
				Pair.Value.Sort();
			}

			TSet<uint64> ClosedParentPortalKeys;
			TSet<uint64> ClosureCapPortalKeys;
			TSet<int32> ClosurePlacementIds;
			for (const FZeroEscapeClosedPortal& Closed : Plan.ClosedPortals)
			{
				if (!ConsumeValidationWork(TEXT("全局验证检查 Closure 时耗尽预算。")))
				{
					return false;
				}
				const uint64 ParentKey = MakeStablePairKey(
					Closed.StablePlacementId, Closed.StableSocketId);
				const FValidatedPortalEndpoint* Parent = PortalEndpointByKey.Find(ParentKey);
				const int32* ClosurePlacementIndex = PlacementIndexById.Find(
					Closed.StableClosurePlacementId);
				if (Parent == nullptr || ClosurePlacementIndex == nullptr
					|| Parent->Portal->Policy != EZeroEscapeSocketPolicy::Sealable
					|| ConnectedPortalKeys.Contains(ParentKey)
					|| ClosedParentPortalKeys.Contains(ParentKey)
					|| ClosurePlacementIds.Contains(Closed.StableClosurePlacementId))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::PortalMismatch,
						TEXT("ClosedPortal 的父端点、唯一性或状态非法。"));
					return false;
				}
				const FZeroEscapePlacedModule& ClosurePlacement =
					Plan.Modules[*ClosurePlacementIndex];
				const int32 ClosureCatalogIndex = CatalogIndexByPlacementId.FindChecked(
					Closed.StableClosurePlacementId);
				const FModuleSnapshot& ClosureModule = Catalog.Modules[ClosureCatalogIndex];
				if (ClosureModule.LayoutPolicy != EZeroEscapeLayoutPolicy::Cap
					|| ClosureModule.StableModuleId != Parent->Portal->ClosureModuleId
					|| ClosureModule.Portals.Num() != 1)
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::PortalMismatch,
						TEXT("ClosedPortal 没有引用其 Catalog 契约指定的单 Portal Cap。"));
					return false;
				}
				const FZeroEscapeModulePortal& CapPortal = ClosureModule.Portals[0];
				const uint64 CapKey = MakeStablePairKey(
					ClosurePlacement.StablePlacementId, CapPortal.StableSocketId);
				if (ConnectedPortalKeys.Contains(CapKey)
					|| ClosureCapPortalKeys.Contains(CapKey)
					|| !AreConnectorSignaturesCompatible(
						MakeOpenSignature(*Parent->Portal), MakeOpenSignature(CapPortal))
					|| !AreOpposedPortalFrames(
						Parent->Portal->LocalTransform * Parent->Placement->LocalTransform,
						CapPortal.LocalTransform * ClosurePlacement.LocalTransform))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::PortalMismatch,
						TEXT("Cap Portal 的签名、唯一性或 Frame 对齐非法。"));
					return false;
				}
				ClosedParentPortalKeys.Add(ParentKey);
				ClosureCapPortalKeys.Add(CapKey);
				ClosurePlacementIds.Add(Closed.StableClosurePlacementId);
			}

			for (const FZeroEscapePlacedModule& Placement : Plan.Modules)
			{
				const FModuleSnapshot& Module = Catalog.Modules[
					CatalogIndexByPlacementId.FindChecked(Placement.StablePlacementId)];
				if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::Cap)
				{
					if (!ClosurePlacementIds.Contains(Placement.StablePlacementId))
					{
						Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::PortalMismatch,
							TEXT("最终 Plan 含未被 ClosedPortal 消费的 Cap Placement。"));
						return false;
					}
					continue;
				}
				for (const FZeroEscapeModulePortal& Portal : Module.Portals)
				{
					if (!ConsumeValidationWork(TEXT("全局 Portal 状态完备性检查耗尽预算。")))
					{
						return false;
					}
					const uint64 Key = MakeStablePairKey(
						Placement.StablePlacementId, Portal.StableSocketId);
					const bool bConnected = ConnectedPortalKeys.Contains(Key);
					const bool bClosed = ClosedParentPortalKeys.Contains(Key);
					const bool bValid = Portal.Policy == EZeroEscapeSocketPolicy::Required
						? (bConnected && !bClosed)
						: Portal.Policy == EZeroEscapeSocketPolicy::Sealable
							? (bConnected != bClosed)
							: false;
					if (!bValid)
					{
						Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::PortalMismatch,
							TEXT("Required/Sealable Portal 未得到唯一 Connected/Closed 状态。"),
							0, 0, Placement.StablePlacementId);
						return false;
					}
				}
			}

			TMap<int32, int32> PlacementByNodeId;
			for (const FZeroEscapeNodePlacementBinding& Binding : Plan.NodeBindings)
			{
				if (!ConsumeValidationWork(TEXT("全局验证检查 NodeBinding 时耗尽预算。")))
				{
					return false;
				}
				const FSpatialNode* const* Node = AbstractNodeById.Find(Binding.AbstractNodeId);
				const int32* PlacementIndex = PlacementIndexById.Find(Binding.StablePlacementId);
				if (Node == nullptr || PlacementIndex == nullptr
					|| PlacementByNodeId.Contains(Binding.AbstractNodeId))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("NodeBinding 缺失、重复或引用非法 Placement。"));
					return false;
				}
				const FModuleSnapshot& Module = Catalog.Modules[
					CatalogIndexByPlacementId.FindChecked(Binding.StablePlacementId)];
				const FZeroEscapePlacedModule& Placement = Plan.Modules[*PlacementIndex];
				if (!Module.AllowedRoles.Contains((*Node)->Role)
					|| (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::SocketModule
						&& Placement.AbstractNodeId != Binding.AbstractNodeId))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::MissingModuleRole,
						TEXT("NodeBinding 的最终模块不允许承载该拓扑 Role。"),
						0, 0, Binding.AbstractNodeId);
					return false;
				}
				PlacementByNodeId.Add(Binding.AbstractNodeId, Binding.StablePlacementId);
			}
			if (PlacementByNodeId.Num() != AbstractNodeById.Num()
				|| PlacementByNodeId.FindRef(Request.AbstractPlan->StartStableNodeId)
					!= Plan.StartPlacementId
				|| PlacementByNodeId.FindRef(Request.AbstractPlan->ExitStableNodeId)
					!= Plan.ExitPlacementId)
			{
				Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("NodeBinding 未一一覆盖抽象 Node 或 Start/Exit 映射漂移。"));
				return false;
			}

			TSet<int32> EdgeRouteIds;
			for (const FZeroEscapeEdgeRouteBinding& Route : Plan.EdgeRoutes)
			{
				if (!ConsumeValidationWork(TEXT("全局验证检查 EdgeRoute 时耗尽预算。")))
				{
					return false;
				}
				const FSpatialEdge* const* AbstractEdge = AbstractEdgeById.Find(Route.AbstractEdgeId);
				if (AbstractEdge == nullptr || EdgeRouteIds.Contains(Route.AbstractEdgeId)
					|| Route.FromNodeId != (*AbstractEdge)->StableNodeA
					|| Route.ToNodeId != (*AbstractEdge)->StableNodeB
					|| Route.OrderedStablePlacementIds.IsEmpty()
					|| Route.OrderedStablePlacementIds[0] != PlacementByNodeId.FindRef(Route.FromNodeId)
					|| Route.OrderedStablePlacementIds.Last() != PlacementByNodeId.FindRef(Route.ToNodeId))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("EdgeRoute 身份、方向或 NodeBinding 端点非法。"),
						0, 0, Route.AbstractEdgeId);
					return false;
				}
				for (int32 Index = 0; Index < Route.OrderedStablePlacementIds.Num(); ++Index)
				{
					if (!ConsumeValidationWork(TEXT("全局验证遍历 EdgeRoute 时耗尽预算。")))
					{
						return false;
					}
					const int32 PlacementId = Route.OrderedStablePlacementIds[Index];
					if (!PlacementIndexById.Contains(PlacementId))
					{
						Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::InvalidGraph,
							TEXT("EdgeRoute 引用了不存在的 Placement。"));
						return false;
					}
					if (Index > 0)
					{
						const int32 Previous = Route.OrderedStablePlacementIds[Index - 1];
						if (Previous == PlacementId
							|| !ConnectedPlacementPairs.Contains(
								MakeUndirectedPlacementKey(Previous, PlacementId)))
						{
							Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
								EZeroEscapeGenerationFailure::InvalidGraph,
								TEXT("EdgeRoute 相邻 Placement 没有直接 PortalConnection。"),
								0, 0, Route.AbstractEdgeId);
							return false;
						}
					}
				}
				EdgeRouteIds.Add(Route.AbstractEdgeId);
			}
			if (EdgeRouteIds.Num() != AbstractEdgeById.Num())
			{
				Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("EdgeRoute 未一一覆盖全部抽象 Edge。"));
				return false;
			}

			TMap<int32, const FZeroEscapeGeneratedAnchor*> GeneratedAnchorById;
			int32 PlayerSpawnCount = 0;
			int32 ExitAnchorCount = 0;
			for (const FZeroEscapeGeneratedAnchor& Anchor : Plan.GameplayAnchors)
			{
				if (!ConsumeValidationWork(TEXT("全局验证检查 GameplayAnchor 时耗尽预算。")))
				{
					return false;
				}
				const int32* PlacementIndex = PlacementIndexById.Find(Anchor.StablePlacementId);
				if (Anchor.StableAnchorInstanceId < 0
					|| GeneratedAnchorById.Contains(Anchor.StableAnchorInstanceId)
					|| PlacementIndex == nullptr
					|| !IsFiniteUnitScaleTransform(Anchor.LocalTransform))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("GameplayAnchor 身份、Placement 或 Transform 非法。"));
					return false;
				}
				const FZeroEscapePlacedModule& Placement = Plan.Modules[*PlacementIndex];
				const FModuleSnapshot& Module = Catalog.Modules[
					CatalogIndexByPlacementId.FindChecked(Anchor.StablePlacementId)];
				const FZeroEscapeModuleAnchor* ModuleAnchor = Module.GameplayAnchors.FindByPredicate(
					[&Anchor](const FZeroEscapeModuleAnchor& Candidate)
					{
						return Candidate.StableAnchorId == Anchor.StableModuleAnchorId;
					});
				if (ModuleAnchor == nullptr || ModuleAnchor->Type != Anchor.Type
					|| !AreEquivalentTransforms(
						Anchor.LocalTransform,
						ModuleAnchor->LocalTransform * Placement.LocalTransform))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("GameplayAnchor 未忠实导出 Catalog Anchor 契约。"));
					return false;
				}
				GeneratedAnchorById.Add(Anchor.StableAnchorInstanceId, &Anchor);
				PlayerSpawnCount += Anchor.Type == EZeroEscapeGameplayAnchorType::PlayerSpawn ? 1 : 0;
				ExitAnchorCount += Anchor.Type == EZeroEscapeGameplayAnchorType::Exit ? 1 : 0;
			}
			const FZeroEscapeGeneratedAnchor* const* PlayerAnchor =
				GeneratedAnchorById.Find(Plan.PlayerSpawnAnchorInstanceId);
			const FZeroEscapeGeneratedAnchor* const* ExitAnchor =
				GeneratedAnchorById.Find(Plan.ExitAnchorInstanceId);
			if (PlayerSpawnCount != 1 || ExitAnchorCount != 1
				|| PlayerAnchor == nullptr || ExitAnchor == nullptr
				|| (*PlayerAnchor)->Type != EZeroEscapeGameplayAnchorType::PlayerSpawn
				|| (*PlayerAnchor)->StablePlacementId != Plan.StartPlacementId
				|| (*ExitAnchor)->Type != EZeroEscapeGameplayAnchorType::Exit
				|| (*ExitAnchor)->StablePlacementId != Plan.ExitPlacementId)
			{
				Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("Start/Exit GameplayAnchor 不是唯一且权威的最终锚点。"));
				return false;
			}

			TSet<int32> ObjectiveBindingIds;
			TMap<int32, uint32> ObjectiveMaskByPlacement;
			for (int32 ObjectiveIndex = 0;
				ObjectiveIndex < Plan.ObjectiveBindings.Num();
				++ObjectiveIndex)
			{
				if (!ConsumeValidationWork(TEXT("全局验证检查 ObjectiveBinding 时耗尽预算。")))
				{
					return false;
				}
				const FZeroEscapeObjectiveBinding& Binding = Plan.ObjectiveBindings[ObjectiveIndex];
				const FObjectivePlacement* const* AbstractObjective =
					AbstractObjectiveById.Find(Binding.StableObjectiveId);
				const FZeroEscapeGeneratedAnchor* const* Anchor =
					GeneratedAnchorById.Find(Binding.StableAnchorInstanceId);
				if (ObjectiveIndex >= ZeroEscape::GenerationLimits::MaxObjectiveCandidates
					|| AbstractObjective == nullptr || Anchor == nullptr
					|| ObjectiveBindingIds.Contains(Binding.StableObjectiveId)
					|| Binding.AbstractNodeId != (*AbstractObjective)->StableNodeId
					|| Binding.StablePlacementId != PlacementByNodeId.FindRef(Binding.AbstractNodeId)
					|| (*Anchor)->Type != EZeroEscapeGameplayAnchorType::Objective
					|| (*Anchor)->StablePlacementId != Binding.StablePlacementId)
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("ObjectiveBinding 未一一映射抽象目标、Node、Placement 与 Anchor。"));
					return false;
				}
				ObjectiveBindingIds.Add(Binding.StableObjectiveId);
				ObjectiveMaskByPlacement.FindOrAdd(Binding.StablePlacementId)
					|= static_cast<uint32>(1u << ObjectiveIndex);
			}
			if (ObjectiveBindingIds.Num() != AbstractObjectiveById.Num())
			{
				Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("ObjectiveBinding 未一一覆盖全部抽象 Objective。"));
				return false;
			}

			TSet<int32> ReachablePlacements;
			TQueue<int32> ReachableQueue;
			ReachablePlacements.Add(Plan.StartPlacementId);
			ReachableQueue.Enqueue(Plan.StartPlacementId);
			int32 ReachablePlacement = INDEX_NONE;
			while (ReachableQueue.Dequeue(ReachablePlacement))
			{
				if (!ConsumeValidationWork(TEXT("全局 Placement 图可达性检查耗尽预算。")))
				{
					return false;
				}
				if (const TArray<int32>* Neighbors = PlacementGraph.Find(ReachablePlacement))
				{
					for (const int32 Neighbor : *Neighbors)
					{
						if (!ReachablePlacements.Contains(Neighbor))
						{
							ReachablePlacements.Add(Neighbor);
							ReachableQueue.Enqueue(Neighbor);
						}
					}
				}
			}
			if (!ReachablePlacements.Contains(Plan.ExitPlacementId))
			{
				Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::ProgressionNoSolution,
					TEXT("实际 Portal 图中 Start 无法到达 Exit。"));
				return false;
			}
			for (const FZeroEscapeNodePlacementBinding& Binding : Plan.NodeBindings)
			{
				if (!ReachablePlacements.Contains(Binding.StablePlacementId))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::ProgressionNoSolution,
						TEXT("实际 Portal 图中存在不可达的抽象 Node Placement。"),
						0, 0, Binding.AbstractNodeId);
					return false;
				}
			}
			for (const FZeroEscapeObjectiveBinding& Binding : Plan.ObjectiveBindings)
			{
				if (!ReachablePlacements.Contains(Binding.StablePlacementId))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::ProgressionNoSolution,
						TEXT("实际 Portal 图中存在不可达的 Objective Placement。"),
						0, 0, Binding.StableObjectiveId);
					return false;
				}
			}

			const int32 ObjectiveCount = Plan.ObjectiveBindings.Num();
			int32 RequiredObjectiveCount = 0;
			switch (Request.AbstractPlan->CompletionRule)
			{
			case EZeroEscapeCompletionRule::EscapeOnly:
				RequiredObjectiveCount = 0;
				break;
			case EZeroEscapeCompletionRule::CollectAll:
				RequiredObjectiveCount = ObjectiveCount;
				break;
			case EZeroEscapeCompletionRule::CollectKOfN:
				RequiredObjectiveCount = Request.AbstractPlan->RequiredObjectiveCount;
				break;
			default:
				RequiredObjectiveCount = INDEX_NONE;
				break;
			}
			if (RequiredObjectiveCount < 0 || RequiredObjectiveCount > ObjectiveCount
				|| Request.AbstractPlan->RequiredObjectiveCount != RequiredObjectiveCount)
			{
				Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::InvalidKOfN,
					TEXT("最终几何流程的 CompletionRule/K-of-N 契约非法。"));
				return false;
			}

			FPlacementProgressionState InitialState;
			InitialState.StablePlacementId = Plan.StartPlacementId;
			InitialState.CollectedMask = ObjectiveMaskByPlacement.FindRef(Plan.StartPlacementId);
			TQueue<FPlacementProgressionState> ProgressionQueue;
			TSet<FPlacementProgressionState> VisitedProgressionStates;
			ProgressionQueue.Enqueue(InitialState);
			VisitedProgressionStates.Add(InitialState);
			FPlacementProgressionState CurrentState;
			while (ProgressionQueue.Dequeue(CurrentState))
			{
				if (!ConsumeValidationWork(TEXT("几何 K-of-N 状态搜索耗尽全局预算。")))
				{
					return false;
				}
				if (CurrentState.StablePlacementId == Plan.ExitPlacementId
					&& static_cast<int32>(FPlatformMath::CountBits(CurrentState.CollectedMask))
						>= RequiredObjectiveCount)
				{
					return true;
				}
				const TArray<int32>* Neighbors = PlacementGraph.Find(
					CurrentState.StablePlacementId);
				if (Neighbors == nullptr)
				{
					continue;
				}
				for (const int32 Neighbor : *Neighbors)
				{
					FPlacementProgressionState NextState;
					NextState.StablePlacementId = Neighbor;
					NextState.CollectedMask = CurrentState.CollectedMask
						| ObjectiveMaskByPlacement.FindRef(Neighbor);
					if (!VisitedProgressionStates.Contains(NextState))
					{
						if (VisitedProgressionStates.Num()
							>= ZeroEscape::GenerationLimits::FirstPassMaxProgressionSearchStates)
						{
							Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
								EZeroEscapeGenerationFailure::SearchBudgetExceeded,
								TEXT("几何 K-of-N 状态搜索超过首版硬上限。"));
							return false;
						}
						VisitedProgressionStates.Add(NextState);
						ProgressionQueue.Enqueue(NextState);
					}
				}
			}

			Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
				EZeroEscapeGenerationFailure::ProgressionNoSolution,
				TEXT("实际 Portal 图无法满足最终 CompletionRule/K-of-N。"));
			return false;
		}
	}

	/**
	 * 完整布局事务入口。
	 *
	 * OutPlan 在入口清空；每个 Attempt 都新建 FPlacementState，且 WFC/Socket 使用隔离的随机域。
	 * 某阶段普通无解时记录分类统计并尝试下一个 Attempt；预算耗尽、不变量错误或“必须发生有效
	 * WFC 选择”未满足时立即失败。Closure、导出和全局验证都写局部 CandidatePlan，最终验证
	 * 通过并计算规范 Hash 后才 Move 到 OutPlan，因此调用者永远看不到部分生成结果。
	 */
	bool FLayoutSolver::Solve(
		const FLayoutRequest& Request,
		const FModuleCatalogSnapshot& Catalog,
		const FZeroEscapeSolverBudgets& Budgets,
		const int32 MasterSeed,
		FZeroEscapeGeneratedLevelPlan& OutPlan,
		FZeroEscapeGenerationReport& OutReport)
	{
		const double PreservedAbstractMilliseconds = OutReport.Metrics.AbstractMilliseconds;
		OutPlan = {};
		OutReport = {};
		OutReport.Metrics.AbstractMilliseconds = PreservedAbstractMilliseconds;
		if (Request.AbstractPlan == nullptr
			|| Request.CellSize.ContainsNaN()
			|| Request.CellSize.X <= 0.0 || Request.CellSize.Y <= 0.0 || Request.CellSize.Z <= 0.0
			|| Request.CellSize.X > ZeroEscape::GenerationLimits::FirstPassMaxCellSizeCm
			|| Request.CellSize.Y > ZeroEscape::GenerationLimits::FirstPassMaxCellSizeCm
			|| Request.CellSize.Z > ZeroEscape::GenerationLimits::FirstPassMaxCellSizeCm
			|| Request.GridExtent.X <= 2 || Request.GridExtent.Y <= 2 || Request.GridExtent.Z != 1
			|| Request.GridExtent.X > 64 || Request.GridExtent.Y > 64
			|| Request.AStarStraightStepCost <= 0 || Request.AStarTurnPenalty < 0
			|| Budgets.MaxLayoutAttempts <= 0
			|| Budgets.MaxLayoutAttempts > ZeroEscape::GenerationLimits::FirstPassMaxLayoutAttempts
			|| Budgets.MaxSocketBacktracks < 0
			|| Budgets.MaxSocketBacktracks > ZeroEscape::GenerationLimits::FirstPassMaxSocketBacktracks
			|| Budgets.MaxSocketCandidateChecks <= 0
			|| Budgets.MaxSocketCandidateChecks > ZeroEscape::GenerationLimits::FirstPassMaxSocketCandidateChecks
			|| Budgets.MaxAStarExpandedStates <= 0
			|| Budgets.MaxAStarExpandedStates > ZeroEscape::GenerationLimits::FirstPassMaxAStarExpandedStates
			|| Budgets.MaxAStarRouteAttempts <= 0
			|| Budgets.MaxAStarRouteAttempts > ZeroEscape::GenerationLimits::FirstPassMaxAStarRouteAttempts
			|| Budgets.MaxWfcBacktracks < 0
			|| Budgets.MaxWfcBacktracks > ZeroEscape::GenerationLimits::FirstPassMaxWfcBacktracks
			|| Budgets.MaxWfcObservationCount <= 0
			|| Budgets.MaxWfcObservationCount > ZeroEscape::GenerationLimits::FirstPassMaxWfcObservationCount
			|| Budgets.MaxWfcSupportUpdates <= 0
			|| Budgets.MaxWfcSupportUpdates > ZeroEscape::GenerationLimits::FirstPassMaxWfcSupportUpdates
			|| Budgets.MaxWfcActiveCells <= 0
			|| Budgets.MaxWfcActiveCells > ZeroEscape::GenerationLimits::FirstPassMaxWfcActiveCells
			|| Budgets.MaxWfcVariants <= 0
			|| Budgets.MaxWfcVariants > ZeroEscape::GenerationLimits::FirstPassMaxWfcVariants
			|| Budgets.MaxWfcSnapshotMemoryMB <= 0
			|| Budgets.MaxWfcSnapshotMemoryMB > ZeroEscape::GenerationLimits::FirstPassMaxWfcSnapshotMemoryMB
			|| Budgets.MaxWfcCumulativeSnapshotCopyMB < Budgets.MaxWfcSnapshotMemoryMB
			|| Budgets.MaxWfcCumulativeSnapshotCopyMB
				> ZeroEscape::GenerationLimits::FirstPassMaxWfcCumulativeSnapshotCopyMB
			|| Budgets.MaxTotalWorkUnits <= 0
			|| Budgets.MaxTotalWorkUnits > ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits)
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("Layout Request/Catalog/Budgets 基础字段非法。"));
			return false;
		}

		// 全部 Attempt 共享一份单调预算；重试不会重置工作量上限。
		FDeterministicWorkBudget WorkBudget(Budgets.MaxTotalWorkUnits);
		for (int32 Attempt = 0; Attempt < Budgets.MaxLayoutAttempts; ++Attempt)
		{
			OutReport.Stage = EZeroEscapeGenerationStage::None;
			OutReport.Failure = EZeroEscapeGenerationFailure::None;
			OutReport.RelatedStableId = INDEX_NONE;
			OutReport.ActualValue = 0;
			OutReport.LimitValue = 0;
			OutReport.Message.Reset();
			ResetWfcAttemptMetrics(OutReport.Metrics);
			OutReport.AttemptIndex = Attempt;
			OutReport.AttemptsExecuted = Attempt + 1;
			// Attempt 局部状态是结构搜索的事务边界；continue 会完整丢弃本次所有中间写入。
			FPlacementState State;
			if (!EmbedAbstractNodes(Request, Catalog, WorkBudget, State, OutReport))
			{
				RecordAttemptFailure(Attempt, OutReport);
				if (OutReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded)
				{
					return false;
				}
				continue;
			}

			FRandomStream SocketRandom = FGenerationCore::MakeRandomStream(
				MasterSeed, GAlgorithmVersion, ERandomDomain::SocketLayout, Attempt);
			if (!PlaceRequiredSocketModules(
					Request, Catalog, SocketRandom, Budgets, WorkBudget, State, OutReport))
			{
				RecordAttemptFailure(Attempt, OutReport);
				if (OutReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded
					|| WorkBudget.GetRemainingUnits() == 0)
				{
					return false;
				}
				continue;
			}

			TArray<FGridConstraint> Constraints;
			TArray<FRoutedGraphEdge> RoutedEdges;
			if (!RouteGraphEdgesWithAStar(
					Request,
					Catalog,
					Budgets,
					WorkBudget,
					State,
					Constraints,
					RoutedEdges,
					OutReport))
			{
				RecordAttemptFailure(Attempt, OutReport);
				if (OutReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded)
				{
					return false;
				}
				continue;
			}

			TArray<FTileVariant> Variants;
			FCompatibilityTable Compatibility;
			if (!BuildWfcVariantsAndCompatibility(
					Catalog, WorkBudget, Variants, Compatibility, OutReport))
			{
				RecordAttemptFailure(Attempt, OutReport);
				if (OutReport.Failure == EZeroEscapeGenerationFailure::WfcBudgetExceeded)
				{
					return false;
				}
				continue;
			}

			TArray<int32> VariantByActiveCell;
			FRandomStream WfcRandom = FGenerationCore::MakeRandomStream(
				MasterSeed, GAlgorithmVersion, ERandomDomain::WfcLayout, Attempt);
			if (!SolveWfc(
					Constraints,
					Variants,
					Compatibility,
					WfcRandom,
					Budgets,
					Request.bRequireEffectiveWfcChoice,
					WorkBudget,
					VariantByActiveCell,
					OutReport))
			{
				RecordAttemptFailure(Attempt, OutReport);
				if (OutReport.Failure == EZeroEscapeGenerationFailure::WfcBudgetExceeded
					|| OutReport.Failure == EZeroEscapeGenerationFailure::WfcNoEffectiveChoice
					|| OutReport.Failure == EZeroEscapeGenerationFailure::SolverInvariantViolation)
				{
					return false;
				}
				continue;
			}

			// Finalize 之后仍可能因 Closure 或全局验证失败，故不能直接写入公开 OutPlan。
			FZeroEscapeGeneratedLevelPlan CandidatePlan;
			CandidatePlan.Signature = Request.Signature;
			CandidatePlan.CanonicalAbstractHash = Request.CanonicalAbstractHash;
			if (!AppendWfcPlacements(
					Request,
					Catalog,
					Constraints,
					Variants,
					VariantByActiveCell,
					State,
					OutReport))
			{
				RecordAttemptFailure(Attempt, OutReport);
				continue;
			}

			TArray<FConnectionDraft> ConnectionDrafts;
			TSet<uint64> ConnectedPortalKeys;
			if (!BuildConnectionsAndEdgeRoutes(
					Catalog,
					Variants,
					RoutedEdges,
					State,
					ConnectionDrafts,
					ConnectedPortalKeys,
					CandidatePlan,
					OutReport)
				|| !CloseUnusedSpecialPortals(
					Catalog,
					ConnectedPortalKeys,
					State,
					CandidatePlan,
					OutReport)
				|| !ExportModulesAndAnchors(
					Request,
					Catalog,
					State,
					CandidatePlan,
					OutReport))
			{
				if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::PortalMismatch,
						TEXT("Portal Finalize 或语义绑定导出失败。"));
				}
				RecordAttemptFailure(Attempt, OutReport);
				if (OutReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded)
				{
					return false;
				}
				continue;
			}

			ConnectionDrafts.Sort(
				[](const FConnectionDraft& A, const FConnectionDraft& B)
				{
					if (A.PlacementA != B.PlacementA) return A.PlacementA < B.PlacementA;
					if (A.SocketA != B.SocketA) return A.SocketA < B.SocketA;
					if (A.PlacementB != B.PlacementB) return A.PlacementB < B.PlacementB;
					return A.SocketB < B.SocketB;
				});
			for (const FConnectionDraft& Draft : ConnectionDrafts)
			{
				FZeroEscapePortalConnection& Connection = CandidatePlan.PortalConnections.AddDefaulted_GetRef();
				Connection.StableConnectionId = CandidatePlan.PortalConnections.Num() - 1;
				Connection.AbstractEdgeId = Draft.AbstractEdgeId;
				Connection.StablePlacementAId = Draft.PlacementA;
				Connection.StableSocketAId = Draft.SocketA;
				Connection.StablePlacementBId = Draft.PlacementB;
				Connection.StableSocketBId = Draft.SocketB;
			}

			CandidatePlan.NodeBindings.Sort(
				[](const FZeroEscapeNodePlacementBinding& A, const FZeroEscapeNodePlacementBinding& B)
				{
					return A.AbstractNodeId < B.AbstractNodeId;
				});
			CandidatePlan.EdgeRoutes.Sort(
				[](const FZeroEscapeEdgeRouteBinding& A, const FZeroEscapeEdgeRouteBinding& B)
				{
					return A.AbstractEdgeId < B.AbstractEdgeId;
				});
			CandidatePlan.ClosedPortals.Sort(
				[](const FZeroEscapeClosedPortal& A, const FZeroEscapeClosedPortal& B)
				{
					return A.StablePlacementId != B.StablePlacementId
						? A.StablePlacementId < B.StablePlacementId
						: A.StableSocketId < B.StableSocketId;
				});
			CandidatePlan.ObjectiveBindings.Sort(
				[](const FZeroEscapeObjectiveBinding& A, const FZeroEscapeObjectiveBinding& B)
				{
					return A.StableObjectiveId < B.StableObjectiveId;
				});
			if (!ValidateGlobalPlan(
					Request,
					Catalog,
					CandidatePlan,
					WorkBudget,
					OutReport))
			{
				if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("最终 Plan 全局验证失败。"));
				}
				RecordAttemptFailure(Attempt, OutReport);
				if (OutReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded)
				{
					return false;
				}
				continue;
			}

			// 只有权威验证通过的规范化 Plan 才能计算 Hash 并原子发布给调用者。
			CandidatePlan.CanonicalLayoutHash = ComputeCanonicalLayoutHash(CandidatePlan);
			OutPlan = MoveTemp(CandidatePlan);
			OutReport.Stage = EZeroEscapeGenerationStage::None;
			OutReport.Failure = EZeroEscapeGenerationFailure::None;
			OutReport.Message.Reset();
			return true;
		}

		OutReport.Stage = EZeroEscapeGenerationStage::GlobalValidation;
		OutReport.LastAttemptFailure = OutReport.Failure;
		OutReport.Failure = EZeroEscapeGenerationFailure::LayoutAttemptsExhausted;
		OutReport.Message = TEXT("所有有界 Layout Attempt 均失败。");
		return false;
	}
}
