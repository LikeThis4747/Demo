// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcLayoutTests.cpp
 *
 * 职责：验证 V4 Grid-WFC 的 16 状态契约、Count/MaxConsecutive/Connected 传播、
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

		/** 构造 V4 自动化所用的合法 Profile；三档难度保持等总权重。 */
		void BuildValidProfile(UZeroEscapeLevelGenerationProfile& Profile)
		{
			Profile.ProfileVersion = 4;
			Profile.SharedRouteConstraints = FZeroEscapeSharedRouteConstraints();
			Profile.SharedRouteConstraints.GridSize = FIntPoint(24, 16);
			Profile.SharedRouteConstraints.LogicalTileSizeCm = 600.0;
			Profile.SharedRouteConstraints.RoomSizeTiles = 2;
			Profile.SharedRouteConstraints.ObjectiveProgressBandCount = 3;
			Profile.SharedRouteConstraints.MinWalkableCellCount = 48;
			Profile.SharedRouteConstraints.MaxWalkableCellCount = 72;
			Profile.SharedRouteConstraints.MaxConsecutiveStraightTiles = 4;
			Profile.SharedRouteConstraints.MaxRequiredRouteLengthTiles = 64;
			Profile.SharedRouteConstraints.MaxRequiredRouteExtraTiles = 24;
			Profile.SharedRouteConstraints.MaxWfcCandidateAttempts = 100000;
			Profile.SharedRouteConstraints.MaxWfcBacktrackCount = 25000;
			Profile.SharedRouteConstraints.MaxWfcSolveAttempts = 10;
			Profile.SharedRouteConstraints.GameplayAnchorHeightCm = 100.0;

			Profile.Difficulties.Reset();
			FZeroEscapeDifficultyDefinition Easy;
			Easy.Difficulty = EZeroEscapeDifficulty::Easy;
			Easy.ObjectiveCandidateCount = 2;
			Easy.RequiredObjectiveCount = 1;
			Easy.WfcShapeWeights = FZeroEscapeWfcShapeWeights();
			Profile.Difficulties.Add(Easy);

			FZeroEscapeDifficultyDefinition Normal = Easy;
			Normal.Difficulty = EZeroEscapeDifficulty::Normal;
			Normal.ObjectiveCandidateCount = 3;
			Normal.RequiredObjectiveCount = 2;
			Profile.Difficulties.Add(Normal);

			FZeroEscapeDifficultyDefinition Hard = Normal;
			Hard.Difficulty = EZeroEscapeDifficulty::Hard;
			Hard.ObjectiveCandidateCount = 4;
			Hard.RequiredObjectiveCount = 3;
			Profile.Difficulties.Add(Hard);

			Profile.Flows.Reset();
			FZeroEscapeFlowDefinition EscapeOnly;
			EscapeOnly.StableFlowId = TEXT("EscapeOnly");
			EscapeOnly.FlowVersion = 1;
			EscapeOnly.CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
			Profile.Flows.Add(EscapeOnly);

			FZeroEscapeFlowDefinition CollectAll;
			CollectAll.StableFlowId = TEXT("CollectAll");
			CollectAll.FlowVersion = 2;
			CollectAll.CompletionRule = EZeroEscapeCompletionRule::CollectAll;
			Profile.Flows.Add(CollectAll);

			FZeroEscapeFlowDefinition CollectKOfN;
			CollectKOfN.StableFlowId = TEXT("CollectKOfN");
			CollectKOfN.FlowVersion = 3;
			CollectKOfN.CompletionRule = EZeroEscapeCompletionRule::CollectKOfN;
			Profile.Flows.Add(CollectKOfN);
		}

		/** 把 Profile 共享空间参数完整复制为 Grid 求解器纯值设置。 */
		FGridLayoutSettings MakeGridSettings(const FZeroEscapeSharedRouteConstraints& Source)
		{
			FGridLayoutSettings Settings;
			Settings.GridSize = Source.GridSize;
			Settings.LogicalTileSizeCm = FMath::RoundToInt(Source.LogicalTileSizeCm);
			Settings.RoomSizeTiles = Source.RoomSizeTiles;
			Settings.ObjectiveProgressBandCount = Source.ObjectiveProgressBandCount;
			Settings.MinWalkableCellCount = Source.MinWalkableCellCount;
			Settings.MaxWalkableCellCount = Source.MaxWalkableCellCount;
			Settings.MaxConsecutiveStraightTiles = Source.MaxConsecutiveStraightTiles;
			Settings.MaxRequiredRouteLengthTiles = Source.MaxRequiredRouteLengthTiles;
			Settings.MaxRequiredRouteExtraTiles = Source.MaxRequiredRouteExtraTiles;
			Settings.MaxWfcCandidateAttempts = Source.MaxWfcCandidateAttempts;
			Settings.MaxWfcBacktrackCount = Source.MaxWfcBacktrackCount;
			Settings.MaxWfcSolveAttempts = Source.MaxWfcSolveAttempts;
			Settings.GameplayAnchorHeightCm = Source.GameplayAnchorHeightCm;
			return Settings;
		}

		/** 构造供 Grid 单元测试直接消费的稳定 Signature。 */
		FZeroEscapeGenerationSignature MakeSignature(
			const int32 Seed,
			const FName FlowId,
			const EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal)
		{
			FZeroEscapeGenerationSignature Signature;
			Signature.Seed = Seed;
			Signature.Difficulty = Difficulty;
			Signature.FlowProfileId = FlowId;
			Signature.AlgorithmVersion = GAlgorithmVersion;
			Signature.GenerationProfileVersion = 4;
			Signature.FlowVersion = 1;
			Signature.PresentationVersion = 1;
			return Signature;
		}

		/** 构造只含 Start/Exit 的 Escape Grid 请求；不预刻任何固定路线。 */
		FGridLayoutRequest MakeEscapeGridRequest(const int32 Seed)
		{
			FGridLayoutRequest Request;
			Request.Signature = MakeSignature(Seed, TEXT("EscapeOnly"));
			Request.Progression.CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
			Request.Progression.ObjectiveCandidateCount = 0;
			Request.Progression.RequiredObjectiveCount = 0;
			Request.Progression.StartStableLandmarkId = 0;
			Request.Progression.ExitStableLandmarkId = 1;
			Request.Progression.Landmarks = {
				{0, EProgressionLandmarkKind::Start, 0, INDEX_NONE, INDEX_NONE},
				{1, EProgressionLandmarkKind::Exit, 4, INDEX_NONE, INDEX_NONE} };
			return Request;
		}

		/** 构造一个 0-based 进度带中的 2x2 CollectAll 房间请求。 */
		FGridLayoutRequest MakeSingleObjectiveGridRequest(const int32 Seed)
		{
			FGridLayoutRequest Request;
			Request.Signature = MakeSignature(Seed, TEXT("CollectAll"));
			Request.Progression.CompletionRule = EZeroEscapeCompletionRule::CollectAll;
			Request.Progression.ObjectiveCandidateCount = 1;
			Request.Progression.RequiredObjectiveCount = 1;
			Request.Progression.StartStableLandmarkId = 0;
			Request.Progression.ExitStableLandmarkId = 2;
			Request.Progression.Landmarks = {
				{0, EProgressionLandmarkKind::Start, 0, INDEX_NONE, INDEX_NONE},
				{1, EProgressionLandmarkKind::Objective, 0, 0, 0},
				{2, EProgressionLandmarkKind::Exit, 4, INDEX_NONE, INDEX_NONE} };
			return Request;
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

		/** 从 Start 沿 OpeningMask 做独立 BFS，确认所有非空 Cell 和 Landmark 在同一连通分量。 */
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
			for (const FZeroEscapeLandmarkBinding& Binding : Plan.LandmarkBindings)
			{
				const int32* BindingIndex = CellIndexByCoordinate.Find(Binding.GridCoordinate);
				if (BindingIndex == nullptr || Visited[*BindingIndex] == 0)
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

	/** 验证 V4 不预刻固定路线时，2x2 房内边、全图连通与数量/直线上限仍成立。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeGridRoomAndConnectivityTest,
		"Demo.PCG.Grid.RoomAndConnectivity",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeGridRoomAndConnectivityTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		FGridLayoutSettings Settings;
		Settings.GridSize = FIntPoint(24, 16);
		Settings.LogicalTileSizeCm = 600;
		Settings.RoomSizeTiles = 2;
		Settings.ObjectiveProgressBandCount = 3;
		Settings.MinWalkableCellCount = 48;
		Settings.MaxWalkableCellCount = 72;
		Settings.MaxConsecutiveStraightTiles = 4;
		Settings.MaxRequiredRouteLengthTiles = 64;
		Settings.MaxRequiredRouteExtraTiles = 24;
		Settings.MaxWfcCandidateAttempts = 100000;
		Settings.MaxWfcBacktrackCount = 25000;
		FZeroEscapeWfcShapeWeights Weights;

		FZeroEscapeGeneratedLevelPlan InspectedPlan;
		for (int32 Seed = 100; Seed < 104; ++Seed)
		{
			FGridLayoutRequest Request = MakeSingleObjectiveGridRequest(Seed);
			FZeroEscapeGeneratedLevelPlan Plan;
			FZeroEscapeGenerationReport Report;
			const bool bSolved = FGridLayoutSolver::Solve(
				Request, Settings, Weights, Seed, Plan, Report);
			if (!bSolved)
			{
				AddError(FString::Printf(
					TEXT("合法 V4 Grid 不应因 Seed=%d 失败：Stage=%d Failure=%d Attempts=%d Contradictions=%d Backtracks=%d LeafRejects=%d %s"),
					Seed, static_cast<int32>(Report.Stage), static_cast<int32>(Report.Failure),
					Report.Metrics.WfcCandidateAttemptCount,
					Report.Metrics.WfcContradictionCount,
					Report.Metrics.WfcBacktrackCount,
					Report.Metrics.WfcCollapsedCandidateRejectionCount,
					*Report.Message));
				continue;
			}
			TestTrue(TEXT("全部非空 Tile 与 Landmark 必须从 Start 可达"),
				IsEntirePlanReachableFromStart(Plan));
			TestTrue(TEXT("成功 Plan 必须满足统一非空 Cell 范围"),
				Plan.Cells.Num() >= Settings.MinWalkableCellCount
					&& Plan.Cells.Num() <= Settings.MaxWalkableCellCount);
			TestEqual(TEXT("单目标 CollectAll 必须导出一个 ObjectiveBinding"),
				Plan.ObjectiveBindings.Num(), 1);
			if (InspectedPlan.Cells.IsEmpty())
			{
				InspectedPlan = MoveTemp(Plan);
			}
		}

		TArray<const FZeroEscapeCollapsedTile*> RoomCells;
		for (const FZeroEscapeCollapsedTile& Cell : InspectedPlan.Cells)
		{
			if (Cell.RegionKind == EZeroEscapeGridRegionKind::Objective)
			{
				RoomCells.Add(&Cell);
			}
		}
		TestEqual(TEXT("Objective 房必须恰好占用 2x2 四个逻辑 Tile"), RoomCells.Num(), 4);
		for (const FZeroEscapeCollapsedTile* RoomCell : RoomCells)
		{
			for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
			{
				const FZeroEscapeCollapsedTile* Neighbor = FindTile(
					InspectedPlan, Grid::Step(RoomCell->GridCoordinate, Direction));
				if (Neighbor != nullptr && Neighbor->RegionId == RoomCell->RegionId)
				{
					TestTrue(TEXT("2x2 房任意内部公共边必须保持开放"),
						(RoomCell->OpeningMask & Grid::DirectionBit(Direction)) != 0);
				}
			}
		}
		return true;
	}

	/** 验证 Grid Solver 同输入确定性、失败输出原子清空和失败后的无状态恢复。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeLayoutDeterminismAndStateIsolationTest,
		"Demo.PCG.Unit.Layout.DeterminismAndStateIsolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeLayoutDeterminismAndStateIsolationTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		const int32 Seed = 54321;
		const FGridLayoutRequest Request = MakeEscapeGridRequest(Seed);
		FGridLayoutSettings Settings;
		Settings.GridSize = FIntPoint(24, 16);
		Settings.MinWalkableCellCount = 48;
		Settings.MaxWalkableCellCount = 72;
		Settings.MaxConsecutiveStraightTiles = 4;
		Settings.MaxRequiredRouteLengthTiles = 64;
		Settings.MaxRequiredRouteExtraTiles = 24;
		Settings.MaxWfcCandidateAttempts = 100000;
		Settings.MaxWfcBacktrackCount = 25000;
		FZeroEscapeWfcShapeWeights Weights;

		FZeroEscapeGeneratedLevelPlan FirstPlan;
		FZeroEscapeGenerationReport FirstReport;
		if (!TestTrue(TEXT("最小 Escape Grid 必须求解成功"),
			FGridLayoutSolver::Solve(Request, Settings, Weights, Seed, FirstPlan, FirstReport)))
		{
			AddError(FString::Printf(
				TEXT(
					"%s Attempts=%d Contradictions=%d "
					"[Local=%d Count=%d MaxConsecutive=%d Connected=%d GlobalBan=%d] "
					"Backtracks=%d LeafRejects=%d"),
				*FirstReport.Message,
				FirstReport.Metrics.WfcCandidateAttemptCount,
				FirstReport.Metrics.WfcContradictionCount,
				FirstReport.Metrics.WfcLocalAdjacencyContradictionCount,
				FirstReport.Metrics.WfcCountContradictionCount,
				FirstReport.Metrics.WfcMaxConsecutiveContradictionCount,
				FirstReport.Metrics.WfcConnectedContradictionCount,
				FirstReport.Metrics.WfcGlobalBanContradictionCount,
				FirstReport.Metrics.WfcBacktrackCount,
				FirstReport.Metrics.WfcCollapsedCandidateRejectionCount));
			return true;
		}
		FZeroEscapeGeneratedLevelPlan SecondPlan;
		FZeroEscapeGenerationReport SecondReport;
		TestTrue(TEXT("相同输入第二次求解必须成功"),
			FGridLayoutSolver::Solve(Request, Settings, Weights, Seed, SecondPlan, SecondReport));
		TestEqual(TEXT("同输入必须复现 CanonicalProgressionHash"),
			SecondPlan.CanonicalProgressionHash, FirstPlan.CanonicalProgressionHash);
		TestEqual(TEXT("同输入必须复现 CanonicalLayoutHash"),
			SecondPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
		TestEqual(TEXT("同输入必须复现非空 Cell 数量"), SecondPlan.Cells.Num(), FirstPlan.Cells.Num());

		FGridLayoutSettings InvalidSettings = Settings;
		InvalidSettings.GridSize = FIntPoint(10, 8);
		FZeroEscapeGeneratedLevelPlan FailedPlan = FirstPlan;
		FZeroEscapeGenerationReport FailedReport;
		TestFalse(TEXT("低于 V4 Landmark 安全容量的配置必须立即失败"),
			FGridLayoutSolver::Solve(
				Request, InvalidSettings, Weights, Seed, FailedPlan, FailedReport));
		TestTrue(TEXT("失败调用必须原子清空旧 Plan"), FailedPlan.Cells.IsEmpty()
			&& FailedPlan.CanonicalProgressionHash == 0
			&& FailedPlan.CanonicalLayoutHash == 0);
		TestTrue(TEXT("非法 Grid 必须报告 Configuration/InvalidConfiguration"),
			FailedReport.Stage == EZeroEscapeGenerationStage::Configuration
				&& FailedReport.Failure == EZeroEscapeGenerationFailure::InvalidConfiguration);

		FZeroEscapeGenerationReport RecoveryReport;
		TestTrue(TEXT("失败后使用合法设置必须恢复且不残留跨调用状态"),
			FGridLayoutSolver::Solve(Request, Settings, Weights, Seed, FailedPlan, RecoveryReport));
		TestEqual(TEXT("恢复求解必须复现最初 Layout Hash"),
			FailedPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
		return true;
	}

	/**
	 * 验证多次确定性尝试共享整局预算：每棵树只获得一个候选尝试时应稳定失败，
	 * 最终报告必须使用整局累计口径，并且不得把半成品 Plan 暴露给调用方。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeGridMultiAttemptBudgetFailureTest,
		"Demo.PCG.Grid.MultiAttemptBudgetFailure",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeGridMultiAttemptBudgetFailureTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		const int32 Seed = 86420;
		const FGridLayoutRequest Request = MakeEscapeGridRequest(Seed);
		FGridLayoutSettings Settings;
		Settings.GridSize = FIntPoint(24, 16);
		Settings.MinWalkableCellCount = 48;
		Settings.MaxWalkableCellCount = 72;
		Settings.MaxConsecutiveStraightTiles = 4;
		Settings.MaxRequiredRouteLengthTiles = 64;
		Settings.MaxRequiredRouteExtraTiles = 24;
		Settings.MaxWfcSolveAttempts = 3;
		Settings.MaxWfcCandidateAttempts = 3;
		Settings.MaxWfcBacktrackCount = 3;
		const FZeroEscapeWfcShapeWeights Weights;

		FZeroEscapeGeneratedLevelPlan FirstPlan;
		FZeroEscapeGenerationReport FirstReport;
		TestFalse(TEXT("每棵树仅一个候选尝试时不得误报求解成功"),
			FGridLayoutSolver::Solve(
				Request, Settings, Weights, Seed, FirstPlan, FirstReport));
		TestTrue(TEXT("三次尝试都必须执行，且整局候选预算不得超支"),
			FirstReport.Metrics.WfcSolveAttemptCount == Settings.MaxWfcSolveAttempts
				&& FirstReport.Metrics.WfcCandidateAttemptCount
					== Settings.MaxWfcCandidateAttempts
				&& FirstReport.Metrics.WfcBacktrackCount <= Settings.MaxWfcBacktrackCount);
		TestTrue(TEXT("预算失败的 Actual/Limit 必须明确表示已用尝试数和尝试上限"),
			FirstReport.Failure == EZeroEscapeGenerationFailure::SolverBudgetExhausted
				&& FirstReport.ActualValue == FirstReport.Metrics.WfcSolveAttemptCount
				&& FirstReport.LimitValue == Settings.MaxWfcSolveAttempts);
		TestTrue(TEXT("预算失败必须原子清空输出 Plan"),
			FirstPlan.Cells.IsEmpty() && FirstPlan.CanonicalLayoutHash == 0);

		FZeroEscapeGeneratedLevelPlan SecondPlan;
		FZeroEscapeGenerationReport SecondReport;
		TestFalse(TEXT("相同 Seed 的低预算失败必须可重放"),
			FGridLayoutSolver::Solve(
				Request, Settings, Weights, Seed, SecondPlan, SecondReport));
		TestTrue(TEXT("失败重放必须复现聚合指标和结构化失败值"),
			SecondReport.Failure == FirstReport.Failure
				&& SecondReport.ActualValue == FirstReport.ActualValue
				&& SecondReport.LimitValue == FirstReport.LimitValue
				&& SecondReport.Metrics.WfcSolveAttemptCount
					== FirstReport.Metrics.WfcSolveAttemptCount
				&& SecondReport.Metrics.WfcCandidateAttemptCount
					== FirstReport.Metrics.WfcCandidateAttemptCount
				&& SecondReport.Metrics.WfcBacktrackCount
					== FirstReport.Metrics.WfcBacktrackCount
				&& SecondPlan.Cells.IsEmpty());
		return true;
	}

	/**
	 * 用 3 难度 x 3 Flow x 32 Seed 验证 Profile -> Core -> Grid 的 V4 纯值集成边界，
	 * 并输出求解成本的 P50/P95/Max，防止只用少量固定 Seed 掩盖长尾失败。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeValidProfileSeedSweepTest,
		"Demo.PCG.Grid.ValidProfileSeedSweep",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeValidProfileSeedSweepTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		UZeroEscapeLevelGenerationProfile* Profile =
			NewObject<UZeroEscapeLevelGenerationProfile>(GetTransientPackage());
		BuildValidProfile(*Profile);

		FGenerationProfileSnapshot Snapshot;
		FZeroEscapeGenerationReport SnapshotReport;
		if (!TestTrue(TEXT("Seed Sweep 使用的 Profile 必须先通过规范快照校验"),
			FGenerationCore::BuildGenerationSnapshot(*Profile, Snapshot, SnapshotReport)))
		{
			AddError(SnapshotReport.Message);
			return true;
		}

		const TStaticArray<EZeroEscapeDifficulty, 3> Difficulties = {
			EZeroEscapeDifficulty::Easy,
			EZeroEscapeDifficulty::Normal,
			EZeroEscapeDifficulty::Hard };
		const TStaticArray<FName, 3> FlowIds = {
			FName(TEXT("EscapeOnly")),
			FName(TEXT("CollectAll")),
			FName(TEXT("CollectKOfN")) };
		const FGridLayoutSettings GridSettings =
			MakeGridSettings(Snapshot.SharedRouteConstraints);

		constexpr int32 SeedsPerCombination = 32;
		constexpr int32 ExpectedRuns = 3 * 3 * SeedsPerCombination;
		int32 SuccessfulRuns = 0;
		TArray<int32> SolveAttempts;
		TArray<int32> CandidateAttempts;
		TArray<int32> Backtracks;
		TArray<int32> Contradictions;
		TArray<int32> LeafRejections;
		TArray<int32> WalkableCells;
		TArray<double> PlanningMilliseconds;
		SolveAttempts.Reserve(ExpectedRuns);
		CandidateAttempts.Reserve(ExpectedRuns);
		Backtracks.Reserve(ExpectedRuns);
		Contradictions.Reserve(ExpectedRuns);
		LeafRejections.Reserve(ExpectedRuns);
		WalkableCells.Reserve(ExpectedRuns);
		PlanningMilliseconds.Reserve(ExpectedRuns);
		for (const EZeroEscapeDifficulty Difficulty : Difficulties)
		{
			for (const FName FlowId : FlowIds)
			{
				for (int32 SeedOffset = 0; SeedOffset < SeedsPerCombination; ++SeedOffset)
				{
					FZeroEscapeGenerationRequest GenerationRequest;
					GenerationRequest.Seed = 70000 + SeedOffset;
					GenerationRequest.Difficulty = Difficulty;
					GenerationRequest.FlowProfileId = FlowId;

					FResolvedProgressionSettings Resolved;
					FProgressionIntent Progression;
					FZeroEscapeGenerationSignature Signature;
					FZeroEscapeGenerationReport Report;
					const bool bPrepared = FGenerationCore::ResolveProgressionSettings(
						GenerationRequest, Snapshot, Resolved, Report)
							&& FGenerationCore::BuildGenerationSignature(
								GenerationRequest, Snapshot, Resolved, 1, Signature, Report)
							&& FGenerationCore::BuildProgressionIntent(
								GenerationRequest, Snapshot, Resolved, Progression, Report);
					if (!bPrepared)
					{
						AddError(FString::Printf(
							TEXT("合法组合在流程阶段失败：Difficulty=%d Flow=%s Seed=%d Message=%s"),
							static_cast<int32>(Difficulty), *FlowId.ToString(),
							GenerationRequest.Seed, *Report.Message));
						continue;
					}

					FGridLayoutRequest GridRequest;
					GridRequest.Signature = Signature;
					GridRequest.Progression = Progression;
					FZeroEscapeGeneratedLevelPlan Plan;
					if (!FGridLayoutSolver::Solve(
						GridRequest,
						GridSettings,
						Resolved.WfcShapeWeights,
						GenerationRequest.Seed,
						Plan,
						Report))
					{
						AddError(FString::Printf(
							TEXT("合法组合不应因 Seed 失败：Difficulty=%d Flow=%s Seed=%d Stage=%d Failure=%d SolveAttempts=%d CandidateAttempts=%d Contradictions=%d [Local=%d Count=%d MaxStraight=%d Connected=%d GlobalBan=%d] Backtracks=%d LeafRejects=%d Message=%s"),
							static_cast<int32>(Difficulty), *FlowId.ToString(),
							GenerationRequest.Seed, static_cast<int32>(Report.Stage),
							static_cast<int32>(Report.Failure),
							Report.Metrics.WfcSolveAttemptCount,
							Report.Metrics.WfcCandidateAttemptCount,
							Report.Metrics.WfcContradictionCount,
							Report.Metrics.WfcLocalAdjacencyContradictionCount,
							Report.Metrics.WfcCountContradictionCount,
							Report.Metrics.WfcMaxConsecutiveContradictionCount,
							Report.Metrics.WfcConnectedContradictionCount,
							Report.Metrics.WfcGlobalBanContradictionCount,
							Report.Metrics.WfcBacktrackCount,
							Report.Metrics.WfcCollapsedCandidateRejectionCount,
							*Report.Message));
						continue;
					}

					if (!IsEntirePlanReachableFromStart(Plan)
						|| Report.Metrics.WfcInvariantFailureCount != 0)
					{
						AddError(FString::Printf(
							TEXT("Seed Sweep 成功结果违反连通/WFC 不变量：Difficulty=%d Flow=%s Seed=%d"),
							static_cast<int32>(Difficulty), *FlowId.ToString(),
							GenerationRequest.Seed));
						continue;
					}
					if (Report.Metrics.WfcSolveAttemptCount <= 0
						|| Report.Metrics.WfcSolveAttemptCount > GridSettings.MaxWfcSolveAttempts
						|| Report.Metrics.WfcCandidateAttemptCount > GridSettings.MaxWfcCandidateAttempts
						|| Report.Metrics.WfcBacktrackCount > GridSettings.MaxWfcBacktrackCount)
					{
						AddError(FString::Printf(
							TEXT("Seed Sweep 成功结果超出 WFC 尝试或总预算：Difficulty=%d Flow=%s Seed=%d SolveAttempts=%d CandidateAttempts=%d Backtracks=%d"),
							static_cast<int32>(Difficulty),
							*FlowId.ToString(),
							GenerationRequest.Seed,
							Report.Metrics.WfcSolveAttemptCount,
							Report.Metrics.WfcCandidateAttemptCount,
							Report.Metrics.WfcBacktrackCount));
						continue;
					}

					SolveAttempts.Add(Report.Metrics.WfcSolveAttemptCount);
					CandidateAttempts.Add(Report.Metrics.WfcCandidateAttemptCount);
					Backtracks.Add(Report.Metrics.WfcBacktrackCount);
					Contradictions.Add(Report.Metrics.WfcContradictionCount);
					LeafRejections.Add(Report.Metrics.WfcCollapsedCandidateRejectionCount);
					WalkableCells.Add(Report.Metrics.WalkableCellCount);
					PlanningMilliseconds.Add(Report.Metrics.PlanningMilliseconds);
					++SuccessfulRuns;
				}
			}
		}

		TestEqual(TEXT("3 难度 x 3 Flow x 32 Seed 必须全部成功"),
			SuccessfulRuns, ExpectedRuns);
		if (SuccessfulRuns == ExpectedRuns)
		{
			UE_LOG(LogTemp, Display,
				TEXT("ZE_PCG_SEED_SWEEP schema=1 runs=%d success=%d "
					"solve_attempts[p50=%d p95=%d max=%d] "
					"candidate_attempts[p50=%d p95=%d max=%d] "
					"backtracks[p50=%d p95=%d max=%d] "
					"contradictions[p50=%d p95=%d max=%d] "
					"leaf_rejects[p50=%d p95=%d max=%d] "
					"walkable[p50=%d p95=%d max=%d] "
					"planning_ms[p50=%.3f p95=%.3f max=%.3f]"),
				ExpectedRuns,
				SuccessfulRuns,
				NearestRankPercentile(SolveAttempts, 0.50),
				NearestRankPercentile(SolveAttempts, 0.95),
				NearestRankPercentile(SolveAttempts, 1.00),
				NearestRankPercentile(CandidateAttempts, 0.50),
				NearestRankPercentile(CandidateAttempts, 0.95),
				NearestRankPercentile(CandidateAttempts, 1.00),
				NearestRankPercentile(Backtracks, 0.50),
				NearestRankPercentile(Backtracks, 0.95),
				NearestRankPercentile(Backtracks, 1.00),
				NearestRankPercentile(Contradictions, 0.50),
				NearestRankPercentile(Contradictions, 0.95),
				NearestRankPercentile(Contradictions, 1.00),
				NearestRankPercentile(LeafRejections, 0.50),
				NearestRankPercentile(LeafRejections, 0.95),
				NearestRankPercentile(LeafRejections, 1.00),
				NearestRankPercentile(WalkableCells, 0.50),
				NearestRankPercentile(WalkableCells, 0.95),
				NearestRankPercentile(WalkableCells, 1.00),
				NearestRankPercentile(PlanningMilliseconds, 0.50),
				NearestRankPercentile(PlanningMilliseconds, 0.95),
				NearestRankPercentile(PlanningMilliseconds, 1.00));
		}
		return true;
	}
}

#endif
