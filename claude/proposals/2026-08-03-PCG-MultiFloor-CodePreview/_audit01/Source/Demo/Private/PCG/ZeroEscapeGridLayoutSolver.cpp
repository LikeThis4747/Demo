// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGridLayoutSolver.cpp
 * 职责：嵌入 Start、Exit 与中立房间局部约束，运行完整 16-mask WFC，并导出纯空间 Plan。
 * 边界：不解释玩法目标，不读取资产，不实例化世界对象；失败候选由同一 WFC 决策栈回溯。
 */

#include "PCG/ZeroEscapeGridLayoutSolver.h"

#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeWfcSolver.h"

#include "Containers/ArrayView.h"
#include "Containers/Queue.h"
#include "HAL/PlatformTime.h"

namespace ZeroEscape::LevelGeneration
{
	namespace GridLayoutPrivate
	{
		/** 把单步四邻域位移解析为 N/E/S/W 索引。 */
		int32 DirectionFromDelta(const FIntPoint Delta)
		{
			for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
			{
				if (Grid::Step(FIntPoint::ZeroValue, Direction) == Delta)
				{
					return Direction;
				}
			}
			return INDEX_NONE;
		}

		/** 按 (Y, X) 比较坐标，供所有规范序列复用。 */
		bool CoordinateLess(const FIntPoint A, const FIntPoint B)
		{
			return A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
		}

		/** 文件专属失败入口，避免 Unity Build 中私有帮助函数重名。 */
		bool FailGrid(
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

		/** 一个已经通过容量校验的中立房间位置。 */
		struct FRoomPlacement
		{
			int32 RegionId = INDEX_NONE;
			FIntPoint MinCoordinate = FIntPoint::ZeroValue;
			FIntPoint AnchorCoordinate = FIntPoint::ZeroValue;
		};

		/** 单次 Solve 私有中间态；Constraints 始终是完整 Grid 的 row-major 数组。 */
		struct FGridWorkingState
		{
			FIntPoint GridSize = FIntPoint::ZeroValue;
			TArray<FGridCellConstraint> Constraints;
			TArray<FRoomPlacement> Rooms;
			FIntPoint StartCoordinate = FIntPoint::ZeroValue;
			FIntPoint ExitCoordinate = FIntPoint::ZeroValue;
		};

		FGridCellConstraint& ConstraintAt(
			FGridWorkingState& State,
			const FIntPoint Coordinate)
		{
			check(Grid::IsInside(Coordinate, State.GridSize));
			return State.Constraints[Grid::ToIndex(Coordinate, State.GridSize)];
		}

		/** 把 Cell 标记为必定非空，并写入其空间职责。 */
		void MarkRequired(
			FGridWorkingState& State,
			const FIntPoint Coordinate,
			const int32 RegionId,
			const EZeroEscapeGridRegionKind RegionKind)
		{
			FGridCellConstraint& Cell = ConstraintAt(State, Coordinate);
			Cell.Domain = EGridCellDomain::Required;
			Cell.RegionId = RegionId;
			Cell.RegionKind = RegionKind;
		}

		/** 对称写入一条房间内部必开边；冲突立即报告代码不变量错误。 */
		bool AddRequiredOpening(
			FGridWorkingState& State,
			const FIntPoint A,
			const FIntPoint B,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (!Grid::IsInside(A, State.GridSize) || !Grid::IsInside(B, State.GridSize))
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::GridLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("Grid Required 开口指向网格外。"));
			}

