// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeMultiFloorDataContractTests.cpp
 * 职责：验证多层 Profile、完整结构定义、共享预算和规范 Hash 的纯数据合同。
 * 边界：不调用 WFC、不创建 World/Actor/组件；结构放置与逐层约束由后续测试覆盖。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include "PCG/ZeroEscapeGenerationCore.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace MultiFloorDataContractTestsPrivate
	{
		FZeroEscapeStructureOpeningDefinition MakeOpening(
			const FName OpeningId,
			const FIntVector LocalCell,
			const EZeroEscapeOpenEdge Edge)
		{
			FZeroEscapeStructureOpeningDefinition Opening;
			Opening.OpeningId = OpeningId;
			Opening.LocalWalkableCell = LocalCell;
			Opening.OutwardEdge = Edge;
			return Opening;
		}

		FZeroEscapeStructureLandingDefinition MakeLanding(
			const FName LandingId,
			const FIntVector LocalCell)
		{
			FZeroEscapeStructureLandingDefinition Landing;
			Landing.LandingId = LandingId;
			Landing.LocalCoordinate = LocalCell;
			return Landing;
		}

		FZeroEscapeLocalCellConnection MakeConnection(
			const FIntVector A,
			const FIntVector B)
		{
			FZeroEscapeLocalCellConnection Connection;
			Connection.FirstCell = A;
			Connection.SecondCell = B;
			return Connection;
		}

		FZeroEscapeStructureDefinition MakeTwoFloorStairDefinition(
			const FName DefinitionId = TEXT("TwoFloorStair_Default"))
		{
			FZeroEscapeStructureDefinition Definition;
			Definition.DefinitionId = DefinitionId;
			Definition.Kind = EZeroEscapeStructureKind::TwoFloorStair;
			Definition.RequiredFloorCount = 2;
			Definition.WalkableCells = {
				FIntVector(0, 0, 0),
				FIntVector(0, 0, 1)};
			Definition.InternalConnections.Add(MakeConnection(
				Definition.WalkableCells[0], Definition.WalkableCells[1]));
			Definition.Openings = {
				MakeOpening(TEXT("LowerNorth"), FIntVector(0, 0, 0), EZeroEscapeOpenEdge::North),
				MakeOpening(TEXT("UpperSouth"), FIntVector(0, 0, 1), EZeroEscapeOpenEdge::South)};
			Definition.Landings = {
				MakeLanding(TEXT("Lower"), FIntVector(0, 0, 0)),
				MakeLanding(TEXT("Upper"), FIntVector(0, 0, 1))};
			FZeroEscapeStructureOpeningSetDefinition OpeningSet;
			OpeningSet.SetId = TEXT("BothFloors");
			OpeningSet.OpenOpeningIds = {TEXT("LowerNorth"), TEXT("UpperSouth")};
			Definition.AllowedOpeningSets.Add(OpeningSet);
			return Definition;
		}

		FZeroEscapeStructureDefinition MakeThreeFloorStairwellDefinition()
		{
			FZeroEscapeStructureDefinition Definition;
			Definition.DefinitionId = TEXT("ThreeFloorStairwell_Default");
			Definition.Kind = EZeroEscapeStructureKind::ThreeFloorStairwell;
			Definition.RequiredFloorCount = 3;
			Definition.WalkableCells = {
				FIntVector(0, 0, 0),
				FIntVector(0, 0, 1),
				FIntVector(0, 0, 2)};
			Definition.InternalConnections = {
				MakeConnection(Definition.WalkableCells[0], Definition.WalkableCells[1]),
				MakeConnection(Definition.WalkableCells[1], Definition.WalkableCells[2])};
			Definition.Openings = {
				MakeOpening(TEXT("Floor0North"), FIntVector(0, 0, 0), EZeroEscapeOpenEdge::North),
				MakeOpening(TEXT("Floor1East"), FIntVector(0, 0, 1), EZeroEscapeOpenEdge::East),
				MakeOpening(TEXT("Floor2South"), FIntVector(0, 0, 2), EZeroEscapeOpenEdge::South)};
			Definition.Landings = {
				MakeLanding(TEXT("Floor0"), FIntVector(0, 0, 0)),
				MakeLanding(TEXT("Floor1"), FIntVector(0, 0, 1)),
				MakeLanding(TEXT("Floor2"), FIntVector(0, 0, 2))};
			FZeroEscapeStructureOpeningSetDefinition OpeningSet;
			OpeningSet.SetId = TEXT("AllFloors");
			OpeningSet.OpenOpeningIds = {
				TEXT("Floor0North"), TEXT("Floor1East"), TEXT("Floor2South")};
			Definition.AllowedOpeningSets.Add(OpeningSet);
			return Definition;
		}

		FZeroEscapeStructureDefinition MakeHighCeilingRoomDefinition()
		{
			FZeroEscapeStructureDefinition Definition;
			Definition.DefinitionId = TEXT("HighCeilingRoom_1x2");
			Definition.Kind = EZeroEscapeStructureKind::HighCeilingRoom;
			Definition.RequiredFloorCount = 1;
			Definition.bAllowClearanceAboveGeneratedTopFloor = true;
			Definition.WalkableCells = {
				FIntVector(0, 0, 0), FIntVector(1, 0, 0)};
			Definition.ClearanceCells = {
				FIntVector(0, 0, 1), FIntVector(1, 0, 1)};
			Definition.InternalConnections.Add(MakeConnection(
				Definition.WalkableCells[0], Definition.WalkableCells[1]));
			Definition.Openings = {
				MakeOpening(TEXT("West"), FIntVector(0, 0, 0), EZeroEscapeOpenEdge::West),
				MakeOpening(TEXT("East"), FIntVector(1, 0, 0), EZeroEscapeOpenEdge::East)};
			FZeroEscapeStructureOpeningSetDefinition OpeningSet;
			OpeningSet.SetId = TEXT("ThroughRoom");
			OpeningSet.OpenOpeningIds = {TEXT("West"), TEXT("East")};
			Definition.AllowedOpeningSets.Add(OpeningSet);
			return Definition;
		}

		FZeroEscapeFloorCountOption MakeFloorOption(
			const int32 FloorCount,
			const int32 SelectionWeight,
			const int32 MaxAdditionalStairs)
		{
			FZeroEscapeFloorCountOption Option;
			Option.FloorCount = FloorCount;
			Option.SelectionWeight = SelectionWeight;
			Option.MinTotalWalkableCellCountPerFloor = 24;
			Option.MaxTotalWalkableCellCountPerFloor = 48;
			Option.MinOrdinaryWalkableCellCountPerFloor = 20;
			Option.MaxPlayerToExitRouteLengthTiles = 160;
			Option.MaxAdditionalTwoFloorStairCount = MaxAdditionalStairs;
			FZeroEscapeWeightedCount NoRoom;
			NoRoom.Count = 0;
			NoRoom.Weight = 1;
			FZeroEscapeWeightedCount OneRoom;
			OneRoom.Count = 1;
			OneRoom.Weight = 1;
			Option.HighCeilingRoomTargetCounts = {NoRoom, OneRoom};
			return Option;
		}

		FZeroEscapeDifficultyDefinition MakeDifficulty(
			const EZeroEscapeDifficulty Difficulty)
		{
			FZeroEscapeDifficultyDefinition Definition;
			Definition.Difficulty = Difficulty;
			Definition.WfcShapeWeights = FZeroEscapeWfcShapeWeights();
			Definition.FloorCountOptions = {
				MakeFloorOption(2, 20, 2),
				MakeFloorOption(3, 60, 3),
				MakeFloorOption(4, 20, 4)};
			Definition.ThreeFloorStairwellChancePercent = 10;
			Definition.HighCeilingRooms.MinimumTotalCount = 0;
			Definition.HighCeilingRooms.MaxCountPerFloor = 2;
			Definition.HighCeilingRooms.MinimumSeparationRatio = 0.2;
			return Definition;
		}

		void BuildValidProfile(UZeroEscapeLevelGenerationProfile& Profile)
		{
			Profile.ProfileVersion = 6;
			Profile.SharedRouteConstraints = FZeroEscapeSharedRouteConstraints();
			Profile.SharedRouteConstraints.GridSize = FIntPoint(20, 12);
			Profile.SharedRouteConstraints.LogicalTileSizeCm = 600.0;
			Profile.SharedRouteConstraints.FloorHeightCm = 450.0;
			Profile.SharedRouteConstraints.MaxConsecutiveStraightTiles = 4;
			Profile.SharedRouteConstraints.AnchorHeightCm = 100.0;
			Profile.SharedBudget = FZeroEscapeSharedGenerationBudget();
			Profile.StructureDefinitions = {
				MakeTwoFloorStairDefinition(),
				MakeThreeFloorStairwellDefinition(),
				MakeHighCeilingRoomDefinition()};
			Profile.Difficulties = {
				MakeDifficulty(EZeroEscapeDifficulty::Easy),
				MakeDifficulty(EZeroEscapeDifficulty::Normal),
				MakeDifficulty(EZeroEscapeDifficulty::Hard)};
		}

		template <typename ElementType>
		void ReverseArray(TArray<ElementType>& Values)
		{
			for (int32 Left = 0, Right = Values.Num() - 1; Left < Right; ++Left, --Right)
			{
				Values.Swap(Left, Right);
			}
		}

		void AddNormalizationCoverage(UZeroEscapeLevelGenerationProfile& Profile)
		{
			FZeroEscapeStructureDefinition& Stair = Profile.StructureDefinitions[0];
			Stair.SolidCells = {FIntVector(3, 4, 0), FIntVector(2, 4, 0)};
			FZeroEscapeStructureOpeningSetDefinition AlternativeSet;
			AlternativeSet.SetId = TEXT("Alternative");
			AlternativeSet.SelectionWeight = 2;
			AlternativeSet.OpenOpeningIds = {TEXT("UpperSouth"), TEXT("LowerNorth")};
			Stair.AllowedOpeningSets.Add(AlternativeSet);
		}

		void ReverseProfileArrayOrder(UZeroEscapeLevelGenerationProfile& Profile)
		{
			for (FZeroEscapeDifficultyDefinition& Difficulty : Profile.Difficulties)
			{
				ReverseArray(Difficulty.FloorCountOptions);
				for (FZeroEscapeFloorCountOption& Option : Difficulty.FloorCountOptions)
				{
					ReverseArray(Option.HighCeilingRoomTargetCounts);
				}
			}
			for (FZeroEscapeStructureDefinition& Definition : Profile.StructureDefinitions)
			{
				ReverseArray(Definition.WalkableCells);
				ReverseArray(Definition.SolidCells);
				ReverseArray(Definition.ClearanceCells);
				for (FZeroEscapeLocalCellConnection& Connection : Definition.InternalConnections)
				{
					Swap(Connection.FirstCell, Connection.SecondCell);
				}
				ReverseArray(Definition.InternalConnections);
				ReverseArray(Definition.Openings);
				ReverseArray(Definition.Landings);
				for (FZeroEscapeStructureOpeningSetDefinition& Set :
					Definition.AllowedOpeningSets)
				{
					ReverseArray(Set.OpenOpeningIds);
				}
				ReverseArray(Definition.AllowedOpeningSets);
			}
			ReverseArray(Profile.StructureDefinitions);
			ReverseArray(Profile.Difficulties);
		}

		FString BuildResolvedInputOrderFingerprint(const FResolvedGenerationInput& Input)
		{
			FString Result;
			for (const FZeroEscapeFloorCountOption& Option :
				Input.Difficulty.FloorCountOptions)
			{
				Result += FString::Printf(TEXT("Floor:%d{"), Option.FloorCount);
				for (const FZeroEscapeWeightedCount& Target :
					Option.HighCeilingRoomTargetCounts)
				{
					Result += FString::Printf(TEXT("%d=%d,"), Target.Count, Target.Weight);
				}
				Result += TEXT("}");
			}
			for (const FZeroEscapeStructureDefinition& Definition : Input.StructureDefinitions)
			{
				Result += FString::Printf(
					TEXT("Definition:%d:%s|"),
					static_cast<int32>(Definition.Kind),
					*Definition.DefinitionId.ToString());
				auto AppendCells = [&Result](
					const TCHAR* Label,
					const TArray<FIntVector>& Cells)
				{
					Result += Label;
					for (const FIntVector Cell : Cells)
					{
						Result += FString::Printf(
							TEXT("(%d,%d,%d)"), Cell.X, Cell.Y, Cell.Z);
					}
				};
				AppendCells(TEXT("W"), Definition.WalkableCells);
				AppendCells(TEXT("S"), Definition.SolidCells);
				AppendCells(TEXT("C"), Definition.ClearanceCells);
				for (const FZeroEscapeLocalCellConnection& Connection :
					Definition.InternalConnections)
				{
					Result += FString::Printf(
						TEXT("E(%d,%d,%d)-(%d,%d,%d)"),
						Connection.FirstCell.X,
						Connection.FirstCell.Y,
						Connection.FirstCell.Z,
						Connection.SecondCell.X,
						Connection.SecondCell.Y,
						Connection.SecondCell.Z);
				}
				for (const FZeroEscapeStructureOpeningDefinition& Opening :
					Definition.Openings)
				{
					Result += FString::Printf(
						TEXT("O%s(%d,%d,%d):%d"),
						*Opening.OpeningId.ToString(),
						Opening.LocalWalkableCell.X,
						Opening.LocalWalkableCell.Y,
						Opening.LocalWalkableCell.Z,
						static_cast<int32>(Opening.OutwardEdge));
				}
				for (const FZeroEscapeStructureLandingDefinition& Landing :
					Definition.Landings)
				{
					Result += FString::Printf(
						TEXT("L%s(%d,%d,%d)"),
						*Landing.LandingId.ToString(),
						Landing.LocalCoordinate.X,
						Landing.LocalCoordinate.Y,
						Landing.LocalCoordinate.Z);
				}
				for (const FZeroEscapeStructureOpeningSetDefinition& Set :
					Definition.AllowedOpeningSets)
				{
					Result += FString::Printf(
						TEXT("A%s=%d["), *Set.SetId.ToString(), Set.SelectionWeight);
					for (const FName OpeningId : Set.OpenOpeningIds)
					{
						Result += OpeningId.ToString();
						Result += TEXT(",");
					}
					Result += TEXT("]");
				}
			}
			return Result;
		}

		FZeroEscapeGeneratedLevelPlan MakeHashFixturePlan()
		{
			FZeroEscapeGeneratedLevelPlan Plan;
			Plan.Signature.Seed = 24680;
			Plan.Signature.Difficulty = EZeroEscapeDifficulty::Normal;
			Plan.Signature.AlgorithmVersion = GAlgorithmVersion;
			Plan.Signature.GenerationProfileVersion = 6;
			Plan.Signature.PresentationVersion = 1;
			Plan.FloorCount = 2;
			Plan.GridSize = FIntPoint(6, 6);
			Plan.LogicalTileSizeCm = 600.0;
			Plan.FloorHeightCm = 450.0;
			Plan.AnchorHeightCm = 100.0;
			Plan.PlayerSpawnCoordinate = FIntVector(0, 0, 0);
			Plan.PursuerSpawnCoordinate = FIntVector(1, 0, 0);
			Plan.ExitCoordinate = FIntVector(1, 0, 1);
			Plan.PlayerToExitRouteLengthTiles = 5;
			Plan.VerticalTransitionCountOnShortestRoute = 1;

			for (const FIntVector Coordinate : {
				FIntVector(0, 0, 0), FIntVector(1, 0, 0),
				FIntVector(0, 0, 1), FIntVector(1, 0, 1)})
			{
				FZeroEscapeGeneratedOrdinaryCell& Cell =
					Plan.OrdinaryCells.AddDefaulted_GetRef();
				Cell.Coordinate = Coordinate;
				Cell.OpeningMask = Coordinate.X == 0
					? Grid::DirectionBit(1) : Grid::DirectionBit(3);
			}

			FZeroEscapeGeneratedStructure& Stair = Plan.Structures.AddDefaulted_GetRef();
			Stair.StableStructureId = 0;
			Stair.DefinitionId = TEXT("TwoFloorStair_Default");
			Stair.Kind = EZeroEscapeStructureKind::TwoFloorStair;
			Stair.BaseCoordinate = FIntVector(2, 2, 0);
			Stair.ActiveOpeningSetId = TEXT("BothFloors");
			Stair.WalkableCells = {FIntVector(2, 2, 0), FIntVector(2, 2, 1)};
			FZeroEscapeGeneratedCellConnection Connection;
			Connection.FirstCoordinate = Stair.WalkableCells[0];
			Connection.SecondCoordinate = Stair.WalkableCells[1];
			Stair.InternalConnections.Add(Connection);
			for (int32 FloorIndex = 0; FloorIndex < 2; ++FloorIndex)
			{
				FZeroEscapeGeneratedStructureOpening& Opening =
					Stair.Openings.AddDefaulted_GetRef();
				Opening.OpeningId = FloorIndex == 0 ? TEXT("LowerNorth") : TEXT("UpperSouth");
				Opening.StructureCoordinate = FIntVector(2, 2, FloorIndex);
				Opening.ConnectedOrdinaryCoordinate = FIntVector(2, 1, FloorIndex);
				FZeroEscapeGeneratedStructureLanding& Landing =
					Stair.Landings.AddDefaulted_GetRef();
				Landing.LandingId = FloorIndex == 0 ? TEXT("Lower") : TEXT("Upper");
				Landing.Coordinate = FIntVector(2, 2, FloorIndex);
			}
			Plan.RequiredTwoFloorStairStableIdByLowerFloor.Add(0);

			for (int32 FloorIndex = 0; FloorIndex < 2; ++FloorIndex)
			{
				FZeroEscapeGeneratedFloorSummary& Floor = Plan.Floors.AddDefaulted_GetRef();
				Floor.FloorIndex = FloorIndex;
				Floor.RequiredEnterCoordinate = FloorIndex == 0
					? Plan.PlayerSpawnCoordinate : FIntVector(2, 2, 1);
				Floor.RequiredLeaveCoordinate = FloorIndex == 0
					? FIntVector(2, 2, 0) : Plan.ExitCoordinate;
				Floor.OrdinaryWalkableCellCount = 2;
				Floor.TotalWalkableCellCount = 3;
				Floor.RequiredRouteLengthTiles = 2;
				Floor.FarthestRouteLengthTiles = 3;
				Floor.SpatialSeparationRatio = 0.5;
				Floor.RouteCoverageRatio = 2.0 / 3.0;
				Floor.JunctionMetrics.DeadEndCount = 2;
				Floor.CycleRank = 0;
			}
			Plan.JunctionMetrics.DeadEndCount = 4;
			Plan.CycleRank = 0;
			return Plan;
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeMultiFloorProfileContractTest,
		"Demo.PCG.Unit.MultiFloor.ProfileContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeMultiFloorProfileContractTest::RunTest(const FString& Parameters)
	{
		using namespace MultiFloorDataContractTestsPrivate;
		(void)Parameters;
		UZeroEscapeLevelGenerationProfile* Profile =
			NewObject<UZeroEscapeLevelGenerationProfile>();
		FString Error;
		BuildValidProfile(*Profile);
		TestTrue(TEXT("合法多层 Profile 必须通过配置校验"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.GridSize = FIntPoint(24, 16);
		TestTrue(TEXT("合法范围内改变 GridSize 不得破坏配置合同"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->StructureDefinitions.Add(MakeTwoFloorStairDefinition(TEXT("TwoFloorStair_Alt")));
		TestTrue(TEXT("同一 Kind 可以配置多个 DefinitionId"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->StructureDefinitions[0].InternalConnections.Add(
			Profile->StructureDefinitions[0].InternalConnections[0]);
		TestFalse(TEXT("同向重复 InternalConnection 必须被拒绝"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		const FZeroEscapeLocalCellConnection ExistingConnection =
			Profile->StructureDefinitions[0].InternalConnections[0];
		FZeroEscapeLocalCellConnection ReversedConnection;
		ReversedConnection.FirstCell = ExistingConnection.SecondCell;
		ReversedConnection.SecondCell = ExistingConnection.FirstCell;
		Profile->StructureDefinitions[0].InternalConnections.Add(ReversedConnection);
		TestFalse(TEXT("反向重复 InternalConnection 必须被拒绝"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->StructureDefinitions[2].bAllowClearanceAboveGeneratedTopFloor = false;
		TestFalse(TEXT("高厅必须明确允许裁掉真实顶层以上净空"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->StructureDefinitions.RemoveAt(1);
		TestFalse(TEXT("三层楼梯间概率大于零时必须存在对应定义"), Profile->IsConfigured(Error));
		for (FZeroEscapeDifficultyDefinition& Difficulty : Profile->Difficulties)
		{
			Difficulty.ThreeFloorStairwellChancePercent = 0;
		}
		TestTrue(TEXT("概率为零时可以不配置三层楼梯间"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[0].FloorCountOptions[2].MaxAdditionalTwoFloorStairCount = 5;
		TestFalse(TEXT("楼梯端点超过共享导航点预算必须失败"), Profile->IsConfigured(Error));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeResolvedInputNormalizationTest,
		"Demo.PCG.Unit.MultiFloor.ResolvedInputNormalization",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeResolvedInputNormalizationTest::RunTest(const FString& Parameters)
	{
		using namespace MultiFloorDataContractTestsPrivate;
		(void)Parameters;
		UZeroEscapeLevelGenerationProfile* BaselineProfile =
			NewObject<UZeroEscapeLevelGenerationProfile>();
		UZeroEscapeLevelGenerationProfile* ReorderedProfile =
			NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*BaselineProfile);
		BuildValidProfile(*ReorderedProfile);
		AddNormalizationCoverage(*BaselineProfile);
		AddNormalizationCoverage(*ReorderedProfile);
		ReverseProfileArrayOrder(*ReorderedProfile);

		FZeroEscapeGenerationRequest Request;
		Request.Seed = 13579;
		Request.Difficulty = EZeroEscapeDifficulty::Normal;
		FResolvedGenerationInput BaselineInput;
		FResolvedGenerationInput ReorderedInput;
		FZeroEscapeGenerationReport BaselineReport;
		FZeroEscapeGenerationReport ReorderedReport;
		TestTrue(TEXT("基准 Profile 必须成功解析"), FGenerationCore::ResolveGenerationInput(
			*BaselineProfile, Request, 1, BaselineInput, BaselineReport));
		TestTrue(TEXT("仅重排数组的 Profile 必须成功解析"), FGenerationCore::ResolveGenerationInput(
			*ReorderedProfile, Request, 1, ReorderedInput, ReorderedReport));
		TestEqual(
			TEXT("语义相同的 DataAsset 数组重排必须得到完全相同的规范化生成输入"),
			BuildResolvedInputOrderFingerprint(ReorderedInput),
			BuildResolvedInputOrderFingerprint(BaselineInput));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeMultiFloorCanonicalHashTest,
		"Demo.PCG.Unit.MultiFloor.CanonicalHash",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeMultiFloorCanonicalHashTest::RunTest(const FString& Parameters)
	{
		using namespace MultiFloorDataContractTestsPrivate;
		(void)Parameters;
		const FZeroEscapeGeneratedLevelPlan Baseline = MakeHashFixturePlan();
		const int64 BaselineHash = FGenerationCore::ComputeCanonicalLayoutHash(Baseline);
		TestTrue(TEXT("合法多层 Plan 必须产生非零 Hash"), BaselineHash != 0);

		FZeroEscapeGeneratedLevelPlan PresentationOnly = Baseline;
		PresentationOnly.Signature.PresentationVersion = 99;
		TestEqual(TEXT("PresentationVersion 不进入纯布局 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(PresentationOnly), BaselineHash);

		FZeroEscapeGeneratedLevelPlan DifferentOpeningSet = Baseline;
		DifferentOpeningSet.Structures[0].ActiveOpeningSetId = TEXT("AnotherSet");
		TestNotEqual(TEXT("ActiveOpeningSetId 字符串内容必须进入 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(DifferentOpeningSet), BaselineHash);

		FZeroEscapeGeneratedLevelPlan DifferentAnchor = Baseline;
		DifferentAnchor.AnchorHeightCm += 0.001;
		TestNotEqual(TEXT("AnchorHeightCm 的 IEEE bit 必须进入 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(DifferentAnchor), BaselineHash);

		FZeroEscapeGeneratedLevelPlan InvalidSpawnFloor = Baseline;
		InvalidSpawnFloor.PlayerSpawnCoordinate.Z = 1;
		TestEqual(TEXT("玩家不在一楼的 Plan 必须拒绝 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(InvalidSpawnFloor), static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan InvalidOrder = Baseline;
		InvalidOrder.OrdinaryCells.Swap(0, 1);
		TestEqual(TEXT("普通格未按 Z/Y/X 稳定排序时必须拒绝 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(InvalidOrder), static_cast<int64>(0));
		return true;
	}
}

#endif
