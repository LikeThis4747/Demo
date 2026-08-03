// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationAssets.cpp
 * 职责：对 Grid/WFC 策划参数，以及结构与顶灯表现绑定执行 fail-closed 校验。
 * 边界：不加载替代资源、不猜测 Pivot、不修正 DataAsset，也不访问 World。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Transform 必须有限且不缩放；结构大小由统一网格决定，禁止在绑定里偷偷拉伸素材。 */
	bool IsFiniteUnitScaleTransform(const FTransform& Transform)
	{
		const FVector Location = Transform.GetLocation();
		const FQuat Rotation = Transform.GetRotation();
		const FVector Scale = Transform.GetScale3D();
		return !Location.ContainsNaN()
			&& !Rotation.ContainsNaN()
			&& !Scale.ContainsNaN()
			&& Rotation.IsNormalized()
			&& Scale.Equals(FVector::OneVector, KINDA_SMALL_NUMBER);
	}

	/** 校验一个已配置绑定；可选空绑定由调用者在进入本函数前跳过。 */
	bool ValidateBinding(
		const FZeroEscapeStructureMeshBinding& Binding,
		const TCHAR* PropertyName,
		FString& OutError)
	{
		if (!IsValid(Binding.StaticMesh))
		{
			OutError = FString::Printf(TEXT("Presentation.%s.StaticMesh 未配置。"), PropertyName);
			return false;
		}

		if (!IsFiniteUnitScaleTransform(Binding.PivotCorrection))
		{
			OutError = FString::Printf(
				TEXT("Presentation.%s.PivotCorrection 必须是有限 Unit Scale Transform。"),
				PropertyName);
			return false;
		}

		FCollisionResponseTemplate ProfileTemplate;
		if (Binding.CollisionProfileName.IsNone()
			|| !UCollisionProfile::Get()->GetProfileTemplate(
				Binding.CollisionProfileName,
				ProfileTemplate))
		{
			OutError = FString::Printf(
				TEXT("Presentation.%s.CollisionProfileName 不是已注册碰撞配置。"),
				PropertyName);
			return false;
		}

		return true;
	}
}

int32 FZeroEscapeWfcShapeWeights::GetWeightForMask(const uint8 OpeningMask) const
{
	if ((OpeningMask & ~ZeroEscape::Grid::AllOpenEdges) != 0)
	{
		return 0;
	}

	const int32 OpenCount = FMath::CountBits(static_cast<uint32>(OpeningMask));
	switch (OpenCount)
	{
	case 0:
		return EmptyWeight;
	case 1:
		return DeadEndWeight;
	case 2:
		// N+S 或 E+W 是直线，其余双开口都是转角。
		return OpeningMask == 0x5 || OpeningMask == 0xA
			? StraightWeight
			: CornerWeight;
	case 3:
		return TJunctionWeight;
	case 4:
		return CrossWeight;
	default:
		return 0;
	}
}

int64 FZeroEscapeWfcShapeWeights::GetTotalNonEmptyVariantWeight() const
{
	/**
	 * 15 个非空 mask 按旋转展开后分别为：4 个单出口、2 个直线、4 个转角、
	 * 4 个 T 字和 1 个十字。使用 int64 计算，避免非法超大配置在校验前发生溢出。
	 */
	return 4LL * DeadEndWeight
		+ 2LL * StraightWeight
		+ 4LL * CornerWeight
		+ 4LL * TJunctionWeight
		+ CrossWeight;
}

bool FZeroEscapeWfcShapeWeights::IsConfigured(FString& OutError) const
{
	OutError.Reset();
	if (EmptyWeight <= 0 || DeadEndWeight <= 0 || StraightWeight <= 0
		|| CornerWeight <= 0 || TJunctionWeight <= 0 || CrossWeight <= 0)
	{
		OutError = TEXT("WfcShapeWeights 的六类权重都必须大于 0。");
		return false;
	}

	const int64 TotalVariantWeight = static_cast<int64>(EmptyWeight)
		+ GetTotalNonEmptyVariantWeight();
	if (TotalVariantWeight > MAX_int32)
	{
		OutError = TEXT("WfcShapeWeights 的 16 个 Variant 总权重超过 int32 加权抽样上限。");
		return false;
	}
	return true;
}

