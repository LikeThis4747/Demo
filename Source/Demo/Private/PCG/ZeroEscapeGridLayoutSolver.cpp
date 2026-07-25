// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGridLayoutSolver.cpp
 * 职责：在固定矩形单层网格中嵌入 Start/Exit/2x2 Objective 局部约束，
 *       调用完整 16-mask、带界时间序回溯 WFC，并在原子提交 Plan 前完成全局玩法验证。
 * 边界：不使用 Socket、A* 或预雕刻固定路线；不读资产对象，不实例化世界对象。
 * 状态 Owner：FGridWorkingState 只属于当前 Solve；任何失败都丢弃候选态，不污染 OutPlan。
 */

#include "ZeroEscapeGridLayoutSolver.h"

#include "PCG/ZeroEscapeGenerationAssets.h"
#include "ZeroEscapeWfcSolver.h"

#include "Algo/Sort.h"
#include "Containers/ArrayView.h"
#include "Containers/Queue.h"

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		/** 返回单个方向位；具体位序始终复用 Types.h 的唯一 Grid 契约。 */
		uint8 DirectionBit(const int32 Direction)
		{
			check(Direction >= 0 && Direction < 4);
			return ZeroEscape::Grid::DirectionBit(static_cast<uint8>(Direction));
		}

		/** 返回 N/E/S/W 方向的反向索引。 */
		int32 OppositeDirection(const int32 Direction)
		{
			check(Direction >= 0 && Direction < 4);
			return static_cast<int32>(ZeroEscape::Grid::OppositeDirectionIndex(static_cast<uint8>(Direction)));
		}

		/** 把单步四邻域位移解析为 N/E/S/W 索引；非单步返回 INDEX_NONE。 */
		int32 DirectionFromDelta(const FIntPoint Delta)
		{
			for (int32 Direction = 0; Direction < 4; ++Direction)
			{
				if (ZeroEscape::Grid::Step(FIntPoint::ZeroValue, static_cast<uint8>(Direction)) == Delta)
				{
					return Direction;
				}
			}
			return INDEX_NONE;
		}

		/** 按 (Y, X) 比较坐标，用于所有规范序列。 */
		bool CoordinateLess(const FIntPoint A, const FIntPoint B)
		{
			return A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
		}

		/** 设置结构化失败报告，并统一返回 false。 */
		bool Fail(
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
			OutReport.RelatedStableId = RelatedStableId;
			OutReport.ActualValue = ActualValue;
			OutReport.LimitValue = LimitValue;
			OutReport.Message = Message;
			return false;
		}

		/** 一个已预验证的 2x2 Objective 房位置及其流程身份。 */
		struct FObjectiveRoomPlacement
		{
			FProgressionLandmark Landmark;
			FIntPoint MinCoordinate = FIntPoint::ZeroValue;
			FIntPoint AnchorCoordinate = FIntPoint::ZeroValue;
			int32 RegionId = INDEX_NONE;
		};

		/** Solve 私有中间态；Constraints 始终是完整 Grid 的 row-major 数组。 */
		struct FGridWorkingState
		{
			FIntPoint GridSize = FIntPoint::ZeroValue;
			TArray<FGridCellConstraint> Constraints;
			TArray<FObjectiveRoomPlacement> ObjectiveRooms;
			FProgressionLandmark StartLandmark;
			FProgressionLandmark ExitLandmark;
			FIntPoint StartCoordinate = FIntPoint::ZeroValue;
			FIntPoint ExitCoordinate = FIntPoint::ZeroValue;
		};

		/** 检查坐标是否位于半开区间 [0, GridSize) 内。 */
		bool IsInsideGrid(const FIntPoint Coordinate, const FIntPoint GridSize)
		{
			return ZeroEscape::Grid::IsInside(Coordinate, GridSize);
		}

		/** 把 Grid 坐标转换为 row-major 索引；调用者必须先验证边界。 */
		int32 ToCellIndex(const FIntPoint Coordinate, const FIntPoint GridSize)
		{
			check(IsInsideGrid(Coordinate, GridSize));
			return ZeroEscape::Grid::ToIndex(Coordinate, GridSize);
		}

		/** 返回可写约束；索引是从坐标计算而来，不依赖稳定 Id。 */
		FGridCellConstraint& ConstraintAt(FGridWorkingState& State, const FIntPoint Coordinate)
		{
			return State.Constraints[ToCellIndex(Coordinate, State.GridSize)];
		}

		/** 返回只读约束。 */
		const FGridCellConstraint& ConstraintAt(const FGridWorkingState& State, const FIntPoint Coordinate)
		{
			return State.Constraints[ToCellIndex(Coordinate, State.GridSize)];
		}

		/**
		 * 把 Cell 提升为 Required。已属于房间/Start/Exit 的语义不会被走廊路线覆盖；
		 * 显式的非 Corridor Region 则可用于首次赋值或最终语义矫正。
		 */
		void MarkRequired(
			FGridWorkingState& State,
			const FIntPoint Coordinate,
			const int32 RegionId = 0,
			const EZeroEscapeGridRegionKind RegionKind = EZeroEscapeGridRegionKind::Corridor)
		{
			FGridCellConstraint& Cell = ConstraintAt(State, Coordinate);
			Cell.Domain = EGridCellDomain::Required;
			if (Cell.RegionId == INDEX_NONE || RegionKind != EZeroEscapeGridRegionKind::Corridor)
			{
				Cell.RegionId = RegionId;
				Cell.RegionKind = RegionKind;
			}
		}

		/**
		 * 对称写入一条必开边。冲突不换 Seed，而是立即报告预验证或代码不变量错误。
		 */
		bool AddRequiredOpening(
			FGridWorkingState& State,
			const FIntPoint A,
			const FIntPoint B,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (!IsInsideGrid(A, State.GridSize) || !IsInsideGrid(B, State.GridSize))
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GridLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("Grid Required 开口指向了网格外。"));
			}

			const int32 Direction = DirectionFromDelta(B - A);
			if (Direction == INDEX_NONE)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GridLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("Grid Required 开口只允许单步四邻域连接。"));
			}

			FGridCellConstraint& CellA = ConstraintAt(State, A);
			FGridCellConstraint& CellB = ConstraintAt(State, B);
			const uint8 ABit = DirectionBit(Direction);
			const uint8 BBit = DirectionBit(OppositeDirection(Direction));
			if ((CellA.RequiredClosedMask & ABit) != 0 || (CellB.RequiredClosedMask & BBit) != 0)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GridLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("同一 Grid Edge 被同时标记为必开与必闭。"));
			}

			MarkRequired(State, A);
			MarkRequired(State, B);
			CellA.RequiredOpenMask |= ABit;
			CellB.RequiredOpenMask |= BBit;
			return true;
		}

		/** 初始化完整 row-major Constraint Grid，并封闭四周越界方向。 */
		void InitializeConstraintGrid(const FIntPoint GridSize, FGridWorkingState& OutState)
		{
			OutState = {};
			OutState.GridSize = GridSize;
			OutState.Constraints.SetNum(GridSize.X * GridSize.Y);
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < GridSize.X; ++X)
				{
					FGridCellConstraint& Cell = OutState.Constraints[Y * GridSize.X + X];
					Cell = {};
					Cell.Coordinate = FIntPoint(X, Y);
					// V4 让完整 Grid 参与 WFC；只有最终被折叠为 Empty 的格子才不进入可行走计划。
					Cell.Domain = EGridCellDomain::Optional;
					Cell.RegionId = INDEX_NONE;
					Cell.RegionKind = EZeroEscapeGridRegionKind::Corridor;
					if (Y == GridSize.Y - 1) Cell.RequiredClosedMask |= DirectionBit(0);
					if (X == GridSize.X - 1) Cell.RequiredClosedMask |= DirectionBit(1);
					if (Y == 0) Cell.RequiredClosedMask |= DirectionBit(2);
					if (X == 0) Cell.RequiredClosedMask |= DirectionBit(3);
				}
			}
		}

		/**
		 * 验证 V4 固定尺度、WFC 预算、全局布局上下界和可用进度槽容量。
		 * 这一阶段不使用 Seed，保证合法 Profile 不会在地标摆放时 rejection sampling。
		 */
		bool ValidateRequestAndSettings(
			const FGridLayoutRequest& Request,
			const FGridLayoutSettings& Settings,
			FZeroEscapeGenerationReport& OutReport)
		{
			const int64 GridCellCount = static_cast<int64>(Settings.GridSize.X) * Settings.GridSize.Y;
			if (Settings.GridSize.X < 14 || Settings.GridSize.Y < 10
				|| Settings.LogicalTileSizeCm != 600 || Settings.RoomSizeTiles != 2
				|| Settings.ObjectiveProgressBandCount <= 0
				|| Settings.ObjectiveProgressBandCount
					> ZeroEscape::GenerationLimits::MaxObjectiveProgressBands
				|| Settings.MinWalkableCellCount <= 0
				|| Settings.MaxWalkableCellCount < Settings.MinWalkableCellCount
				|| Settings.MaxWalkableCellCount > GridCellCount
				|| Settings.MaxConsecutiveStraightTiles <= 0
				|| Settings.MaxConsecutiveStraightTiles > FMath::Max(Settings.GridSize.X, Settings.GridSize.Y)
				|| Settings.MaxWfcCandidateAttempts <= 0
				|| Settings.MaxWfcBacktrackCount <= 0
				|| Settings.MaxWfcSolveAttempts <= 0
				|| Settings.MaxWfcSolveAttempts > 16
				|| Settings.MaxWfcCandidateAttempts < Settings.MaxWfcSolveAttempts
				|| Settings.MaxWfcBacktrackCount < Settings.MaxWfcSolveAttempts
				|| Settings.MaxRequiredRouteLengthTiles <= 0
				|| Settings.MaxRequiredRouteExtraTiles < 0
				|| !FMath::IsFinite(Settings.GameplayAnchorHeightCm))
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("Grid V4 需要有效的尺度、非空格数量、连续直线、WFC 尝试/总预算与路线上限。"));
			}

			int32 StartCount = 0;
			int32 ExitCount = 0;
			int32 ObjectiveCount = 0;
			int32 ActualStartLandmarkId = INDEX_NONE;
			int32 ActualExitLandmarkId = INDEX_NONE;
			TSet<int32> StableLandmarkIds;
			TSet<int32> StableObjectiveIds;
			TMap<int32, int32> ObjectivesPerBand;
			TSet<int64> UsedBandLanes;
			for (const FProgressionLandmark& Landmark : Request.Progression.Landmarks)
			{
				if (Landmark.StableLandmarkId == INDEX_NONE || StableLandmarkIds.Contains(Landmark.StableLandmarkId))
				{
					return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("Progression Landmark Stable Id 缺失或重复。"), 0, 0,
						Landmark.StableLandmarkId);
				}
				StableLandmarkIds.Add(Landmark.StableLandmarkId);

				switch (Landmark.Kind)
				{
				case EProgressionLandmarkKind::Start:
					++StartCount;
					ActualStartLandmarkId = Landmark.StableLandmarkId;
					break;
				case EProgressionLandmarkKind::Exit:
					++ExitCount;
					ActualExitLandmarkId = Landmark.StableLandmarkId;
					break;
				case EProgressionLandmarkKind::Objective:
					++ObjectiveCount;
					if (Landmark.ProgressBandIndex < 0
						|| Landmark.ProgressBandIndex >= Settings.ObjectiveProgressBandCount
						|| Landmark.LaneIndex < 0 || Landmark.LaneIndex > 1
						|| Landmark.StableObjectiveId == INDEX_NONE)
					{
						return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
							EZeroEscapeGenerationFailure::CapacityInsufficient,
							TEXT("Objective ProgressBandIndex 超出 Profile 推进带容量。"),
							Landmark.ProgressBandIndex, Settings.ObjectiveProgressBandCount,
							Landmark.StableLandmarkId);
					}
					if (StableObjectiveIds.Contains(Landmark.StableObjectiveId))
					{
						return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("Objective Stable Id 不能重复。"), 0, 0,
							Landmark.StableObjectiveId);
					}
					StableObjectiveIds.Add(Landmark.StableObjectiveId);
					{
						const int64 BandLaneKey = (static_cast<int64>(Landmark.ProgressBandIndex) << 32)
							| static_cast<uint32>(Landmark.LaneIndex);
						if (UsedBandLanes.Contains(BandLaneKey))
						{
							return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
								EZeroEscapeGenerationFailure::CapacityInsufficient,
								TEXT("两个 Objective 不能占用同一 ProgressBand/Lane 槽位。"),
								Landmark.LaneIndex, 1, Landmark.StableLandmarkId);
						}
						UsedBandLanes.Add(BandLaneKey);
					}
					++ObjectivesPerBand.FindOrAdd(Landmark.ProgressBandIndex);
					break;
				default:
					return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("Progression 含有未支持的 Landmark Kind。"));
				}
			}

			if (StartCount != 1 || ExitCount != 1
				|| ActualStartLandmarkId != Request.Progression.StartStableLandmarkId
				|| ActualExitLandmarkId != Request.Progression.ExitStableLandmarkId)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("Grid 布局要求恰好一个 Start 和一个 Exit Landmark。"));
			}
			for (const TPair<int32, int32>& Pair : ObjectivesPerBand)
			{
				if (Pair.Value > 2)
				{
					return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
						EZeroEscapeGenerationFailure::CapacityInsufficient,
						TEXT("每个 Objective 推进带只有上下两个预验证 Lane。"),
						Pair.Value, 2);
				}
			}
			if (ObjectiveCount != Request.Progression.ObjectiveCandidateCount
				|| ObjectiveCount > Settings.ObjectiveProgressBandCount * 2)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::CapacityInsufficient,
					TEXT("Objective Landmark 数量必须与 Intent.N 一致，且不超过推进带 x 双 Lane 容量。"),
					ObjectiveCount, FMath::Min(Request.Progression.ObjectiveCandidateCount,
						Settings.ObjectiveProgressBandCount * 2));
			}

			const int32 RequiredCount = Request.Progression.RequiredObjectiveCount;
			if (RequiredCount < 0 || RequiredCount > ObjectiveCount
				|| (Request.Progression.CompletionRule == EZeroEscapeCompletionRule::EscapeOnly && RequiredCount != 0)
				|| (Request.Progression.CompletionRule == EZeroEscapeCompletionRule::CollectAll
					&& RequiredCount != ObjectiveCount)
				|| (Request.Progression.CompletionRule == EZeroEscapeCompletionRule::CollectKOfN
					&& (RequiredCount <= 0 || ObjectiveCount <= 0)))
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::InvalidKOfN,
					TEXT("CompletionRule 与 Objective K/N 不一致。"), RequiredCount, ObjectiveCount);
			}

			const int32 MinRoomBaseX = 4;
			/**
			 * 同带上房会在 BaseX 基础上右移一格，避免上下目标房重叠。
			 * 因此右侧容量必须额外预留这一格；再扣除房宽和 WFC 连通空间缓冲。
			 */
			const int32 MaxRoomBaseX = Settings.GridSize.X - Settings.RoomSizeTiles - 5;
			const int32 RoomSpan = MaxRoomBaseX - MinRoomBaseX;
			const int32 MinimumBandSpacing = Settings.RoomSizeTiles + 1;
			if (MaxRoomBaseX < MinRoomBaseX
				|| (Settings.ObjectiveProgressBandCount > 1
					&& RoomSpan < (Settings.ObjectiveProgressBandCount - 1) * MinimumBandSpacing))
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::CapacityInsufficient,
					TEXT("Grid X 尺寸无法容纳当前推进带数和 2x2 房间安全间隔。"));
			}

			const int32 CenterY = Settings.GridSize.Y / 2;
			const int32 LowerLaneY = CenterY - Settings.RoomSizeTiles;
			const int32 UpperLaneY = CenterY + 1;
			// 两条 Lane 保持在中部，但外侧仍需至少一格，供 WFC 自主选择房间的外部连接。
			if (LowerLaneY < 1
				|| UpperLaneY + Settings.RoomSizeTiles > Settings.GridSize.Y - 1)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::CapacityInsufficient,
					TEXT("Grid Y 尺寸无法容纳相邻 2x2 房间 Lane 和外侧连接缓冲格。"));
			}
			return true;
		}

		/**
		 * 把 Progression Landmark 嵌入完全可数的进度槽。
		 * 这里只确定语义位置和房间内部，不预先规定 Start、房间与 Exit 之间的路线形状。
		 */
		bool EmbedLandmarks(
			const FGridLayoutRequest& Request,
			const FGridLayoutSettings& Settings,
			FGridWorkingState& OutState,
			FZeroEscapeGenerationReport& OutReport)
		{
			InitializeConstraintGrid(Settings.GridSize, OutState);
			TArray<FProgressionLandmark> Objectives;
			for (const FProgressionLandmark& Landmark : Request.Progression.Landmarks)
			{
				switch (Landmark.Kind)
				{
				case EProgressionLandmarkKind::Start:
					OutState.StartLandmark = Landmark;
					break;
				case EProgressionLandmarkKind::Exit:
					OutState.ExitLandmark = Landmark;
					break;
				case EProgressionLandmarkKind::Objective:
					Objectives.Add(Landmark);
					break;
				default:
					return Fail(OutReport, EZeroEscapeGenerationStage::Progression,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("地标嵌入阶段遇到了未支持的 Landmark Kind。"));
				}
			}
			Objectives.Sort([](const FProgressionLandmark& A, const FProgressionLandmark& B)
			{
				return A.ProgressBandIndex != B.ProgressBandIndex
					? A.ProgressBandIndex < B.ProgressBandIndex
					: A.StableLandmarkId < B.StableLandmarkId;
			});

			const int32 CenterY = Settings.GridSize.Y / 2;
			int32 StartY = FMath::Clamp(CenterY - 1, 2, Settings.GridSize.Y - 3);
			int32 ExitY = FMath::Clamp(CenterY + 1, 2, Settings.GridSize.Y - 3);
			if (StartY == ExitY)
			{
				ExitY = FMath::Min(Settings.GridSize.Y - 2, StartY + 1);
			}
			OutState.StartCoordinate = FIntPoint(1, StartY);
			OutState.ExitCoordinate = FIntPoint(Settings.GridSize.X - 2, ExitY);

			const int32 MinRoomBaseX = 4;
			const int32 MaxRoomBaseX = Settings.GridSize.X - Settings.RoomSizeTiles - 5;
			const int32 LowerLaneY = CenterY - Settings.RoomSizeTiles;
			const int32 UpperLaneY = CenterY + 1;
			TMap<int32, TArray<FProgressionLandmark>> ObjectivesByBand;
			for (const FProgressionLandmark& Objective : Objectives)
			{
				ObjectivesByBand.FindOrAdd(Objective.ProgressBandIndex).Add(Objective);
			}

			int32 NextObjectiveRegionId = 100;
			for (int32 Band = 0; Band < Settings.ObjectiveProgressBandCount; ++Band)
			{
				TArray<FProgressionLandmark>* BandObjectives = ObjectivesByBand.Find(Band);
				if (BandObjectives == nullptr)
				{
					continue;
				}

				BandObjectives->Sort([](const FProgressionLandmark& A, const FProgressionLandmark& B)
				{
					return A.StableLandmarkId < B.StableLandmarkId;
				});
				const int32 BaseRoomX = Settings.ObjectiveProgressBandCount == 1
					? (MinRoomBaseX + MaxRoomBaseX) / 2
					: MinRoomBaseX + ((MaxRoomBaseX - MinRoomBaseX) * Band)
						/ (Settings.ObjectiveProgressBandCount - 1);
				for (const FProgressionLandmark& Objective : *BandObjectives)
				{
					FObjectiveRoomPlacement Room;
					Room.Landmark = Objective;
					// 同一进度带若同时有上下两个房间，上房向右错开一格，避免两个 2x2 区域重叠。
					Room.MinCoordinate = FIntPoint(
						BaseRoomX + Objective.LaneIndex,
						Objective.LaneIndex == 0 ? LowerLaneY : UpperLaneY);
					// Anchor 放在房间靠网格中部的一侧；外部入口数量与方向仍由 WFC 决定。
					const int32 NearCenterLocalY = Objective.LaneIndex == 0
						? Settings.RoomSizeTiles - 1
						: 0;
					Room.AnchorCoordinate = Room.MinCoordinate + FIntPoint(0, NearCenterLocalY);
					Room.RegionId = NextObjectiveRegionId++;
					OutState.ObjectiveRooms.Add(MoveTemp(Room));
				}
			}

			OutState.ObjectiveRooms.Sort([](
				const FObjectiveRoomPlacement& A,
				const FObjectiveRoomPlacement& B)
			{
				return A.Landmark.StableLandmarkId < B.Landmark.StableLandmarkId;
			});
			return true;
		}

		/**
		 * 把 Start、Exit 与 Objective 房内部提升为 Required，并只固定房间内部开口。
		 * 房间外部入口与地标之间的路线全部交给 WFC；这样不会再预雕刻固定横向主干。
		 */
		bool ApplyLandmarkConstraints(
			const FGridLayoutSettings& Settings,
			FGridWorkingState& State,
			FZeroEscapeGenerationReport& OutReport)
		{
			MarkRequired(State, State.StartCoordinate, 1, EZeroEscapeGridRegionKind::Start);
			MarkRequired(State, State.ExitCoordinate, 2, EZeroEscapeGridRegionKind::Exit);

			for (FObjectiveRoomPlacement& Room : State.ObjectiveRooms)
			{
				for (int32 LocalY = 0; LocalY < Settings.RoomSizeTiles; ++LocalY)
				{
					for (int32 LocalX = 0; LocalX < Settings.RoomSizeTiles; ++LocalX)
					{
						const FIntPoint Cell = Room.MinCoordinate + FIntPoint(LocalX, LocalY);
						MarkRequired(State, Cell, Room.RegionId, EZeroEscapeGridRegionKind::Objective);
						if (LocalX + 1 < Settings.RoomSizeTiles
							&& !AddRequiredOpening(State, Cell, Cell + FIntPoint(1, 0), OutReport))
						{
							return false;
						}
						if (LocalY + 1 < Settings.RoomSizeTiles
							&& !AddRequiredOpening(State, Cell, Cell + FIntPoint(0, 1), OutReport))
						{
							return false;
						}
					}
				}
			}
			return true;
		}

		/** 将 WFC 的完整稠密输出转换为 Plan 的非空稀疏 Cells，并建立地标/Anchor 稳定绑定。 */
		bool ExportCandidatePlan(
			const FGridLayoutRequest& Request,
			const FGridLayoutSettings& Settings,
			const FGridWorkingState& State,
			const TConstArrayView<uint8> OpeningMasks,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (OpeningMasks.Num() != State.Constraints.Num())
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::WfcLayout,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("WFC 输出数量与完整 Grid Constraint 数量不一致。"),
					OpeningMasks.Num(), State.Constraints.Num());
			}

			OutPlan = {};
			OutPlan.Signature = Request.Signature;
			OutPlan.CanonicalProgressionHash = FGenerationCore::ComputeCanonicalProgressionHash(Request.Progression);
			OutPlan.GridSize = Settings.GridSize;
			OutPlan.LogicalTileSizeCm = static_cast<double>(Settings.LogicalTileSizeCm);
			OutPlan.CompletionRule = Request.Progression.CompletionRule;
			OutPlan.ObjectiveCandidateCount = Request.Progression.ObjectiveCandidateCount;
			OutPlan.RequiredObjectiveCount = Request.Progression.RequiredObjectiveCount;
			OutPlan.StartCoordinate = State.StartCoordinate;
			OutPlan.ExitCoordinate = State.ExitCoordinate;

			for (int32 DenseIndex = 0; DenseIndex < OpeningMasks.Num(); ++DenseIndex)
			{
				if (OpeningMasks[DenseIndex] == 0)
				{
					continue;
				}
				const FGridCellConstraint& Source = State.Constraints[DenseIndex];
				FZeroEscapeCollapsedTile& Tile = OutPlan.Cells.AddDefaulted_GetRef();
				Tile.StableCellId = OutPlan.Cells.Num() - 1;
				Tile.GridCoordinate = Source.Coordinate;
				Tile.OpeningMask = OpeningMasks[DenseIndex];
				Tile.RegionId = Source.RegionId == INDEX_NONE ? 0 : Source.RegionId;
				Tile.RegionKind = Source.RegionKind;
			}

			TArray<FProgressionLandmark> Landmarks = Request.Progression.Landmarks;
			Landmarks.Sort([](const FProgressionLandmark& A, const FProgressionLandmark& B)
			{
				return A.StableLandmarkId < B.StableLandmarkId;
			});
			int32 NextAnchorId = 0;
			for (const FProgressionLandmark& Landmark : Landmarks)
			{
				FIntPoint Coordinate = FIntPoint::ZeroValue;
				int32 RegionId = INDEX_NONE;
				EZeroEscapeGameplayAnchorType AnchorType = EZeroEscapeGameplayAnchorType::Objective;
				if (Landmark.Kind == EProgressionLandmarkKind::Start)
				{
					Coordinate = State.StartCoordinate;
					RegionId = 1;
					AnchorType = EZeroEscapeGameplayAnchorType::PlayerSpawn;
				}
				else if (Landmark.Kind == EProgressionLandmarkKind::Exit)
				{
					Coordinate = State.ExitCoordinate;
					RegionId = 2;
					AnchorType = EZeroEscapeGameplayAnchorType::Exit;
				}
				else
				{
					const FObjectiveRoomPlacement* Room = State.ObjectiveRooms.FindByPredicate(
						[&](const FObjectiveRoomPlacement& Candidate)
						{
							return Candidate.Landmark.StableLandmarkId == Landmark.StableLandmarkId;
						});
					if (Room == nullptr)
					{
						return Fail(OutReport, EZeroEscapeGenerationStage::GridLayout,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("Objective Landmark 没有对应的预验证房间槽。"),
							0, 0, Landmark.StableLandmarkId);
					}
					Coordinate = Room->AnchorCoordinate;
					RegionId = Room->RegionId;
				}

				FZeroEscapeLandmarkBinding& LandmarkBinding = OutPlan.LandmarkBindings.AddDefaulted_GetRef();
				LandmarkBinding.StableLandmarkId = Landmark.StableLandmarkId;
				LandmarkBinding.GridCoordinate = Coordinate;
				LandmarkBinding.RegionId = RegionId;

				FZeroEscapeGeneratedAnchor& Anchor = OutPlan.GameplayAnchors.AddDefaulted_GetRef();
				Anchor.StableAnchorInstanceId = NextAnchorId++;
				Anchor.Type = AnchorType;
				Anchor.GridCoordinate = Coordinate;
				Anchor.RegionId = RegionId;
				Anchor.LocalTransform = FTransform(
					FVector(static_cast<double>(Coordinate.X * Settings.LogicalTileSizeCm),
						static_cast<double>(Coordinate.Y * Settings.LogicalTileSizeCm),
						Settings.GameplayAnchorHeightCm));

				if (Landmark.Kind == EProgressionLandmarkKind::Start)
				{
					OutPlan.PlayerSpawnAnchorInstanceId = Anchor.StableAnchorInstanceId;
				}
				else if (Landmark.Kind == EProgressionLandmarkKind::Exit)
				{
					OutPlan.ExitAnchorInstanceId = Anchor.StableAnchorInstanceId;
				}
				else
				{
					FZeroEscapeObjectiveBinding& Binding = OutPlan.ObjectiveBindings.AddDefaulted_GetRef();
					Binding.StableObjectiveId = Landmark.StableObjectiveId;
					Binding.GridCoordinate = Coordinate;
					Binding.RegionId = RegionId;
					Binding.StableAnchorInstanceId = Anchor.StableAnchorInstanceId;
				}
			}
			OutPlan.ObjectiveBindings.Sort([](const FZeroEscapeObjectiveBinding& A, const FZeroEscapeObjectiveBinding& B)
			{
				return A.StableObjectiveId < B.StableObjectiveId;
			});
			if (OutPlan.CanonicalProgressionHash == 0)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("规范 Progression Hash 不能为 0。"));
			}
			return true;
		}

		/** 用当前 OpeningMask 从 Start 做 BFS，返回稀疏 Cell 索引到最短距离的数组。 */
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
				for (uint8 Direction = 0; Direction < ZeroEscape::Grid::DirectionCount; ++Direction)
				{
					if ((Current.OpeningMask & ZeroEscape::Grid::DirectionBit(Direction)) == 0)
					{
						continue;
					}
					const int32* NeighborIndex = OutCellByCoordinate.Find(
						ZeroEscape::Grid::Step(Current.GridCoordinate, Direction));
					if (NeighborIndex != nullptr && Distances[*NeighborIndex] == INDEX_NONE)
					{
						Distances[*NeighborIndex] = Distances[CurrentIndex] + 1;
						Queue.Enqueue(*NeighborIndex);
					}
				}
			}
			return Distances;
		}

		/** 根据非空 Mask 形态更新观测指标；任何计数为 0 都不是失败条件。 */
		void BuildJunctionMetrics(FZeroEscapeGeneratedLevelPlan& Plan)
		{
			Plan.JunctionMetrics = {};
			for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
			{
				const uint8 Mask = Cell.OpeningMask & ZeroEscape::Grid::AllOpenEdges;
				const int32 OpenCount = FMath::CountBits(static_cast<uint32>(Mask));
				if (OpenCount == 1) ++Plan.JunctionMetrics.DeadEndCount;
				else if (OpenCount == 3) ++Plan.JunctionMetrics.TJunctionCount;
				else if (OpenCount == 4) ++Plan.JunctionMetrics.CrossJunctionCount;
				else if (OpenCount == 2)
				{
					const bool bStraight = (Mask & 0x05u) == 0x05u || (Mask & 0x0Au) == 0x0Au;
					if (bStraight) ++Plan.JunctionMetrics.StraightCount;
					else ++Plan.JunctionMetrics.CornerCount;
				}
			}
		}

		/**
		 * 在无权网格的最短路闭包上做小型 bitmask DP，返回 Start 访问任意 K 个目标后到 Exit
		 * 的最短长度。N 由 Core 上限约束；这里不固定必须哪 K 个，与 K-of-N 玩法语义一致。
		 */
		int32 ComputeShortestCompletionRoute(const FZeroEscapeGeneratedLevelPlan& Plan)
		{
			const int32 ObjectiveCount = Plan.ObjectiveBindings.Num();
			const int32 RequiredCount = Plan.RequiredObjectiveCount;
			TMap<FIntPoint, int32> Lookup;
			const TArray<int32> StartDistances = BuildDistances(Plan, Plan.StartCoordinate, Lookup);
			const int32* ExitIndex = Lookup.Find(Plan.ExitCoordinate);
			if (ExitIndex == nullptr || StartDistances[*ExitIndex] == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			if (RequiredCount == 0)
			{
				return StartDistances[*ExitIndex];
			}
			if (ObjectiveCount <= 0 || ObjectiveCount > 12)
			{
				return INDEX_NONE;
			}

			TArray<TArray<int32>> DistancesFromObjective;
			DistancesFromObjective.Reserve(ObjectiveCount);
			for (const FZeroEscapeObjectiveBinding& Objective : Plan.ObjectiveBindings)
			{
				TMap<FIntPoint, int32> IgnoredLookup;
				DistancesFromObjective.Add(BuildDistances(Plan, Objective.GridCoordinate, IgnoredLookup));
			}

			const int32 StateCount = 1 << ObjectiveCount;
			TArray<int32> Best;
			Best.Init(MAX_int32, StateCount * ObjectiveCount);
			for (int32 Objective = 0; Objective < ObjectiveCount; ++Objective)
			{
				const int32* ObjectiveIndex = Lookup.Find(Plan.ObjectiveBindings[Objective].GridCoordinate);
				if (ObjectiveIndex != nullptr && StartDistances[*ObjectiveIndex] != INDEX_NONE)
				{
					Best[(1 << Objective) * ObjectiveCount + Objective] = StartDistances[*ObjectiveIndex];
				}
			}

			int32 Result = MAX_int32;
			for (int32 Mask = 1; Mask < StateCount; ++Mask)
			{
				for (int32 Last = 0; Last < ObjectiveCount; ++Last)
				{
					const int32 Current = Best[Mask * ObjectiveCount + Last];
					if (Current == MAX_int32 || (Mask & (1 << Last)) == 0)
					{
						continue;
					}
					if (FMath::CountBits(static_cast<uint32>(Mask)) >= RequiredCount)
					{
						const int32* FinalIndex = Lookup.Find(Plan.ExitCoordinate);
						const int32 Tail = FinalIndex == nullptr ? INDEX_NONE : DistancesFromObjective[Last][*FinalIndex];
						if (Tail != INDEX_NONE) Result = FMath::Min(Result, Current + Tail);
					}
					for (int32 Next = 0; Next < ObjectiveCount; ++Next)
					{
						if ((Mask & (1 << Next)) != 0) continue;
						const int32* NextIndex = Lookup.Find(Plan.ObjectiveBindings[Next].GridCoordinate);
						const int32 Step = NextIndex == nullptr ? INDEX_NONE : DistancesFromObjective[Last][*NextIndex];
						if (Step != INDEX_NONE)
						{
							int32& Destination = Best[(Mask | (1 << Next)) * ObjectiveCount + Next];
							Destination = FMath::Min(Destination, Current + Step);
						}
					}
				}
			}
			return Result == MAX_int32 ? INDEX_NONE : Result;
		}

		/** 对最终稀疏 Plan 执行开闭对称、必达可达、Anchor 和共享路线上限验证。 */
		bool ValidateFinalPlan(
			const FGridLayoutRequest& Request,
			const FGridLayoutSettings& Settings,
			const FGridWorkingState& State,
			const FZeroEscapeGeneratedLevelPlan& Plan,
			FZeroEscapeGenerationReport& OutReport)
		{
			TMap<FIntPoint, int32> CellByCoordinate;
			if (Plan.Cells.Num() < Settings.MinWalkableCellCount
				|| Plan.Cells.Num() > Settings.MaxWalkableCellCount)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("最终非空 Tile 数量越过了已传播的 Count 约束。"),
					Plan.Cells.Num(), Plan.Cells.Num() < Settings.MinWalkableCellCount
						? Settings.MinWalkableCellCount
						: Settings.MaxWalkableCellCount);
			}
			for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
			{
				const FZeroEscapeCollapsedTile& Cell = Plan.Cells[Index];
				if (Cell.StableCellId != Index || Cell.OpeningMask == 0
					|| (Cell.OpeningMask & ~ZeroEscape::Grid::AllOpenEdges) != 0
					|| !IsInsideGrid(Cell.GridCoordinate, Plan.GridSize)
					|| CellByCoordinate.Contains(Cell.GridCoordinate))
				{
					return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("最终 Cells 的稳定序、坐标唯一性或 OpeningMask 非法。"), Index);
				}
				CellByCoordinate.Add(Cell.GridCoordinate, Index);
			}
			for (const FGridCellConstraint& Constraint : State.Constraints)
			{
				if (Constraint.Domain == EGridCellDomain::Required
					&& !CellByCoordinate.Contains(Constraint.Coordinate))
				{
					return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("WFC 丢失了 Required Cell。"),
						ToCellIndex(Constraint.Coordinate, State.GridSize));
				}
			}

			/**
			 * 独立复核连续直线约束，避免约束传播实现退化时把非法叶节点当作可提交计划。
			 * 横向只统计同时具备 E/W 开口的贯通格，纵向同理统计 N/S 贯通格。
			 */
			const uint8 HorizontalThroughMask = DirectionBit(1) | DirectionBit(3);
			const uint8 VerticalThroughMask = DirectionBit(0) | DirectionBit(2);
			for (int32 Y = 0; Y < Plan.GridSize.Y; ++Y)
			{
				int32 ConsecutiveCount = 0;
				for (int32 X = 0; X < Plan.GridSize.X; ++X)
				{
					const int32* CellIndex = CellByCoordinate.Find(FIntPoint(X, Y));
					const bool bHorizontalThrough = CellIndex != nullptr
						&& (Plan.Cells[*CellIndex].OpeningMask & HorizontalThroughMask) == HorizontalThroughMask;
					ConsecutiveCount = bHorizontalThrough ? ConsecutiveCount + 1 : 0;
					if (ConsecutiveCount > Settings.MaxConsecutiveStraightTiles)
					{
						return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("最终布局存在超过上限的水平连续直行 Tile。"),
							ConsecutiveCount, Settings.MaxConsecutiveStraightTiles);
					}
				}
			}
			for (int32 X = 0; X < Plan.GridSize.X; ++X)
			{
				int32 ConsecutiveCount = 0;
				for (int32 Y = 0; Y < Plan.GridSize.Y; ++Y)
				{
					const int32* CellIndex = CellByCoordinate.Find(FIntPoint(X, Y));
					const bool bVerticalThrough = CellIndex != nullptr
						&& (Plan.Cells[*CellIndex].OpeningMask & VerticalThroughMask) == VerticalThroughMask;
					ConsecutiveCount = bVerticalThrough ? ConsecutiveCount + 1 : 0;
					if (ConsecutiveCount > Settings.MaxConsecutiveStraightTiles)
					{
						return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("最终布局存在超过上限的垂直连续直行 Tile。"),
							ConsecutiveCount, Settings.MaxConsecutiveStraightTiles);
					}
				}
			}

			for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
			{
				for (uint8 Direction = 0; Direction < ZeroEscape::Grid::DirectionCount; ++Direction)
				{
					const bool bOpen = (Cell.OpeningMask & ZeroEscape::Grid::DirectionBit(Direction)) != 0;
					const FIntPoint NeighborCoordinate = ZeroEscape::Grid::Step(Cell.GridCoordinate, Direction);
					const int32* NeighborIndex = CellByCoordinate.Find(NeighborCoordinate);
					if (bOpen && (NeighborIndex == nullptr
						|| (Plan.Cells[*NeighborIndex].OpeningMask & ZeroEscape::Grid::DirectionBit(
							ZeroEscape::Grid::OppositeDirectionIndex(Direction))) == 0))
					{
						return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("最终 Grid 存在越界开口、开向 Empty 的边或非对称开口。"),
							Cell.StableCellId);
					}
				}
				const FGridCellConstraint& Constraint = ConstraintAt(State, Cell.GridCoordinate);
				if (Constraint.Domain == EGridCellDomain::Required
					&& ((Cell.OpeningMask & Constraint.RequiredOpenMask) != Constraint.RequiredOpenMask
						|| (Cell.OpeningMask & Constraint.RequiredClosedMask) != 0))
				{
					return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("最终 Required Cell 没有保留必开边或穿过了必闭边。"),
						Cell.StableCellId);
				}
			}

			TMap<FIntPoint, int32> Lookup;
			const TArray<int32> Distances = BuildDistances(Plan, Plan.StartCoordinate, Lookup);
			for (int32 Index = 0; Index < Distances.Num(); ++Index)
			{
				if (Distances[Index] == INDEX_NONE)
				{
					return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("最终布局仍有非空 Tile 不可从 Start 到达。"), Index);
				}
			}
			for (const FZeroEscapeLandmarkBinding& Binding : Plan.LandmarkBindings)
			{
				const int32* Index = Lookup.Find(Binding.GridCoordinate);
				if (Index == nullptr || Distances[*Index] == INDEX_NONE)
				{
					return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("有 Landmark Binding 不可从 Start 到达。"), 0, 0,
						Binding.StableLandmarkId);
				}
			}

			const int32* ExitIndex = Lookup.Find(Plan.ExitCoordinate);
			const int32 DirectLength = ExitIndex == nullptr ? INDEX_NONE : Distances[*ExitIndex];
			if (DirectLength == INDEX_NONE || DirectLength > Settings.MaxRequiredRouteLengthTiles)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::RequiredRouteTooLong,
					TEXT("Start -> Exit 最短路超过所有难度共享上限。"),
					DirectLength, Settings.MaxRequiredRouteLengthTiles);
			}
			const int32 CompletionLength = ComputeShortestCompletionRoute(Plan);
			/**
			 * MaxRequiredRouteLengthTiles 是所有难度共享的完整通关路线总长上限，不只是
			 * EscapeOnly 的 Start -> Exit 上限。必须先检查完成条件后的绝对长度，再独立检查
			 * 相对直达路线的额外成本，避免“直达 39、完成 53、Extra 14”错误通过总长 40。
			 */
			if (CompletionLength == INDEX_NONE
				|| CompletionLength > Settings.MaxRequiredRouteLengthTiles)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::RequiredRouteTooLong,
					TEXT("满足 K-of-N/CollectAll 条件后的最短完成路线无解或超过共享总长上限。"),
					CompletionLength, Settings.MaxRequiredRouteLengthTiles);
			}
			if (CompletionLength - DirectLength > Settings.MaxRequiredRouteExtraTiles)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::LongRetraceLimitExceeded,
					TEXT("K-of-N 最短完成路线相对直达 Exit 的额外长度超过共享上限。"),
					CompletionLength - DirectLength,
					Settings.MaxRequiredRouteExtraTiles);
			}
			if (Plan.LandmarkBindings.Num() != Request.Progression.Landmarks.Num()
				|| Plan.GameplayAnchors.Num() != Request.Progression.Landmarks.Num()
				|| Plan.ObjectiveBindings.Num() != Request.Progression.ObjectiveCandidateCount)
			{
				return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("最终 Landmark、Anchor 或 Objective Binding 数量与 Progression Intent 不一致。"));
			}
			return true;
		}

		/**
		 * 把一棵 WFC 搜索树的纯搜索指标累加到整局总计。
		 *
		 * Walkable、Planning 和实例化字段由 Grid/Runtime 在成功提交后写入，不能在重试间相加。
		 */
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
			InOutTotal.WfcCountContradictionCount += Attempt.WfcCountContradictionCount;
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

		/** 把整局总预算稳定地平均分给各棵搜索树；全部分片之和严格等于 TotalBudget。 */
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
		const FGridLayoutRequest& Request,
		const FGridLayoutSettings& Settings,
		const FZeroEscapeWfcShapeWeights& Weights,
		const int32 MasterSeed,
		FZeroEscapeGeneratedLevelPlan& OutPlan,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutPlan = {};
		OutReport = {};
		const double StartSeconds = FPlatformTime::Seconds();
		if (!ValidateRequestAndSettings(Request, Settings, OutReport))
		{
			return false;
		}

		FGridWorkingState State;
		if (!EmbedLandmarks(Request, Settings, State, OutReport)
			|| !ApplyLandmarkConstraints(Settings, State, OutReport))
		{
			return false;
		}

		TStaticArray<FTileVariant, 16> StaticVariants;
		FWfcSolver::BuildCanonicalVariants(Weights, StaticVariants);
		TArray<FTileVariant> Variants;
		Variants.Reserve(16);
		for (const FTileVariant& Variant : StaticVariants) Variants.Add(Variant);
		FZeroEscapeWfcSolveSettings WfcSettings;
		WfcSettings.StartCoordinate = State.StartCoordinate;
		WfcSettings.MinWalkableCellCount = Settings.MinWalkableCellCount;
		WfcSettings.MaxWalkableCellCount = Settings.MaxWalkableCellCount;
		WfcSettings.MaxConsecutiveStraightTiles = Settings.MaxConsecutiveStraightTiles;

		FZeroEscapeGeneratedLevelPlan AcceptedCandidate;
		TArray<uint8> AcceptedOpeningMasks;
		FZeroEscapeGenerationMetrics AggregateWfcMetrics;
		bool bSolved = false;
		for (int32 AttemptIndex = 0;
			AttemptIndex < Settings.MaxWfcSolveAttempts;
			++AttemptIndex)
		{
			/**
			 * 有限重试沿用请求 Seed，只把 AttemptIndex 作为 WFC 随机域的稳定子流编号。
			 * 因此同一 Signature 永远得到相同的尝试序列；它不是运行时偷偷换玩家 Seed。
			 */
			FRandomStream WfcRandom = FGenerationCore::MakeRandomStream(
				MasterSeed,
				Request.Signature.AlgorithmVersion,
				ERandomDomain::WfcLayout,
				AttemptIndex);
			WfcSettings.MaxCandidateAttempts = GetWfcAttemptBudget(
				Settings.MaxWfcCandidateAttempts,
				AttemptIndex,
				Settings.MaxWfcSolveAttempts);
			WfcSettings.MaxBacktrackCount = GetWfcAttemptBudget(
				Settings.MaxWfcBacktrackCount,
				AttemptIndex,
				Settings.MaxWfcSolveAttempts);

			AcceptedCandidate = {};
			FZeroEscapeGenerationReport FatalCandidateReport;
			bool bHasFatalCandidateReport = false;
			auto ValidateCollapsedCandidate = [&](const TConstArrayView<uint8> OpeningMasks)
				-> FWfcCollapsedCandidateEvaluation
			{
				FZeroEscapeGeneratedLevelPlan Candidate;
				FZeroEscapeGenerationReport CandidateReport;
				if (!ExportCandidatePlan(
						Request,
						Settings,
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
				if (!ValidateFinalPlan(Request, Settings, State, Candidate, CandidateReport))
				{
					const bool bRecoverableRouteFailure =
						(CandidateReport.Failure == EZeroEscapeGenerationFailure::RequiredRouteTooLong
							|| CandidateReport.Failure == EZeroEscapeGenerationFailure::LongRetraceLimitExceeded)
						&& CandidateReport.ActualValue != INDEX_NONE;
					if (bRecoverableRouteFailure)
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
				Settings.GridSize,
				State.Constraints,
				WfcSettings,
				Variants,
				WfcRandom,
				ValidateCollapsedCandidate,
				AcceptedOpeningMasks,
				AttemptReport);
			AccumulateWfcSearchMetrics(AggregateWfcMetrics, AttemptReport.Metrics);

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

			// 带回溯求解器返回 NoValid 时已经穷尽当前完整搜索树，属于无解证明；
			// 只有分片预算耗尽才需要用同一请求 Seed 的下一个确定性子流继续搜索。
			const bool bRetryableSearchFailure =
				AttemptReport.Failure == EZeroEscapeGenerationFailure::SolverBudgetExhausted;
			if (!bRetryableSearchFailure
				|| AttemptIndex + 1 >= Settings.MaxWfcSolveAttempts)
			{
				OutReport = MoveTemp(AttemptReport);
				OutReport.Metrics = AggregateWfcMetrics;
				if (bRetryableSearchFailure)
				{
					const int32 CompletedAttemptCount = OutReport.Metrics.WfcSolveAttemptCount;
					OutReport.RelatedStableId = INDEX_NONE;
					// Actual/Limit 明确表示“已用尝试数/尝试上限”；候选和回溯使用量保留在 Metrics 与 Message。
					// 这避免把未在各分片之间结转的剩余工作量误报成已经用满的整局候选预算。
					OutReport.ActualValue = CompletedAttemptCount;
					OutReport.LimitValue = Settings.MaxWfcSolveAttempts;
					OutReport.Message = FString::Printf(
						TEXT("已完成 %d/%d 次确定性 WFC 尝试，每次均达到其候选或回溯分片上限；候选尝试累计 %d/%d，回溯累计 %d/%d。"),
						CompletedAttemptCount,
						Settings.MaxWfcSolveAttempts,
						OutReport.Metrics.WfcCandidateAttemptCount,
						Settings.MaxWfcCandidateAttempts,
						OutReport.Metrics.WfcBacktrackCount,
						Settings.MaxWfcBacktrackCount);
				}
				else if (OutReport.Failure == EZeroEscapeGenerationFailure::NoValidWfcSolution)
				{
					// 单棵树的 NoValid 已是完整无解证明；若此前有预算失败尝试，
					// ActualValue 仍应与已经替换进报告的整局矛盾累计值保持同一口径。
					OutReport.ActualValue = OutReport.Metrics.WfcContradictionCount;
					OutReport.LimitValue = 0;
				}
				return false;
			}
		}

		if (!bSolved)
		{
			return Fail(OutReport, EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::SolverInvariantViolation,
				TEXT("WFC 尝试循环结束后既没有成功结果，也没有返回失败。"));
		}
		if (AcceptedOpeningMasks.Num() != State.Constraints.Num() || AcceptedCandidate.Cells.IsEmpty())
		{
			return Fail(OutReport, EZeroEscapeGenerationStage::WfcLayout,
				EZeroEscapeGenerationFailure::SolverInvariantViolation,
				TEXT("WFC 接受候选后没有原子移交完整稠密结果与非空计划。"),
				AcceptedOpeningMasks.Num(), State.Constraints.Num());
		}

		BuildJunctionMetrics(AcceptedCandidate);
		AcceptedCandidate.CanonicalLayoutHash = FGenerationCore::ComputeCanonicalLayoutHash(AcceptedCandidate);
		if (AcceptedCandidate.CanonicalLayoutHash == 0)
		{
			return Fail(OutReport, EZeroEscapeGenerationStage::GlobalValidation,
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
		OutReport.Metrics.PlanningMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		OutPlan = MoveTemp(AcceptedCandidate);
		return true;
	}

	namespace
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

		/** 为规范墙段提供 Hash；共享闭边的两次输入会合并成一个实例。 */
		uint32 GetTypeHash(const FStructureEdgeKey& Edge)
		{
			const uint32 VertexHash = HashCombine(
				::GetTypeHash(Edge.StartVertex.X),
				::GetTypeHash(Edge.StartVertex.Y));
			return HashCombine(VertexHash, ::GetTypeHash(Edge.Axis));
		}

		/** 向输出添加一个 Unit Scale 规范结构件。 */
		void AddStructureInstance(
			const EStructurePieceKind Kind,
			const FVector Location,
			const double YawDegrees,
			TArray<FStructureInstance>& OutInstances)
		{
			FStructureInstance& Instance = OutInstances.AddDefaulted_GetRef();
			Instance.Kind = Kind;
			Instance.CanonicalLocalTransform = FTransform(FRotator(0.0, YawDegrees, 0.0), Location);
		}

		/** 把一个逻辑 Tile 某条闭边拆成两个 300 cm 规范墙段 Key。 */
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

		/** 记录墙段两端的入射方向，供端点/转角/T/Cross 柱子分类使用。 */
		void AddEdgeToWallGraph(const FStructureEdgeKey& Edge, TMap<FIntPoint, uint8>& InOutGraph)
		{
			const FIntPoint End = Edge.StartVertex + (Edge.Axis == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1));
			if (Edge.Axis == 0)
			{
				InOutGraph.FindOrAdd(Edge.StartVertex) |= ZeroEscape::Grid::DirectionBit(1);
				InOutGraph.FindOrAdd(End) |= ZeroEscape::Grid::DirectionBit(3);
			}
			else
			{
				InOutGraph.FindOrAdd(Edge.StartVertex) |= ZeroEscape::Grid::DirectionBit(0);
				InOutGraph.FindOrAdd(End) |= ZeroEscape::Grid::DirectionBit(2);
			}
		}
	}

	bool BuildCanonicalStructureInstances(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const FCanonicalStructureSettings& Settings,
		TArray<FStructureInstance>& OutInstances,
		FString& OutError)
	{
		OutInstances.Reset();
		OutError.Reset();
		if (Settings.LogicalTileSizeCm != 600 || Settings.StructureUnitSizeCm != 300
			|| Settings.LogicalTileSizeCm != Settings.StructureUnitSizeCm * 2
			|| !FMath::IsFinite(Settings.FloorTopZCm)
			|| !FMath::IsFinite(Settings.WallBaseZCm)
			|| !FMath::IsFinite(Settings.CeilingPivotZCm)
			|| !FMath::IsNearlyEqual(Plan.LogicalTileSizeCm, static_cast<double>(Settings.LogicalTileSizeCm)))
		{
			OutError = TEXT("V3.2 结构展开只接受 600 cm Tile -> 2x2 个 300 cm 单元与有限 Z 参数。");
			return false;
		}

		TSet<FStructureEdgeKey> WallEdges;
		for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
		{
			if (Cell.OpeningMask == 0 || !IsInsideGrid(Cell.GridCoordinate, Plan.GridSize))
			{
				OutInstances.Reset();
				OutError = TEXT("结构展开收到了 Empty 或越界 Cell。");
				return false;
			}

			const double TileX = static_cast<double>(Cell.GridCoordinate.X * Settings.LogicalTileSizeCm);
			const double TileY = static_cast<double>(Cell.GridCoordinate.Y * Settings.LogicalTileSizeCm);
			const double Offset = static_cast<double>(Settings.StructureUnitSizeCm) * 0.5;
			for (int32 LocalY = 0; LocalY < 2; ++LocalY)
			{
				for (int32 LocalX = 0; LocalX < 2; ++LocalX)
				{
					const FVector FloorLocation(
						TileX + (LocalX == 0 ? -Offset : Offset),
						TileY + (LocalY == 0 ? -Offset : Offset), Settings.FloorTopZCm);
					AddStructureInstance(EStructurePieceKind::Floor, FloorLocation, 0.0, OutInstances);
					AddStructureInstance(EStructurePieceKind::Ceiling,
						FVector(FloorLocation.X, FloorLocation.Y, Settings.CeilingPivotZCm), 0.0, OutInstances);
				}
			}
			for (uint8 Direction = 0; Direction < ZeroEscape::Grid::DirectionCount; ++Direction)
			{
				if ((Cell.OpeningMask & ZeroEscape::Grid::DirectionBit(Direction)) == 0)
				{
					AddClosedTileEdge(Cell.GridCoordinate, Direction, WallEdges);
				}
			}
		}

		TArray<FStructureEdgeKey> StableEdges = WallEdges.Array();
		StableEdges.Sort([](const FStructureEdgeKey& A, const FStructureEdgeKey& B)
		{
			if (A.StartVertex.Y != B.StartVertex.Y) return A.StartVertex.Y < B.StartVertex.Y;
			if (A.StartVertex.X != B.StartVertex.X) return A.StartVertex.X < B.StartVertex.X;
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
			AddStructureInstance(EStructurePieceKind::Wall, WallLocation, Yaw, OutInstances);
			AddStructureInstance(EStructurePieceKind::WallTopTrim,
				FVector(WallLocation.X, WallLocation.Y, Settings.CeilingPivotZCm), Yaw, OutInstances);
			AddEdgeToWallGraph(Edge, WallGraph);
		}

		TArray<FIntPoint> StableVertices;
		WallGraph.GetKeys(StableVertices);
		StableVertices.Sort(CoordinateLess);
		for (const FIntPoint Vertex : StableVertices)
		{
			const uint8 IncidentMask = WallGraph.FindChecked(Vertex);
			const int32 Degree = FMath::CountBits(static_cast<uint32>(IncidentMask));
			const bool bStraightThrough = Degree == 2 && (IncidentMask == 0x05u || IncidentMask == 0x0Au);
			if (Degree == 1 || Degree >= 3 || (Degree == 2 && !bStraightThrough))
			{
				AddStructureInstance(EStructurePieceKind::Pillar,
					FVector(static_cast<double>(Vertex.X * Settings.StructureUnitSizeCm),
						static_cast<double>(Vertex.Y * Settings.StructureUnitSizeCm), Settings.WallBaseZCm),
					0.0, OutInstances);
			}
		}
		return true;
	}
}
