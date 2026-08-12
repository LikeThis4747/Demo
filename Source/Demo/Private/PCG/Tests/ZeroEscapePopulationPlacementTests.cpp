// Copyright Epic Games, Inc. All Rights Reserved.

/** 纯值测试：难度合同、高厅必放、图距离、两层随机隔离和原子失败。 */

#include "PCG/Population/ZeroEscapePopulationPlacementPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace
	{
		constexpr uint8 North = static_cast<uint8>(EZeroEscapeOpenEdge::North);
		constexpr uint8 East = static_cast<uint8>(EZeroEscapeOpenEdge::East);
		constexpr uint8 South = static_cast<uint8>(EZeroEscapeOpenEdge::South);
		constexpr uint8 West = static_cast<uint8>(EZeroEscapeOpenEdge::West);

		FZeroEscapeGeneratedOrdinaryCell MakeCell(
			const FIntVector Address,
			const uint8 Openings)
		{
			FZeroEscapeGeneratedOrdinaryCell Cell;
			Cell.Coordinate = Address;
			Cell.OpeningMask = Openings;
			return Cell;
		}

		FZeroEscapeGeneratedLevelPlan MakeLinearPlan(
			const int32 Seed = 240812,
			const bool bIncludeHighHall = true)
		{
			FZeroEscapeGeneratedLevelPlan Plan;
			Plan.Signature.Seed = Seed;
			Plan.Signature.Difficulty = EZeroEscapeDifficulty::Normal;
			Plan.FloorCount = 2;
			Plan.GridSize = FIntPoint(10, 6);
			Plan.LogicalTileSizeCm = 600.0;
			Plan.FloorHeightCm = 450.0;
			Plan.AnchorHeightCm = 100.0;
			for (int32 X = 0; X < 8; ++X)
			{
				uint8 Openings = 0;
				if (X > 0) Openings |= West;
				if (X < 7) Openings |= East;
				Plan.OrdinaryCells.Add(MakeCell(FIntVector(X, 0, 0), Openings));
			}
			for (int32 X = 0; X < 8; ++X)
			{
				uint8 Openings = 0;
				if (X > 0) Openings |= West;
				if (X < 7) Openings |= East;
				Plan.OrdinaryCells.Add(MakeCell(FIntVector(X, 0, 1), Openings));
			}

			FZeroEscapeGeneratedStructure Stair;
			Stair.StableStructureId = 0;
			Stair.Kind = EZeroEscapeStructureKind::TwoFloorStair;
			Stair.BaseCoordinate = FIntVector(8, 0, 0);
			Stair.WalkableCells = {
				FIntVector(8, 0, 0), FIntVector(8, 0, 1) };
			FZeroEscapeGeneratedCellConnection Vertical;
			Vertical.FirstCoordinate = Stair.WalkableCells[0];
			Vertical.SecondCoordinate = Stair.WalkableCells[1];
			Stair.InternalConnections.Add(Vertical);
			for (int32 Floor = 0; Floor < 2; ++Floor)
			{
				FZeroEscapeGeneratedStructureOpening Opening;
				Opening.StructureCoordinate = FIntVector(8, 0, Floor);
				Opening.ConnectedOrdinaryCoordinate = FIntVector(7, 0, Floor);
				Stair.Openings.Add(Opening);
				Plan.OrdinaryCells[Floor * 8 + 7].OpeningMask |= East;
			}
			Plan.Structures.Add(Stair);

			if (bIncludeHighHall)
			{
				FZeroEscapeGeneratedStructure Hall;
				Hall.StableStructureId = 1;
				Hall.Kind = EZeroEscapeStructureKind::HighCeilingRoom;
				Hall.BaseCoordinate = FIntVector(3, 1, 0);
				Hall.WalkableCells = {
					FIntVector(3, 1, 0), FIntVector(4, 1, 0) };
				FZeroEscapeGeneratedCellConnection Connection;
				Connection.FirstCoordinate = Hall.WalkableCells[0];
				Connection.SecondCoordinate = Hall.WalkableCells[1];
				Hall.InternalConnections.Add(Connection);
				FZeroEscapeGeneratedStructureOpening Opening;
				Opening.StructureCoordinate = Hall.WalkableCells[0];
				Opening.ConnectedOrdinaryCoordinate = FIntVector(3, 0, 0);
				Hall.Openings.Add(Opening);
				Plan.OrdinaryCells[3].OpeningMask |= North;
				Plan.Structures.Add(Hall);
			}
			Plan.PlayerSpawnCoordinate = FIntVector(0, 0, 0);
			Plan.PursuerSpawnCoordinate = FIntVector(0, 0, 1);
			Plan.ExitCoordinate = FIntVector(7, 0, 1);
			return Plan;
		}

		FZeroEscapeGeneratedLevelPlan MakeKnownCrossFloorDistancePlan()
		{
			FZeroEscapeGeneratedLevelPlan Plan;
			Plan.Signature.Seed = 240812;
			Plan.Signature.Difficulty = EZeroEscapeDifficulty::Normal;
			Plan.FloorCount = 2;
			Plan.GridSize = FIntPoint(10, 6);
			Plan.LogicalTileSizeCm = 600.0;
			Plan.FloorHeightCm = 450.0;
			Plan.OrdinaryCells = {
				MakeCell(FIntVector(0, 1, 0), North | East | South),
				MakeCell(FIntVector(0, 2, 0), South),
				MakeCell(FIntVector(0, 0, 0), North),
				MakeCell(FIntVector(0, 1, 1), North | East),
				MakeCell(FIntVector(0, 2, 1), South) };

			FZeroEscapeGeneratedStructure Stair;
			Stair.StableStructureId = 0;
			Stair.Kind = EZeroEscapeStructureKind::TwoFloorStair;
			Stair.BaseCoordinate = FIntVector(1, 1, 0);
			Stair.WalkableCells = {
				FIntVector(1, 1, 0), FIntVector(1, 1, 1) };
			FZeroEscapeGeneratedCellConnection Vertical;
			Vertical.FirstCoordinate = Stair.WalkableCells[0];
			Vertical.SecondCoordinate = Stair.WalkableCells[1];
			Stair.InternalConnections.Add(Vertical);
			for (int32 Floor = 0; Floor < 2; ++Floor)
			{
				FZeroEscapeGeneratedStructureOpening Opening;
				Opening.StructureCoordinate = FIntVector(1, 1, Floor);
				Opening.ConnectedOrdinaryCoordinate = FIntVector(0, 1, Floor);
				Stair.Openings.Add(Opening);
			}
			Plan.Structures.Add(Stair);
			Plan.PlayerSpawnCoordinate = FIntVector(0, 2, 0);
			Plan.PursuerSpawnCoordinate = FIntVector(0, 0, 0);
			Plan.ExitCoordinate = FIntVector(0, 2, 1);
			return Plan;
		}

		FZeroEscapeGeneratedLevelPlan MakeSingleClosedWallPlan()
		{
			FZeroEscapeGeneratedLevelPlan Plan;
			Plan.Signature.Seed = 240812;
			Plan.Signature.Difficulty = EZeroEscapeDifficulty::Normal;
			Plan.FloorCount = 2;
			Plan.GridSize = FIntPoint(10, 6);
			Plan.LogicalTileSizeCm = 600.0;
			Plan.FloorHeightCm = 450.0;
			Plan.OrdinaryCells = {
				MakeCell(FIntVector(1, 1, 0), North | East | South),
				MakeCell(FIntVector(1, 2, 0), South),
				MakeCell(FIntVector(2, 1, 0), West | East),
				MakeCell(FIntVector(1, 0, 0), North) };

			FZeroEscapeGeneratedStructure Stair;
			Stair.StableStructureId = 0;
			Stair.Kind = EZeroEscapeStructureKind::TwoFloorStair;
			Stair.BaseCoordinate = FIntVector(3, 1, 0);
			Stair.WalkableCells = {
				FIntVector(3, 1, 0), FIntVector(3, 1, 1) };
			FZeroEscapeGeneratedCellConnection Vertical;
			Vertical.FirstCoordinate = Stair.WalkableCells[0];
			Vertical.SecondCoordinate = Stair.WalkableCells[1];
			Stair.InternalConnections.Add(Vertical);
			FZeroEscapeGeneratedStructureOpening Opening;
			Opening.StructureCoordinate = Stair.WalkableCells[0];
			Opening.ConnectedOrdinaryCoordinate = FIntVector(2, 1, 0);
			Stair.Openings.Add(Opening);
			Plan.Structures.Add(Stair);
			Plan.PlayerSpawnCoordinate = FIntVector(1, 2, 0);
			Plan.PursuerSpawnCoordinate = FIntVector(2, 1, 0);
			Plan.ExitCoordinate = FIntVector(1, 0, 0);
			return Plan;
		}

		TArray<FZeroEscapePopulationDifficultySettings> MakeDifficulties(
			const float HazardDensity = 20.0f,
			const float ResourceDensity = 20.0f)
		{
			TArray<FZeroEscapePopulationDifficultySettings> Result;
			for (const EZeroEscapeDifficulty Difficulty : {
				EZeroEscapeDifficulty::Easy,
				EZeroEscapeDifficulty::Normal,
				EZeroEscapeDifficulty::Hard })
			{
				FZeroEscapePopulationDifficultySettings& Entry = Result.AddDefaulted_GetRef();
				Entry.Difficulty = Difficulty;
				Entry.Hazards.ExpectedHazardsPer100GameplayCells = HazardDensity;
				Entry.Hazards.MinimumRouteSpacingTiles = 2;
				Entry.Resources.ExpectedResourcesPer100GameplayCells = ResourceDensity;
				Entry.Resources.MinimumRouteSpacingTiles = 2;
			}
			return Result;
		}

		FZeroEscapeHazardPopulationAssembly MakeHazards()
		{
			FZeroEscapeHazardPopulationAssembly Result;
			Result.SpikeTrapActorCount = 2;
			Result.SpikeTrapLateralSpacingCm = 300.0f;
			Result.BatteringRamWallInsetCm = 50.0f;
			Result.BatteringRamMountHeightCm = 100.0f;
			Result.GuidedLauncherWallInsetCm = 0.0f;
			Result.GuidedLauncherMountHeightCm = 100.0f;
			return Result;
		}

		FZeroEscapeResourcePopulationAssembly MakeResources()
		{
			FZeroEscapeResourcePopulationAssembly Result;
			Result.SpawnZOffsetCm = 50.0f;
			Result.PlacementFootprintRadiusCm = 75.0f;
			return Result;
		}

		bool PlacementsEqual(
			const TArray<FPopulationPlannedPlacement>& First,
			const TArray<FPopulationPlannedPlacement>& Second)
		{
			if (First.Num() != Second.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < First.Num(); ++Index)
			{
				if (First[Index].Kind != Second[Index].Kind
					|| First[Index].AnchorAddress != Second[Index].AnchorAddress
					|| First[Index].LocalSpawnTransforms.Num()
						!= Second[Index].LocalSpawnTransforms.Num()
					|| First[Index].ResourceBlockedAddresses
						!= Second[Index].ResourceBlockedAddresses)
				{
					return false;
				}
				for (int32 TransformIndex = 0;
					TransformIndex < First[Index].LocalSpawnTransforms.Num();
					++TransformIndex)
				{
					if (!First[Index].LocalSpawnTransforms[TransformIndex].Equals(
							Second[Index].LocalSpawnTransforms[TransformIndex], 0.001))
					{
						return false;
					}
				}
			}
			return true;
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationConfigurationTest,
		"Demo.PCG.Population.Configuration",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationConfigurationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FZeroEscapeGeneratedLevelPlan Plan = MakeLinearPlan();
		const FZeroEscapeHazardPopulationAssembly Hazards = MakeHazards();
		const FZeroEscapeResourcePopulationAssembly Resources = MakeResources();
		TArray<FZeroEscapePopulationDifficultySettings> Difficulties = MakeDifficulties();
		FPopulationPlacementPlan Result;
		FString Error;
		TestTrue(TEXT("完整三档配置成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::Success);
		const FPopulationPlacementPlan Ordered = Result;
		Difficulties.Swap(0, 2);
		TestTrue(TEXT("难度数组换序成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::Success);
		TestTrue(TEXT("难度数组顺序不改变机关"),
			PlacementsEqual(Ordered.HazardPlacements, Result.HazardPlacements));
		TestTrue(TEXT("难度数组顺序不改变资源"),
			PlacementsEqual(Ordered.ResourcePlacements, Result.ResourcePlacements));

		Difficulties.Pop();
		TestTrue(TEXT("缺档必须拒绝"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::InvalidConfiguration);
		TestTrue(TEXT("配置失败保持空计划"),
			Result.HazardPlacements.IsEmpty() && Result.ResourcePlacements.IsEmpty());

		FZeroEscapeHazardPopulationAssembly DisabledSpikeHazards = Hazards;
		DisabledSpikeHazards.SpikeTrapActorCount = MAX_int32;
		DisabledSpikeHazards.SpikeTrapLateralSpacingCm = 0.0f;
		Difficulties = MakeDifficulties(20.0f, 0.0f);
		for (FZeroEscapePopulationDifficultySettings& Difficulty : Difficulties)
		{
			Difficulty.Hazards.SpikeTrapWeight = 0;
			Difficulty.Hazards.BatteringRamWeight = 1;
			Difficulty.Hazards.GuidedLauncherWeight = 0;
		}
		TestTrue(TEXT("已禁用的地刺数量不虚构 Actor 预算"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, DisabledSpikeHazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::Success);

		FZeroEscapeHazardPopulationAssembly LauncherHazards = Hazards;
		LauncherHazards.SpikeTrapActorCount = 1;
		LauncherHazards.SpikeTrapLateralSpacingCm = 0.0f;
		Difficulties = MakeDifficulties(20000.0f, 0.0f);
		for (FZeroEscapePopulationDifficultySettings& Difficulty : Difficulties)
		{
			Difficulty.Hazards.SpikeTrapWeight = 0;
			Difficulty.Hazards.BatteringRamWeight = 0;
			Difficulty.Hazards.GuidedLauncherWeight = 1;
		}
		TestTrue(TEXT("发射器的预装弹体必须计入世界 Actor 预算"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, LauncherHazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::InvalidConfiguration);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationLayeringTest,
		"Demo.PCG.Population.LayeringAndDeterminism",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationLayeringTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FZeroEscapeGeneratedLevelPlan Plan = MakeLinearPlan();
		const FZeroEscapeHazardPopulationAssembly Hazards = MakeHazards();
		FZeroEscapeResourcePopulationAssembly Resources = MakeResources();
		TArray<FZeroEscapePopulationDifficultySettings> Difficulties = MakeDifficulties();
		FPopulationPlacementPlan First;
		FPopulationPlacementPlan Replay;
		FString Error;
		TestTrue(TEXT("首轮规划成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, First, Error)
				== EPopulationPlacementResult::Success);
		TestTrue(TEXT("同 Seed 重放成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Replay, Error)
				== EPopulationPlacementResult::Success);
		TestEqual(TEXT("合法高厅恰好一个摆锤"), First.KindCounts.Pendulums, 1);
		TestEqual(TEXT("摆锤计入机关实际数"),
			First.HazardStats.ActualCount, First.HazardPlacements.Num());
		TestTrue(TEXT("同 Seed 机关完全复现"),
			PlacementsEqual(First.HazardPlacements, Replay.HazardPlacements));
		TestTrue(TEXT("同 Seed 资源完全复现"),
			PlacementsEqual(First.ResourcePlacements, Replay.ResourcePlacements));

		Resources.SpawnZOffsetCm = 80.0f;
		Difficulties[1].Resources.ExpectedResourcesPer100GameplayCells = 35.0f;
		FPopulationPlacementPlan ResourceChanged;
		TestTrue(TEXT("修改资源参数仍成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, ResourceChanged, Error)
				== EPopulationPlacementResult::Success);
		TestTrue(TEXT("资源参数不洗牌机关"),
			PlacementsEqual(First.HazardPlacements, ResourceChanged.HazardPlacements));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationMultiFloorSpacingBoundaryTest,
		"Demo.PCG.Population.MultiFloorSpacingBoundary",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationMultiFloorSpacingBoundaryTest::RunTest(
		const FString& Parameters)
	{
		(void)Parameters;
		const FZeroEscapeGeneratedLevelPlan Plan = MakeKnownCrossFloorDistancePlan();
		const FZeroEscapeHazardPopulationAssembly Hazards = MakeHazards();
		const FZeroEscapeResourcePopulationAssembly Resources = MakeResources();
		TArray<FZeroEscapePopulationDifficultySettings> Difficulties =
			MakeDifficulties(0.0f, 40.0f);
		Difficulties[1].Resources.MinimumRouteSpacingTiles = 3;
		FPopulationPlacementPlan Result;
		FString Error;
		TestTrue(TEXT("跨层图距离等于半径时放满"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::Success);
		TestEqual(TEXT("跨层间距 3 的资源目标"), Result.ResourceStats.TargetCount, 2);
		TestEqual(TEXT("跨层距离 3 允许两个锚点"), Result.ResourceStats.ActualCount, 2);
		TSet<FIntVector> Anchors;
		for (const FPopulationPlannedPlacement& Placement : Result.ResourcePlacements)
		{
			Anchors.Add(Placement.AnchorAddress);
		}
		TestTrue(TEXT("低层锚点被选中"), Anchors.Contains(FIntVector(0, 1, 0)));
		TestTrue(TEXT("高层锚点被选中"), Anchors.Contains(FIntVector(0, 1, 1)));

		Difficulties[1].Resources.MinimumRouteSpacingTiles = 4;
		TestTrue(TEXT("跨层半径 4 的欠填仍成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::Success);
		TestEqual(TEXT("跨层半径 4 只保留一个"), Result.ResourceStats.ActualCount, 1);
		TestEqual(TEXT("跨层半径 4 精确欠填一个"), Result.ResourceStats.UnderfilledCount, 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationTypeFirstSelectionTest,
		"Demo.PCG.Population.TypeFirstSelection",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationTypeFirstSelectionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FZeroEscapeGeneratedLevelPlan Plan = MakeLinearPlan(0, false);
		Plan.PlayerSpawnCoordinate = FIntVector(3, 0, 0);
		Plan.PursuerSpawnCoordinate = FIntVector(4, 0, 0);
		Plan.ExitCoordinate = FIntVector(3, 0, 1);
		const FZeroEscapeHazardPopulationAssembly Hazards = MakeHazards();
		const FZeroEscapeResourcePopulationAssembly Resources = MakeResources();
		TArray<FZeroEscapePopulationDifficultySettings> Difficulties =
			MakeDifficulties(6.25f, 0.0f);
		for (FZeroEscapePopulationDifficultySettings& Difficulty : Difficulties)
		{
			Difficulty.Hazards.MinimumRouteSpacingTiles = 1;
			Difficulty.Hazards.SpikeTrapWeight = 1;
			Difficulty.Hazards.BatteringRamWeight = 0;
			Difficulty.Hazards.GuidedLauncherWeight = 1;
		}

		int32 LauncherSelections = 0;
		for (int32 SeedIndex = 0; SeedIndex < 256; ++SeedIndex)
		{
			Plan.Signature.Seed = 810000 + SeedIndex;
			FPopulationPlacementPlan Result;
			FString Error;
			if (!TestTrue(TEXT("类型先抽 Seed 规划成功"),
				FPopulationPlacementPolicy::BuildPlan(
					Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
					== EPopulationPlacementResult::Success))
			{
				continue;
			}
			TestEqual(TEXT("每个 Seed 只选一处普通机关"), Result.HazardPlacements.Num(), 1);
			if (SeedIndex == 0)
			{
				TestEqual(TEXT("地刺候选明显更多"), Result.KindCounts.SpikeCandidateAnchors, 13);
				TestEqual(TEXT("发射器候选保持稀缺"), Result.KindCounts.LauncherCandidateAnchors, 2);
			}
			if (!Result.HazardPlacements.IsEmpty()
				&& Result.HazardPlacements[0].Kind
					== EPopulationPlacementKind::GuidedLauncher)
			{
				++LauncherSelections;
			}
		}
		TestTrue(TEXT("相同类型权重不被候选数量吞没"),
			LauncherSelections >= 64 && LauncherSelections <= 192);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationHazardFacingContractTest,
		"Demo.PCG.Population.HazardFacingAndFlowExclusion",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationHazardFacingContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FZeroEscapeGeneratedLevelPlan Plan = MakeSingleClosedWallPlan();
		const FZeroEscapeHazardPopulationAssembly Hazards = MakeHazards();
		const FZeroEscapeResourcePopulationAssembly Resources = MakeResources();
		TArray<FZeroEscapePopulationDifficultySettings> Difficulties =
			MakeDifficulties(25.0f, 0.0f);
		for (FZeroEscapePopulationDifficultySettings& Difficulty : Difficulties)
		{
			Difficulty.Hazards.MinimumRouteSpacingTiles = 1;
			Difficulty.Hazards.SpikeTrapWeight = 0;
			Difficulty.Hazards.BatteringRamWeight = 1;
			Difficulty.Hazards.GuidedLauncherWeight = 0;
		}
		FPopulationPlacementPlan Result;
		FString Error;
		TestTrue(TEXT("单闭墙冲锤规划成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::Success);
		TestEqual(TEXT("单闭墙恰好一处冲锤"), Result.KindCounts.BatteringRams, 1);
		if (!Result.HazardPlacements.IsEmpty())
		{
			const FTransform& Transform = Result.HazardPlacements[0].LocalSpawnTransforms[0];
			TestTrue(TEXT("冲锤安装在西侧闭墙内侧"),
				Transform.GetLocation().Equals(FVector(350.0, 600.0, 100.0), 0.01));
			TestTrue(TEXT("冲锤局部 +X 从闭墙朝向格心"),
				Transform.GetRotation().GetForwardVector().Equals(FVector::ForwardVector, 0.001));
		}

		for (FZeroEscapePopulationDifficultySettings& Difficulty : Difficulties)
		{
			Difficulty.Hazards.BatteringRamWeight = 0;
			Difficulty.Hazards.GuidedLauncherWeight = 1;
		}
		TestTrue(TEXT("正前格是流程点时规划仍正常返回"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::Success);
		TestEqual(TEXT("流程点前方不建立发射器候选"),
			Result.KindCounts.LauncherCandidateAnchors, 0);
		TestEqual(TEXT("流程点前方不能实际生成发射器"),
			Result.KindCounts.GuidedLaunchers, 0);
		TestEqual(TEXT("无合法发射器时准确欠填"), Result.HazardStats.UnderfilledCount, 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationHazardOperationFootprintTest,
		"Demo.PCG.Population.HazardOperationFootprints",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationHazardOperationFootprintTest::RunTest(
		const FString& Parameters)
	{
		(void)Parameters;
		FZeroEscapeGeneratedLevelPlan Plan = MakeLinearPlan(0, false);
		Plan.PlayerSpawnCoordinate = FIntVector(3, 0, 0);
		Plan.PursuerSpawnCoordinate = FIntVector(4, 0, 0);
		Plan.ExitCoordinate = FIntVector(3, 0, 1);
		const FZeroEscapeHazardPopulationAssembly Hazards = MakeHazards();
		const FZeroEscapeResourcePopulationAssembly Resources = MakeResources();
		TArray<FZeroEscapePopulationDifficultySettings> Difficulties =
			MakeDifficulties(100.0f, 0.0f);
		for (FZeroEscapePopulationDifficultySettings& Difficulty : Difficulties)
		{
			Difficulty.Hazards.MinimumRouteSpacingTiles = 1;
			Difficulty.Hazards.SpikeTrapWeight = 0;
			Difficulty.Hazards.BatteringRamWeight = 1;
			Difficulty.Hazards.GuidedLauncherWeight = 100;
		}

		int32 TotalLauncherCount = 0;
		for (int32 SeedIndex = 0; SeedIndex < 64; ++SeedIndex)
		{
			Plan.Signature.Seed = 820000 + SeedIndex;
			FPopulationPlacementPlan Result;
			FString Error;
			if (!TestTrue(TEXT("机关操作格多 Seed 规划成功"),
				FPopulationPlacementPolicy::BuildPlan(
					Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
					== EPopulationPlacementResult::Success))
			{
				continue;
			}

			TotalLauncherCount += Result.KindCounts.GuidedLaunchers;
			TSet<FIntVector> AcceptedOperationAddresses;
			for (int32 PlacementIndex = 0;
				PlacementIndex < Result.HazardPlacements.Num();
				++PlacementIndex)
			{
				const FPopulationPlannedPlacement& Placement =
					Result.HazardPlacements[PlacementIndex];
				for (const FIntVector Address : Placement.ResourceBlockedAddresses)
				{
					const FString Assertion = FString::Printf(
						TEXT("Seed %d 的第 %d 处机关不得复用实际操作格 %s"),
						Plan.Signature.Seed,
						PlacementIndex,
						*Address.ToString());
					TestFalse(*Assertion, AcceptedOperationAddresses.Contains(Address));
					AcceptedOperationAddresses.Add(Address);
				}
			}
		}
		TestTrue(TEXT("固定 Seed 集必须实际覆盖发射器前方操作格"),
			TotalLauncherCount > 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationResourceBoundsTest,
		"Demo.PCG.Population.ResourceBoundsAndUnderfill",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationResourceBoundsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FZeroEscapeGeneratedLevelPlan Plan = MakeLinearPlan(240900, false);
		const FZeroEscapeHazardPopulationAssembly Hazards = MakeHazards();
		const FZeroEscapeResourcePopulationAssembly Resources = MakeResources();
		TArray<FZeroEscapePopulationDifficultySettings> Difficulties = MakeDifficulties(0.0f, 100.0f);
		Difficulties[1].Resources.MinimumRouteSpacingTiles = 20;
		FPopulationPlacementPlan Result;
		FString Error;
		TestTrue(TEXT("候选不足仍是成功结果"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error)
				== EPopulationPlacementResult::Success);
		TestTrue(TEXT("资源欠填被统计"), Result.ResourceStats.UnderfilledCount > 0);
		for (const FPopulationPlannedPlacement& Placement : Result.ResourcePlacements)
		{
			TestTrue(TEXT("资源避开玩家"), Placement.AnchorAddress != Plan.PlayerSpawnCoordinate);
			TestTrue(TEXT("资源避开追猎者"), Placement.AnchorAddress != Plan.PursuerSpawnCoordinate);
			TestTrue(TEXT("资源避开 Exit"), Placement.AnchorAddress != Plan.ExitCoordinate);
			const FVector Location = Placement.LocalSpawnTransforms[0].GetLocation();
			const FVector Center(
				Placement.AnchorAddress.X * Plan.LogicalTileSizeCm,
				Placement.AnchorAddress.Y * Plan.LogicalTileSizeCm,
				Placement.AnchorAddress.Z * Plan.FloorHeightCm);
			const double SafeRange = Plan.LogicalTileSizeCm * 0.5
				- Resources.PlacementFootprintRadiusCm;
			TestTrue(TEXT("资源 X 在格内安全范围"),
				FMath::Abs(Location.X - Center.X) <= SafeRange + 0.01);
			TestTrue(TEXT("资源 Y 在格内安全范围"),
				FMath::Abs(Location.Y - Center.Y) <= SafeRange + 0.01);
			TestTrue(TEXT("资源保持 Identity 旋转"),
				Placement.LocalSpawnTransforms[0].GetRotation().Equals(FQuat::Identity));
		}

		Difficulties = MakeDifficulties(0.0f, 50.0f);
		Difficulties[1].Resources.MinimumRouteSpacingTiles = 2;
		FPopulationPlacementPlan Jittered;
		TestTrue(TEXT("多资源格内随机规划成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Jittered, Error)
				== EPopulationPlacementResult::Success);
		TestTrue(TEXT("夹具必须选出多个资源"),
			Jittered.ResourcePlacements.Num() >= 2);
		bool bFoundNonCentered = false;
		bool bFoundDifferentOffset = false;
		FVector2D FirstOffset = FVector2D::ZeroVector;
		for (int32 Index = 0; Index < Jittered.ResourcePlacements.Num(); ++Index)
		{
			const FPopulationPlannedPlacement& Placement = Jittered.ResourcePlacements[Index];
			const FVector Location = Placement.LocalSpawnTransforms[0].GetLocation();
			const FVector Center(
				Placement.AnchorAddress.X * Plan.LogicalTileSizeCm,
				Placement.AnchorAddress.Y * Plan.LogicalTileSizeCm,
				Placement.AnchorAddress.Z * Plan.FloorHeightCm);
			const FVector2D Offset(Location.X - Center.X, Location.Y - Center.Y);
			if (Index == 0)
			{
				FirstOffset = Offset;
			}
			else if (!Offset.Equals(FirstOffset, 0.01))
			{
				bFoundDifferentOffset = true;
			}
			bFoundNonCentered |= FMath::Abs(Offset.X) > 1.0 || FMath::Abs(Offset.Y) > 1.0;
			TestTrue(TEXT("资源 Z 偏移精确来自装配数据"),
				FMath::IsNearlyEqual(
					Location.Z - Center.Z,
					static_cast<double>(Resources.SpawnZOffsetCm),
					0.01));
		}
		TestTrue(TEXT("资源不再全部固定在格心"), bFoundNonCentered);
		TestTrue(TEXT("同局多资源不共用一个固定 XY 偏移"), bFoundDifferentOffset);

		FZeroEscapeResourcePopulationAssembly AdjustedResources = Resources;
		AdjustedResources.SpawnZOffsetCm = 80.0f;
		AdjustedResources.PlacementFootprintRadiusCm = 225.0f;
		FPopulationPlacementPlan AssemblyChanged;
		TestTrue(TEXT("修改格内装配后规划成功"),
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, AdjustedResources, Difficulties, AssemblyChanged, Error)
				== EPopulationPlacementResult::Success);
		TestEqual(TEXT("格内装配不改变资源数量"),
			AssemblyChanged.ResourcePlacements.Num(), Jittered.ResourcePlacements.Num());
		for (int32 Index = 0;
			Index < FMath::Min(
				AssemblyChanged.ResourcePlacements.Num(), Jittered.ResourcePlacements.Num());
			++Index)
		{
			const FPopulationPlannedPlacement& Baseline = Jittered.ResourcePlacements[Index];
			const FPopulationPlannedPlacement& Adjusted = AssemblyChanged.ResourcePlacements[Index];
			TestTrue(TEXT("格内装配不洗牌宏观锚点"),
				Baseline.AnchorAddress == Adjusted.AnchorAddress);
			const FVector AdjustedLocation = Adjusted.LocalSpawnTransforms[0].GetLocation();
			const FVector AdjustedCenter(
				Adjusted.AnchorAddress.X * Plan.LogicalTileSizeCm,
				Adjusted.AnchorAddress.Y * Plan.LogicalTileSizeCm,
				Adjusted.AnchorAddress.Z * Plan.FloorHeightCm);
			const double AdjustedSafeRange = Plan.LogicalTileSizeCm * 0.5
				- AdjustedResources.PlacementFootprintRadiusCm;
			TestTrue(TEXT("收紧后资源 X 仍在安全区"),
				FMath::Abs(AdjustedLocation.X - AdjustedCenter.X) <= AdjustedSafeRange + 0.01);
			TestTrue(TEXT("收紧后资源 Y 仍在安全区"),
				FMath::Abs(AdjustedLocation.Y - AdjustedCenter.Y) <= AdjustedSafeRange + 0.01);
			TestTrue(TEXT("收紧后 Z 偏移正确"),
				FMath::IsNearlyEqual(
					AdjustedLocation.Z - AdjustedCenter.Z,
					static_cast<double>(AdjustedResources.SpawnZOffsetCm),
					0.01));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationInvalidHallTest,
		"Demo.PCG.Population.InvalidHighHall",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationInvalidHallTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FZeroEscapeGeneratedLevelPlan Plan = MakeLinearPlan();
		Plan.Structures[1].InternalConnections.Reset();
		const FZeroEscapeHazardPopulationAssembly Hazards = MakeHazards();
		const FZeroEscapeResourcePopulationAssembly Resources = MakeResources();
		const TArray<FZeroEscapePopulationDifficultySettings> Difficulties = MakeDifficulties();
		FPopulationPlacementPlan Result;
		FString Error;
		const EPopulationPlacementResult PlacementResult =
			FPopulationPlacementPolicy::BuildPlan(
				Plan, 0.0, Hazards, Resources, Difficulties, Result, Error);
		TestTrue(TEXT("畸形高厅必须拒绝"),
			PlacementResult == EPopulationPlacementResult::InvalidTraversalGraph
			|| PlacementResult == EPopulationPlacementResult::InvalidPlan);
		TestTrue(TEXT("畸形高厅失败保持空计划"),
			Result.HazardPlacements.IsEmpty() && Result.ResourcePlacements.IsEmpty());
		return true;
	}
}

#endif
