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
			Option.MinTotalWalkableCellCount = FloorCount * 24;
			Option.MaxTotalWalkableCellCount = FloorCount * 80;
			Option.MinOrdinaryWalkableCellCountPerFloor = 20;
			Option.MaxAdditionalTwoFloorStairCount = MaxAdditionalStairs;
			FZeroEscapeWeightedCount TwoRooms;
			TwoRooms.Count = 2;
			TwoRooms.Weight = 1;
			FZeroEscapeWeightedCount ThreeRooms;
			ThreeRooms.Count = 3;
			ThreeRooms.Weight = 1;
			Option.HighCeilingRoomTargetCounts = {TwoRooms, ThreeRooms};
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
			Definition.HighCeilingRooms.MinimumTotalCount = 2;
			Definition.HighCeilingRooms.MaxCountPerFloor = 2;
			Definition.HighCeilingRooms.MinimumSeparationRatio = 0.2;
			return Definition;
		}

		void BuildValidProfile(UZeroEscapeLevelGenerationProfile& Profile)
		{
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
			Stair.DefinitionId = TEXT("ZetaStair");
			Stair.SolidCells = {FIntVector(3, 4, 0), FIntVector(2, 4, 0)};
			Stair.Openings[0].OpeningId = TEXT("ZetaOpening");
			Stair.Openings[1].OpeningId = TEXT("alphaOpening");
			Stair.Landings[0].LandingId = TEXT("ZetaLanding");
			Stair.Landings[1].LandingId = TEXT("alphaLanding");
			Stair.AllowedOpeningSets[0].SetId = TEXT("ZetaSet");
			Stair.AllowedOpeningSets[0].OpenOpeningIds = {
				TEXT("ZetaOpening"), TEXT("alphaOpening")};
			FZeroEscapeStructureOpeningSetDefinition AlternativeSet;
			AlternativeSet.SetId = TEXT("alphaSet");
			AlternativeSet.SelectionWeight = 2;
			AlternativeSet.OpenOpeningIds = {
				TEXT("ZetaOpening"), TEXT("alphaOpening")};
			Stair.AllowedOpeningSets.Add(AlternativeSet);
			Profile.StructureDefinitions.Add(
				MakeTwoFloorStairDefinition(TEXT("alphaStair")));
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
				Result += FString::Printf(
					TEXT("Floor:%d:%d:%d:%d{"),
					Option.FloorCount,
					Option.MinTotalWalkableCellCount,
					Option.MaxTotalWalkableCellCount,
					Option.MinOrdinaryWalkableCellCountPerFloor);
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
			Stair.WalkableCells = {
				FIntVector(2, 2, 0), FIntVector(3, 2, 0), FIntVector(2, 2, 1)};
			FZeroEscapeGeneratedCellConnection LowerFloorConnection;
			LowerFloorConnection.FirstCoordinate = Stair.WalkableCells[0];
			LowerFloorConnection.SecondCoordinate = Stair.WalkableCells[1];
			Stair.InternalConnections.Add(LowerFloorConnection);
			FZeroEscapeGeneratedCellConnection VerticalConnection;
			VerticalConnection.FirstCoordinate = Stair.WalkableCells[0];
			VerticalConnection.SecondCoordinate = Stair.WalkableCells[2];
			Stair.InternalConnections.Add(VerticalConnection);
			for (int32 FloorIndex = 0; FloorIndex < 2; ++FloorIndex)
			{
				FZeroEscapeGeneratedStructureOpening& Opening =
					Stair.Openings.AddDefaulted_GetRef();
				Opening.OpeningId = FloorIndex == 0
					? TEXT("alphaOpening") : TEXT("ZetaOpening");
				Opening.StructureCoordinate = FIntVector(2, 2, FloorIndex);
				Opening.ConnectedOrdinaryCoordinate = FIntVector(2, 1, FloorIndex);
				FZeroEscapeGeneratedStructureLanding& Landing =
					Stair.Landings.AddDefaulted_GetRef();
				Landing.LandingId = FloorIndex == 0
					? TEXT("alphaLanding") : TEXT("ZetaLanding");
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
				Floor.TotalWalkableCellCount = FloorIndex == 0 ? 4 : 3;
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
		FZeroEscapeGenerationMetrics EmptyMetrics;
		TestTrue(TEXT("每层 WFC 报告默认必须为空"), EmptyMetrics.FloorWfcMetrics.IsEmpty());
		FZeroEscapeFloorWfcMetrics FloorMetrics;
		TestEqual(TEXT("未填写的每层 WFC 报告没有合法楼层"), FloorMetrics.FloorIndex, INDEX_NONE);
		TestEqual(TEXT("每层 WFC 候选统计默认归零"), FloorMetrics.WfcCandidateAttemptCount, 0);
		FloorMetrics.FloorIndex = 0;
		EmptyMetrics.FloorWfcMetrics.Add(FloorMetrics);
		TestEqual(TEXT("合法楼层统计可按楼层号保存"),
			EmptyMetrics.FloorWfcMetrics[0].FloorIndex, 0);

		BuildValidProfile(*Profile);
		TestTrue(TEXT("合法多层 Profile 必须通过配置校验"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.GridSize = FIntPoint(24, 16);
		TestTrue(TEXT("合法范围内改变 GridSize 不得破坏配置合同"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[0].FloorCountOptions[0].FloorCount = MIN_int32;
		TestFalse(TEXT("楼层数极小值必须在计算数组下标前失败"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[0].HighCeilingRooms.MaxCountPerFloor = MAX_int32;
		TestTrue(TEXT("高天花板房间每层上限与楼层数相乘必须使用 int64"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		const FZeroEscapeFloorCountOption& FourFloorOption =
			Profile->Difficulties[0].FloorCountOptions[2];
		TestTrue(
			TEXT("四层整栋总可走上限可以大于单层 Grid 容量"),
			FourFloorOption.MaxTotalWalkableCellCount
				> Profile->SharedRouteConstraints.GridSize.X
					* Profile->SharedRouteConstraints.GridSize.Y);
		TestTrue(TEXT("整栋总可走量合同必须通过配置校验"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		FZeroEscapeFloorCountOption& InvalidWholeBuildingMinimum =
			Profile->Difficulties[0].FloorCountOptions[2];
		InvalidWholeBuildingMinimum.MinTotalWalkableCellCount =
			InvalidWholeBuildingMinimum.MinOrdinaryWalkableCellCountPerFloor
				* InvalidWholeBuildingMinimum.FloorCount - 1;
		TestFalse(TEXT("整栋总可走下限不得低于各层普通内容下限之和"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->StructureDefinitions.Add(MakeTwoFloorStairDefinition(TEXT("TwoFloorStair_Alt")));
		TestTrue(TEXT("同一 Kind 可以配置多个 DefinitionId"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->StructureDefinitions[0].WalkableCells[0].X =
			ZeroEscape::GenerationLimits::MaxGridAxis;
		TestFalse(TEXT("结构局部正向 X 超出所有受支持 Grid 的可容纳范围必须失败"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->StructureDefinitions[0].WalkableCells[0].Y =
			-ZeroEscape::GenerationLimits::MaxGridAxis;
		TestFalse(TEXT("结构局部负向 Y 超出所有受支持 Grid 的可容纳范围必须失败"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		const FZeroEscapeLocalCellConnection DuplicateConnection =
			Profile->StructureDefinitions[0].InternalConnections[0];
		Profile->StructureDefinitions[0].InternalConnections.Add(DuplicateConnection);
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
		FZeroEscapeStructureOpeningSetDefinition OverflowOpeningSet =
			Profile->StructureDefinitions[0].AllowedOpeningSets[0];
		OverflowOpeningSet.SetId = TEXT("OverflowSet");
		OverflowOpeningSet.SelectionWeight = 1;
		Profile->StructureDefinitions[0].AllowedOpeningSets[0].SelectionWeight = MAX_int32;
		Profile->StructureDefinitions[0].AllowedOpeningSets.Add(OverflowOpeningSet);
		TestFalse(TEXT("Opening Set 权重总和不得超过 int32"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		FZeroEscapeAdditionalTwoFloorStairWeights& OverflowStairWeights =
			Profile->Difficulties[0].AdditionalTwoFloorStairsPerFloorPair;
		OverflowStairWeights.ZeroAdditionalWeight = MAX_int32;
		OverflowStairWeights.OneAdditionalWeight = 1;
		OverflowStairWeights.TwoAdditionalWeight = 0;
		TestFalse(TEXT("额外楼梯权重总和不得超过 int32"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[0].FloorCountOptions[0].SelectionWeight = MAX_int32;
		Profile->Difficulties[0].FloorCountOptions[1].SelectionWeight = 1;
		Profile->Difficulties[0].FloorCountOptions[2].SelectionWeight = 0;
		TestFalse(TEXT("楼层数权重总和不得超过 int32"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		TArray<FZeroEscapeWeightedCount>& OverflowHighRoomWeights =
			Profile->Difficulties[0].FloorCountOptions[0].HighCeilingRoomTargetCounts;
		OverflowHighRoomWeights[0].Weight = MAX_int32;
		OverflowHighRoomWeights[1].Weight = 1;
		TestFalse(TEXT("高天花板房间权重总和不得超过 int32"), Profile->IsConfigured(Error));

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
			*BaselineProfile, Request, BaselineInput, BaselineReport));
		TestTrue(TEXT("仅重排数组的 Profile 必须成功解析"), FGenerationCore::ResolveGenerationInput(
			*ReorderedProfile, Request, ReorderedInput, ReorderedReport));
		TestEqual(
			TEXT("语义相同的 DataAsset 数组重排必须得到完全相同的规范化生成输入"),
			BuildResolvedInputOrderFingerprint(ReorderedInput),
			BuildResolvedInputOrderFingerprint(BaselineInput));
		TestTrue(TEXT("混合大小写 DefinitionId 必须使用 FName 词法顺序"),
			BaselineInput.StructureDefinitions[0].DefinitionId == TEXT("alphaStair")
				&& BaselineInput.StructureDefinitions[1].DefinitionId == TEXT("ZetaStair"));
		const FZeroEscapeStructureDefinition* MixedCaseStair =
			BaselineInput.StructureDefinitions.FindByPredicate(
				[](const FZeroEscapeStructureDefinition& Definition)
				{
					return Definition.DefinitionId == TEXT("ZetaStair");
				});
		TestNotNull(TEXT("混合大小写测试楼梯必须存在"), MixedCaseStair);
		if (MixedCaseStair != nullptr)
		{
			TestTrue(TEXT("Opening/Landing/OpeningSet 必须统一使用 FName 词法顺序"),
				MixedCaseStair->Openings[0].OpeningId == TEXT("alphaOpening")
					&& MixedCaseStair->Landings[0].LandingId == TEXT("alphaLanding")
					&& MixedCaseStair->AllowedOpeningSets[0].SetId == TEXT("alphaSet")
					&& MixedCaseStair->AllowedOpeningSets[0].OpenOpeningIds[0]
						== TEXT("alphaOpening"));
		}
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
		TestTrue(TEXT("按 FName 词法顺序排列的混合大小写 ID 必须产生非零 Hash"),
			BaselineHash != 0);

		FZeroEscapeGeneratedLevelPlan DifferentOpeningSet = Baseline;
		DifferentOpeningSet.Structures[0].ActiveOpeningSetId = TEXT("AnotherSet");
		TestNotEqual(TEXT("ActiveOpeningSetId 字符串内容必须进入 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(DifferentOpeningSet), BaselineHash);

		FZeroEscapeGeneratedLevelPlan DifferentAnchor = Baseline;
		DifferentAnchor.AnchorHeightCm += 0.001;
		TestNotEqual(TEXT("AnchorHeightCm 的 IEEE bit 必须进入 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(DifferentAnchor), BaselineHash);

		FZeroEscapeGeneratedLevelPlan DifferentWholeBuildingTotal = Baseline;
		++DifferentWholeBuildingTotal.Floors[0].TotalWalkableCellCount;
		TestNotEqual(TEXT("构成整栋总可走量的每层统计必须进入 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(DifferentWholeBuildingTotal),
			BaselineHash);

		FZeroEscapeGeneratedLevelPlan WithRewardBranch = Baseline;
		for (const FIntVector Coordinate : {
			FIntVector(2, 3, 0), FIntVector(3, 3, 0), FIntVector(3, 4, 0)})
		{
			FZeroEscapeGeneratedOrdinaryCell& Cell =
				WithRewardBranch.OrdinaryCells.AddDefaulted_GetRef();
			Cell.Coordinate = Coordinate;
			Cell.OpeningMask = Grid::DirectionBit(1);
		}
		WithRewardBranch.OrdinaryCells.Sort(
			[](const FZeroEscapeGeneratedOrdinaryCell& A,
				const FZeroEscapeGeneratedOrdinaryCell& B)
			{
				return A.Coordinate.Z != B.Coordinate.Z
					? A.Coordinate.Z < B.Coordinate.Z
					: A.Coordinate.Y != B.Coordinate.Y
						? A.Coordinate.Y < B.Coordinate.Y
						: A.Coordinate.X < B.Coordinate.X;
			});
		FZeroEscapeGeneratedRewardBranch& RewardBranch =
			WithRewardBranch.RewardBranches.AddDefaulted_GetRef();
		RewardBranch.GatewayCoordinate = FIntVector(2, 4, 0);
		RewardBranch.PathCoordinates = {
			FIntVector(2, 3, 0), FIntVector(3, 3, 0), FIntVector(3, 4, 0)};
		RewardBranch.EndpointCoordinate = RewardBranch.PathCoordinates.Last();
		WithRewardBranch.Floors[0].OrdinaryWalkableCellCount = 5;
		WithRewardBranch.Floors[0].TotalWalkableCellCount = 7;
		WithRewardBranch.Floors[0].RewardBranchCount = 1;
		WithRewardBranch.Floors[0].RewardBranchCellRatio = 3.0 / 5.0;
		const int64 RewardBranchHash =
			FGenerationCore::ComputeCanonicalLayoutHash(WithRewardBranch);
		TestTrue(TEXT("合法奖励支线必须进入非零规范 Hash"), RewardBranchHash != 0);
		TestNotEqual(TEXT("奖励支线必须改变地图身份"),
			RewardBranchHash, BaselineHash);

		FZeroEscapeGeneratedLevelPlan DifferentAlternativeCoverage = Baseline;
		DifferentAlternativeCoverage.Floors[0].AlternativeRouteCoverageRatio = 0.25;
		TestNotEqual(TEXT("替代路线覆盖诊断必须进入 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(DifferentAlternativeCoverage),
			BaselineHash);

		FZeroEscapeGeneratedLevelPlan DuplicateRewardEndpoint = WithRewardBranch;
		const FZeroEscapeGeneratedRewardBranch RepeatedRewardBranch =
			DuplicateRewardEndpoint.RewardBranches[0];
		DuplicateRewardEndpoint.RewardBranches.Add(RepeatedRewardBranch);
		TestEqual(TEXT("重复奖励支线端点必须拒绝 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(DuplicateRewardEndpoint),
			static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan FlowAnchorRewardPath = WithRewardBranch;
		FlowAnchorRewardPath.RewardBranches[0].PathCoordinates[0] =
			FlowAnchorRewardPath.PlayerSpawnCoordinate;
		TestEqual(TEXT("奖励支线 Path 与流程锚点冲突必须拒绝 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(FlowAnchorRewardPath),
			static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan InvalidSpawnFloor = Baseline;
		InvalidSpawnFloor.PlayerSpawnCoordinate.Z = 1;
		TestEqual(TEXT("玩家不在一楼的 Plan 必须拒绝 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(InvalidSpawnFloor), static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan InvalidOrder = Baseline;
		InvalidOrder.OrdinaryCells.Swap(0, 1);
		TestEqual(TEXT("普通格未按 Z/Y/X 稳定排序时必须拒绝 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(InvalidOrder), static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan InvalidConnectionOrder = Baseline;
		InvalidConnectionOrder.Structures[0].InternalConnections.Swap(0, 1);
		TestEqual(TEXT("Structure InternalConnections order must be canonical"),
			FGenerationCore::ComputeCanonicalLayoutHash(InvalidConnectionOrder),
			static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan ReversedConnection = Baseline;
		Swap(
			ReversedConnection.Structures[0].InternalConnections[0].FirstCoordinate,
			ReversedConnection.Structures[0].InternalConnections[0].SecondCoordinate);
		TestEqual(TEXT("Structure InternalConnections endpoints must be canonical"),
			FGenerationCore::ComputeCanonicalLayoutHash(ReversedConnection),
			static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan DuplicateConnection = Baseline;
		const FZeroEscapeGeneratedCellConnection RepeatedConnection =
			DuplicateConnection.Structures[0].InternalConnections.Last();
		DuplicateConnection.Structures[0].InternalConnections.Add(RepeatedConnection);
		TestEqual(TEXT("Duplicate Structure InternalConnections must be rejected"),
			FGenerationCore::ComputeCanonicalLayoutHash(DuplicateConnection),
			static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan InvalidOpeningOrder = Baseline;
		InvalidOpeningOrder.Structures[0].Openings.Swap(0, 1);
		TestEqual(TEXT("Structure Openings order must be canonical"),
			FGenerationCore::ComputeCanonicalLayoutHash(InvalidOpeningOrder),
			static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan DuplicateOpening = Baseline;
		const FZeroEscapeGeneratedStructureOpening RepeatedOpening =
			DuplicateOpening.Structures[0].Openings.Last();
		DuplicateOpening.Structures[0].Openings.Add(RepeatedOpening);
		TestEqual(TEXT("Duplicate Structure Opening IDs must be rejected"),
			FGenerationCore::ComputeCanonicalLayoutHash(DuplicateOpening),
			static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan InvalidLandingOrder = Baseline;
		InvalidLandingOrder.Structures[0].Landings.Swap(0, 1);
		TestEqual(TEXT("Structure Landings order must be canonical"),
			FGenerationCore::ComputeCanonicalLayoutHash(InvalidLandingOrder),
			static_cast<int64>(0));

		FZeroEscapeGeneratedLevelPlan DuplicateLanding = Baseline;
		const FZeroEscapeGeneratedStructureLanding RepeatedLanding =
			DuplicateLanding.Structures[0].Landings.Last();
		DuplicateLanding.Structures[0].Landings.Add(RepeatedLanding);
		TestEqual(TEXT("Duplicate Structure Landing IDs must be rejected"),
			FGenerationCore::ComputeCanonicalLayoutHash(DuplicateLanding),
			static_cast<int64>(0));
		return true;
	}
}

#endif
