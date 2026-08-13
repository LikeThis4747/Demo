// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeMultiFloorLayoutTests.cpp
 * 职责：验证完整结构先放置、逐层二维 WFC、整栋一次 BFS、确定性和共享预算。
 * 边界：大部分测试构造纯值定义；Seed Sweep 只读正式 Profile，不创建 World，
 * 不把导航当成逻辑连通证明。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <initializer_list>

#include "Algo/Reverse.h"
#include "Containers/Queue.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include "PCG/ZeroEscapeGenerationCore.h"
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
			Input.SharedRules.GridSize = FIntPoint(14, 10);
			Input.SharedRules.LogicalTileSizeCm = 600.0;
			Input.SharedRules.FloorHeightCm = 450.0;
			Input.SharedRules.MaxConsecutiveStraightTiles = 8;
			Input.SharedRules.AnchorHeightCm = 100.0;
			Input.Budget.MaxWholeLayoutAttempts = 4;
			Input.Budget.MaxStructureCandidateEvaluations = 250000;
			Input.Budget.MaxWfcCandidateAttemptsPerFloor = 100000;
			Input.Budget.MaxWfcBacktrackCountPerFloor = 25000;
			Input.Budget.MaxWfcSolveAttemptsPerFloor = 3;
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
			Input.Difficulty.HighCeilingRooms.MinimumTotalCount = 2;
			Input.Difficulty.HighCeilingRooms.MaxCountPerFloor = 2;
			Input.Difficulty.HighCeilingRooms.MinimumSeparationRatio = 0.0;

			FZeroEscapeFloorCountOption FloorOption;
			FloorOption.FloorCount = 3;
			FloorOption.SelectionWeight = 1;
			FloorOption.MinTotalWalkableCellCount = 84;
			FloorOption.MaxTotalWalkableCellCount = 192;
			FloorOption.MinOrdinaryWalkableCellCountPerFloor = 22;
			FloorOption.MaxAdditionalTwoFloorStairCount = 4;
			FZeroEscapeWeightedCount TwoHighRooms;
			TwoHighRooms.Count = 2;
			TwoHighRooms.Weight = 1;
			FloorOption.HighCeilingRoomTargetCounts = { TwoHighRooms };
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
			int32& OutDistance,
			int32* OutVerticalTransitions = nullptr)
		{
			OutDistance = INDEX_NONE;
			if (OutVerticalTransitions != nullptr)
			{
				*OutVerticalTransitions = INDEX_NONE;
			}
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
			TMap<FIntVector, int32> VerticalTransitions;
			TQueue<FIntVector> Queue;
			Distance.Add(Start, 0);
			VerticalTransitions.Add(Start, 0);
			Queue.Enqueue(Start);
			FIntVector Current;
			while (Queue.Dequeue(Current))
			{
				const TArray<FIntVector>* CurrentNeighbors = Neighbors.Find(Current);
				if (CurrentNeighbors == nullptr)
				{
					continue;
				}
				for (const FIntVector Neighbor : *CurrentNeighbors)
				{
					const int32 CandidateDistance = Distance[Current] + 1;
					const int32 CandidateVertical = VerticalTransitions[Current]
						+ (Current.Z == Neighbor.Z ? 0 : 1);
					const int32* ExistingDistance = Distance.Find(Neighbor);
					const int32* ExistingVertical = VerticalTransitions.Find(Neighbor);
					if (ExistingDistance == nullptr
						|| CandidateDistance < *ExistingDistance
						|| (CandidateDistance == *ExistingDistance
							&& CandidateVertical < *ExistingVertical))
					{
						Distance.Add(Neighbor, CandidateDistance);
						VerticalTransitions.Add(Neighbor, CandidateVertical);
						Queue.Enqueue(Neighbor);
					}
				}
			}
			const int32* GoalDistance = Distance.Find(Goal);
			const int32* GoalVertical = VerticalTransitions.Find(Goal);
			if (GoalDistance == nullptr || GoalVertical == nullptr)
			{
				return false;
			}
			OutDistance = *GoalDistance;
			if (OutVerticalTransitions != nullptr)
			{
				*OutVerticalTransitions = *GoalVertical;
			}
			return true;
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
		const int32 HighRoomCount = CountStructures(
			FirstPlan, EZeroEscapeStructureKind::HighCeilingRoom);
		bool bHasNonTopHighRoom = false;
		for (const FZeroEscapeGeneratedStructure& Structure : FirstPlan.Structures)
		{
			bHasNonTopHighRoom |=
				Structure.Kind == EZeroEscapeStructureKind::HighCeilingRoom
				&& Structure.BaseCoordinate.Z < FirstPlan.FloorCount - 1;
		}
		TestTrue(TEXT("整栋至少生成两个高天花板房间"), HighRoomCount >= 2);
		TestTrue(TEXT("高天花板房间不能全部位于顶层"), bHasNonTopHighRoom);

		int32 SummedWalkable = 0;
		for (const FZeroEscapeGeneratedFloorSummary& Floor : FirstPlan.Floors)
		{
			SummedWalkable += Floor.TotalWalkableCellCount;
			const int32 HardOrdinaryMinimum = Floor.FloorIndex == 0 ? 2 : 1;
			TestTrue(TEXT("每层只保留玩家可用性所需的普通格硬下限"),
				Floor.OrdinaryWalkableCellCount
					>= HardOrdinaryMinimum);
			TestTrue(TEXT("软质量指标必须仍可测量"),
				FMath::IsFinite(Floor.SpatialSeparationRatio)
					&& FMath::IsFinite(Floor.RouteCoverageRatio));
		}
		TestTrue(TEXT("整栋硬合法结果必须包含可走格"), SummedWalkable > 0);
		TestEqual(TEXT("报告必须按楼层保留 WFC 指标"),
			FirstReport.Metrics.FloorWfcMetrics.Num(), FirstPlan.FloorCount);
		int32 FloorSolveAttempts = 0;
		int32 FloorCandidateAttempts = 0;
		int32 FloorBacktracks = 0;
		int32 FloorContradictions = 0;
		for (int32 Floor = 0;
			Floor < FirstReport.Metrics.FloorWfcMetrics.Num();
			++Floor)
		{
			const FZeroEscapeFloorWfcMetrics& FloorMetrics =
				FirstReport.Metrics.FloorWfcMetrics[Floor];
			TestEqual(TEXT("每层 WFC 指标必须按 FloorIndex 升序"),
				FloorMetrics.FloorIndex, Floor);
			FloorSolveAttempts += FloorMetrics.WfcSolveAttemptCount;
			FloorCandidateAttempts += FloorMetrics.WfcCandidateAttemptCount;
			FloorBacktracks += FloorMetrics.WfcBacktrackCount;
			FloorContradictions += FloorMetrics.WfcContradictionCount;
		}
		TestTrue(TEXT("分层指标之和必须等于整栋聚合指标"),
			FloorSolveAttempts == FirstReport.Metrics.WfcSolveAttemptCount
				&& FloorCandidateAttempts
					== FirstReport.Metrics.WfcCandidateAttemptCount
				&& FloorBacktracks == FirstReport.Metrics.WfcBacktrackCount
				&& FloorContradictions
					== FirstReport.Metrics.WfcContradictionCount);

		TestTrue(TEXT("玩家必须是一楼普通可走格且不同于追猎者"),
			FirstPlan.PlayerSpawnCoordinate.Z == 0
				&& FirstPlan.PursuerSpawnCoordinate
					!= FirstPlan.PlayerSpawnCoordinate
				&& FindOrdinaryCell(
					FirstPlan, FirstPlan.PlayerSpawnCoordinate) != nullptr);
		TestEqual(TEXT("追猎者必须占据一楼主路线起点"),
			FirstPlan.PursuerSpawnCoordinate,
			FirstPlan.Floors[0].RequiredEnterCoordinate);
		int32 PlayerPursuerDistance = INDEX_NONE;
		TestTrue(TEXT("独立整栋图必须能重算玩家到追猎者路线"),
			ComputePlanRouteDistance(
				FirstPlan,
				FirstPlan.PlayerSpawnCoordinate,
				FirstPlan.PursuerSpawnCoordinate,
				PlayerPursuerDistance));
		TestTrue(TEXT("玩家出生点必须满足实际路线距离而非二维直线距离"),
			PlayerPursuerDistance * FirstPlan.LogicalTileSizeCm
					+ UE_DOUBLE_SMALL_NUMBER
				>= Input.Difficulty.MinPlayerPursuerRouteDistanceCm);
		for (const FZeroEscapeGeneratedOrdinaryCell& Candidate : FirstPlan.OrdinaryCells)
		{
			if (Candidate.Coordinate.Z != 0
				|| Candidate.Coordinate == FirstPlan.PursuerSpawnCoordinate)
			{
				continue;
			}
			int32 CandidateDistance = INDEX_NONE;
			if (ComputePlanRouteDistance(
					FirstPlan,
					FirstPlan.PursuerSpawnCoordinate,
					Candidate.Coordinate,
					CandidateDistance)
				&& CandidateDistance * FirstPlan.LogicalTileSizeCm
						+ UE_DOUBLE_SMALL_NUMBER
					>= Input.Difficulty.MinPlayerPursuerRouteDistanceCm)
			{
				TestTrue(TEXT("玩家必须是满足安全距离的最近普通格"),
					PlayerPursuerDistance <= CandidateDistance);
			}
		}
		int32 PlayerExitDistance = INDEX_NONE;
		int32 PlayerExitVerticalTransitions = INDEX_NONE;
		TestTrue(TEXT("独立整栋图必须能重算玩家到 Exit 路线"),
			ComputePlanRouteDistance(
				FirstPlan,
				FirstPlan.PlayerSpawnCoordinate,
				FirstPlan.ExitCoordinate,
				PlayerExitDistance,
				&PlayerExitVerticalTransitions));
		TestEqual(TEXT("Plan 保存的玩家到 Exit 最短路必须等于独立 BFS"),
			FirstPlan.PlayerToExitRouteLengthTiles, PlayerExitDistance);
		TestEqual(TEXT("等长最短路必须保存较少的跨层次数"),
			FirstPlan.VerticalTransitionCountOnShortestRoute,
			PlayerExitVerticalTransitions);

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
		FZeroEscapePublicSeedStabilitySweepTest,
		"Demo.PCG.Stress.PublicSeedStability900",
		EAutomationTestFlags_ApplicationContextMask
			| EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePublicSeedStabilitySweepTest::RunTest(
		const FString& Parameters)
	{
		using namespace MultiFloorLayoutTestsPrivate;
		(void)Parameters;
		const UZeroEscapeLevelGenerationProfile* Profile =
			LoadObject<UZeroEscapeLevelGenerationProfile>(
				nullptr,
				TEXT("/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile.DA_LevelGenerationProfile"));
		if (!TestNotNull(TEXT("Seed Sweep 必须加载正式生成 Profile"), Profile))
		{
			return true;
		}

		constexpr EZeroEscapeDifficulty Difficulties[] = {
			EZeroEscapeDifficulty::Easy,
			EZeroEscapeDifficulty::Normal,
			EZeroEscapeDifficulty::Hard };
		for (const EZeroEscapeDifficulty Difficulty : Difficulties)
		{
			for (int32 Seed = 0; Seed < 300; ++Seed)
			{
				FZeroEscapeGenerationRequest Request;
				Request.Seed = Seed;
				Request.Difficulty = Difficulty;
				FResolvedGenerationInput FirstInput;
				FResolvedGenerationInput ReplayInput;
				FZeroEscapeGenerationReport FirstResolveReport;
				FZeroEscapeGenerationReport ReplayResolveReport;
				if (!FGenerationCore::ResolveGenerationInput(
						*Profile, Request, FirstInput, FirstResolveReport)
					|| !FGenerationCore::ResolveGenerationInput(
						*Profile, Request, ReplayInput, ReplayResolveReport))
				{
					AddError(FString::Printf(
						TEXT("Difficulty=%d Seed=%d Profile 解析失败：%s / %s"),
						static_cast<int32>(Difficulty),
						Seed,
						*FirstResolveReport.Message,
						*ReplayResolveReport.Message));
					return true;
				}

				FZeroEscapeGeneratedLevelPlan FirstPlan;
				FZeroEscapeGeneratedLevelPlan ReplayPlan;
				FZeroEscapeGenerationReport FirstReport;
				FZeroEscapeGenerationReport ReplayReport;
				if (!FMultiFloorLayoutPlanner::Solve(
						FirstInput, FirstPlan, FirstReport)
					|| !FMultiFloorLayoutPlanner::Solve(
						ReplayInput, ReplayPlan, ReplayReport))
				{
					AddError(FString::Printf(
						TEXT("Difficulty=%d Seed=%d 出现玩家可见生成失败：%s / %s"),
						static_cast<int32>(Difficulty),
						Seed,
						*FirstReport.Message,
						*ReplayReport.Message));
					return true;
				}

				int32 HighRoomCount = 0;
				bool bHasNonTopHighRoom = false;
				for (const FZeroEscapeGeneratedStructure& Structure : FirstPlan.Structures)
				{
					if (Structure.Kind != EZeroEscapeStructureKind::HighCeilingRoom)
					{
						continue;
					}
					++HighRoomCount;
					bHasNonTopHighRoom |=
						Structure.BaseCoordinate.Z < FirstPlan.FloorCount - 1;
				}
				if (FirstPlan.Signature.Seed != Seed
					|| ReplayPlan.Signature.Seed != Seed
					|| FirstPlan.CanonicalLayoutHash == 0
					|| FirstPlan.CanonicalLayoutHash
						!= ReplayPlan.CanonicalLayoutHash
					|| HighRoomCount < 2
					|| !bHasNonTopHighRoom)
				{
					AddError(FString::Printf(
						TEXT("Difficulty=%d Seed=%d 违反 Seed/确定性/高厅合同：FirstSeed=%d ReplaySeed=%d Hash=%lld/%lld HighRooms=%d NonTop=%d"),
						static_cast<int32>(Difficulty), Seed,
						FirstPlan.Signature.Seed, ReplayPlan.Signature.Seed,
						FirstPlan.CanonicalLayoutHash,
						ReplayPlan.CanonicalLayoutHash,
						HighRoomCount,
						bHasNonTopHighRoom ? 1 : 0));
					return true;
				}
			}
		}
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