bool UZeroEscapeLevelGenerationProfile::IsConfigured(FString& OutError) const
{
	OutError.Reset();
	auto Fail = [&OutError](FString Message)
	{
		OutError = MoveTemp(Message);
		return false;
	};

	if (ProfileVersion <= 0)
	{
		return Fail(TEXT("GenerationProfile.ProfileVersion 必须大于 0。"));
	}

	const FZeroEscapeSharedRouteConstraints& Route = SharedRouteConstraints;
	const FIntPoint GridSize = Route.GridSize;
	const int64 GridCellCount = static_cast<int64>(GridSize.X) * GridSize.Y;
	if (GridSize.X < ZeroEscape::GenerationLimits::MinGridAxis
		|| GridSize.Y < ZeroEscape::GenerationLimits::MinGridAxis
		|| GridSize.X > ZeroEscape::GenerationLimits::MaxGridAxis
		|| GridSize.Y > ZeroEscape::GenerationLimits::MaxGridAxis
		|| GridCellCount > ZeroEscape::GenerationLimits::MaxGridCells)
	{
		return Fail(TEXT("SharedRouteConstraints.GridSize 超出每层运行时安全范围。"));
	}
	if (!FMath::IsFinite(Route.LogicalTileSizeCm)
		|| !FMath::IsNearlyEqual(Route.LogicalTileSizeCm, 600.0))
	{
		return Fail(TEXT("当前 HydroLab 结构合同要求 LogicalTileSizeCm=600。"));
	}
	if (!FMath::IsFinite(Route.FloorHeightCm) || Route.FloorHeightCm <= 0.0
		|| !FMath::IsFinite(Route.AnchorHeightCm) || Route.AnchorHeightCm < 0.0
		|| Route.MaxConsecutiveStraightTiles <= 0
		|| Route.MaxConsecutiveStraightTiles > FMath::Max(GridSize.X, GridSize.Y))
	{
		return Fail(TEXT("SharedRouteConstraints 包含非法层高、锚点高度或连续直线限制。"));
	}

	const FZeroEscapeSharedGenerationBudget& Budget = SharedBudget;
	if (Budget.MaxWholeLayoutAttempts <= 0
		|| Budget.MaxWholeLayoutAttempts > ZeroEscape::GenerationLimits::MaxWholeLayoutAttempts
		|| Budget.MaxStructureCandidateEvaluations <= 0
		|| Budget.MaxStructureCandidateEvaluations
			> ZeroEscape::GenerationLimits::MaxStructureCandidateEvaluations
		|| Budget.MaxWfcCandidateAttemptsPerFloor <= 0
		|| Budget.MaxWfcCandidateAttemptsPerFloor
			> ZeroEscape::GenerationLimits::MaxWfcCandidateAttemptsPerFloor
		|| Budget.MaxWfcBacktrackCountPerFloor <= 0
		|| Budget.MaxWfcBacktrackCountPerFloor
			> ZeroEscape::GenerationLimits::MaxWfcBacktrackCountPerFloor
		|| Budget.MaxWfcSolveAttemptsPerFloor <= 0
		|| Budget.MaxWfcSolveAttemptsPerFloor
			> ZeroEscape::GenerationLimits::MaxWfcSolveAttemptsPerFloor
		|| Budget.MaxWfcCandidateAttemptsPerFloor < Budget.MaxWfcSolveAttemptsPerFloor
		|| Budget.MaxWfcBacktrackCountPerFloor < Budget.MaxWfcSolveAttemptsPerFloor)
	{
		return Fail(TEXT("SharedBudget 的结构候选或逐层 WFC 预算非法。"));
	}
	if (!FMath::IsFinite(Budget.NavigationBuildTimeoutSeconds)
		|| Budget.NavigationBuildTimeoutSeconds <= 0.0
		|| Budget.NavigationBuildTimeoutSeconds
			> ZeroEscape::GenerationLimits::MaxNavigationBuildTimeoutSeconds
		|| Budget.MaxNavigationValidationPoints < 3
		|| Budget.MaxNavigationValidationPoints
			> ZeroEscape::GenerationLimits::MaxNavigationValidationPoints)
	{
		return Fail(TEXT("SharedBudget 的导航等待或验证点上限非法。"));
	}

	if (StructureDefinitions.IsEmpty())
	{
		return Fail(TEXT("StructureDefinitions 不能为空。"));
	}
	bool SeenStructureKinds[3] = {false, false, false};
	TSet<FName> SeenDefinitionIds;
	for (const FZeroEscapeStructureDefinition& Definition : StructureDefinitions)
	{
		const int32 KindIndex = static_cast<int32>(Definition.Kind);
		if (Definition.DefinitionId.IsNone()
			|| SeenDefinitionIds.Contains(Definition.DefinitionId)
			|| KindIndex < 0 || KindIndex >= UE_ARRAY_COUNT(SeenStructureKinds))
		{
			return Fail(TEXT("StructureDefinitions 的 DefinitionId 缺失或重复，或 Kind 非法。"));
		}
		SeenDefinitionIds.Add(Definition.DefinitionId);
		SeenStructureKinds[KindIndex] = true;

		const int32 ExpectedRequiredFloorCount =
			Definition.Kind == EZeroEscapeStructureKind::ThreeFloorStairwell ? 3
			: Definition.Kind == EZeroEscapeStructureKind::TwoFloorStair ? 2
			: 1;
		if (Definition.RequiredFloorCount != ExpectedRequiredFloorCount)
		{
			return Fail(FString::Printf(
				TEXT("结构 %s 的 RequiredFloorCount 与 Kind 不一致。"),
				*Definition.DefinitionId.ToString()));
		}
		const bool bIsHighCeilingRoom =
			Definition.Kind == EZeroEscapeStructureKind::HighCeilingRoom;
		if (Definition.bAllowClearanceAboveGeneratedTopFloor != bIsHighCeilingRoom)
		{
			return Fail(FString::Printf(
				TEXT("只有高天花板房间 %s 可以裁掉真实顶层以上的 Clearance。"),
				*Definition.DefinitionId.ToString()));
		}
		if (Definition.WalkableCells.IsEmpty())
		{
			return Fail(FString::Printf(
				TEXT("结构 %s 必须至少包含一个 Walkable Cell。"),
				*Definition.DefinitionId.ToString()));
		}

		TSet<FIntVector> WalkableCells;
		TSet<FIntVector> AllReservedCells;
		auto AddCellArray = [
			&Definition,
			&WalkableCells,
			&AllReservedCells,
			&Fail](
			const TArray<FIntVector>& Cells,
			const bool bWalkable,
			const bool bClearance)
		{
			for (const FIntVector Cell : Cells)
			{
				const bool bInsideRequiredFloors =
					Cell.Z >= 0 && Cell.Z < Definition.RequiredFloorCount;
				const bool bAllowedTopClearance = bClearance
					&& Definition.bAllowClearanceAboveGeneratedTopFloor
					&& Cell.Z == Definition.RequiredFloorCount;
				if ((!bInsideRequiredFloors && !bAllowedTopClearance)
					|| AllReservedCells.Contains(Cell))
				{
					return Fail(FString::Printf(
						TEXT("结构 %s 的 Cell 越界、重复或跨占用类别。"),
						*Definition.DefinitionId.ToString()));
				}
				AllReservedCells.Add(Cell);
				if (bWalkable)
				{
					WalkableCells.Add(Cell);
				}
			}
			return true;
		};
		if (!AddCellArray(Definition.WalkableCells, true, false)
			|| !AddCellArray(Definition.SolidCells, false, false)
			|| !AddCellArray(Definition.ClearanceCells, false, true))
		{
			return false;
		}

		TArray<bool> HasConnectionToNextFloor;
		HasConnectionToNextFloor.Init(false, Definition.RequiredFloorCount - 1);
		TMap<FIntVector, TSet<FIntVector>> NormalizedInternalConnections;
		auto IsCoordinateLess = [](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.X != Right.X ? Left.X < Right.X
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.Z < Right.Z;
		};
		for (const FZeroEscapeLocalCellConnection& Connection :
			Definition.InternalConnections)
		{
			if (Connection.FirstCell == Connection.SecondCell
				|| !WalkableCells.Contains(Connection.FirstCell)
				|| !WalkableCells.Contains(Connection.SecondCell))
			{
				return Fail(FString::Printf(
					TEXT("结构 %s 的内部连接必须连接两个不同 Walkable Cell。"),
					*Definition.DefinitionId.ToString()));
			}
			FIntVector NormalizedFirst = Connection.FirstCell;
			FIntVector NormalizedSecond = Connection.SecondCell;
			if (IsCoordinateLess(NormalizedSecond, NormalizedFirst))
			{
				Swap(NormalizedFirst, NormalizedSecond);
			}
			TSet<FIntVector>& Neighbors =
				NormalizedInternalConnections.FindOrAdd(NormalizedFirst);
			if (Neighbors.Contains(NormalizedSecond))
			{
				return Fail(FString::Printf(
					TEXT("结构 %s 的 InternalConnections 不得重复；反向记录视为同一条无向边。"),
					*Definition.DefinitionId.ToString()));
			}
			Neighbors.Add(NormalizedSecond);
			const int32 FloorDelta =
				FMath::Abs(Connection.FirstCell.Z - Connection.SecondCell.Z);
			if (FloorDelta == 0)
			{
				const int32 PlanarDistance =
					FMath::Abs(Connection.FirstCell.X - Connection.SecondCell.X)
					+ FMath::Abs(Connection.FirstCell.Y - Connection.SecondCell.Y);
				if (PlanarDistance != 1)
				{
					return Fail(TEXT("结构同层内部连接必须是四邻域相邻格。"));
				}
			}
			else if (FloorDelta == 1)
			{
				const int32 LowerFloor =
					FMath::Min(Connection.FirstCell.Z, Connection.SecondCell.Z);
				if (!HasConnectionToNextFloor.IsValidIndex(LowerFloor))
				{
					return Fail(TEXT("结构跨层连接落在 RequiredFloorCount 之外。"));
				}
				HasConnectionToNextFloor[LowerFloor] = true;
			}
			else
			{
				return Fail(TEXT("结构内部连接不得跨过中间楼层。"));
			}
		}
		TSet<FIntVector> ReachableWalkableCells;
		TArray<FIntVector> WalkableQueue;
		WalkableQueue.Add(Definition.WalkableCells[0]);
		ReachableWalkableCells.Add(Definition.WalkableCells[0]);
		for (int32 QueueIndex = 0; QueueIndex < WalkableQueue.Num(); ++QueueIndex)
		{
			const FIntVector Current = WalkableQueue[QueueIndex];
			for (const FZeroEscapeLocalCellConnection& Connection :
				Definition.InternalConnections)
			{
				FIntVector Neighbor;
				bool bHasNeighbor = false;
				if (Connection.FirstCell == Current)
				{
					Neighbor = Connection.SecondCell;
					bHasNeighbor = true;
				}
				else if (Connection.SecondCell == Current)
				{
					Neighbor = Connection.FirstCell;
					bHasNeighbor = true;
				}
				if (bHasNeighbor && !ReachableWalkableCells.Contains(Neighbor))
				{
					ReachableWalkableCells.Add(Neighbor);
					WalkableQueue.Add(Neighbor);
				}
			}
		}
		if (ReachableWalkableCells.Num() != WalkableCells.Num())
		{
			return Fail(FString::Printf(
				TEXT("结构 %s 的 WalkableCells 未被 InternalConnections 全部连通。"),
				*Definition.DefinitionId.ToString()));
		}
		if (!bIsHighCeilingRoom)
		{
			for (const bool bConnected : HasConnectionToNextFloor)
			{
				if (!bConnected)
				{
					return Fail(TEXT("楼梯必须逐层包含内部跨层连接。"));
				}
			}
		}

		TSet<FName> LandingIds;
		TSet<int32> LandingFloors;
		for (const FZeroEscapeStructureLandingDefinition& Landing : Definition.Landings)
		{
			if (Landing.LandingId.IsNone()
				|| LandingIds.Contains(Landing.LandingId)
				|| !WalkableCells.Contains(Landing.LocalCoordinate)
				|| LandingFloors.Contains(Landing.LocalCoordinate.Z))
			{
				return Fail(TEXT("结构 Landing 必须 ID 唯一、位于 Walkable Cell 且每层一个。"));
			}
			LandingIds.Add(Landing.LandingId);
			LandingFloors.Add(Landing.LocalCoordinate.Z);
		}
		if ((!bIsHighCeilingRoom
				&& Definition.Landings.Num() != Definition.RequiredFloorCount)
			|| (bIsHighCeilingRoom && !Definition.Landings.IsEmpty()))
		{
			return Fail(TEXT("楼梯每层必须有一个 Landing；高天花板房间不得伪造 Landing。"));
		}

		TSet<FName> OpeningIds;
		TMap<FName, int32> OpeningFloorById;
		for (const FZeroEscapeStructureOpeningDefinition& Opening : Definition.Openings)
		{
			const uint8 Edge = static_cast<uint8>(Opening.OutwardEdge);
			if (Opening.OpeningId.IsNone()
				|| OpeningIds.Contains(Opening.OpeningId)
				|| !WalkableCells.Contains(Opening.LocalWalkableCell)
				|| Edge == 0
				|| (Edge & ~ZeroEscape::Grid::AllOpenEdges) != 0
				|| FMath::CountBits(static_cast<uint32>(Edge)) != 1)
			{
				return Fail(TEXT("结构 Opening 必须 ID 唯一、位于 Walkable Cell 且只有一个朝向。"));
			}
			uint8 DirectionIndex = ZeroEscape::Grid::DirectionCount;
			for (uint8 CandidateDirection = 0;
				CandidateDirection < ZeroEscape::Grid::DirectionCount;
				++CandidateDirection)
			{
				if (Edge == ZeroEscape::Grid::DirectionBit(CandidateDirection))
				{
					DirectionIndex = CandidateDirection;
					break;
				}
			}
			const FIntPoint OutsidePlanar = ZeroEscape::Grid::Step(
				FIntPoint(Opening.LocalWalkableCell.X, Opening.LocalWalkableCell.Y),
				DirectionIndex);
			const FIntVector OutsideCell(
				OutsidePlanar.X,
				OutsidePlanar.Y,
				Opening.LocalWalkableCell.Z);
			if (AllReservedCells.Contains(OutsideCell))
			{
				return Fail(FString::Printf(
					TEXT("结构 %s 的 Opening 指向了本结构内部占用。"),
					*Definition.DefinitionId.ToString()));
			}
			OpeningIds.Add(Opening.OpeningId);
			OpeningFloorById.Add(Opening.OpeningId, Opening.LocalWalkableCell.Z);
		}
		if (Definition.Openings.IsEmpty() || Definition.AllowedOpeningSets.IsEmpty())
		{
			return Fail(TEXT("每种结构必须配置连接口和至少一个合法开放组合。"));
		}

		TSet<FName> OpeningSetIds;
		for (const FZeroEscapeStructureOpeningSetDefinition& Set :
			Definition.AllowedOpeningSets)
		{
			if (Set.SetId.IsNone() || OpeningSetIds.Contains(Set.SetId)
				|| Set.SelectionWeight <= 0 || Set.OpenOpeningIds.IsEmpty())
			{
				return Fail(TEXT("结构 Opening Set 必须 ID 唯一、权重为正且至少开一个口。"));
			}
			OpeningSetIds.Add(Set.SetId);
			TSet<FName> SeenOpeningsInSet;
			TArray<int32> OpenCountByFloor;
			OpenCountByFloor.Init(0, Definition.RequiredFloorCount);
			for (const FName OpeningId : Set.OpenOpeningIds)
			{
				const int32* Floor = OpeningFloorById.Find(OpeningId);
				if (Floor == nullptr || SeenOpeningsInSet.Contains(OpeningId)
					|| !OpenCountByFloor.IsValidIndex(*Floor))
				{
					return Fail(TEXT("Opening Set 引用了缺失、重复或越层的 OpeningId。"));
				}
				SeenOpeningsInSet.Add(OpeningId);
				++OpenCountByFloor[*Floor];
			}
			for (const int32 OpenCount : OpenCountByFloor)
			{
				if (OpenCount > 3 || (!bIsHighCeilingRoom && OpenCount < 1))
				{
					return Fail(TEXT("楼梯每层必须开放 1 至 3 个连接口。"));
				}
			}
		}

		if (bIsHighCeilingRoom)
		{
			if (Definition.WalkableCells.Num() != 2
				|| Definition.ClearanceCells.Num() != 2
				|| Definition.WalkableCells[0].Z != 0
				|| Definition.WalkableCells[1].Z != 0)
			{
				return Fail(TEXT("首版高天花板房间必须是本层 1x2 Walkable 和上一层对应净空。"));
			}
			const int32 RoomCellDistance =
				FMath::Abs(Definition.WalkableCells[0].X - Definition.WalkableCells[1].X)
				+ FMath::Abs(Definition.WalkableCells[0].Y - Definition.WalkableCells[1].Y);
			if (RoomCellDistance != 1)
			{
				return Fail(TEXT("首版高天花板房间的两个 Walkable Cell 必须相邻。"));
			}
			for (const FIntVector Walkable : Definition.WalkableCells)
			{
				if (!Definition.ClearanceCells.Contains(FIntVector(Walkable.X, Walkable.Y, 1)))
				{
					return Fail(TEXT("高天花板房间的 Clearance 必须与 1x2 Walkable 垂直对应。"));
				}
			}
		}
	}

	if (Difficulties.Num() != 3)
	{
		return Fail(TEXT("Difficulties 必须恰好包含 Easy、Normal、Hard 各一条。"));
	}
	bool SeenDifficulties[3] = {false, false, false};
	int32 SharedEmptyWeight = INDEX_NONE;
	int64 SharedNonEmptyWeight = INDEX_NONE;
	bool bAnyThreeFloorStairwellRequested = false;
	bool bAnyHighCeilingRoomRequested = false;
	for (const FZeroEscapeDifficultyDefinition& Definition : Difficulties)
	{
		const int32 DifficultyIndex = static_cast<int32>(Definition.Difficulty);
		if (DifficultyIndex < 0 || DifficultyIndex >= UE_ARRAY_COUNT(SeenDifficulties)
			|| SeenDifficulties[DifficultyIndex])
		{
			return Fail(TEXT("Difficulties 包含非法或重复难度。"));
		}
		SeenDifficulties[DifficultyIndex] = true;

		FString WeightError;
		if (!Definition.WfcShapeWeights.IsConfigured(WeightError))
		{
			return Fail(FString::Printf(
				TEXT("难度 %d 的 WFC 权重非法：%s"), DifficultyIndex, *WeightError));
		}
		const int64 NonEmptyWeight = Definition.WfcShapeWeights.GetTotalNonEmptyVariantWeight();
		if (SharedEmptyWeight == INDEX_NONE)
		{
			SharedEmptyWeight = Definition.WfcShapeWeights.EmptyWeight;
			SharedNonEmptyWeight = NonEmptyWeight;
		}
		else if (SharedEmptyWeight != Definition.WfcShapeWeights.EmptyWeight
			|| SharedNonEmptyWeight != NonEmptyWeight)
		{
			return Fail(TEXT("三档难度必须保持相同 EmptyWeight 与非空 Variant 总权重。"));
		}

		const FZeroEscapeAdditionalTwoFloorStairWeights& ExtraWeights =
			Definition.AdditionalTwoFloorStairsPerFloorPair;
		if (ExtraWeights.ZeroAdditionalWeight < 0
			|| ExtraWeights.OneAdditionalWeight < 0
			|| ExtraWeights.TwoAdditionalWeight < 0
			|| static_cast<int64>(ExtraWeights.ZeroAdditionalWeight)
				+ ExtraWeights.OneAdditionalWeight + ExtraWeights.TwoAdditionalWeight <= 0)
		{
			return Fail(TEXT("额外双层楼梯 0/1/2 数量权重必须非负且总和大于 0。"));
		}
		auto IsRatio = [](const double Value)
		{
			return FMath::IsFinite(Value) && Value >= 0.0 && Value <= 1.0;
		};
		if (Definition.ThreeFloorStairwellChancePercent < 0
			|| Definition.ThreeFloorStairwellChancePercent > 100
			|| !IsRatio(Definition.MinRequiredEndpointSpatialSeparationRatio)
			|| !IsRatio(Definition.MinRequiredRouteCoverageRatio)
			|| !IsRatio(Definition.MinAdditionalStairSeparationRatio)
			|| !FMath::IsFinite(Definition.MinPlayerPursuerRouteDistanceCm)
			|| Definition.MinPlayerPursuerRouteDistanceCm < 0.0)
		{
			return Fail(TEXT("难度的三层楼梯间概率、距离比例或出生路线距离非法。"));
		}
		bAnyThreeFloorStairwellRequested |=
			Definition.ThreeFloorStairwellChancePercent > 0;
		const FZeroEscapeHighCeilingRoomSettings& HighRooms = Definition.HighCeilingRooms;
		if (HighRooms.MinimumTotalCount < 0 || HighRooms.MaxCountPerFloor < 0
			|| !IsRatio(HighRooms.MinimumSeparationRatio))
		{
			return Fail(TEXT("高天花板房间的最低数量、每层上限或间距比例非法。"));
		}
		bAnyHighCeilingRoomRequested |= HighRooms.MinimumTotalCount > 0;

		if (Definition.FloorCountOptions.Num() != 3)
		{
			return Fail(TEXT("每档难度必须恰好配置 2、3、4 层三个选项。"));
		}
		bool SeenFloorCounts[3] = {false, false, false};
		int64 TotalFloorSelectionWeight = 0;
		for (const FZeroEscapeFloorCountOption& Option : Definition.FloorCountOptions)
		{
			const int32 FloorOptionIndex =
				Option.FloorCount - ZeroEscape::GenerationLimits::MinFloorCount;
			if (FloorOptionIndex < 0 || FloorOptionIndex >= UE_ARRAY_COUNT(SeenFloorCounts)
				|| SeenFloorCounts[FloorOptionIndex] || Option.SelectionWeight < 0)
			{
				return Fail(TEXT("FloorCountOptions 必须包含互不重复的 2、3、4 层。"));
			}
			SeenFloorCounts[FloorOptionIndex] = true;
			TotalFloorSelectionWeight += Option.SelectionWeight;

			const int64 BuildingCellCapacity =
				GridCellCount * static_cast<int64>(Option.FloorCount);
			const int64 RequiredOrdinaryMinimum =
				static_cast<int64>(Option.MinOrdinaryWalkableCellCountPerFloor)
					* Option.FloorCount;
			if (Option.MinTotalWalkableCellCount <= 0
				|| Option.MaxTotalWalkableCellCount
					< Option.MinTotalWalkableCellCount
				|| Option.MaxTotalWalkableCellCount > BuildingCellCapacity
				|| Option.MinOrdinaryWalkableCellCountPerFloor <= 0
				|| Option.MinOrdinaryWalkableCellCountPerFloor > GridCellCount
				|| Option.MinTotalWalkableCellCount < RequiredOrdinaryMinimum
				|| Option.MaxPlayerToExitRouteLengthTiles <= 0
				|| Option.MaxAdditionalTwoFloorStairCount < 0
				|| Option.MaxAdditionalTwoFloorStairCount > 2 * (Option.FloorCount - 1))
			{
				return Fail(TEXT("FloorCountOption 的整栋总量、每层普通内容或额外楼梯上限非法。"));
			}

			const int32 RequiredTwoFloorStairs = Option.FloorCount - 1;
			const int32 MaxNavigationPoints = 3
				+ 2 * (RequiredTwoFloorStairs + Option.MaxAdditionalTwoFloorStairCount)
				+ (Option.FloorCount >= 3
					&& Definition.ThreeFloorStairwellChancePercent > 0 ? 3 : 0);
			if (MaxNavigationPoints > Budget.MaxNavigationValidationPoints)
			{
				return Fail(TEXT("楼梯上限产生的导航验证点超过 SharedBudget。"));
			}

			if (HighRooms.MinimumTotalCount > HighRooms.MaxCountPerFloor * Option.FloorCount
				|| Option.HighCeilingRoomTargetCounts.IsEmpty())
			{
				return Fail(TEXT("高天花板房间整栋最低数量或目标权重无法适配该楼层数。"));
			}
			TSet<int32> SeenHighRoomCounts;
			int64 TotalHighRoomWeight = 0;
			for (const FZeroEscapeWeightedCount& CountOption :
				Option.HighCeilingRoomTargetCounts)
			{
				if (CountOption.Count < HighRooms.MinimumTotalCount
					|| CountOption.Count > HighRooms.MaxCountPerFloor * Option.FloorCount
					|| CountOption.Weight < 0
					|| SeenHighRoomCounts.Contains(CountOption.Count))
				{
					return Fail(TEXT("高天花板房间目标数量必须唯一、可容纳且权重非负。"));
				}
				SeenHighRoomCounts.Add(CountOption.Count);
				TotalHighRoomWeight += CountOption.Weight;
				bAnyHighCeilingRoomRequested |=
					CountOption.Count > 0 && CountOption.Weight > 0;
			}
			if (TotalHighRoomWeight <= 0)
			{
				return Fail(TEXT("高天花板房间目标数量权重总和必须大于 0。"));
			}
		}
		if (!SeenFloorCounts[0] || !SeenFloorCounts[1] || !SeenFloorCounts[2]
			|| TotalFloorSelectionWeight <= 0)
		{
			return Fail(TEXT("每档难度的 2、3、4 层权重必须齐全且总和大于 0。"));
		}
	}

	if (!SeenDifficulties[0] || !SeenDifficulties[1] || !SeenDifficulties[2])
	{
		return Fail(TEXT("Difficulties 缺少 Easy、Normal 或 Hard。"));
	}
	if (!SeenStructureKinds[static_cast<int32>(EZeroEscapeStructureKind::TwoFloorStair)]
		|| (bAnyThreeFloorStairwellRequested
			&& !SeenStructureKinds[
				static_cast<int32>(EZeroEscapeStructureKind::ThreeFloorStairwell)])
		|| (bAnyHighCeilingRoomRequested
			&& !SeenStructureKinds[
				static_cast<int32>(EZeroEscapeStructureKind::HighCeilingRoom)]))
	{
		return Fail(TEXT("StructureDefinitions 缺少当前难度配置实际需要的结构 Kind。"));
	}
	return true;
}

