// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeMultiFloorLayoutTests.cpp
 * 职责：验证完整结构先放置、逐层二维 WFC、整栋一次 BFS、确定性和共享预算。
 * 边界：只构造纯值定义；不加载项目资产，不创建 World，不把导航当成逻辑连通证明。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <initializer_list>

#include "Algo/Reverse.h"
#include "Containers/Queue.h"
#include "Misc/AutomationTest.h"

#include "PCG/Layout/ZeroEscapeMultiFloorLayoutPlanner.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace MultiFloorLayoutTestsPrivate
	{
		FZeroEscapeStructureOpeningSetDefinition MakeOpeningSet(
			const FName SetId,
			std::initializer_list<const TCHAR*> OpeningIds)
		{
			FZeroEscapeStructureOpeningSetDefinition Set;
			Set.SetId = SetId;
			Set.SelectionWeight = 1;
			for (const TCHAR* OpeningId : OpeningIds)
			{
				Set.OpenOpeningIds.Add(FName(OpeningId));
			}
			return Set;
		}

		FZeroEscapeStructureOpeningDefinition MakeOpening(
			const FName OpeningId,
			const FIntVector LocalWalkableCell,
			const EZeroEscapeOpenEdge Edge = EZeroEscapeOpenEdge::North)
		{
			FZeroEscapeStructureOpeningDefinition Opening;
			Opening.OpeningId = OpeningId;
			Opening.LocalWalkableCell = LocalWalkableCell;
			Opening.OutwardEdge = Edge;
			return Opening;
		}

		FZeroEscapeStructureLandingDefinition MakeLanding(
			const FName LandingId,
			const FIntVector LocalCoordinate)
		{
			FZeroEscapeStructureLandingDefinition Landing;
			Landing.LandingId = LandingId;
			Landing.LocalCoordinate = LocalCoordinate;
			return Landing;
		}

		FZeroEscapeLocalCellConnection MakeConnection(
			const FIntVector First,
			const FIntVector Second)
		{
			FZeroEscapeLocalCellConnection Connection;
			Connection.FirstCell = First;
			Connection.SecondCell = Second;
			return Connection;
		}

		FZeroEscapeStructureDefinition MakeTwoFloorStairDefinition()
		{
			FZeroEscapeStructureDefinition Definition;
			Definition.DefinitionId = TEXT("Stair_TwoFloor_A");
			Definition.Kind = EZeroEscapeStructureKind::TwoFloorStair;
			Definition.RequiredFloorCount = 2;
			Definition.WalkableCells = {
				FIntVector(0, 0, 0), FIntVector(1, 0, 0),
				FIntVector(0, 0, 1), FIntVector(1, 0, 1) };
			Definition.InternalConnections = {
				MakeConnection(FIntVector(0, 0, 0), FIntVector(1, 0, 0)),
				MakeConnection(FIntVector(1, 0, 0), FIntVector(1, 0, 1)),
				MakeConnection(FIntVector(0, 0, 1), FIntVector(1, 0, 1)) };
			Definition.Openings = {
				MakeOpening(TEXT("LowerDoor"), FIntVector(0, 0, 0)),
				MakeOpening(TEXT("UpperDoor"), FIntVector(0, 0, 1)) };
			Definition.Landings = {
				MakeLanding(TEXT("LowerLanding"), FIntVector(1, 0, 0)),
				MakeLanding(TEXT("UpperLanding"), FIntVector(1, 0, 1)) };
			Definition.AllowedOpeningSets = {
				MakeOpeningSet(
					TEXT("BothDoors"),
					{ TEXT("LowerDoor"), TEXT("UpperDoor") }) };
			return Definition;
		}

		FZeroEscapeStructureDefinition MakeThreeFloorStairwellDefinition()
		{
			FZeroEscapeStructureDefinition Definition;
			Definition.DefinitionId = TEXT("Stairwell_ThreeFloor_A");
			Definition.Kind = EZeroEscapeStructureKind::ThreeFloorStairwell;
			Definition.RequiredFloorCount = 3;
			Definition.WalkableCells = {
				FIntVector(0, 0, 0),
				FIntVector(0, 0, 1),
				FIntVector(0, 0, 2) };
			Definition.InternalConnections = {
				MakeConnection(FIntVector(0, 0, 0), FIntVector(0, 0, 1)),
				MakeConnection(FIntVector(0, 0, 1), FIntVector(0, 0, 2)) };
			Definition.Openings = {
				MakeOpening(TEXT("Floor0Door"), FIntVector(0, 0, 0)),
				MakeOpening(TEXT("Floor1Door"), FIntVector(0, 0, 1)),
				MakeOpening(TEXT("Floor2Door"), FIntVector(0, 0, 2)) };
			Definition.Landings = {
				MakeLanding(TEXT("Floor0Landing"), FIntVector(0, 0, 0)),
				MakeLanding(TEXT("Floor1Landing"), FIntVector(0, 0, 1)),
				MakeLanding(TEXT("Floor2Landing"), FIntVector(0, 0, 2)) };
			Definition.AllowedOpeningSets = {
				MakeOpeningSet(
					TEXT("AllDoors"),
					{ TEXT("Floor0Door"), TEXT("Floor1Door"), TEXT("Floor2Door") }) };
			return Definition;
		}

		FZeroEscapeStructureDefinition MakeHighCeilingRoomDefinition()
		{
			FZeroEscapeStructureDefinition Definition;
			Definition.DefinitionId = TEXT("HighRoom_A");
			Definition.Kind = EZeroEscapeStructureKind::HighCeilingRoom;
			Definition.RequiredFloorCount = 1;
			Definition.bAllowClearanceAboveGeneratedTopFloor = true;
			Definition.WalkableCells = {
				FIntVector(0, 0, 0), FIntVector(1, 0, 0) };
			Definition.ClearanceCells = {
				FIntVector(0, 0, 1), FIntVector(1, 0, 1) };
			Definition.InternalConnections = {
				MakeConnection(FIntVector(0, 0, 0), FIntVector(1, 0, 0)) };
			Definition.Openings = {
				MakeOpening(TEXT("RoomDoor"), FIntVector(0, 0, 0)) };
			Definition.AllowedOpeningSets = {
				MakeOpeningSet(TEXT("DoorOpen"), { TEXT("RoomDoor") }) };
			return Definition;
		}

		FResolvedGenerationInput MakeResolvedInput(const int32 Seed = 240813)
		{
			FResolvedGenerationInput Input;
			Input.Signature.Seed = Seed;
			Input.Signature.Difficulty = EZeroEscapeDifficulty::Normal;
			Input.Signature.AlgorithmVersion = GAlgorithmVersion;
			Input.Signature.GenerationProfileVersion = 6;
			Input.Signature.PresentationVersion = 3;
			Input.SharedRules.GridSize = FIntPoint(14, 10);
			Input.SharedRules.LogicalTileSizeCm = 600.0;
			Input.SharedRules.FloorHeightCm = 450.0;
			Input.SharedRules.MaxConsecutiveStraightTiles = 8;
			Input.SharedRules.AnchorHeightCm = 100.0;
			Input.Budget.MaxWholeLayoutAttempts = 4;
			Input.Budget.MaxStructureCandidateEvaluations = 250000;
			Input.Budget.MaxWfcCandidateAttemptsPerFloor = 100000;
			Input.Budget.MaxWfcBacktrackCountPerFloor = 25000;
			Input.Budget.MaxWfcSolveAttemptsPerFloor = 10;
			Input.Difficulty.Difficulty = EZeroEscapeDifficulty::Normal;
			Input.Difficulty.WfcShapeWeights = FZeroEscapeWfcShapeWeights();
			Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.ZeroAdditionalWeight = 0;
			Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.OneAdditionalWeight = 0;
			Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.TwoAdditionalWeight = 1;
			Input.Difficulty.ThreeFloorStairwellChancePercent = 100;
			Input.Difficulty.MinRequiredEndpointSpatialSeparationRatio = 0.45;
			Input.Difficulty.MinRequiredRouteCoverageRatio = 0.10;
			Input.Difficulty.MinAdditionalStairSeparationRatio = 1.0;
			Input.Difficulty.MinPlayerPursuerRouteDistanceCm = 1200.0;
			Input.Difficulty.HighCeilingRooms.MinimumTotalCount = 0;
			Input.Difficulty.HighCeilingRooms.MaxCountPerFloor = 2;
			Input.Difficulty.HighCeilingRooms.MinimumSeparationRatio = 0.0;

			FZeroEscapeFloorCountOption FloorOption;
			FloorOption.FloorCount = 3;
			FloorOption.SelectionWeight = 1;
			FloorOption.MinTotalWalkableCellCount = 84;
			FloorOption.MaxTotalWalkableCellCount = 192;
			FloorOption.MinOrdinaryWalkableCellCountPerFloor = 22;
			FloorOption.MaxPlayerToExitRouteLengthTiles = 220;
			FloorOption.MaxAdditionalTwoFloorStairCount = 4;
			FZeroEscapeWeightedCount NoHighRooms;
			NoHighRooms.Count = 0;
			NoHighRooms.Weight = 1;
			FloorOption.HighCeilingRoomTargetCounts = { NoHighRooms };
			Input.Difficulty.FloorCountOptions = { FloorOption };
			Input.StructureDefinitions = {
				MakeHighCeilingRoomDefinition(),
				MakeThreeFloorStairwellDefinition(),
				MakeTwoFloorStairDefinition() };
			Input.WfcShapeWeights = Input.Difficulty.WfcShapeWeights;
			return Input;
		}

		const FZeroEscapeGeneratedOrdinaryCell* FindOrdinaryCell(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntVector Coordinate)
		{
			return Plan.OrdinaryCells.FindByPredicate(
				[Coordinate](const FZeroEscapeGeneratedOrdinaryCell& Cell)
				{
					return Cell.Coordinate == Coordinate;
				});
		}

		bool FindDirection(
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
				const FIntPoint Neighbor = Grid::Step(
					FIntPoint(From.X, From.Y), Direction);
				if (FIntVector(Neighbor.X, Neighbor.Y, From.Z) == To)
				{
					OutDirection = Direction;
					return true;
				}
			}
			return false;
		}

		void AddUndirectedNeighbor(
			TMap<FIntVector, TArray<FIntVector>>& InOutNeighbors,
			const FIntVector First,
			const FIntVector Second)
		{
			InOutNeighbors.FindOrAdd(First).AddUnique(Second);
			InOutNeighbors.FindOrAdd(Second).AddUnique(First);
		}

		bool ComputePlanRouteDistance(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntVector Start,
			const FIntVector Goal,
			int32& OutDistance)
		{
			OutDistance = INDEX_NONE;
			TSet<FIntVector> Walkable;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				Walkable.Add(Cell.Coordinate);
			}
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				for (const FIntVector Cell : Structure.WalkableCells)
				{
					Walkable.Add(Cell);
				}
			}
			if (!Walkable.Contains(Start) || !Walkable.Contains(Goal))
			{
				return false;
			}

			TMap<FIntVector, TArray<FIntVector>> Neighbors;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if ((Cell.OpeningMask & Grid::DirectionBit(Direction)) == 0)
					{
						continue;
					}
					const FIntPoint Neighbor2D = Grid::Step(
						FIntPoint(Cell.Coordinate.X, Cell.Coordinate.Y),
						Direction);
					const FIntVector Neighbor(
						Neighbor2D.X, Neighbor2D.Y, Cell.Coordinate.Z);
					if (!Walkable.Contains(Neighbor))
					{
						return false;
					}
					AddUndirectedNeighbor(Neighbors, Cell.Coordinate, Neighbor);
				}
			}
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				for (const FZeroEscapeGeneratedCellConnection& Connection :
					Structure.InternalConnections)
				{
					AddUndirectedNeighbor(
						Neighbors,
						Connection.FirstCoordinate,
						Connection.SecondCoordinate);
				}
				for (const FZeroEscapeGeneratedStructureOpening& Opening :
					Structure.Openings)
				{
					AddUndirectedNeighbor(
						Neighbors,
						Opening.StructureCoordinate,
						Opening.ConnectedOrdinaryCoordinate);
				}
			}

			TMap<FIntVector, int32> Distance;
			TQueue<FIntVector> Queue;
			Distance.Add(Start, 0);
			Queue.Enqueue(Start);
			FIntVector Current;
			while (Queue.Dequeue(Current))
			{
				if (Current == Goal)
				{
					OutDistance = Distance[Current];
					return true;
				}
				const TArray<FIntVector>* CurrentNeighbors = Neighbors.Find(Current);
				if (CurrentNeighbors == nullptr)
				{
					continue;
				}
				for (const FIntVector Neighbor : *CurrentNeighbors)
				{
					if (!Distance.Contains(Neighbor))
					{
						Distance.Add(Neighbor, Distance[Current] + 1);
						Queue.Enqueue(Neighbor);
					}
				}
			}
			return false;
		}

		bool HasOrdinaryOpeningTowardStructure(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FZeroEscapeGeneratedStructureOpening& Opening)
		{
			const FZeroEscapeGeneratedOrdinaryCell* Ordinary = FindOrdinaryCell(
				Plan, Opening.ConnectedOrdinaryCoordinate);
			uint8 Direction = 0;
			return Ordinary != nullptr
				&& FindDirection(
					Opening.ConnectedOrdinaryCoordinate,
					Opening.StructureCoordinate,
					Direction)
				&& (Ordinary->OpeningMask & Grid::DirectionBit(Direction)) != 0;
		}

		int32 CountStructures(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const EZeroEscapeStructureKind Kind)
		{
			int32 Count = 0;
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				Count += Structure.Kind == Kind ? 1 : 0;
			}
			return Count;
		}

		void ShuffleSemanticArrays(FResolvedGenerationInput& InOutInput)
		{
			Algo::Reverse(InOutInput.StructureDefinitions);
			for (FZeroEscapeStructureDefinition& Definition :
				InOutInput.StructureDefinitions)
			{
				Algo::Reverse(Definition.WalkableCells);
				Algo::Reverse(Definition.SolidCells);
				Algo::Reverse(Definition.ClearanceCells);
				Algo::Reverse(Definition.InternalConnections);
				for (FZeroEscapeLocalCellConnection& Connection :
					Definition.InternalConnections)
				{
					Swap(Connection.FirstCell, Connection.SecondCell);
				}
				Algo::Reverse(Definition.Openings);
				Algo::Reverse(Definition.Landings);
				Algo::Reverse(Definition.AllowedOpeningSets);
				for (FZeroEscapeStructureOpeningSetDefinition& Set :
					Definition.AllowedOpeningSets)
				{
					Algo::Reverse(Set.OpenOpeningIds);
				}
			}
		}

		FIntVector RotateClockwiseForExpected(
			const FIntVector Local,
			const uint8 QuarterTurns)
		{
			switch (QuarterTurns & 3u)
			{
			case 0: return Local;
			case 1: return FIntVector(Local.Y, -Local.X, Local.Z);
			case 2: return FIntVector(-Local.X, -Local.Y, Local.Z);
			default: return FIntVector(-Local.Y, Local.X, Local.Z);
			}
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeStructureClockwiseRotationTest,
		"Demo.PCG.Unit.MultiFloor.StructureClockwiseRotation",
		EAutomationTestFlags_ApplicationContextMask
			| EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeStructureClockwiseRotationTest::RunTest(
		const FString& Parameters)
	{
		using namespace MultiFloorLayoutTestsPrivate;
		(void)Parameters;
		FZeroEscapeStructureDefinition Definition;
		Definition.DefinitionId = TEXT("RotationFixture");
		Definition.Kind = EZeroEscapeStructureKind::HighCeilingRoom;
		Definition.RequiredFloorCount = 1;
		Definition.WalkableCells = { FIntVector(1, 2, 0) };
		Definition.SolidCells = { FIntVector(2, 2, 0) };
		Definition.ClearanceCells = { FIntVector(1, 2, 1) };
		Definition.Openings = {
			MakeOpening(TEXT("NorthDoor"), FIntVector(1, 2, 0)) };
		Definition.Landings = {
			MakeLanding(TEXT("Landing"), FIntVector(1, 2, 0)) };
		Definition.AllowedOpeningSets = {
			MakeOpeningSet(TEXT("DoorOpen"), { TEXT("NorthDoor") }) };
		const FIntVector Base(10, 10, 1);

		for (uint8 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
		{
			FZeroEscapeGeneratedStructure Structure;
			FString Error;
			if (!TestTrue(
					FString::Printf(
						TEXT("q=%d 的结构定义必须可解析"), QuarterTurns),
					FMultiFloorLayoutPlanner::ResolveStructureForTesting(
						Definition,
						FIntPoint(24, 24),
						4,
						Base,
						QuarterTurns,
						TEXT("DoorOpen"),
						Structure,
						Error)))
			{
				AddError(Error);
				continue;
			}

			const FIntVector ExpectedWalkable = Base
				+ RotateClockwiseForExpected(FIntVector(1, 2, 0), QuarterTurns);
			const FIntVector ExpectedSolid = Base
				+ RotateClockwiseForExpected(FIntVector(2, 2, 0), QuarterTurns);
			const FIntVector ExpectedClearance = Base
				+ RotateClockwiseForExpected(FIntVector(1, 2, 1), QuarterTurns);
			const FIntVector RotatedNorth = RotateClockwiseForExpected(
				FIntVector(0, 1, 0), QuarterTurns);
			const FIntVector ExpectedConnected = ExpectedWalkable + RotatedNorth;
			TestTrue(TEXT("Walkable 必须按顺时针四分之一圈旋转"),
				Structure.WalkableCells.Contains(ExpectedWalkable));
			TestTrue(TEXT("Solid 必须使用相同顺时针旋转"),
				Structure.SolidCells.Contains(ExpectedSolid));
			TestTrue(TEXT("Clearance 必须使用相同顺时针旋转"),
				Structure.ClearanceCells.Contains(ExpectedClearance));
			TestTrue(TEXT("Landing 必须与结构单元格使用同一旋转"),
				Structure.Landings.Num() == 1
					&& Structure.Landings[0].Coordinate == ExpectedWalkable);
			TestTrue(TEXT("Opening 的结构格与相邻普通格必须一起顺时针旋转"),
				Structure.Openings.Num() == 1
					&& Structure.Openings[0].StructureCoordinate == ExpectedWalkable
					&& Structure.Openings[0].ConnectedOrdinaryCoordinate
						== ExpectedConnected);

			const FVector BuilderRotated = FRotator(
				0.0, -90.0 * QuarterTurns, 0.0).RotateVector(FVector(1.0, 2.0, 0.0));
			TestTrue(TEXT("逻辑旋转必须与 Builder 的 -90 度约定一致"),
				FMath::RoundToInt(BuilderRotated.X)
					== ExpectedWalkable.X - Base.X
				&& FMath::RoundToInt(BuilderRotated.Y)
					== ExpectedWalkable.Y - Base.Y);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeMultiFloorDeterminismAndRouteContractTest,
		"Demo.PCG.Unit.MultiFloor.DeterminismRequiredStairsAndSpawns",
		EAutomationTestFlags_ApplicationContextMask
			| EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeMultiFloorDeterminismAndRouteContractTest::RunTest(
		const FString& Parameters)
	{
		using namespace MultiFloorLayoutTestsPrivate;
		(void)Parameters;
		const FResolvedGenerationInput Input = MakeResolvedInput();
		FZeroEscapeGeneratedLevelPlan FirstPlan;
		FZeroEscapeGenerationReport FirstReport;
		if (!TestTrue(
				TEXT("三层纯值夹具必须生成完整 Plan"),
				FMultiFloorLayoutPlanner::Solve(Input, FirstPlan, FirstReport)))
		{
			AddError(FirstReport.Message);
			return true;
		}

		TestTrue(TEXT("玩家在一楼、Exit 在顶楼"),
			FirstPlan.FloorCount == 3
				&& FirstPlan.PlayerSpawnCoordinate.Z == 0
				&& FirstPlan.ExitCoordinate.Z == 2);
		TestEqual(TEXT("三层必须有两座映射到相邻楼层对的必需双层楼梯"),
			FirstPlan.RequiredTwoFloorStairStableIdByLowerFloor.Num(), 2);
		TSet<int32> RequiredIds;
		for (int32 LowerFloor = 0; LowerFloor < 2; ++LowerFloor)
		{
			const int32 StableId =
				FirstPlan.RequiredTwoFloorStairStableIdByLowerFloor[LowerFloor];
			RequiredIds.Add(StableId);
			TestTrue(TEXT("必需映射必须指向对应低楼层的双层楼梯"),
				FirstPlan.Structures.IsValidIndex(StableId)
					&& FirstPlan.Structures[StableId].Kind
						== EZeroEscapeStructureKind::TwoFloorStair
					&& FirstPlan.Structures[StableId].BaseCoordinate.Z == LowerFloor);
		}
		TestEqual(TEXT("每对相邻楼层必须使用不同的必需双层楼梯"),
			RequiredIds.Num(), 2);
		TestEqual(TEXT("分离比例 1.0 时额外双层楼梯目标必须可安全跳过"),
			CountStructures(FirstPlan, EZeroEscapeStructureKind::TwoFloorStair), 2);
		TestTrue(TEXT("贯通三层的楼梯间永远最多一座，且不能替代必需楼梯"),
			CountStructures(
				FirstPlan, EZeroEscapeStructureKind::ThreeFloorStairwell) <= 1
				&& FirstPlan.RequiredTwoFloorStairStableIdByLowerFloor.Num() == 2);
		TestEqual(TEXT("目标为零时允许每层都没有高天花板房间"),
			CountStructures(FirstPlan, EZeroEscapeStructureKind::HighCeilingRoom), 0);

		const FZeroEscapeFloorCountOption& FloorOption =
			Input.Difficulty.FloorCountOptions[0];
		int32 SummedWalkable = 0;
		for (const FZeroEscapeGeneratedFloorSummary& Floor : FirstPlan.Floors)
		{
			SummedWalkable += Floor.TotalWalkableCellCount;
			TestTrue(TEXT("每层必须保留配置的普通 WFC 内容下限"),
				Floor.OrdinaryWalkableCellCount
					>= FloorOption.MinOrdinaryWalkableCellCountPerFloor);
			TestTrue(TEXT("每层必经端点的空间与路线覆盖比例必须达标"),
				Floor.SpatialSeparationRatio + UE_DOUBLE_SMALL_NUMBER
					>= Input.Difficulty.MinRequiredEndpointSpatialSeparationRatio
				&& Floor.RouteCoverageRatio + UE_DOUBLE_SMALL_NUMBER
					>= Input.Difficulty.MinRequiredRouteCoverageRatio);
		}
		TestTrue(TEXT("整栋总可走量必须直接落在 DataAsset 的整栋范围内"),
			SummedWalkable >= FloorOption.MinTotalWalkableCellCount
				&& SummedWalkable <= FloorOption.MaxTotalWalkableCellCount);

		TestTrue(TEXT("追猎者出生点必须是一楼普通可走格且不同于玩家"),
			FirstPlan.PursuerSpawnCoordinate.Z == 0
				&& FirstPlan.PursuerSpawnCoordinate
					!= FirstPlan.PlayerSpawnCoordinate
				&& FindOrdinaryCell(
					FirstPlan, FirstPlan.PursuerSpawnCoordinate) != nullptr);
		int32 PlayerPursuerDistance = INDEX_NONE;
		TestTrue(TEXT("独立整栋图必须能重算玩家到追猎者路线"),
			ComputePlanRouteDistance(
				FirstPlan,
				FirstPlan.PlayerSpawnCoordinate,
				FirstPlan.PursuerSpawnCoordinate,
				PlayerPursuerDistance));
		TestTrue(TEXT("追猎者出生点必须满足实际路线距离而非二维直线距离"),
			PlayerPursuerDistance * FirstPlan.LogicalTileSizeCm
					+ UE_DOUBLE_SMALL_NUMBER
				>= Input.Difficulty.MinPlayerPursuerRouteDistanceCm);
		int32 PlayerExitDistance = INDEX_NONE;
		TestTrue(TEXT("独立整栋图必须能重算玩家到 Exit 路线"),
			ComputePlanRouteDistance(
				FirstPlan,
				FirstPlan.PlayerSpawnCoordinate,
				FirstPlan.ExitCoordinate,
				PlayerExitDistance));
		TestEqual(TEXT("Plan 保存的玩家到 Exit 最短路必须等于独立 BFS"),
			FirstPlan.PlayerToExitRouteLengthTiles, PlayerExitDistance);

		for (const FZeroEscapeGeneratedStructure& Structure : FirstPlan.Structures)
		{
			for (const FZeroEscapeGeneratedStructureOpening& Opening :
				Structure.Openings)
			{
				TestTrue(TEXT("每个结构开口的普通格侧必须保留镜像必开边"),
					HasOrdinaryOpeningTowardStructure(FirstPlan, Opening));
			}
			for (const FIntVector Solid : Structure.SolidCells)
			{
				TestNull(TEXT("结构 Solid 格不得导出为普通可走格"),
					FindOrdinaryCell(FirstPlan, Solid));
			}
			for (const FIntVector Clearance : Structure.ClearanceCells)
			{
				TestNull(TEXT("结构 Clearance 格不得导出为普通可走格"),
					FindOrdinaryCell(FirstPlan, Clearance));
			}
		}

		FZeroEscapeGeneratedLevelPlan ReplayPlan;
		FZeroEscapeGenerationReport ReplayReport;
		TestTrue(TEXT("同一输入的第二次完整求解必须成功"),
			FMultiFloorLayoutPlanner::Solve(Input, ReplayPlan, ReplayReport));
		TestEqual(TEXT("同 Seed、难度和逻辑 Profile 必须复现规范 Hash"),
			ReplayPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
		TestEqual(TEXT("同 Seed 必须复现追猎者出生点"),
			ReplayPlan.PursuerSpawnCoordinate, FirstPlan.PursuerSpawnCoordinate);

		FResolvedGenerationInput ShuffledInput = Input;
		ShuffleSemanticArrays(ShuffledInput);
		FZeroEscapeGeneratedLevelPlan ShuffledPlan;
		FZeroEscapeGenerationReport ShuffledReport;
		TestTrue(TEXT("同语义 DataAsset 数组乱序后的求解必须成功"),
			FMultiFloorLayoutPlanner::Solve(
				ShuffledInput, ShuffledPlan, ShuffledReport));
		TestEqual(TEXT("同语义数组顺序不得改变完整 Plan 的规范 Hash"),
			ShuffledPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeHighCeilingWholeBuildingContractTest,
		"Demo.PCG.Unit.MultiFloor.HighCeilingWholeBuildingContract",
		EAutomationTestFlags_ApplicationContextMask
			| EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeHighCeilingWholeBuildingContractTest::RunTest(
		const FString& Parameters)
	{
		using namespace MultiFloorLayoutTestsPrivate;
		(void)Parameters;
		FResolvedGenerationInput Input = MakeResolvedInput(90210);
		Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.ZeroAdditionalWeight = 1;
		Input.Difficulty.AdditionalTwoFloorStairsPerFloorPair.TwoAdditionalWeight = 0;
		Input.Difficulty.ThreeFloorStairwellChancePercent = 0;
		Input.Difficulty.HighCeilingRooms.MinimumTotalCount = 3;
		Input.Difficulty.HighCeilingRooms.MaxCountPerFloor = 1;
		Input.Difficulty.HighCeilingRooms.MinimumSeparationRatio = 0.0;
		FZeroEscapeWeightedCount ThreeRooms;
		ThreeRooms.Count = 3;
		ThreeRooms.Weight = 1;
		Input.Difficulty.FloorCountOptions[0].HighCeilingRoomTargetCounts = {
			ThreeRooms };

		FZeroEscapeGeneratedLevelPlan Plan;
		FZeroEscapeGenerationReport Report;
		if (!TestTrue(TEXT("每层上限一间、整栋最低三间的三层夹具必须成功"),
				FMultiFloorLayoutPlanner::Solve(Input, Plan, Report)))
		{
			AddError(Report.Message);
			return true;
		}

		TArray<int32> CountByFloor;
		CountByFloor.Init(0, Plan.FloorCount);
		int32 HighRoomCount = 0;
		bool bFoundTopFloorRoom = false;
		for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
		{
			if (Structure.Kind != EZeroEscapeStructureKind::HighCeilingRoom)
			{
				continue;
			}
			++HighRoomCount;
			++CountByFloor[Structure.BaseCoordinate.Z];
			for (const FIntVector Clearance : Structure.ClearanceCells)
			{
				TestTrue(TEXT("Plan 只能保存真实生成楼层内的净空格"),
					Clearance.Z >= 0 && Clearance.Z < Plan.FloorCount);
				TestNull(TEXT("高天花板净空不得同时导出普通可走格"),
					FindOrdinaryCell(Plan, Clearance));
			}
			if (Structure.BaseCoordinate.Z == Plan.FloorCount - 1)
			{
				bFoundTopFloorRoom = true;
				TestTrue(TEXT("顶层允许高天花板房间，并裁掉楼外净空地址"),
					Structure.ClearanceCells.IsEmpty());
			}
		}
		TestEqual(TEXT("整栋最低和目标数量都要求三间"), HighRoomCount, 3);
		for (const int32 Count : CountByFloor)
		{
			TestTrue(TEXT("每层最多一间的 DataAsset 上限必须生效"), Count <= 1);
		}
		TestTrue(TEXT("三间配合每层上限一间时顶层必须出现房间"),
			bFoundTopFloorRoom);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeSharedBudgetAtomicFailureTest,
		"Demo.PCG.Unit.MultiFloor.SharedBudgetAtomicFailure",
		EAutomationTestFlags_ApplicationContextMask
			| EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeSharedBudgetAtomicFailureTest::RunTest(
		const FString& Parameters)
	{
		using namespace MultiFloorLayoutTestsPrivate;
		(void)Parameters;
		FResolvedGenerationInput Input = MakeResolvedInput(17);
		Input.Budget.MaxStructureCandidateEvaluations = 1;
		FZeroEscapeGeneratedLevelPlan Plan;
		FZeroEscapeGenerationReport Report;
		TestFalse(TEXT("一个结构候选预算不可能完成三层必需楼梯链"),
			FMultiFloorLayoutPlanner::Solve(Input, Plan, Report));
		TestEqual(TEXT("预算耗尽必须返回结构化失败"),
			Report.Failure, EZeroEscapeGenerationFailure::SolverBudgetExhausted);
		TestEqual(TEXT("结构候选计数不得超过整栋共享硬上限"),
			Report.Metrics.StructureCandidateEvaluationCount, 1);
		TestTrue(TEXT("失败不得提交半张多层 Plan"),
			Plan.CanonicalLayoutHash == 0
				&& Plan.OrdinaryCells.IsEmpty()
				&& Plan.Structures.IsEmpty()
				&& Plan.Floors.IsEmpty());
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
