// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcLayoutTests.cpp
 *
 * 职责：验证 V5 Grid-WFC 的 16 状态契约、Count/MaxConsecutive/Connected 传播、
 *       带界时间顺序回溯、同 Seed 重放、Grid 完成态验收与输出原子性。
 * 边界：测试只操作纯值 Domain/Plan，不创建 World、Actor、HISM，也不执行 PIE。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/ArrayView.h"
#include "Containers/Queue.h"
#include "Containers/StaticArray.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeGridLayoutSolver.h"
#include "PCG/ZeroEscapeWfcConstraints.h"
#include "PCG/ZeroEscapeWfcSolver.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace WfcLayoutTestsPrivate
	{
		/**
		 * 返回已观测样本的 nearest-rank 百分位。
		 *
		 * 复制后排序让调用端保留原始收集顺序；N=288 时 P50/P95 都有稳定、无插值的样本定义。
		 */
		template <typename TValue>
		TValue NearestRankPercentile(TArray<TValue> Values, const double Percentile)
		{
			check(!Values.IsEmpty());
			check(Percentile > 0.0 && Percentile <= 1.0);
			Values.Sort();
			const int32 RankIndex = FMath::Clamp(
				FMath::CeilToInt(Percentile * Values.Num()) - 1,
				0,
				Values.Num() - 1);
			return Values[RankIndex];
		}

		/** 构造 V5 自动化所用的合法空间 Profile；三档难度保持等总权重。 */
		void BuildValidProfile(UZeroEscapeLevelGenerationProfile& Profile)
		{
			Profile.ProfileVersion = 5;
			Profile.SharedRouteConstraints = FZeroEscapeSharedRouteConstraints();
			Profile.SharedRouteConstraints.GridSize = FIntPoint(24, 16);
			Profile.SharedRouteConstraints.LogicalTileSizeCm = 600.0;
			Profile.SharedRouteConstraints.RoomSizeTiles = 2;
			Profile.SharedRouteConstraints.RoomCount = 3;
			Profile.SharedRouteConstraints.MinWalkableCellCount = 48;
			Profile.SharedRouteConstraints.MaxWalkableCellCount = 72;
			Profile.SharedRouteConstraints.MaxConsecutiveStraightTiles = 4;
			Profile.SharedRouteConstraints.MaxRequiredRouteLengthTiles = 64;
			Profile.SharedRouteConstraints.MaxWfcCandidateAttempts = 100000;
			Profile.SharedRouteConstraints.MaxWfcBacktrackCount = 25000;
			Profile.SharedRouteConstraints.MaxWfcSolveAttempts = 10;
			Profile.SharedRouteConstraints.AnchorHeightCm = 100.0;

			Profile.Difficulties.Reset();
			FZeroEscapeDifficultyDefinition Easy;
			Easy.Difficulty = EZeroEscapeDifficulty::Easy;
			Easy.WfcShapeWeights = FZeroEscapeWfcShapeWeights();
			Profile.Difficulties.Add(Easy);

			FZeroEscapeDifficultyDefinition Normal = Easy;
			Normal.Difficulty = EZeroEscapeDifficulty::Normal;
			Profile.Difficulties.Add(Normal);

			FZeroEscapeDifficultyDefinition Hard = Easy;
			Hard.Difficulty = EZeroEscapeDifficulty::Hard;
			Profile.Difficulties.Add(Hard);
		}

		/** 构造供 Grid 单元测试直接消费的稳定 Signature。 */
		FZeroEscapeGenerationSignature MakeSignature(
			const int32 Seed,
			const EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal)
		{
			FZeroEscapeGenerationSignature Signature;
			Signature.Seed = Seed;
			Signature.Difficulty = Difficulty;
			Signature.AlgorithmVersion = GAlgorithmVersion;
			Signature.GenerationProfileVersion = 5;
			Signature.PresentationVersion = 1;
			return Signature;
		}

		/** 建立完整 row-major 约束数组；调用者随后按需要提升 Required 或 Outside。 */
		TArray<FGridCellConstraint> MakeDenseConstraints(
			const FIntPoint GridSize,
			const EGridCellDomain InitialDomain)
		{
			TArray<FGridCellConstraint> Constraints;
			Constraints.SetNum(GridSize.X * GridSize.Y);
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < GridSize.X; ++X)
				{
					FGridCellConstraint& Cell = Constraints[Grid::ToIndex(FIntPoint(X, Y), GridSize)];
					Cell.Coordinate = FIntPoint(X, Y);
					Cell.Domain = InitialDomain;
					Cell.RegionId = InitialDomain == EGridCellDomain::Outside ? INDEX_NONE : 0;
				}
			}
			return Constraints;
		}

		/** 把四邻域两格提升为 Required，并双向写入同一条必开边。 */
		bool AddRequiredEdge(
			const FIntPoint GridSize,
			const FIntPoint A,
			const FIntPoint B,
			TArray<FGridCellConstraint>& Constraints)
		{
			uint8 Direction = Grid::DirectionCount;
			for (uint8 Candidate = 0; Candidate < Grid::DirectionCount; ++Candidate)
			{
				if (Grid::Step(A, Candidate) == B)
				{
					Direction = Candidate;
					break;
				}
			}
			if (Direction >= Grid::DirectionCount
				|| !Grid::IsInside(A, GridSize)
				|| !Grid::IsInside(B, GridSize))
			{
				return false;
			}

			FGridCellConstraint& CellA = Constraints[Grid::ToIndex(A, GridSize)];
			FGridCellConstraint& CellB = Constraints[Grid::ToIndex(B, GridSize)];
			CellA.Domain = EGridCellDomain::Required;
			CellB.Domain = EGridCellDomain::Required;
			CellA.RegionId = 0;
			CellB.RegionId = 0;
			CellA.RequiredOpenMask |= Grid::DirectionBit(Direction);
			CellB.RequiredOpenMask |= Grid::DirectionBit(Grid::OppositeDirectionIndex(Direction));
			return true;
		}

		/** 构造“Outside 包围一条三格 Required 直线”的唯一解夹具。 */
		TArray<FGridCellConstraint> MakeConstructiveWfcFixture(const FIntPoint GridSize)
		{
			TArray<FGridCellConstraint> Constraints =
				MakeDenseConstraints(GridSize, EGridCellDomain::Outside);
			AddRequiredEdge(GridSize, FIntPoint(1, 1), FIntPoint(2, 1), Constraints);
			AddRequiredEdge(GridSize, FIntPoint(2, 1), FIntPoint(3, 1), Constraints);
			return Constraints;
		}

		/** 把固定 TStaticArray 状态集复制成 Solve 接收的稳定 TArray。 */
		TArray<FTileVariant> MakeCanonicalVariantArray(const FZeroEscapeWfcShapeWeights& Weights)
		{
			TStaticArray<FTileVariant, 16> StaticVariants;
			FWfcSolver::BuildCanonicalVariants(Weights, StaticVariants);
			TArray<FTileVariant> Variants;
			Variants.Reserve(StaticVariants.Num());
			for (const FTileVariant& Variant : StaticVariants)
			{
				Variants.Add(Variant);
			}
			return Variants;
		}

		/** 构造小夹具使用的合法 WFC 设置；每个测试再收紧 Count 边界。 */
		FZeroEscapeWfcSolveSettings MakeWfcSettings(
			const FIntPoint StartCoordinate,
			const int32 MinWalkable,
			const int32 MaxWalkable,
			const int32 MaxConsecutive)
		{
			FZeroEscapeWfcSolveSettings Settings;
			Settings.StartCoordinate = StartCoordinate;
			Settings.MinWalkableCellCount = MinWalkable;
			Settings.MaxWalkableCellCount = MaxWalkable;
			Settings.MaxConsecutiveStraightTiles = MaxConsecutive;
			Settings.MaxCandidateAttempts = 100000;
			Settings.MaxBacktrackCount = 25000;
			return Settings;
		}

		/** 把 OpeningMask 转为 FWfcDomain 中对应的唯一 Variant bit。 */
		constexpr FWfcDomain VariantBit(const uint8 OpeningMask)
		{
			return static_cast<FWfcDomain>(1u << OpeningMask);
		}

		/** 验证稠密 WFC 输出的边界关闭和每条公共边镜像一致。 */
		bool AreDenseOpeningMasksConsistent(
			const FIntPoint GridSize,
			const TConstArrayView<uint8> Masks)
		{
			if (Masks.Num() != GridSize.X * GridSize.Y)
			{
				return false;
			}
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < GridSize.X; ++X)
				{
					const FIntPoint Coordinate(X, Y);
					const uint8 Mask = Masks[Grid::ToIndex(Coordinate, GridSize)];
					for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
					{
						const bool bOpen = (Mask & Grid::DirectionBit(Direction)) != 0;
						const FIntPoint Neighbor = Grid::Step(Coordinate, Direction);
						if (!Grid::IsInside(Neighbor, GridSize))
						{
							if (bOpen)
							{
								return false;
							}
							continue;
						}
						const uint8 NeighborMask = Masks[Grid::ToIndex(Neighbor, GridSize)];
						const bool bNeighborOpen = (NeighborMask
							& Grid::DirectionBit(Grid::OppositeDirectionIndex(Direction))) != 0;
						if (bOpen != bNeighborOpen)
						{
							return false;
						}
					}
				}
			}
			return true;
		}

		/** 在稀疏最终 Plan 中按坐标查询 Tile；返回值只在当前 Plan 生命周期内有效。 */
		const FZeroEscapeCollapsedTile* FindTile(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntPoint Coordinate)
		{
			return Plan.Cells.FindByPredicate(
				[Coordinate](const FZeroEscapeCollapsedTile& Cell)
				{
					return Cell.GridCoordinate == Coordinate;
				});
		}

		/** 从 Start 沿 OpeningMask 做独立 BFS，确认所有非空格、Exit 和房间锚点连通。 */
		bool IsEntirePlanReachableFromStart(const FZeroEscapeGeneratedLevelPlan& Plan)
		{
			TMap<FIntPoint, int32> CellIndexByCoordinate;
			for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
			{
				CellIndexByCoordinate.Add(Plan.Cells[Index].GridCoordinate, Index);
			}
			const int32* StartIndex = CellIndexByCoordinate.Find(Plan.StartCoordinate);
			if (StartIndex == nullptr)
			{
				return false;
			}

			TArray<uint8> Visited;
			Visited.Init(0, Plan.Cells.Num());
			TQueue<int32> Queue;
			Visited[*StartIndex] = 1;
			Queue.Enqueue(*StartIndex);
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
					const int32* NeighborIndex = CellIndexByCoordinate.Find(
						Grid::Step(Current.GridCoordinate, Direction));
					if (NeighborIndex == nullptr)
					{
						return false;
					}
					if (Visited[*NeighborIndex] == 0)
					{
						Visited[*NeighborIndex] = 1;
						Queue.Enqueue(*NeighborIndex);
					}
				}
			}

			for (const uint8 bVisited : Visited)
			{
				if (bVisited == 0)
				{
					return false;
				}
			}
			const int32* ExitIndex = CellIndexByCoordinate.Find(Plan.ExitCoordinate);
			if (ExitIndex == nullptr || Visited[*ExitIndex] == 0)
			{
				return false;
			}
			for (const FZeroEscapeGeneratedRoom& Room : Plan.Rooms)
			{
				const int32* RoomIndex = CellIndexByCoordinate.Find(Room.AnchorCoordinate);
				if (RoomIndex == nullptr || Visited[*RoomIndex] == 0)
				{
					return false;
				}
			}
			return true;
		}
	}

	/** 验证 N/E/S/W 位序、反向、步进以及代码生成的 0..15 状态集完整且唯一。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeOpeningMaskAndVariantContractTest,
		"Demo.PCG.Unit.OpeningMaskAndVariantContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeOpeningMaskAndVariantContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TestEqual(TEXT("North 必须固定为 bit 0"), Grid::DirectionBit(0), static_cast<uint8>(0x1));
		TestEqual(TEXT("East 必须固定为 bit 1"), Grid::DirectionBit(1), static_cast<uint8>(0x2));
		TestEqual(TEXT("South 必须固定为 bit 2"), Grid::DirectionBit(2), static_cast<uint8>(0x4));
		TestEqual(TEXT("West 必须固定为 bit 3"), Grid::DirectionBit(3), static_cast<uint8>(0x8));
		const TStaticArray<FIntPoint, 4> ExpectedSteps = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0) };
		for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
		{
			TestTrue(TEXT("稳定方向索引必须得到预期四邻域步进"),
				Grid::Step(FIntPoint::ZeroValue, Direction) == ExpectedSteps[Direction]);
			TestEqual(TEXT("任意方向取两次反向必须回到自身"),
				Grid::OppositeDirectionIndex(Grid::OppositeDirectionIndex(Direction)), Direction);
		}

		FZeroEscapeWfcShapeWeights Weights;
		TStaticArray<FTileVariant, 16> Variants;
		FWfcSolver::BuildCanonicalVariants(Weights, Variants);
		TSet<uint8> UniqueMasks;
		for (int32 Index = 0; Index < Variants.Num(); ++Index)
		{
			TestEqual(TEXT("Variant 数组索引必须等于稳定 OpeningMask"),
				Variants[Index].OpeningMask, static_cast<uint8>(Index));
			TestTrue(TEXT("每个固定状态权重必须为正"), Variants[Index].Weight > 0);
			UniqueMasks.Add(Variants[Index].OpeningMask);
		}
		TestEqual(TEXT("固定状态集必须无缺失、无重复地覆盖 0..15"), UniqueMasks.Num(), 16);
		TestEqual(TEXT("N+S 必须读取 Straight 权重"), Weights.GetWeightForMask(0x5), Weights.StraightWeight);
		TestEqual(TEXT("N+E 必须读取 Corner 权重"), Weights.GetWeightForMask(0x3), Weights.CornerWeight);
		TestEqual(TEXT("使用高位的非法 Mask 必须返回零权重"), Weights.GetWeightForMask(0x10), 0);
		return true;
	}

	/** 直接验证 Count、MaxConsecutive 与 Connected 的矛盾证明和稳定 Ban 输出。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeGlobalWfcConstraintContractsTest,
		"Demo.PCG.WFC.GlobalConstraintContracts",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeGlobalWfcConstraintContractsTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		FWfcConstraintWorkspace Workspace;
		FWfcConstraintFailure Failure;

		// Count 达到上限时，必须从其余 Maybe Cell 删除所有非空候选。
		const FIntPoint CountGrid(3, 1);
		FZeroEscapeWfcSolveSettings CountSettings = MakeWfcSettings(FIntPoint(0, 0), 1, 1, 3);
		TArray<FWfcDomain> CountDomains = {
			VariantBit(0x2),
			static_cast<FWfcDomain>(VariantBit(0x0) | VariantBit(0x8)),
			VariantBit(0x0) };
		TestTrue(TEXT("Count 上限稳定点必须可传播"),
			FWfcConstraints::Evaluate(CountGrid, CountSettings, CountDomains, Workspace, Failure));
		TestTrue(TEXT("Count 达到上限后必须 Ban 第二格的非空 West 候选"),
			(Workspace.BanMaskByCell[1] & VariantBit(0x8)) != 0);

		CountDomains = {VariantBit(0x2), VariantBit(0x8), VariantBit(0x0)};
		TestFalse(TEXT("已被迫非空的 Cell 超过 Count 上限必须矛盾"),
			FWfcConstraints::Evaluate(CountGrid, CountSettings, CountDomains, Workspace, Failure));
		TestTrue(TEXT("Count 矛盾必须返回结构化 Kind"),
			Failure.Kind == EWfcConstraintContradiction::Count);

		// 三格水平贯通超过 2 格上限，必须在完成叶之前矛盾。
		const FIntPoint ConsecutiveGrid(3, 1);
		FZeroEscapeWfcSolveSettings ConsecutiveSettings =
			MakeWfcSettings(FIntPoint(0, 0), 3, 3, 2);
		TArray<FWfcDomain> ConsecutiveDomains = {
			VariantBit(0xA), VariantBit(0xA), VariantBit(0xA) };
		TestFalse(TEXT("三格被迫水平贯通必须超过 MaxConsecutive=2"),
			FWfcConstraints::Evaluate(
				ConsecutiveGrid, ConsecutiveSettings, ConsecutiveDomains, Workspace, Failure));
		TestTrue(TEXT("MaxConsecutive 矛盾必须返回结构化 Kind 和窗口长度"),
			Failure.Kind == EWfcConstraintContradiction::MaxConsecutive
				&& Failure.ObservedCount == 3
				&& Failure.Limit == 2);

		ConsecutiveDomains = {
			VariantBit(0xA),
			VariantBit(0xA),
			static_cast<FWfcDomain>(VariantBit(0xA) | VariantBit(0x8)) };
		TestTrue(TEXT("窗口中只剩一个可选贯通格时必须可传播"),
			FWfcConstraints::Evaluate(
				ConsecutiveGrid, ConsecutiveSettings, ConsecutiveDomains, Workspace, Failure));
		TestTrue(TEXT("MaxConsecutive 必须从唯一弹性格 Ban 水平贯通候选"),
			(Workspace.BanMaskByCell[2] & VariantBit(0xA)) != 0);

		// 中间格固定 Empty 切断可能图，右端被迫非空时必须矛盾。
		const FIntPoint ConnectedGrid(3, 1);
		FZeroEscapeWfcSolveSettings ConnectedSettings =
			MakeWfcSettings(FIntPoint(0, 0), 2, 2, 3);
		TArray<FWfcDomain> ConnectedDomains = {
			VariantBit(0x2), VariantBit(0x0), VariantBit(0x8) };
		TestFalse(TEXT("被迫非空 Cell 已无法从 Start 到达时 Connected 必须矛盾"),
			FWfcConstraints::Evaluate(
				ConnectedGrid, ConnectedSettings, ConnectedDomains, Workspace, Failure));
		TestTrue(TEXT("Connected 矛盾必须标记无法到达的第三格"),
			Failure.Kind == EWfcConstraintContradiction::Connected && Failure.CellIndex == 2);

		ConnectedSettings = MakeWfcSettings(FIntPoint(0, 0), 1, 2, 3);
		ConnectedDomains = {
			VariantBit(0x2),
			VariantBit(0x0),
			static_cast<FWfcDomain>(VariantBit(0x0) | VariantBit(0x8)) };
		TestTrue(TEXT("无法到达但仍可 Empty 的 Optional Cell 应被安全剪除"),
			FWfcConstraints::Evaluate(
				ConnectedGrid, ConnectedSettings, ConnectedDomains, Workspace, Failure));
		TestTrue(TEXT("Connected 必须 Ban 不可达 Optional Cell 的非空候选"),
			(Workspace.BanMaskByCell[2] & VariantBit(0x8)) != 0);
		return true;
	}

	/**
	 * 验证 expanded graph + Tarjan 传播：唯一桥不仅必须非空，两条公共边也必须打开。
	 * 同时锁定多个 Relevant 分量会在观察前被 Connected 直接证明为矛盾。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeConnectedExpandedGraphPropagationTest,
		"Demo.PCG.WFC.ConnectedExpandedGraphPropagation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeConnectedExpandedGraphPropagationTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;

		const FIntPoint GridSize(3, 1);
		const FZeroEscapeWfcSolveSettings Settings =
			MakeWfcSettings(FIntPoint(0, 0), 2, 3, 3);
		FWfcConstraintWorkspace Workspace;
		FWfcConstraintFailure Failure;

		// 两端已被迫非空，中格可 Empty、单向开口或 E+W；它是连接两端的唯一桥。
		const TArray<FWfcDomain> UniqueBridgeDomains = {
			VariantBit(0x2),
			static_cast<FWfcDomain>(
				VariantBit(0x0) | VariantBit(0x2) | VariantBit(0x8) | VariantBit(0xA)),
			VariantBit(0x8) };
		TestTrue(TEXT("唯一桥仍有可行 E+W 候选时 Connected 必须成功传播"),
			FWfcConstraints::Evaluate(
				GridSize,
				Settings,
				UniqueBridgeDomains,
				Workspace,
				Failure));
		TestTrue(TEXT("中心关节点必须从唯一桥 Ban Empty"),
			(Workspace.BanMaskByCell[1] & VariantBit(0x0)) != 0);
		TestTrue(TEXT("West 方向关节点必须 Ban 只向 East 开口的候选"),
			(Workspace.BanMaskByCell[1] & VariantBit(0x2)) != 0);
		TestTrue(TEXT("East 方向关节点必须 Ban 只向 West 开口的候选"),
			(Workspace.BanMaskByCell[1] & VariantBit(0x8)) != 0);
		TestTrue(TEXT("唯一可连通两端的 E+W 候选必须保留"),
			(Workspace.BanMaskByCell[1] & VariantBit(0xA)) == 0);

		// 固定 Empty 把两个被迫非空端点分到不同 Potential 分量，不能等待叶节点才失败。
		const TArray<FWfcDomain> DisconnectedDomains = {
			VariantBit(0x2), VariantBit(0x0), VariantBit(0x8) };
		TestFalse(TEXT("多个 Relevant 分量必须立即产生 Connected 矛盾"),
			FWfcConstraints::Evaluate(
				GridSize,
				Settings,
				DisconnectedDomains,
				Workspace,
				Failure));
		TestTrue(TEXT("断连矛盾必须返回 Connected Kind 和第二分量 Cell"),
			Failure.Kind == EWfcConstraintContradiction::Connected
				&& Failure.CellIndex == 2);
		return true;
	}

	/** 验证稠密 Required 契约、唯一解导出与非法输入原子失败。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeWfcInputAndConstructiveSolveTest,
		"Demo.PCG.WFC.InputAndConstructiveSolve",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeWfcInputAndConstructiveSolveTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		const FIntPoint GridSize(5, 3);
		const TArray<FGridCellConstraint> ValidConstraints = MakeConstructiveWfcFixture(GridSize);
		const TArray<FTileVariant> Variants = MakeCanonicalVariantArray(FZeroEscapeWfcShapeWeights());
		const FZeroEscapeWfcSolveSettings Settings =
			MakeWfcSettings(FIntPoint(1, 1), 3, 3, 3);
		auto AcceptEveryLeaf = [](const TConstArrayView<uint8>)
		{
			return FWfcCollapsedCandidateEvaluation::Accept();
		};

		FRandomStream Random(12345);
		TArray<uint8> Output;
		FZeroEscapeGenerationReport Report;
		TestTrue(TEXT("三格 Required 直线必须求解成功"), FWfcSolver::Solve(
			GridSize,
			ValidConstraints,
			Settings,
			Variants,
			Random,
			AcceptEveryLeaf,
			Output,
			Report));
		TestTrue(TEXT("构造性结果必须保持全部公共边镜像一致"),
			AreDenseOpeningMasksConsistent(GridSize, Output));
		TestEqual(TEXT("直线左端只能向 East 开口"),
			Output[Grid::ToIndex(FIntPoint(1, 1), GridSize)], Grid::DirectionBit(1));
		TestEqual(TEXT("直线中段只能 East+West 贯通"),
			Output[Grid::ToIndex(FIntPoint(2, 1), GridSize)],
			static_cast<uint8>(Grid::DirectionBit(1) | Grid::DirectionBit(3)));
		TestEqual(TEXT("直线右端只能向 West 开口"),
			Output[Grid::ToIndex(FIntPoint(3, 1), GridSize)], Grid::DirectionBit(3));

		TArray<FGridCellConstraint> Asymmetric = ValidConstraints;
		Asymmetric[Grid::ToIndex(FIntPoint(2, 1), GridSize)].RequiredOpenMask &=
			static_cast<uint8>(~Grid::DirectionBit(3));
		Output.Add(255);
		FRandomStream InvalidRandom(12345);
		TestFalse(TEXT("删除 RequiredOpen 的反向镜像必须失败"), FWfcSolver::Solve(
			GridSize,
			Asymmetric,
			Settings,
			Variants,
			InvalidRandom,
			AcceptEveryLeaf,
			Output,
			Report));
		TestTrue(TEXT("非法稠密约束必须报告不变量失败且清空旧输出"),
			Report.Failure == EZeroEscapeGenerationFailure::SolverInvariantViolation
				&& Output.IsEmpty());
		return true;
	}

	/**
	 * 审计边界 1：根稳定点 Count Ban 后必须清空 Trail 但保留 Domain 收缩。
	 *
	 * 3x2 夹具左侧 2x2 全 Required、右列 Optional，Min=Max=4 使 Count 在根稳定点
	 * 将右列固定为 Empty。左侧四环恰有 5 个连通边子图；拒绝全部叶必须穷尽搜索，
	 * 且任一回溯都不得把右列恢复成非空。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeWfcRootStablePointExhaustionTest,
		"Demo.PCG.WFC.Backtracking.RootStablePointExhaustion",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeWfcRootStablePointExhaustionTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		const FIntPoint GridSize(3, 2);
		TArray<FGridCellConstraint> Constraints =
			MakeDenseConstraints(GridSize, EGridCellDomain::Optional);
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 X = 0; X < 2; ++X)
			{
				Constraints[Grid::ToIndex(FIntPoint(X, Y), GridSize)].Domain =
					EGridCellDomain::Required;
			}
		}

		const FZeroEscapeWfcSolveSettings Settings =
			MakeWfcSettings(FIntPoint(0, 0), 4, 4, 2);
		const TArray<FTileVariant> Variants = MakeCanonicalVariantArray(FZeroEscapeWfcShapeWeights());
		int32 VisitedLeafCount = 0;
		bool bEveryLeafPreservedRootBans = true;
		auto RejectEveryLeaf = [this, &VisitedLeafCount, &bEveryLeafPreservedRootBans, GridSize](
			const TConstArrayView<uint8> Masks)
		{
			++VisitedLeafCount;
			const bool bRootBansPreserved = Masks[Grid::ToIndex(FIntPoint(2, 0), GridSize)] == 0
				&& Masks[Grid::ToIndex(FIntPoint(2, 1), GridSize)] == 0;
			bEveryLeafPreservedRootBans &= bRootBansPreserved
				&& AreDenseOpeningMasksConsistent(GridSize, Masks);
			TestTrue(TEXT("回溯后的每个叶都必须保留根 Count/Opening 传播结果"),
				bRootBansPreserved);
			return FWfcCollapsedCandidateEvaluation::Reject(
				TEXT("测试故意拒绝当前完整叶。"),
				VisitedLeafCount,
				5);
		};

		FRandomStream Random(24680);
		TArray<uint8> Output;
		FZeroEscapeGenerationReport Report;
		TestFalse(TEXT("全部五个叶被拒绝后必须报告真正无解"), FWfcSolver::Solve(
			GridSize,
			Constraints,
			Settings,
			Variants,
			Random,
			RejectEveryLeaf,
			Output,
			Report));
		TestEqual(TEXT("2x2 四环必须恰好穷尽 5 个连通完整叶"), VisitedLeafCount, 5);
		TestTrue(TEXT("根稳定点 Ban 必须跨全部决策帧保留"), bEveryLeafPreservedRootBans);
		TestTrue(TEXT("穷尽首层决策后必须报 NoValid 而非不变量/预算失败"),
			Report.Failure == EZeroEscapeGenerationFailure::NoValidWfcSolution
				&& Report.Metrics.WfcInvariantFailureCount == 0
				&& Report.Metrics.WfcBacktrackCount > 0
				&& Output.IsEmpty());
		return true;
	}

	/**
	 * 审计边界 2：一次决策经传播直接生成完整叶，第一叶被拒绝后必须恢复同一活动帧。
	 * 左中两格必开，右格 Optional，因此恰有“右格 Empty”与“向右延伸”两个完整叶。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeWfcActiveDecisionLeafRejectionTest,
		"Demo.PCG.WFC.Backtracking.ActiveDecisionLeafRejection",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeWfcActiveDecisionLeafRejectionTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		const FIntPoint GridSize(3, 1);
		TArray<FGridCellConstraint> Constraints =
			MakeDenseConstraints(GridSize, EGridCellDomain::Optional);
		AddRequiredEdge(GridSize, FIntPoint(0, 0), FIntPoint(1, 0), Constraints);
		const FZeroEscapeWfcSolveSettings Settings =
			MakeWfcSettings(FIntPoint(0, 0), 2, 3, 3);
		const TArray<FTileVariant> Variants = MakeCanonicalVariantArray(FZeroEscapeWfcShapeWeights());

		int32 VisitedLeafCount = 0;
		TArray<uint8> FirstLeaf;
		TArray<uint8> SecondLeaf;
		auto RejectFirstAcceptSecond = [&VisitedLeafCount, &FirstLeaf, &SecondLeaf](
			const TConstArrayView<uint8> Masks)
		{
			++VisitedLeafCount;
			TArray<uint8>& Destination = VisitedLeafCount == 1 ? FirstLeaf : SecondLeaf;
			Destination.Append(Masks.GetData(), Masks.Num());
			return VisitedLeafCount == 1
				? FWfcCollapsedCandidateEvaluation::Reject(
					TEXT("测试故意拒绝第一个传播完成叶。"), 1, 2)
				: FWfcCollapsedCandidateEvaluation::Accept();
		};

		FRandomStream Random(13579);
		TArray<uint8> Output;
		FZeroEscapeGenerationReport Report;
		TestTrue(TEXT("拒绝第一叶后必须在同一决策帧找到第二叶"), FWfcSolver::Solve(
			GridSize,
			Constraints,
			Settings,
			Variants,
			Random,
			RejectFirstAcceptSecond,
			Output,
			Report));
		TestEqual(TEXT("活动决策帧夹具必须恰好看到两个完整叶"), VisitedLeafCount, 2);
		TestTrue(TEXT("夹具必须真正经过最少一次观察和回溯"),
			Report.Metrics.WfcObservationCount > 0
				&& Report.Metrics.WfcBacktrackCount > 0
				&& Report.Metrics.WfcCollapsedCandidateRejectionCount == 1);
		TestFalse(TEXT("恢复活动帧后第二叶必须与被拒绝的第一叶不同"),
			FirstLeaf == SecondLeaf);
		TestTrue(TEXT("最终原子提交必须恰好是第二个完整叶"),
			Output == SecondLeaf && AreDenseOpeningMasksConsistent(GridSize, Output));
		return true;
	}

	/** 审计边界 3：根状态本身已完成且 Decisions 为空时，Reject 必须直接 NoValid。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeWfcRootCompleteLeafRejectionTest,
		"Demo.PCG.WFC.Backtracking.RootCompleteLeafRejection",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeWfcRootCompleteLeafRejectionTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		const FIntPoint GridSize(2, 1);
		TArray<FGridCellConstraint> Constraints =
			MakeDenseConstraints(GridSize, EGridCellDomain::Outside);
		AddRequiredEdge(GridSize, FIntPoint(0, 0), FIntPoint(1, 0), Constraints);
		const FZeroEscapeWfcSolveSettings Settings =
			MakeWfcSettings(FIntPoint(0, 0), 2, 2, 2);
		const TArray<FTileVariant> Variants = MakeCanonicalVariantArray(FZeroEscapeWfcShapeWeights());
		int32 VisitedLeafCount = 0;
		auto RejectRootLeaf = [&VisitedLeafCount](const TConstArrayView<uint8>)
		{
			++VisitedLeafCount;
			return FWfcCollapsedCandidateEvaluation::Reject(
				TEXT("测试故意拒绝根完成叶。"), 1, 1);
		};

		FRandomStream Random(97531);
		TArray<uint8> Output;
		FZeroEscapeGenerationReport Report;
		TestFalse(TEXT("没有决策帧的根完成叶被拒绝后必须直接无解"), FWfcSolver::Solve(
			GridSize,
			Constraints,
			Settings,
			Variants,
			Random,
			RejectRootLeaf,
			Output,
			Report));
		TestEqual(TEXT("根完成夹具只能验收一次"), VisitedLeafCount, 1);
		TestTrue(TEXT("根叶 Reject 必须 NoValid，且 Observation/Backtrack 均为零"),
			Report.Failure == EZeroEscapeGenerationFailure::NoValidWfcSolution
				&& Report.Metrics.WfcObservationCount == 0
				&& Report.Metrics.WfcBacktrackCount == 0
				&& Report.Metrics.WfcCollapsedCandidateRejectionCount == 1
				&& Output.IsEmpty());
		return true;
	}

	/** 验证加权候选顺序、完整输出和工作量指标都能按同 Seed 逐项重放。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeWfcDeterministicReplayTest,
		"Demo.PCG.WFC.DeterministicReplay",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeWfcDeterministicReplayTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		const FIntPoint GridSize(3, 1);
		TArray<FGridCellConstraint> Constraints =
			MakeDenseConstraints(GridSize, EGridCellDomain::Optional);
		AddRequiredEdge(GridSize, FIntPoint(0, 0), FIntPoint(1, 0), Constraints);
		const FZeroEscapeWfcSolveSettings Settings =
			MakeWfcSettings(FIntPoint(0, 0), 2, 3, 3);
		FZeroEscapeWfcShapeWeights Weights;
		Weights.EmptyWeight = 20;
		Weights.DeadEndWeight = 35;
		Weights.StraightWeight = 90;
		Weights.CornerWeight = 70;
		Weights.TJunctionWeight = 7;
		Weights.CrossWeight = 2;
		const TArray<FTileVariant> Variants = MakeCanonicalVariantArray(Weights);
		auto AcceptEveryLeaf = [](const TConstArrayView<uint8>)
		{
			return FWfcCollapsedCandidateEvaluation::Accept();
		};

		for (int32 Seed = 0; Seed < 16; ++Seed)
		{
			FRandomStream FirstRandom = FGenerationCore::MakeRandomStream(
				Seed, GAlgorithmVersion, ERandomDomain::WfcLayout);
			FRandomStream SecondRandom = FGenerationCore::MakeRandomStream(
				Seed, GAlgorithmVersion, ERandomDomain::WfcLayout);
			TArray<uint8> FirstOutput;
			TArray<uint8> SecondOutput;
			FZeroEscapeGenerationReport FirstReport;
			FZeroEscapeGenerationReport SecondReport;
			const bool bFirstSolved = FWfcSolver::Solve(
				GridSize, Constraints, Settings, Variants, FirstRandom,
				AcceptEveryLeaf, FirstOutput, FirstReport);
			const bool bSecondSolved = FWfcSolver::Solve(
				GridSize, Constraints, Settings, Variants, SecondRandom,
				AcceptEveryLeaf, SecondOutput, SecondReport);
			if (!TestTrue(TEXT("同 Seed 重放的两次 WFC 都必须成功"),
				bFirstSolved && bSecondSolved))
			{
				continue;
			}
			TestTrue(TEXT("同 Seed 必须复现每个 OpeningMask"), FirstOutput == SecondOutput);
			TestTrue(TEXT("重放输出必须保持公共边对称和边界关闭"),
				AreDenseOpeningMasksConsistent(GridSize, FirstOutput));
			TestTrue(TEXT("同 Seed 必须复现观察、尝试与回溯计数"),
				FirstReport.Metrics.WfcObservationCount == SecondReport.Metrics.WfcObservationCount
					&& FirstReport.Metrics.WfcCandidateAttemptCount
						== SecondReport.Metrics.WfcCandidateAttemptCount
					&& FirstReport.Metrics.WfcBacktrackCount
						== SecondReport.Metrics.WfcBacktrackCount);
		}
		return true;
	}

	/** 验证房间只提供局部约束，完整 WFC 仍保证全图、Exit 和房间锚点可达。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeGridRoomAndConnectivityTest,
		"Demo.PCG.Grid.RoomAndConnectivity",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeGridRoomAndConnectivityTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;

		UZeroEscapeLevelGenerationProfile* Profile =
			NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		const FZeroEscapeSharedRouteConstraints& Rules =
			Profile->SharedRouteConstraints;
		const FZeroEscapeWfcShapeWeights Weights =
			Profile->Difficulties[1].WfcShapeWeights;

		for (int32 Seed = 100; Seed < 104; ++Seed)
		{
			FZeroEscapeGeneratedLevelPlan Plan;
			FZeroEscapeGenerationReport Report;
			const bool bSolved = FGridLayoutSolver::Solve(
				MakeSignature(Seed),
				Rules,
				Weights,
				Plan,
				Report);
			if (!TestTrue(TEXT("标准空间配置必须生成成功"), bSolved))
			{
				AddError(FString::Printf(
					TEXT("Seed=%d Stage=%d Failure=%d Message=%s"),
					Seed,
					static_cast<int32>(Report.Stage),
					static_cast<int32>(Report.Failure),
					*Report.Message));
				continue;
			}

			TestEqual(TEXT("Plan 必须导出 Profile 指定数量的中立房间"),
				Plan.Rooms.Num(), Rules.RoomCount);
			TestTrue(TEXT("所有非空格、Exit 与房间锚点必须在 Start 连通分量内"),
				IsEntirePlanReachableFromStart(Plan));
			TestTrue(TEXT("非空格数必须落在共享 Count 区间"),
				Plan.Cells.Num() >= Rules.MinWalkableCellCount
					&& Plan.Cells.Num() <= Rules.MaxWalkableCellCount);
			TestTrue(TEXT("Start 与 Exit 必须导出为各自空间职责"),
				FindTile(Plan, Plan.StartCoordinate) != nullptr
					&& FindTile(Plan, Plan.StartCoordinate)->RegionKind
						== EZeroEscapeGridRegionKind::Start
					&& FindTile(Plan, Plan.ExitCoordinate) != nullptr
					&& FindTile(Plan, Plan.ExitCoordinate)->RegionKind
						== EZeroEscapeGridRegionKind::Exit);
			TestTrue(TEXT("Start 与 Exit Transform 必须保持有限 Unit Scale"),
				FGenerationCore::IsFiniteUnitScaleTransform(
					Plan.PlayerStartLocalTransform)
					&& FGenerationCore::IsFiniteUnitScaleTransform(
						Plan.ExitLocalTransform));
			TestTrue(TEXT("成功 Plan 必须产生非零规范布局 Hash"),
				Plan.CanonicalLayoutHash != 0);

			for (const FZeroEscapeGeneratedRoom& Room : Plan.Rooms)
			{
				int32 RoomCellCount = 0;
				for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
				{
					RoomCellCount += Cell.RegionKind
							== EZeroEscapeGridRegionKind::Room
						&& Cell.RegionId == Room.RegionId
						? 1
						: 0;
				}
				TestEqual(TEXT("每间中立房必须恰好占用 2x2 四个逻辑格"),
					RoomCellCount, 4);
				TestTrue(TEXT("房间 Anchor Transform 必须保持有限 Unit Scale"),
					FGenerationCore::IsFiniteUnitScaleTransform(
						Room.LocalTransform));
			}

			const FZeroEscapeJunctionMetrics& Junctions = Plan.JunctionMetrics;
			TestEqual(TEXT("全部非空格必须恰好归入一种开口形态"),
				Junctions.DeadEndCount
					+ Junctions.StraightCount
					+ Junctions.CornerCount
					+ Junctions.TJunctionCount
					+ Junctions.CrossJunctionCount,
				Plan.Cells.Num());
		}
		return true;
	}

	/** 验证完整 Grid 同输入重放、失败原子性和失败后恢复不受前次调用污染。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeLayoutDeterminismAndStateIsolationTest,
		"Demo.PCG.Grid.DeterminismAndStateIsolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeLayoutDeterminismAndStateIsolationTest::RunTest(
		const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;

		UZeroEscapeLevelGenerationProfile* Profile =
			NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		const FZeroEscapeGenerationSignature Signature = MakeSignature(20260319);
		const FZeroEscapeWfcShapeWeights Weights =
			Profile->Difficulties[1].WfcShapeWeights;

		FZeroEscapeGeneratedLevelPlan FirstPlan;
		FZeroEscapeGenerationReport FirstReport;
		if (!TestTrue(TEXT("确定性基线必须求解成功"),
			FGridLayoutSolver::Solve(
				Signature,
				Profile->SharedRouteConstraints,
				Weights,
				FirstPlan,
				FirstReport)))
		{
			AddError(FirstReport.Message);
			return true;
		}

		FZeroEscapeGeneratedLevelPlan SecondPlan;
		FZeroEscapeGenerationReport SecondReport;
		TestTrue(TEXT("同输入第二次求解必须成功"),
			FGridLayoutSolver::Solve(
				Signature,
				Profile->SharedRouteConstraints,
				Weights,
				SecondPlan,
				SecondReport));
		TestEqual(TEXT("同输入必须复现 CanonicalLayoutHash"),
			SecondPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
		TestTrue(TEXT("同输入必须复现全部稀疏 Cell"),
			SecondPlan.Cells.Num() == FirstPlan.Cells.Num());
		if (SecondPlan.Cells.Num() == FirstPlan.Cells.Num())
		{
			for (int32 Index = 0; Index < FirstPlan.Cells.Num(); ++Index)
			{
				TestTrue(TEXT("同输入必须逐格复现坐标、Mask 与空间职责"),
					SecondPlan.Cells[Index].GridCoordinate
							== FirstPlan.Cells[Index].GridCoordinate
						&& SecondPlan.Cells[Index].OpeningMask
							== FirstPlan.Cells[Index].OpeningMask
						&& SecondPlan.Cells[Index].RegionId
							== FirstPlan.Cells[Index].RegionId
						&& SecondPlan.Cells[Index].RegionKind
							== FirstPlan.Cells[Index].RegionKind);
			}
		}
		TestTrue(TEXT("同输入必须复现 WFC 观察、候选与回溯计数"),
			SecondReport.Metrics.WfcObservationCount
					== FirstReport.Metrics.WfcObservationCount
				&& SecondReport.Metrics.WfcCandidateAttemptCount
					== FirstReport.Metrics.WfcCandidateAttemptCount
				&& SecondReport.Metrics.WfcBacktrackCount
					== FirstReport.Metrics.WfcBacktrackCount);

		FZeroEscapeSharedRouteConstraints TinyBudget =
			Profile->SharedRouteConstraints;
		TinyBudget.MaxWfcSolveAttempts = 1;
		TinyBudget.MaxWfcCandidateAttempts = 1;
		TinyBudget.MaxWfcBacktrackCount = 1;
		FZeroEscapeGeneratedLevelPlan FailedPlan = FirstPlan;
		FZeroEscapeGenerationReport FailureReport;
		TestFalse(TEXT("单候选预算不足以求解完整 24x16 Grid"),
			FGridLayoutSolver::Solve(
				Signature,
				TinyBudget,
				Weights,
				FailedPlan,
				FailureReport));
		TestTrue(TEXT("失败必须原子清空旧 Plan"),
			FailedPlan.Cells.IsEmpty()
				&& FailedPlan.Rooms.IsEmpty()
				&& FailedPlan.CanonicalLayoutHash == 0);
		TestTrue(TEXT("预算失败必须返回结构化原因"),
			FailureReport.Failure
				== EZeroEscapeGenerationFailure::SolverBudgetExhausted);

		FZeroEscapeGenerationReport RecoveryReport;
		TestTrue(TEXT("失败调用后使用合法预算必须恢复成功"),
			FGridLayoutSolver::Solve(
				Signature,
				Profile->SharedRouteConstraints,
				Weights,
				FailedPlan,
				RecoveryReport));
		TestEqual(TEXT("恢复结果必须与未受污染的基线一致"),
			FailedPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
		return true;
	}

	/**
	 * 对 Easy、Normal、Hard 各扫描 96 个 Seed，共 288 局。
	 * 本测试验证空间求解成功率、确定性预算上限和连通性；不包含玩法目标或表现验收。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeValidProfileSeedSweepTest,
		"Demo.PCG.SeedSweep.ValidProfile288",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeValidProfileSeedSweepTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;

		UZeroEscapeLevelGenerationProfile* Profile =
			NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		FString ProfileError;
		if (!TestTrue(TEXT("Seed Sweep 需要合法 V5 Profile"),
			Profile->IsConfigured(ProfileError)))
		{
			AddError(ProfileError);
			return true;
		}

		TArray<int32> CandidateAttempts;
		TArray<int32> Backtracks;
		TArray<int32> SolveAttempts;
		TArray<int32> WalkableCells;
		TArray<double> PlanningMilliseconds;
		CandidateAttempts.Reserve(288);
		Backtracks.Reserve(288);
		SolveAttempts.Reserve(288);
		WalkableCells.Reserve(288);
		PlanningMilliseconds.Reserve(288);

		int32 SuccessCount = 0;
		int32 FailureCount = 0;
		for (const FZeroEscapeDifficultyDefinition& Difficulty :
			Profile->Difficulties)
		{
			for (int32 Seed = 0; Seed < 96; ++Seed)
			{
				FZeroEscapeGeneratedLevelPlan Plan;
				FZeroEscapeGenerationReport Report;
				const FZeroEscapeGenerationSignature Signature =
					MakeSignature(Seed, Difficulty.Difficulty);
				const bool bSolved = FGridLayoutSolver::Solve(
					Signature,
					Profile->SharedRouteConstraints,
					Difficulty.WfcShapeWeights,
					Plan,
					Report);
				if (!bSolved)
				{
					++FailureCount;
					AddError(FString::Printf(
						TEXT("Seed Sweep 失败 Difficulty=%d Seed=%d Stage=%d Failure=%d "
							"Actual=%d Limit=%d Message=%s"),
						static_cast<int32>(Difficulty.Difficulty),
						Seed,
						static_cast<int32>(Report.Stage),
						static_cast<int32>(Report.Failure),
						Report.ActualValue,
						Report.LimitValue,
						*Report.Message));
					continue;
				}

				++SuccessCount;
				TestTrue(TEXT("每个成功 Seed 的完整 Plan 都必须从 Start 全可达"),
					IsEntirePlanReachableFromStart(Plan));
				TestTrue(TEXT("Seed Sweep 不得超过候选总预算"),
					Report.Metrics.WfcCandidateAttemptCount
						<= Profile->SharedRouteConstraints.MaxWfcCandidateAttempts);
				TestTrue(TEXT("Seed Sweep 不得超过回溯总预算"),
					Report.Metrics.WfcBacktrackCount
						<= Profile->SharedRouteConstraints.MaxWfcBacktrackCount);
				TestTrue(TEXT("Seed Sweep 不得超过确定性尝试数"),
					Report.Metrics.WfcSolveAttemptCount
						<= Profile->SharedRouteConstraints.MaxWfcSolveAttempts);
				CandidateAttempts.Add(Report.Metrics.WfcCandidateAttemptCount);
				Backtracks.Add(Report.Metrics.WfcBacktrackCount);
				SolveAttempts.Add(Report.Metrics.WfcSolveAttemptCount);
				WalkableCells.Add(Plan.Cells.Num());
				PlanningMilliseconds.Add(Report.Metrics.PlanningMilliseconds);
			}
		}

		TestEqual(TEXT("Seed Sweep 必须执行 288 局"), SuccessCount + FailureCount, 288);
		TestEqual(TEXT("合法 V5 Profile 的 288 局必须全部成功"), SuccessCount, 288);
		TestEqual(TEXT("合法 V5 Profile 的 Seed Sweep 不得失败"), FailureCount, 0);
		if (!CandidateAttempts.IsEmpty())
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("ZE_PCG_SEED_SWEEP schema=5 runs=%d failures=%d "
					"candidates_p50=%d candidates_p95=%d candidates_max=%d "
					"backtracks_p50=%d backtracks_p95=%d backtracks_max=%d "
					"attempts_p95=%d attempts_max=%d walkable_p50=%d walkable_max=%d "
					"planning_ms_p50=%.3f planning_ms_p95=%.3f planning_ms_max=%.3f"),
				SuccessCount + FailureCount,
				FailureCount,
				NearestRankPercentile(CandidateAttempts, 0.50),
				NearestRankPercentile(CandidateAttempts, 0.95),
				NearestRankPercentile(CandidateAttempts, 1.0),
				NearestRankPercentile(Backtracks, 0.50),
				NearestRankPercentile(Backtracks, 0.95),
				NearestRankPercentile(Backtracks, 1.0),
				NearestRankPercentile(SolveAttempts, 0.95),
				NearestRankPercentile(SolveAttempts, 1.0),
				NearestRankPercentile(WalkableCells, 0.50),
				NearestRankPercentile(WalkableCells, 1.0),
				NearestRankPercentile(PlanningMilliseconds, 0.50),
				NearestRankPercentile(PlanningMilliseconds, 0.95),
				NearestRankPercentile(PlanningMilliseconds, 1.0));
		}
		return true;
	}
}

#endif