			const int32 Direction = DirectionFromDelta(B - A);
			if (Direction == INDEX_NONE)
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::GridLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("Grid Required 开口只允许单步四邻域连接。"));
			}

			FGridCellConstraint& CellA = ConstraintAt(State, A);
			FGridCellConstraint& CellB = ConstraintAt(State, B);
			const uint8 ABit = Grid::DirectionBit(static_cast<uint8>(Direction));
			const uint8 BBit = Grid::DirectionBit(
				Grid::OppositeDirectionIndex(static_cast<uint8>(Direction)));
			if ((CellA.RequiredClosedMask & ABit) != 0
				|| (CellB.RequiredClosedMask & BBit) != 0)
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::GridLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("同一 Grid Edge 被同时标记为必开与必闭。"));
			}

			CellA.Domain = EGridCellDomain::Required;
			CellB.Domain = EGridCellDomain::Required;
			CellA.RequiredOpenMask |= ABit;
			CellB.RequiredOpenMask |= BBit;
			return true;
		}

		/** 初始化完整 Optional Grid，并封闭所有越界方向。 */
		void InitializeConstraintGrid(
			const FIntPoint GridSize,
			FGridWorkingState& OutState)
		{
			OutState = {};
			OutState.GridSize = GridSize;
			OutState.Constraints.SetNum(GridSize.X * GridSize.Y);
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < GridSize.X; ++X)
				{
					FGridCellConstraint& Cell =
						OutState.Constraints[Y * GridSize.X + X];
					Cell = {};
					Cell.Coordinate = FIntPoint(X, Y);
					Cell.Domain = EGridCellDomain::Optional;
					Cell.RegionId = INDEX_NONE;
					Cell.RegionKind = EZeroEscapeGridRegionKind::Corridor;
					if (Y == GridSize.Y - 1) Cell.RequiredClosedMask |= Grid::DirectionBit(0);
					if (X == GridSize.X - 1) Cell.RequiredClosedMask |= Grid::DirectionBit(1);
					if (Y == 0) Cell.RequiredClosedMask |= Grid::DirectionBit(2);
					if (X == 0) Cell.RequiredClosedMask |= Grid::DirectionBit(3);
				}
			}
		}

		/** 纯值 Solver 入口也 fail-closed，不假定调用者一定来自已校验 DataAsset。 */
		bool ValidateInput(
			const FZeroEscapeGenerationSignature& Signature,
			const FZeroEscapeSharedRouteConstraints& Rules,
			const FZeroEscapeWfcShapeWeights& Weights,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int64 CellCount =
				static_cast<int64>(Rules.GridSize.X) * Rules.GridSize.Y;
			FString WeightError;
			if (Signature.AlgorithmVersion <= 0
				|| Signature.GenerationProfileVersion <= 0
				|| Signature.PresentationVersion <= 0
				|| Rules.GridSize.X < 14
				|| Rules.GridSize.Y < 10
				|| Rules.GridSize.X > GenerationLimits::MaxGridAxis
				|| Rules.GridSize.Y > GenerationLimits::MaxGridAxis
				|| CellCount <= 0
				|| CellCount > GenerationLimits::MaxGridCells
				|| !FMath::IsNearlyEqual(Rules.LogicalTileSizeCm, 600.0)
				|| Rules.RoomSizeTiles != 2
				|| Rules.RoomCount < 0
				|| Rules.RoomCount > GenerationLimits::MaxRoomCount
				|| Rules.MinWalkableCellCount <= 0
				|| Rules.MaxWalkableCellCount < Rules.MinWalkableCellCount
				|| Rules.MaxWalkableCellCount > CellCount
				|| Rules.MaxConsecutiveStraightTiles <= 0
				|| Rules.MaxConsecutiveStraightTiles
					> FMath::Max(Rules.GridSize.X, Rules.GridSize.Y)
				|| Rules.MaxRequiredRouteLengthTiles <= 0
				|| Rules.MaxWfcCandidateAttempts <= 0
				|| Rules.MaxWfcBacktrackCount <= 0
				|| Rules.MaxWfcSolveAttempts <= 0
				|| Rules.MaxWfcSolveAttempts > 16
				|| Rules.MaxWfcCandidateAttempts < Rules.MaxWfcSolveAttempts
				|| Rules.MaxWfcBacktrackCount < Rules.MaxWfcSolveAttempts
				|| !FMath::IsFinite(Rules.AnchorHeightCm)
				|| Rules.AnchorHeightCm < 0.0
				|| !Weights.IsConfigured(WeightError))
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					WeightError.IsEmpty()
						? FString(TEXT("Grid/WFC 输入尺度、数量、路线或预算非法。"))
						: WeightError);
			}

			const int64 FixedCells = 2LL
				+ static_cast<int64>(Rules.RoomCount)
					* Rules.RoomSizeTiles * Rules.RoomSizeTiles;
			const int32 MinRoomX = 4;
			const int32 MaxRoomX =
				Rules.GridSize.X - Rules.RoomSizeTiles - 5;
			const int32 MinimumSpacing = Rules.RoomSizeTiles + 1;
			const int32 CenterY = Rules.GridSize.Y / 2;
			const int32 LowerLaneY = CenterY - Rules.RoomSizeTiles;
			const int32 UpperLaneY = CenterY + 1;
			if (FixedCells > Rules.MaxWalkableCellCount
				|| (Rules.RoomCount > 0
					&& (MaxRoomX < MinRoomX
						|| (Rules.RoomCount > 1
							&& MaxRoomX - MinRoomX
								< (Rules.RoomCount - 1) * MinimumSpacing)))
				|| LowerLaneY < 1
				|| UpperLaneY + Rules.RoomSizeTiles > Rules.GridSize.Y - 1)
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::CapacityInsufficient,
					TEXT("Grid 无法容纳 Start、Exit、RoomCount 与房间外部连接缓冲。"));
			}
			return true;
		}

		/**
		 * 只固定 Start/Exit、房间占格和房内共享边；房间入口与所有外部连接仍由 WFC 生成。
		 */
		bool EmbedSpatialConstraints(
			const FZeroEscapeGenerationSignature& Signature,
			const FZeroEscapeSharedRouteConstraints& Rules,
			FGridWorkingState& OutState,
			FZeroEscapeGenerationReport& OutReport)
		{
			InitializeConstraintGrid(Rules.GridSize, OutState);
			const int32 CenterY = Rules.GridSize.Y / 2;
			OutState.StartCoordinate = FIntPoint(
				1,
				FMath::Clamp(CenterY - 1, 2, Rules.GridSize.Y - 3));
			OutState.ExitCoordinate = FIntPoint(
				Rules.GridSize.X - 2,
				FMath::Clamp(CenterY + 1, 2, Rules.GridSize.Y - 3));

			MarkRequired(
				OutState,
				OutState.StartCoordinate,
				INDEX_NONE,
				EZeroEscapeGridRegionKind::Start);
			MarkRequired(
				OutState,
				OutState.ExitCoordinate,
				INDEX_NONE,
				EZeroEscapeGridRegionKind::Exit);

			const int32 MinRoomX = 4;
			const int32 MaxRoomX =
				Rules.GridSize.X - Rules.RoomSizeTiles - 5;
			const int32 LowerLaneY = CenterY - Rules.RoomSizeTiles;
			const int32 UpperLaneY = CenterY + 1;
			FRandomStream RoomRandom = FGenerationCore::MakeRandomStream(
				Signature.Seed,
				Signature.AlgorithmVersion,
				ERandomDomain::RoomPlacement);

			for (int32 RoomIndex = 0; RoomIndex < Rules.RoomCount; ++RoomIndex)
			{
				const int32 RoomX = Rules.RoomCount == 1
					? (MinRoomX + MaxRoomX) / 2
					: MinRoomX + ((MaxRoomX - MinRoomX) * RoomIndex)
						/ (Rules.RoomCount - 1);
				const bool bUpperLane = RoomRandom.RandRange(0, 1) != 0;

				FRoomPlacement& Room = OutState.Rooms.AddDefaulted_GetRef();
				Room.RegionId = RoomIndex;
				Room.MinCoordinate = FIntPoint(
					RoomX,
					bUpperLane ? UpperLaneY : LowerLaneY);
				Room.AnchorCoordinate = Room.MinCoordinate + FIntPoint(
					0,
					bUpperLane ? 0 : Rules.RoomSizeTiles - 1);

				for (int32 LocalY = 0; LocalY < Rules.RoomSizeTiles; ++LocalY)
				{
					for (int32 LocalX = 0; LocalX < Rules.RoomSizeTiles; ++LocalX)
					{
						const FIntPoint Cell =
							Room.MinCoordinate + FIntPoint(LocalX, LocalY);
						MarkRequired(
							OutState,
							Cell,
							Room.RegionId,
							EZeroEscapeGridRegionKind::Room);

						if (LocalX + 1 < Rules.RoomSizeTiles
							&& !AddRequiredOpening(
								OutState,
								Cell,
								Cell + FIntPoint(1, 0),
								OutReport))
						{
							return false;
						}
						if (LocalY + 1 < Rules.RoomSizeTiles
							&& !AddRequiredOpening(
								OutState,
								Cell,
								Cell + FIntPoint(0, 1),
								OutReport))
						{
							return false;
						}
					}
				}
			}
			return true;
		}

		/** 把完整稠密 WFC 输出转换为只含非空格的空间 Plan。 */
		bool ExportCandidatePlan(
			const FZeroEscapeGenerationSignature& Signature,
			const FZeroEscapeSharedRouteConstraints& Rules,
			const FGridWorkingState& State,
			const TConstArrayView<uint8> OpeningMasks,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (OpeningMasks.Num() != State.Constraints.Num())
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("WFC 输出数量与完整 Grid Constraint 数量不一致。"),
					OpeningMasks.Num(),
					State.Constraints.Num());
			}

			OutPlan = {};
			OutPlan.Signature = Signature;
			OutPlan.GridSize = Rules.GridSize;
			OutPlan.LogicalTileSizeCm = Rules.LogicalTileSizeCm;
			OutPlan.StartCoordinate = State.StartCoordinate;
			OutPlan.ExitCoordinate = State.ExitCoordinate;

			const auto MakeAnchorTransform = [&Rules](const FIntPoint Coordinate)
			{
				return FTransform(FVector(
					Coordinate.X * Rules.LogicalTileSizeCm,
					Coordinate.Y * Rules.LogicalTileSizeCm,
					Rules.AnchorHeightCm));
			};
			OutPlan.PlayerStartLocalTransform =
				MakeAnchorTransform(State.StartCoordinate);
			OutPlan.ExitLocalTransform = MakeAnchorTransform(State.ExitCoordinate);

			for (int32 DenseIndex = 0; DenseIndex < OpeningMasks.Num(); ++DenseIndex)
			{
				if (OpeningMasks[DenseIndex] == 0)
				{
					continue;
				}

				const FGridCellConstraint& Source = State.Constraints[DenseIndex];
				FZeroEscapeCollapsedTile& Tile =
					OutPlan.Cells.AddDefaulted_GetRef();
				Tile.StableCellId = OutPlan.Cells.Num() - 1;
				Tile.GridCoordinate = Source.Coordinate;
				Tile.OpeningMask = OpeningMasks[DenseIndex];
				Tile.RegionId = Source.RegionId;
				Tile.RegionKind = Source.RegionKind;
			}

			for (const FRoomPlacement& Source : State.Rooms)
			{
				FZeroEscapeGeneratedRoom& Room =
					OutPlan.Rooms.AddDefaulted_GetRef();
				Room.RegionId = Source.RegionId;
				Room.AnchorCoordinate = Source.AnchorCoordinate;
				Room.LocalTransform = MakeAnchorTransform(Source.AnchorCoordinate);
			}
			return true;
		}

		/** 从给定 Cell 做无权 BFS，返回 Plan.Cells 下标到最短距离的数组。 */
		TArray<int32> BuildDistances(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntPoint Source,
			TMap<FIntPoint, int32>& OutCellByCoordinate)
		{
			OutCellByCoordinate.Reset();
			for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
			{
				OutCellByCoordinate.Add(Plan.Cells[Index].GridCoordinate, Index);
			}

			TArray<int32> Distances;
			Distances.Init(INDEX_NONE, Plan.Cells.Num());
			const int32* SourceIndex = OutCellByCoordinate.Find(Source);
			if (SourceIndex == nullptr)
			{
				return Distances;
			}

			TQueue<int32> Queue;
			Distances[*SourceIndex] = 0;
			Queue.Enqueue(*SourceIndex);
			int32 CurrentIndex = INDEX_NONE;
			while (Queue.Dequeue(CurrentIndex))
			{
				const FZeroEscapeCollapsedTile& Current = Plan.Cells[CurrentIndex];
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if ((Current.OpeningMask & Grid::DirectionBit(Direction)) == 0)
					{
						continue;
					}

					const int32* NeighborIndex = OutCellByCoordinate.Find(
						Grid::Step(Current.GridCoordinate, Direction));
					if (NeighborIndex != nullptr
						&& Distances[*NeighborIndex] == INDEX_NONE)
					{
						Distances[*NeighborIndex] = Distances[CurrentIndex] + 1;
						Queue.Enqueue(*NeighborIndex);
					}
				}
			}
			return Distances;
		}

		/** 最终只保留有产品意义的一遍 BFS：全图可达、Exit 距离和房间 Anchor。 */
		bool ValidateFinalRoute(
			const FZeroEscapeSharedRouteConstraints& Rules,
			const FZeroEscapeGeneratedLevelPlan& Plan,
			FZeroEscapeGenerationReport& OutReport)
		{
			TMap<FIntPoint, int32> CellByCoordinate;
			const TArray<int32> Distances =
				BuildDistances(Plan, Plan.StartCoordinate, CellByCoordinate);
			const int32* ExitIndex = CellByCoordinate.Find(Plan.ExitCoordinate);
			if (ExitIndex == nullptr || Distances[*ExitIndex] == INDEX_NONE)
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("Connected WFC 结果中 Start 无法到达 Exit。"));
			}

			for (int32 Index = 0; Index < Distances.Num(); ++Index)
			{
				if (Distances[Index] == INDEX_NONE)
				{
					return FailGrid(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("最终 Plan 含有不属于 Start 连通分量的非空 Cell。"),
						Index);
				}
			}

			if (Plan.Rooms.Num() != Rules.RoomCount)
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("最终 Plan 的中立房间数量与 Profile 不一致。"),
					Plan.Rooms.Num(),
					Rules.RoomCount);
			}
			for (const FZeroEscapeGeneratedRoom& Room : Plan.Rooms)
			{
				const int32* RoomIndex = CellByCoordinate.Find(Room.AnchorCoordinate);
				if (RoomIndex == nullptr || Distances[*RoomIndex] == INDEX_NONE)
				{
					return FailGrid(
						OutReport,
						EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("中立房间 Anchor 不在最终可达 Cell 中。"),
						0,
						0,
						Room.RegionId);
				}
			}

			const int32 RouteLength = Distances[*ExitIndex];
			if (RouteLength > Rules.MaxRequiredRouteLengthTiles)
			{
				return FailGrid(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::RequiredRouteTooLong,
					TEXT("Start -> Exit 逻辑最短距离超过共享上限。"),
					RouteLength,
					Rules.MaxRequiredRouteLengthTiles);
			}
			return true;
		}

		/** 根据非空 Mask 统计路口形态；计数为 0 不是失败。 */
		void BuildJunctionMetrics(FZeroEscapeGeneratedLevelPlan& Plan)
		{
			Plan.JunctionMetrics = {};
			for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
			{
				const uint8 Mask = Cell.OpeningMask & Grid::AllOpenEdges;
				const int32 OpenCount = FMath::CountBits(static_cast<uint32>(Mask));
				if (OpenCount == 1) ++Plan.JunctionMetrics.DeadEndCount;
				else if (OpenCount == 3) ++Plan.JunctionMetrics.TJunctionCount;
				else if (OpenCount == 4) ++Plan.JunctionMetrics.CrossJunctionCount;
				else if (OpenCount == 2)
				{
					const bool bStraight =
						(Mask & 0x05u) == 0x05u || (Mask & 0x0Au) == 0x0Au;
					if (bStraight) ++Plan.JunctionMetrics.StraightCount;
					else ++Plan.JunctionMetrics.CornerCount;
				}
			}
		}

		/** 把一棵 WFC 搜索树的纯搜索指标累加到整局。 */
		void AccumulateWfcSearchMetrics(
			FZeroEscapeGenerationMetrics& InOutTotal,
			const FZeroEscapeGenerationMetrics& Attempt)
		{
			InOutTotal.WfcSolveAttemptCount += Attempt.WfcSolveAttemptCount;
			InOutTotal.WfcObservationCount += Attempt.WfcObservationCount;
			InOutTotal.WfcCandidateAttemptCount += Attempt.WfcCandidateAttemptCount;
			InOutTotal.WfcPropagationCount += Attempt.WfcPropagationCount;
			InOutTotal.WfcContradictionCount += Attempt.WfcContradictionCount;
			InOutTotal.WfcLocalAdjacencyContradictionCount +=
				Attempt.WfcLocalAdjacencyContradictionCount;
			InOutTotal.WfcCountContradictionCount +=
				Attempt.WfcCountContradictionCount;
			InOutTotal.WfcMaxConsecutiveContradictionCount +=
				Attempt.WfcMaxConsecutiveContradictionCount;
			InOutTotal.WfcConnectedContradictionCount +=
				Attempt.WfcConnectedContradictionCount;
			InOutTotal.WfcGlobalBanContradictionCount +=
				Attempt.WfcGlobalBanContradictionCount;
			InOutTotal.WfcBacktrackCount += Attempt.WfcBacktrackCount;
			InOutTotal.WfcCollapsedCandidateRejectionCount +=
				Attempt.WfcCollapsedCandidateRejectionCount;
			InOutTotal.WfcInvariantFailureCount += Attempt.WfcInvariantFailureCount;
		}

		/** 把整局总预算稳定平均分片，全部分片之和严格等于 TotalBudget。 */
		int32 GetWfcAttemptBudget(
			const int32 TotalBudget,
			const int32 AttemptIndex,
			const int32 AttemptCount)
		{
			check(AttemptCount > 0 && AttemptIndex >= 0 && AttemptIndex < AttemptCount);
			check(TotalBudget >= AttemptCount);
			const int32 BaseBudget = TotalBudget / AttemptCount;
			const int32 Remainder = TotalBudget % AttemptCount;
			return BaseBudget + (AttemptIndex < Remainder ? 1 : 0);
		}
	}

	bool FGridLayoutSolver::Solve(
		const FZeroEscapeGenerationSignature& Signature,
		const FZeroEscapeSharedRouteConstraints& Rules,
		const FZeroEscapeWfcShapeWeights& Weights,
		FZeroEscapeGeneratedLevelPlan& OutPlan,
		FZeroEscapeGenerationReport& OutReport)
	{
		using namespace GridLayoutPrivate;
		OutPlan = {};
		OutReport = {};
		const double StartSeconds = FPlatformTime::Seconds();
		if (!ValidateInput(Signature, Rules, Weights, OutReport))
		{
			return false;
		}

		FGridWorkingState State;
		if (!EmbedSpatialConstraints(Signature, Rules, State, OutReport))
		{
			return false;
		}

		TStaticArray<FTileVariant, 16> StaticVariants;
		FWfcSolver::BuildCanonicalVariants(Weights, StaticVariants);
		TArray<FTileVariant> Variants;
		Variants.Reserve(StaticVariants.Num());
		for (const FTileVariant& Variant : StaticVariants)
		{
			Variants.Add(Variant);
		}

		FZeroEscapeWfcSolveSettings WfcSettings;
		WfcSettings.StartCoordinate = State.StartCoordinate;
		WfcSettings.MinWalkableCellCount = Rules.MinWalkableCellCount;
		WfcSettings.MaxWalkableCellCount = Rules.MaxWalkableCellCount;
		WfcSettings.MaxConsecutiveStraightTiles = Rules.MaxConsecutiveStraightTiles;

		FZeroEscapeGeneratedLevelPlan AcceptedCandidate;
		TArray<uint8> AcceptedOpeningMasks;
		FZeroEscapeGenerationMetrics AggregateWfcMetrics;
		bool bSolved = false;
		for (int32 AttemptIndex = 0;
			AttemptIndex < Rules.MaxWfcSolveAttempts;
			++AttemptIndex)
		{
			FRandomStream WfcRandom = FGenerationCore::MakeRandomStream(
				Signature.Seed,
				Signature.AlgorithmVersion,
				ERandomDomain::WfcLayout,
				AttemptIndex);
			WfcSettings.MaxCandidateAttempts = GetWfcAttemptBudget(
				Rules.MaxWfcCandidateAttempts,
				AttemptIndex,
				Rules.MaxWfcSolveAttempts);
			WfcSettings.MaxBacktrackCount = GetWfcAttemptBudget(
				Rules.MaxWfcBacktrackCount,
				AttemptIndex,
				Rules.MaxWfcSolveAttempts);

			AcceptedCandidate = {};
			FZeroEscapeGenerationReport FatalCandidateReport;
			bool bHasFatalCandidateReport = false;
			auto ValidateCollapsedCandidate =
				[&](const TConstArrayView<uint8> OpeningMasks)
					-> FWfcCollapsedCandidateEvaluation
				{
					FZeroEscapeGeneratedLevelPlan Candidate;
					FZeroEscapeGenerationReport CandidateReport;
					if (!ExportCandidatePlan(
							Signature,
							Rules,
							State,
							OpeningMasks,
							Candidate,
							CandidateReport))
					{
						FatalCandidateReport = MoveTemp(CandidateReport);
						bHasFatalCandidateReport = true;
						return FWfcCollapsedCandidateEvaluation::Fatal(
							FatalCandidateReport.Message,
							FatalCandidateReport.RelatedStableId);
					}

					if (!ValidateFinalRoute(Rules, Candidate, CandidateReport))
					{
						if (CandidateReport.Failure
							== EZeroEscapeGenerationFailure::RequiredRouteTooLong)
						{
							return FWfcCollapsedCandidateEvaluation::Reject(
								MoveTemp(CandidateReport.Message),
								CandidateReport.ActualValue,
								CandidateReport.LimitValue);
						}

						FatalCandidateReport = MoveTemp(CandidateReport);
						bHasFatalCandidateReport = true;
						return FWfcCollapsedCandidateEvaluation::Fatal(
							FatalCandidateReport.Message,
							FatalCandidateReport.RelatedStableId);
					}

					AcceptedCandidate = MoveTemp(Candidate);
					return FWfcCollapsedCandidateEvaluation::Accept();
				};

			FZeroEscapeGenerationReport AttemptReport;
			const bool bAttemptSolved = FWfcSolver::Solve(
				Rules.GridSize,
				State.Constraints,
				WfcSettings,
				Variants,
				WfcRandom,
				ValidateCollapsedCandidate,
				AcceptedOpeningMasks,
				AttemptReport);
			AccumulateWfcSearchMetrics(
				AggregateWfcMetrics,
				AttemptReport.Metrics);

			if (bAttemptSolved)
			{
				OutReport = MoveTemp(AttemptReport);
				OutReport.Metrics = AggregateWfcMetrics;
				bSolved = true;
				break;
			}
			if (bHasFatalCandidateReport)
			{
				OutReport = MoveTemp(FatalCandidateReport);
				OutReport.Metrics = AggregateWfcMetrics;
				return false;
			}

			const bool bBudgetExhausted =
				AttemptReport.Failure
				== EZeroEscapeGenerationFailure::SolverBudgetExhausted;
			if (!bBudgetExhausted
				|| AttemptIndex + 1 >= Rules.MaxWfcSolveAttempts)
			{
				OutReport = MoveTemp(AttemptReport);
				OutReport.Metrics = AggregateWfcMetrics;
				if (bBudgetExhausted)
				{
					OutReport.ActualValue =
						OutReport.Metrics.WfcSolveAttemptCount;
					OutReport.LimitValue = Rules.MaxWfcSolveAttempts;
					OutReport.Message = FString::Printf(
						TEXT("已完成 %d/%d 次确定性 WFC 尝试；候选累计 %d/%d，回溯累计 %d/%d。"),
						OutReport.Metrics.WfcSolveAttemptCount,
						Rules.MaxWfcSolveAttempts,
						OutReport.Metrics.WfcCandidateAttemptCount,
						Rules.MaxWfcCandidateAttempts,
						OutReport.Metrics.WfcBacktrackCount,
						Rules.MaxWfcBacktrackCount);
				}
				return false;
			}
		}

		if (!bSolved
			|| AcceptedOpeningMasks.Num() != State.Constraints.Num()
			|| AcceptedCandidate.Cells.IsEmpty())
		{
			return FailGrid(
				OutReport,
				EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::SolverInvariantViolation,
				TEXT("WFC 没有原子移交完整稠密结果与非空 Plan。"));
		}

		BuildJunctionMetrics(AcceptedCandidate);
		AcceptedCandidate.CanonicalLayoutHash =
			FGenerationCore::ComputeCanonicalLayoutHash(AcceptedCandidate);
		if (AcceptedCandidate.CanonicalLayoutHash == 0)
		{
			return FailGrid(
				OutReport,
				EZeroEscapeGenerationStage::GlobalValidation,
				EZeroEscapeGenerationFailure::SolverInvariantViolation,
				TEXT("规范 Layout Hash 不能为 0。"));
		}

		OutReport.Stage = EZeroEscapeGenerationStage::None;
		OutReport.Failure = EZeroEscapeGenerationFailure::None;
		OutReport.RelatedStableId = INDEX_NONE;
		OutReport.ActualValue = 0;
		OutReport.LimitValue = 0;
		OutReport.Message.Reset();
		OutReport.Metrics.WalkableCellCount = AcceptedCandidate.Cells.Num();
		OutReport.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		OutPlan = MoveTemp(AcceptedCandidate);
		return true;
	}

	namespace GridStructurePrivate
	{
		/** 300 cm 墙段的规范 Key；Axis 0 沿 +X，Axis 1 沿 +Y。 */
		struct FStructureEdgeKey
		{
			FIntPoint StartVertex = FIntPoint::ZeroValue;
			uint8 Axis = 0;

			bool operator==(const FStructureEdgeKey& Other) const
			{
				return StartVertex == Other.StartVertex && Axis == Other.Axis;
			}
		};

		uint32 GetTypeHash(const FStructureEdgeKey& Edge)
		{
			const uint32 VertexHash = HashCombine(
				::GetTypeHash(Edge.StartVertex.X),
				::GetTypeHash(Edge.StartVertex.Y));
			return HashCombine(VertexHash, ::GetTypeHash(Edge.Axis));
		}

		void AddStructureInstance(
			const EStructurePieceKind Kind,
			const FVector Location,
			const double YawDegrees,
			TArray<FStructureInstance>& OutInstances)
		{
			FStructureInstance& Instance = OutInstances.AddDefaulted_GetRef();
			Instance.Kind = Kind;
			Instance.CanonicalLocalTransform =
				FTransform(FRotator(0.0, YawDegrees, 0.0), Location);
		}

		/** 把一个逻辑 Tile 的闭边拆成两个 300 cm 墙段。 */
		void AddClosedTileEdge(
			const FIntPoint Tile,
			const uint8 Direction,
			TSet<FStructureEdgeKey>& OutEdges)
		{
			const int32 CenterX = Tile.X * 2;
			const int32 CenterY = Tile.Y * 2;
			for (int32 Segment = 0; Segment < 2; ++Segment)
			{
				FStructureEdgeKey Edge;
				switch (Direction)
				{
				case 0: Edge = {FIntPoint(CenterX - 1 + Segment, CenterY + 1), 0}; break;
				case 1: Edge = {FIntPoint(CenterX + 1, CenterY - 1 + Segment), 1}; break;
				case 2: Edge = {FIntPoint(CenterX - 1 + Segment, CenterY - 1), 0}; break;
				default: Edge = {FIntPoint(CenterX - 1, CenterY - 1 + Segment), 1}; break;
				}
				OutEdges.Add(Edge);
			}
		}

		/** 记录墙段两端入射方向，供端点/转角/T/Cross 柱子分类。 */
		void AddEdgeToWallGraph(
			const FStructureEdgeKey& Edge,
			TMap<FIntPoint, uint8>& InOutGraph)
		{
			const FIntPoint End = Edge.StartVertex
				+ (Edge.Axis == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1));
			if (Edge.Axis == 0)
			{
				InOutGraph.FindOrAdd(Edge.StartVertex) |= Grid::DirectionBit(1);
				InOutGraph.FindOrAdd(End) |= Grid::DirectionBit(3);
			}
			else
			{
				InOutGraph.FindOrAdd(Edge.StartVertex) |= Grid::DirectionBit(0);
				InOutGraph.FindOrAdd(End) |= Grid::DirectionBit(2);
			}
		}
	}

	bool BuildCanonicalStructureInstances(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const FCanonicalStructureSettings& Settings,
		TArray<FStructureInstance>& OutInstances,
		FString& OutError)
	{
		using namespace GridStructurePrivate;
		OutInstances.Reset();
		OutError.Reset();
		if (Settings.LogicalTileSizeCm != 600
			|| Settings.StructureUnitSizeCm != 300
			|| Settings.LogicalTileSizeCm != Settings.StructureUnitSizeCm * 2
			|| !FMath::IsFinite(Settings.FloorTopZCm)
			|| !FMath::IsFinite(Settings.WallBaseZCm)
			|| !FMath::IsFinite(Settings.CeilingPivotZCm)
			|| !FMath::IsNearlyEqual(
				Plan.LogicalTileSizeCm,
				static_cast<double>(Settings.LogicalTileSizeCm)))
		{
			OutError = TEXT("结构展开只接受 600 cm Tile -> 2x2 个 300 cm 单元与有限 Z 参数。");
			return false;
		}

		TSet<FStructureEdgeKey> WallEdges;
		for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
		{
			if (Cell.OpeningMask == 0
				|| !Grid::IsInside(Cell.GridCoordinate, Plan.GridSize))
			{
				OutInstances.Reset();
				OutError = TEXT("结构展开收到了 Empty 或越界 Cell。");
				return false;
			}

			const double TileX =
				static_cast<double>(Cell.GridCoordinate.X * Settings.LogicalTileSizeCm);
			const double TileY =
				static_cast<double>(Cell.GridCoordinate.Y * Settings.LogicalTileSizeCm);
			const double Offset =
				static_cast<double>(Settings.StructureUnitSizeCm) * 0.5;
			for (int32 LocalY = 0; LocalY < 2; ++LocalY)
			{
				for (int32 LocalX = 0; LocalX < 2; ++LocalX)
				{
					const FVector FloorLocation(
						TileX + (LocalX == 0 ? -Offset : Offset),
						TileY + (LocalY == 0 ? -Offset : Offset),
						Settings.FloorTopZCm);
					AddStructureInstance(
						EStructurePieceKind::Floor,
						FloorLocation,
						0.0,
						OutInstances);
					AddStructureInstance(
						EStructurePieceKind::Ceiling,
						FVector(FloorLocation.X, FloorLocation.Y, Settings.CeilingPivotZCm),
						0.0,
						OutInstances);
				}
			}

			for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
			{
				if ((Cell.OpeningMask & Grid::DirectionBit(Direction)) == 0)
				{
					AddClosedTileEdge(Cell.GridCoordinate, Direction, WallEdges);
				}
			}
		}

		TArray<FStructureEdgeKey> StableEdges = WallEdges.Array();
		StableEdges.Sort([](const FStructureEdgeKey& A, const FStructureEdgeKey& B)
		{
			if (A.StartVertex.Y != B.StartVertex.Y)
			{
				return A.StartVertex.Y < B.StartVertex.Y;
			}
			if (A.StartVertex.X != B.StartVertex.X)
			{
				return A.StartVertex.X < B.StartVertex.X;
			}
			return A.Axis < B.Axis;
		});

		TMap<FIntPoint, uint8> WallGraph;
		for (const FStructureEdgeKey& Edge : StableEdges)
		{
			const double Unit = static_cast<double>(Settings.StructureUnitSizeCm);
			const FVector WallLocation(
				(Edge.StartVertex.X + (Edge.Axis == 0 ? 0.5 : 0.0)) * Unit,
				(Edge.StartVertex.Y + (Edge.Axis == 1 ? 0.5 : 0.0)) * Unit,
				Settings.WallBaseZCm);
			const double Yaw = Edge.Axis == 0 ? 90.0 : 0.0;
			AddStructureInstance(
				EStructurePieceKind::Wall,
				WallLocation,
				Yaw,
				OutInstances);
			AddStructureInstance(
				EStructurePieceKind::WallTopTrim,
				FVector(WallLocation.X, WallLocation.Y, Settings.CeilingPivotZCm),
				Yaw,
				OutInstances);
			AddEdgeToWallGraph(Edge, WallGraph);
		}

		TArray<FIntPoint> StableVertices;
		WallGraph.GetKeys(StableVertices);
		StableVertices.Sort(GridLayoutPrivate::CoordinateLess);
		for (const FIntPoint Vertex : StableVertices)
		{
			const uint8 IncidentMask = WallGraph.FindChecked(Vertex);
			const int32 Degree = FMath::CountBits(static_cast<uint32>(IncidentMask));
			const bool bStraightThrough = Degree == 2
				&& (IncidentMask == 0x05u || IncidentMask == 0x0Au);
			if (Degree == 1 || Degree >= 3 || (Degree == 2 && !bStraightThrough))
			{
				AddStructureInstance(
					EStructurePieceKind::Pillar,
					FVector(
						static_cast<double>(Vertex.X * Settings.StructureUnitSizeCm),
						static_cast<double>(Vertex.Y * Settings.StructureUnitSizeCm),
						Settings.WallBaseZCm),
					0.0,
					OutInstances);
			}
		}
		return true;
	}
}