bool UZeroEscapePresentationProfile::IsConfigured(
	const double LogicalTileSizeCm,
	FString& OutError) const
{
	OutError.Reset();
	if (PresentationVersion <= 0
		|| !FMath::IsFinite(StructureUnitSizeCm)
		|| StructureUnitSizeCm <= 0.0
		|| !FMath::IsNearlyEqual(LogicalTileSizeCm, StructureUnitSizeCm * 2.0)
		|| !FMath::IsFinite(FloorTopZCm)
		|| !FMath::IsFinite(WallBaseZCm)
		|| !FMath::IsFinite(CeilingPivotZCm))
	{
		OutError = TEXT("Presentation 的版本、结构高度或 2:1 尺寸关系非法。");
		return false;
	}

	if (!ValidateBinding(Floor, TEXT("Floor"), OutError)
		|| !ValidateBinding(Ceiling, TEXT("Ceiling"), OutError)
		|| !ValidateBinding(Wall, TEXT("Wall"), OutError))
	{
		return false;
	}

	if (IsValid(WallTopTrim.StaticMesh)
		&& !ValidateBinding(WallTopTrim, TEXT("WallTopTrim"), OutError))
	{
		return false;
	}

	if (IsValid(Pillar.StaticMesh)
		&& !ValidateBinding(Pillar, TEXT("Pillar"), OutError))
	{
		return false;
	}

	// 关闭顶灯时允许灯类为空，确保同一套结构表现可以无代码地回退到“不生成灯”。
	if (bSpawnCeilingLights)
	{
		const UClass* LightActorClass = CeilingLightActorClass.Get();
		if (!IsValid(LightActorClass)
			|| LightActorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutError = TEXT("Presentation.CeilingLightActorClass 必须是有效且非抽象的 Actor 类。");
			return false;
		}

		if (!IsFiniteUnitScaleTransform(CeilingLightCellTransform))
		{
			OutError = TEXT(
				"Presentation.CeilingLightCellTransform 必须是有限 Unit Scale Transform。");
			return false;
		}
	}

	return true;
}
