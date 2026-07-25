// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationTests.cpp
 * 职责：验证 V3.2 Runtime Grid-WFC 的纯值契约、资产校验、确定性、结构展开与失败原子性。
 * 边界：大多数测试只创建瞬态 UObject/StaticMesh；末尾项目烟测只读已落盘 HydroLab 资产。
 *       测试不创建 World、不 Spawn Actor/HISM，也不执行碰撞、导航或 PIE。
 * 状态 Owner：所有夹具和输出都只属于单个 RunTest 调用，不跨测试共享随机流或求解状态。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/Queue.h"
#include "Containers/StaticArray.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include "ZeroEscapeGenerationCore.h"
#include "ZeroEscapeGridLayoutSolver.h"
#include "ZeroEscapeWfcSolver.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace
	{
		/** Transform 点位比较只用于揭示乘法顺序错误，不参与运行时容差。 */
		constexpr double TransformTolerance = 0.01;

		/**
		 * 构造包含三档难度和三种 Flow 的最小合法 Profile。
		 * 数组顺序刻意使用正常作者顺序；排序测试会在自己的夹具中主动打乱它。
		 */
		void BuildValidProfile(UZeroEscapeLevelGenerationProfile& Profile)
		{
			Profile.ProfileVersion = 3;
			Profile.SharedRouteConstraints = FZeroEscapeSharedRouteConstraints();
			Profile.SharedRouteConstraints.GridSize = FIntPoint(24, 16);
			Profile.SharedRouteConstraints.LogicalTileSizeCm = 600.0;
			Profile.SharedRouteConstraints.RoomSizeTiles = 2;
			Profile.SharedRouteConstraints.ObjectiveProgressBandCount = 3;
			Profile.SharedRouteConstraints.OptionalEnvelopeRadius = 1;
			Profile.SharedRouteConstraints.MaxRequiredRouteLengthTiles = 64;
			Profile.SharedRouteConstraints.MaxRequiredRouteExtraTiles = 24;
			Profile.SharedRouteConstraints.GameplayAnchorHeightCm = 100.0;

			Profile.Difficulties.Reset();
			FZeroEscapeDifficultyDefinition Easy;
			Easy.Difficulty = EZeroEscapeDifficulty::Easy;
			Easy.MaxOptionalSideBranches = 1;
			Easy.MaxOptionalForwardLinks = 0;
			Easy.ObjectiveCandidateCount = 2;
			Easy.RequiredObjectiveCount = 1;
			Profile.Difficulties.Add(Easy);

			FZeroEscapeDifficultyDefinition Normal = Easy;
			Normal.Difficulty = EZeroEscapeDifficulty::Normal;
			Normal.MaxOptionalSideBranches = 2;
			Normal.MaxOptionalForwardLinks = 1;
			Normal.ObjectiveCandidateCount = 3;
			Normal.RequiredObjectiveCount = 2;
			Profile.Difficulties.Add(Normal);

			FZeroEscapeDifficultyDefinition Hard = Normal;
			Hard.Difficulty = EZeroEscapeDifficulty::Hard;
			Hard.MaxOptionalSideBranches = 4;
			Hard.MaxOptionalForwardLinks = 2;
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

			Profile.WfcShapeWeights = FZeroEscapeWfcShapeWeights();
		}

		/**
		 * 构造不依赖磁盘素材的合法 Presentation。
		 * 三个必填结构类型可以共享一个瞬态 Mesh，因为本测试只验证配置契约，不验证几何 Bounds。
		 */
		void BuildValidTransientPresentation(UZeroEscapePresentationProfile& Presentation)
		{
			Presentation.PresentationVersion = 2;
			Presentation.StructureUnitSizeCm = 300.0;
			Presentation.FloorTopZCm = 0.0;
			Presentation.WallBaseZCm = 5.0;
			Presentation.CeilingPivotZCm = 305.0;

			UStaticMesh* SharedTransientMesh = NewObject<UStaticMesh>(GetTransientPackage());
			Presentation.Floor = FZeroEscapeStructureMeshBinding();
			Presentation.Ceiling = FZeroEscapeStructureMeshBinding();
			Presentation.Wall = FZeroEscapeStructureMeshBinding();
			Presentation.WallTopTrim = FZeroEscapeStructureMeshBinding();
			Presentation.Pillar = FZeroEscapeStructureMeshBinding();
			Presentation.Floor.StaticMesh = SharedTransientMesh;
			Presentation.Ceiling.StaticMesh = SharedTransientMesh;
			Presentation.Wall.StaticMesh = SharedTransientMesh;
			Presentation.Floor.CollisionProfileName = TEXT("BlockAll");
			Presentation.Ceiling.CollisionProfileName = TEXT("BlockAll");
			Presentation.Wall.CollisionProfileName = TEXT("BlockAll");
		}

		/** 把 Profile 的共享空间参数复制为 Solver 只读纯值设置。 */
		FGridLayoutSettings MakeGridSettings(const FZeroEscapeSharedRouteConstraints& Source)
		{
			FGridLayoutSettings Settings;
			Settings.GridSize = Source.GridSize;
			Settings.LogicalTileSizeCm = FMath::RoundToInt(Source.LogicalTileSizeCm);
			Settings.RoomSizeTiles = Source.RoomSizeTiles;
			Settings.ObjectiveProgressBandCount = Source.ObjectiveProgressBandCount;
			Settings.OptionalEnvelopeRadius = Source.OptionalEnvelopeRadius;
			Settings.MaxRequiredRouteLengthTiles = Source.MaxRequiredRouteLengthTiles;
			Settings.MaxRequiredRouteExtraTiles = Source.MaxRequiredRouteExtraTiles;
			Settings.GameplayAnchorHeightCm = Source.GameplayAnchorHeightCm;
			return Settings;
		}

		/** 构造供 Grid 单元测试直接消费的稳定 Signature，不引入 UObject 依赖。 */
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
			Signature.GenerationProfileVersion = 3;
			Signature.FlowVersion = 1;
			Signature.PresentationVersion = 1;
			return Signature;
		}

		/**
		 * 构造只含 Start/Exit 的最小 Escape 请求。
		 * 该夹具用于确定性测试，关闭 Optional 后结果不依赖任何路口数量配额。
		 */
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
			Request.MaxOptionalSideBranches = 0;
			Request.MaxOptionalForwardLinks = 0;
			return Request;
		}

		/**
		 * 构造一个 0-based 进度带中的 2x2 CollectAll 房间请求。
		 * 房间使用 StableObjectiveId=0，便于从最终 ObjectiveBinding 反查对应区域。
		 */
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
			Request.MaxOptionalSideBranches = 2;
			Request.MaxOptionalForwardLinks = 1;
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

		/** 构造“Outside 包围一条三格 Required 直线”的确定性见证夹具。 */
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

		/** 验证稠密 WFC 输出的边界关闭和每条公共边镜像一致。 */
		bool AreDenseOpeningMasksConsistent(
			const FIntPoint GridSize,
			const TArray<uint8>& Masks)
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

		/**
		 * 从 Start 沿 OpeningMask 做 BFS，确认最终所有非空 Cell 和 Landmark 都位于同一连通分量。
		 * 这项检查独立于 Solver 内部队列，能捕获剪枝后残留孤岛或单向开口。
		 */
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

		/** 构造 2x2 Tile 环路夹具，其中底部内部边关闭，用于验证共享墙、端点柱和直墙去重。 */
		FZeroEscapeGeneratedLevelPlan MakeStructureFixturePlan()
		{
			FZeroEscapeGeneratedLevelPlan Plan;
			Plan.GridSize = FIntPoint(2, 2);
			Plan.LogicalTileSizeCm = 600.0;
			const TArray<TPair<FIntPoint, uint8>> Definitions = {
				{FIntPoint(0, 0), Grid::DirectionBit(0)},
				{FIntPoint(1, 0), Grid::DirectionBit(0)},
				{FIntPoint(0, 1), static_cast<uint8>(Grid::DirectionBit(2) | Grid::DirectionBit(1))},
				{FIntPoint(1, 1), static_cast<uint8>(Grid::DirectionBit(2) | Grid::DirectionBit(3))} };
			for (const TPair<FIntPoint, uint8>& Definition : Definitions)
			{
				FZeroEscapeCollapsedTile& Cell = Plan.Cells.AddDefaulted_GetRef();
				Cell.StableCellId = Plan.Cells.Num() - 1;
				Cell.GridCoordinate = Definition.Key;
				Cell.OpeningMask = Definition.Value;
				Cell.RegionId = 0;
				Cell.RegionKind = EZeroEscapeGridRegionKind::Corridor;
			}
			return Plan;
		}

		/** 统计某一种规范结构件数量；测试不依赖具体 StaticMesh。 */
		int32 CountStructureKind(
			const TArray<FStructureInstance>& Instances,
			const EStructurePieceKind Kind)
		{
			int32 Count = 0;
			for (const FStructureInstance& Instance : Instances)
			{
				Count += Instance.Kind == Kind ? 1 : 0;
			}
			return Count;
		}
	}

	/**
	 * 冻结素材 Pivot -> 规范结构 -> Generator Root 的组合顺序。
	 * V3.2 不再计算 Portal 对齐 Transform，因此本测试只保护仍真实存在的表现边界。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeTransformCompositionTest,
		"Demo.PCG.Unit.TransformComposition",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeTransformCompositionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FTransform PivotCorrection(
			FRotator(0.0, -31.0, 0.0), FVector(27.0, -13.0, 8.0), FVector::OneVector);
		const FTransform CanonicalStructure(
			FRotator(0.0, 90.0, 0.0), FVector(600.0, 300.0, 5.0), FVector::OneVector);
		const FTransform GeneratorRoot(
			FRotator(0.0, 23.0, 0.0), FVector(-450.0, 810.0, 40.0), FVector::OneVector);
		const FTransform PresentationLocal = PivotCorrection * CanonicalStructure;
		const FTransform PresentationWorld = PresentationLocal * GeneratorRoot;

		const TStaticArray<FVector, 4> TestPoints = {
			FVector::ZeroVector, FVector(100.0, 0.0, 0.0),
			FVector(0.0, 70.0, 30.0), FVector(11.0, -7.0, 3.0) };
		for (const FVector& Point : TestPoints)
		{
			const FVector Expected = GeneratorRoot.TransformPosition(
				CanonicalStructure.TransformPosition(PivotCorrection.TransformPosition(Point)));
			TestTrue(TEXT("Pivot -> Canonical -> Root 的逐点结果必须与组合 Transform 一致"),
				PresentationWorld.TransformPosition(Point).Equals(Expected, TransformTolerance));
		}
		const FVector Probe = TestPoints[TestPoints.Num() - 1];
		const FVector WrongOrder = GeneratorRoot.TransformPosition(
			PivotCorrection.TransformPosition(CanonicalStructure.TransformPosition(Probe)));
		TestFalse(TEXT("测试夹具必须能识别 Pivot 与规范结构顺序被交换"),
			WrongOrder.Equals(PresentationWorld.TransformPosition(Probe), TransformTolerance));
		TestTrue(TEXT("合法组合必须保持有限 Unit Scale"),
			FGenerationCore::IsFiniteUnitScaleTransform(PresentationWorld));
		TestFalse(TEXT("表现绑定不得通过 Scale 适配素材尺寸"),
			FGenerationCore::IsFiniteUnitScaleTransform(
				FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0, 1.0, 1.0))));
		return true;
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

	/** 验证 Profile/Grid 容量、K/N、WFC 权重和 600:300 Presentation 的前置失败契约。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeProfileAndPresentationContractsTest,
		"Demo.PCG.Unit.Assets.ProfileAndPresentationContracts",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeProfileAndPresentationContractsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		UZeroEscapePresentationProfile* Presentation = NewObject<UZeroEscapePresentationProfile>();
		FString Error;
		BuildValidProfile(*Profile);
		BuildValidTransientPresentation(*Presentation);
		TestTrue(TEXT("合法 V3.2 Profile 必须通过配置校验"), Profile->IsConfigured(Error));
		TestTrue(TEXT("瞬态必填 Mesh 足以验证 Presentation 结构契约"),
			Presentation->IsConfigured(600.0, Error));
		TestTrue(TEXT("Profile 与 Presentation 的 600:300 联合契约必须通过"),
			ValidateZeroEscapeGenerationAssetSet(*Profile, *Presentation, Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[1].RequiredObjectiveCount =
			Profile->Difficulties[1].ObjectiveCandidateCount + 1;
		TestFalse(TEXT("K>N 必须在消费随机数前被拒绝"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[2].ObjectiveCandidateCount = 7;
		Profile->Difficulties[2].RequiredObjectiveCount = 4;
		TestFalse(TEXT("候选目标数超过 3 个进度带 x 双 Lane 容量时必须失败"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.GridSize = FIntPoint(12, 9);
		TestFalse(TEXT("无法容纳首版 2x2 房间安全间距的 Grid 应在 Profile 阶段失败"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->WfcShapeWeights.CrossWeight = 0;
		TestFalse(TEXT("任何形态权重为零都会移除状态，必须被拒绝"), Profile->IsConfigured(Error));

		BuildValidTransientPresentation(*Presentation);
		Presentation->StructureUnitSizeCm = 299.0;
		TestFalse(TEXT("Presentation 不满足 600:300 固定 2:1 契约时必须失败"),
			Presentation->IsConfigured(600.0, Error));

		BuildValidTransientPresentation(*Presentation);
		Presentation->Floor.StaticMesh = nullptr;
		TestFalse(TEXT("Floor/Wall/Ceiling 中任一必填 Mesh 缺失必须失败"),
			Presentation->IsConfigured(600.0, Error));

		BuildValidTransientPresentation(*Presentation);
		Presentation->Wall.PivotCorrection.SetScale3D(FVector(1.0, 2.0, 1.0));
		TestFalse(TEXT("PivotCorrection 使用非 Unit Scale 必须失败"),
			Presentation->IsConfigured(600.0, Error));
		return true;
	}

	/** 验证 Snapshot 只按稳定 Difficulty/Flow 身份排序，不受 DataAsset 数组作者顺序影响。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeSnapshotStableOrderingTest,
		"Demo.PCG.Unit.Core.SnapshotStableOrdering",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeSnapshotStableOrderingTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		Profile->Difficulties.Swap(0, 2);
		Profile->Flows.Swap(0, 2);

		FGenerationProfileSnapshot Snapshot;
		FZeroEscapeGenerationReport Report;
		TestTrue(TEXT("打乱作者顺序的合法 Profile 仍应能建立纯值 Snapshot"),
			FGenerationCore::BuildGenerationSnapshot(*Profile, Snapshot, Report));
		TestEqual(TEXT("Snapshot 必须保留三档难度"), Snapshot.Difficulties.Num(), 3);
		TestEqual(TEXT("Snapshot 必须保留三种 Flow"), Snapshot.Flows.Num(), 3);
		if (Snapshot.Difficulties.Num() == 3)
		{
			TestTrue(TEXT("Difficulty 必须按稳定枚举值排序"),
				Snapshot.Difficulties[0].Difficulty == EZeroEscapeDifficulty::Easy
				&& Snapshot.Difficulties[1].Difficulty == EZeroEscapeDifficulty::Normal
				&& Snapshot.Difficulties[2].Difficulty == EZeroEscapeDifficulty::Hard);
		}
		if (Snapshot.Flows.Num() == 3)
		{
			TestTrue(TEXT("Flow 必须按 StableFlowId 词法排序"),
				Snapshot.Flows[0].StableFlowId == TEXT("CollectAll")
				&& Snapshot.Flows[1].StableFlowId == TEXT("CollectKOfN")
				&& Snapshot.Flows[2].StableFlowId == TEXT("EscapeOnly"));
		}
		return true;
	}

	/**
	 * 验证 Escape/CollectAll/K-of-N 的轻量 Intent 语义、0-based 进度槽和同 Seed 确定性。
	 * 测试不再建立旧抽象 Graph，也不为目标位置执行回退搜索。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeProgressionIntentContractsAndDeterminismTest,
		"Demo.PCG.Unit.Core.ProgressionIntentContractsAndDeterminism",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeProgressionIntentContractsAndDeterminismTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		FGenerationProfileSnapshot Snapshot;
		FZeroEscapeGenerationReport Report;
		if (!TestTrue(TEXT("Progression 测试需要合法 Snapshot"),
			FGenerationCore::BuildGenerationSnapshot(*Profile, Snapshot, Report)))
		{
			return true;
		}

		FZeroEscapeGenerationRequest EscapeRequest;
		EscapeRequest.Seed = 24680;
		EscapeRequest.Difficulty = EZeroEscapeDifficulty::Easy;
		EscapeRequest.FlowProfileId = TEXT("EscapeOnly");
		FResolvedProgressionSettings EscapeSettings;
		TestTrue(TEXT("EscapeOnly 必须能解析"), FGenerationCore::ResolveProgressionSettings(
			EscapeRequest, Snapshot, EscapeSettings, Report));
		TestTrue(TEXT("EscapeOnly 强制 K=N=0"), EscapeSettings.ObjectiveCandidateCount == 0
			&& EscapeSettings.RequiredObjectiveCount == 0);

		FZeroEscapeGenerationRequest CollectAllRequest = EscapeRequest;
		CollectAllRequest.Difficulty = EZeroEscapeDifficulty::Normal;
		CollectAllRequest.FlowProfileId = TEXT("CollectAll");
		FResolvedProgressionSettings CollectAllSettings;
		TestTrue(TEXT("CollectAll 必须能解析"), FGenerationCore::ResolveProgressionSettings(
			CollectAllRequest, Snapshot, CollectAllSettings, Report));
		TestTrue(TEXT("CollectAll 必须解析为 K=N"),
			CollectAllSettings.ObjectiveCandidateCount == 3
			&& CollectAllSettings.RequiredObjectiveCount == 3);

		FZeroEscapeGenerationRequest KOfNRequest = CollectAllRequest;
		KOfNRequest.FlowProfileId = TEXT("CollectKOfN");
		FResolvedProgressionSettings KOfNSettings;
		TestTrue(TEXT("CollectKOfN 必须能解析"), FGenerationCore::ResolveProgressionSettings(
			KOfNRequest, Snapshot, KOfNSettings, Report));
		TestTrue(TEXT("Normal CollectKOfN 必须保持 K=2/N=3"),
			KOfNSettings.RequiredObjectiveCount == 2
			&& KOfNSettings.ObjectiveCandidateCount == 3);

		FProgressionIntent FirstIntent;
		FProgressionIntent SecondIntent;
		TestTrue(TEXT("第一次 Progression Intent 构建必须成功"),
			FGenerationCore::BuildProgressionIntent(
				KOfNRequest, Snapshot, KOfNSettings, FirstIntent, Report));
		TestTrue(TEXT("第二次同输入 Progression Intent 构建必须成功"),
			FGenerationCore::BuildProgressionIntent(
				KOfNRequest, Snapshot, KOfNSettings, SecondIntent, Report));
		const int64 FirstHash = FGenerationCore::ComputeCanonicalProgressionHash(FirstIntent);
		const int64 SecondHash = FGenerationCore::ComputeCanonicalProgressionHash(SecondIntent);
		TestTrue(TEXT("合法 Progression Hash 必须非零"), FirstHash != 0);
		TestEqual(TEXT("同 Seed/Flow/Difficulty 必须复现 Progression Hash"), FirstHash, SecondHash);
		TestEqual(TEXT("Intent 必须包含 Start、全部 N 个 Objective 与 Exit"),
			FirstIntent.Landmarks.Num(), KOfNSettings.ObjectiveCandidateCount + 2);

		int32 ObjectiveCount = 0;
		int32 PreviousStableId = INDEX_NONE;
		for (const FProgressionLandmark& Landmark : FirstIntent.Landmarks)
		{
			TestTrue(TEXT("Landmark 必须按严格递增 Stable Id 导出"),
				Landmark.StableLandmarkId > PreviousStableId);
			PreviousStableId = Landmark.StableLandmarkId;
			if (Landmark.Kind == EProgressionLandmarkKind::Objective)
			{
				++ObjectiveCount;
				TestTrue(TEXT("Objective 必须使用 Grid 共同消费的 0-based 进度带"),
					Landmark.ProgressBandIndex >= 0
					&& Landmark.ProgressBandIndex
						< Snapshot.SharedRouteConstraints.ObjectiveProgressBandCount);
				TestTrue(TEXT("Objective Lane 必须为下/上二选一"),
					Landmark.LaneIndex == 0 || Landmark.LaneIndex == 1);
			}
		}
		TestEqual(TEXT("K 只改变完成条件，全部 N 个候选目标仍必须生成"),
			ObjectiveCount, KOfNSettings.ObjectiveCandidateCount);
		return true;
	}

	/** 验证四个稳定随机职责互相隔离，且同 Seed/版本/Domain 复现完全相同序列。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeRandomDomainIsolationTest,
		"Demo.PCG.Unit.Core.RandomDomainIsolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeRandomDomainIsolationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const TStaticArray<ERandomDomain, 4> Domains = {
			ERandomDomain::Landmark, ERandomDomain::OptionalLayout,
			ERandomDomain::WfcLayout, ERandomDomain::Presentation };
		TStaticArray<TArray<uint32>, 4> Sequences;
		for (int32 DomainIndex = 0; DomainIndex < Domains.Num(); ++DomainIndex)
		{
			FRandomStream First = FGenerationCore::MakeRandomStream(
				13579, GAlgorithmVersion, Domains[DomainIndex]);
			FRandomStream Second = FGenerationCore::MakeRandomStream(
				13579, GAlgorithmVersion, Domains[DomainIndex]);
			for (int32 DrawIndex = 0; DrawIndex < 8; ++DrawIndex)
			{
				const uint32 FirstValue = First.GetUnsignedInt();
				Sequences[DomainIndex].Add(FirstValue);
				TestEqual(TEXT("同一稳定随机域必须复现逐次抽样"),
					FirstValue, Second.GetUnsignedInt());
			}
		}
		for (int32 A = 0; A < Domains.Num(); ++A)
		{
			for (int32 B = A + 1; B < Domains.Num(); ++B)
			{
				TestFalse(TEXT("不同随机职责不得共享同一派生序列"), Sequences[A] == Sequences[B]);
			}
		}
		return true;
	}

	/**
	 * 验证 RequiredOpen 双向/界内/不冲突不变量，以及完整状态集提供的构造性见证解。
	 * 三格直线外侧全为 Outside，因此成功结果不可能含 T/Cross，也证明路口没有最低配额。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeConstructiveConstraintContractsTest,
		"Demo.PCG.WFC.ConstructiveConstraintContracts",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeConstructiveConstraintContractsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FIntPoint GridSize(5, 3);
		const TArray<FGridCellConstraint> ValidConstraints = MakeConstructiveWfcFixture(GridSize);
		FString Error;
		TestTrue(TEXT("双向、非零、界内且无冲突的 Required 约束必须被证明可解"),
			FWfcSolver::ValidateGuaranteedSolvableConstraints(GridSize, ValidConstraints, Error));

		TArray<FGridCellConstraint> Asymmetric = ValidConstraints;
		Asymmetric[Grid::ToIndex(FIntPoint(2, 1), GridSize)].RequiredOpenMask &=
			static_cast<uint8>(~Grid::DirectionBit(3));
		TestFalse(TEXT("删除 RequiredOpen 的反向镜像必须失败"),
			FWfcSolver::ValidateGuaranteedSolvableConstraints(GridSize, Asymmetric, Error));

		TArray<FGridCellConstraint> Conflicting = ValidConstraints;
		FGridCellConstraint& ConflictCell = Conflicting[Grid::ToIndex(FIntPoint(2, 1), GridSize)];
		ConflictCell.RequiredClosedMask |= Grid::DirectionBit(1);
		TestFalse(TEXT("同一方向 RequiredOpen/RequiredClosed 冲突必须失败"),
			FWfcSolver::ValidateGuaranteedSolvableConstraints(GridSize, Conflicting, Error));

		TArray<FGridCellConstraint> ZeroRequired = ValidConstraints;
		FGridCellConstraint& ZeroCell = ZeroRequired[Grid::ToIndex(FIntPoint(0, 0), GridSize)];
		ZeroCell.Domain = EGridCellDomain::Required;
		ZeroCell.RequiredOpenMask = 0;
		TestFalse(TEXT("Required Cell 没有任何必开边必须失败"),
			FWfcSolver::ValidateGuaranteedSolvableConstraints(GridSize, ZeroRequired, Error));

		const TArray<FTileVariant> Variants = MakeCanonicalVariantArray(FZeroEscapeWfcShapeWeights());
		FRandomStream Random(12345);
		TArray<uint8> Output;
		FZeroEscapeGenerationReport Report;
		TestTrue(TEXT("构造性三格直线必须一次求解成功"), FWfcSolver::Solve(
			GridSize, ValidConstraints, Variants, Random, Output, Report));
		TestTrue(TEXT("构造性结果必须保持全部公共边镜像一致"),
			AreDenseOpeningMasksConsistent(GridSize, Output));
		TestEqual(TEXT("直线左端只能向 East 开口"),
			Output[Grid::ToIndex(FIntPoint(1, 1), GridSize)], Grid::DirectionBit(1));
		TestEqual(TEXT("直线中段只能 East+West 贯通"),
			Output[Grid::ToIndex(FIntPoint(2, 1), GridSize)],
			static_cast<uint8>(Grid::DirectionBit(1) | Grid::DirectionBit(3)));
		TestEqual(TEXT("直线右端只能向 West 开口"),
			Output[Grid::ToIndex(FIntPoint(3, 1), GridSize)], Grid::DirectionBit(3));
		return true;
	}

	/**
	 * 在完全 Optional 的小网格上批量运行不同权重和 Seed。
	 * 成功标准是每次单次坍缩、无空 Domain、无不变量失败；不要求任何路口数量。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeWeightedCollapseNeedsNoBacktrackingTest,
		"Demo.PCG.WFC.WeightedCollapseNeedsNoBacktracking",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeWeightedCollapseNeedsNoBacktrackingTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FIntPoint GridSize(6, 4);
		const TArray<FGridCellConstraint> Constraints =
			MakeDenseConstraints(GridSize, EGridCellDomain::Optional);
		FZeroEscapeWfcShapeWeights Weights;
		Weights.EmptyWeight = 20;
		Weights.DeadEndWeight = 35;
		Weights.StraightWeight = 90;
		Weights.CornerWeight = 70;
		Weights.TJunctionWeight = 7;
		Weights.CrossWeight = 2;
		const TArray<FTileVariant> Variants = MakeCanonicalVariantArray(Weights);

		bool bObservedAnyNonEmpty = false;
		for (int32 Seed = 0; Seed < 32; ++Seed)
		{
			FRandomStream Random = FGenerationCore::MakeRandomStream(
				Seed, GAlgorithmVersion, ERandomDomain::WfcLayout);
			TArray<uint8> Output;
			FZeroEscapeGenerationReport Report;
			const bool bSolved = FWfcSolver::Solve(
				GridSize, Constraints, Variants, Random, Output, Report);
			if (!bSolved)
			{
				AddError(FString::Printf(
					TEXT("完整 16-mask WFC 不应因 Seed=%d 失败：%s"), Seed, *Report.Message));
				continue;
			}
			TestTrue(TEXT("每个批量 Seed 的导出边必须保持对称且不越界"),
				AreDenseOpeningMasksConsistent(GridSize, Output));
			TestEqual(TEXT("成功 WFC 不变量失败计数必须为零"),
				Report.Metrics.WfcInvariantFailureCount, 0);
			TestTrue(TEXT("Optional Grid 必须实际执行至少一次加权观察"),
				Report.Metrics.WfcObservationCount > 0);
			bObservedAnyNonEmpty |= Output.ContainsByPredicate([](const uint8 Mask) { return Mask != 0; });
		}
		TestTrue(TEXT("批量权重夹具应至少观察到一个非 Empty Optional Cell"), bObservedAnyNonEmpty);
		return true;
	}

	/**
	 * 验证正交骨架、2x2 房内部开口、全部玩法地标连通，以及 Optional 剪枝后的零孤岛结果。
	 * 多 Seed 只检查构造性成功，不把 T/Cross/Corner 数量当作易波动的通过门槛。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeRouteRoomConnectivityAndPruneTest,
		"Demo.PCG.Grid.RouteRoomConnectivityAndPrune",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeRouteRoomConnectivityAndPruneTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FGridLayoutSettings Settings;
		Settings.GridSize = FIntPoint(24, 16);
		Settings.LogicalTileSizeCm = 600;
		Settings.RoomSizeTiles = 2;
		Settings.ObjectiveProgressBandCount = 3;
		Settings.OptionalEnvelopeRadius = 1;
		Settings.MaxRequiredRouteLengthTiles = 64;
		Settings.MaxRequiredRouteExtraTiles = 24;
		FZeroEscapeWfcShapeWeights Weights;

		FZeroEscapeGeneratedLevelPlan InspectedPlan;
		for (int32 Seed = 100; Seed < 108; ++Seed)
		{
			FGridLayoutRequest Request = MakeSingleObjectiveGridRequest(Seed);
			FZeroEscapeGeneratedLevelPlan Plan;
			FZeroEscapeGenerationReport Report;
			const bool bSolved = FGridLayoutSolver::Solve(
				Request, Settings, Weights, Seed, Plan, Report);
			if (!bSolved)
			{
				AddError(FString::Printf(
					TEXT("合法 Gate/正交路线不应因 Seed=%d 失败：Stage=%d Failure=%d %s"),
					Seed, static_cast<int32>(Report.Stage), static_cast<int32>(Report.Failure),
					*Report.Message));
				continue;
			}
			TestTrue(TEXT("剪枝后全部非空 Tile 与 Landmark 必须从 Start 可达"),
				IsEntirePlanReachableFromStart(Plan));
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
		TestEqual(TEXT("首版 Objective 房必须恰好占用 2x2 四个逻辑 Tile"), RoomCells.Num(), 4);
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

	/**
	 * 用 2x2 环路和一条内部闭边验证 600 Tile -> 300 构件展开。
	 * 预期：共享内部闭边只生成一份墙；长直墙的 300 接缝不刷柱；四角、T 点和端点生成柱。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeExpansionDedupAndPillarsTest,
		"Demo.PCG.Structure.ExpansionDedupAndPillars",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeExpansionDedupAndPillarsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const FZeroEscapeGeneratedLevelPlan Plan = MakeStructureFixturePlan();
		FCanonicalStructureSettings Settings;
		Settings.LogicalTileSizeCm = 600;
		Settings.StructureUnitSizeCm = 300;
		Settings.FloorTopZCm = 0.0;
		Settings.WallBaseZCm = 5.0;
		Settings.CeilingPivotZCm = 305.0;
		TArray<FStructureInstance> Instances;
		FString Error;
		TestTrue(TEXT("合法四 Tile 夹具必须能展开规范结构"),
			BuildCanonicalStructureInstances(Plan, Settings, Instances, Error));
		TestEqual(TEXT("四个非 Empty Tile 必须展开 4x4=16 块 Floor"),
			CountStructureKind(Instances, EStructurePieceKind::Floor), 16);
		TestEqual(TEXT("四个非 Empty Tile 必须展开 4x4=16 块 Ceiling"),
			CountStructureKind(Instances, EStructurePieceKind::Ceiling), 16);
		TestEqual(TEXT("外周 16 段加共享内部闭边 2 段应只生成 18 块 Wall"),
			CountStructureKind(Instances, EStructurePieceKind::Wall), 18);
		TestEqual(TEXT("WallTopTrim 必须与去重后的 Wall 一一对应"),
			CountStructureKind(Instances, EStructurePieceKind::WallTopTrim), 18);
		TestEqual(TEXT("直墙接缝不刷柱；四角、T 点和内部墙端点共六根 Pillar"),
			CountStructureKind(Instances, EStructurePieceKind::Pillar), 6);

		for (const FStructureInstance& Instance : Instances)
		{
			TestTrue(TEXT("所有规范结构 Transform 必须保持有限 Unit Scale"),
				FGenerationCore::IsFiniteUnitScaleTransform(Instance.CanonicalLocalTransform));
		}

		FCanonicalStructureSettings InvalidSettings = Settings;
		InvalidSettings.StructureUnitSizeCm = 250;
		Instances.AddDefaulted();
		TestFalse(TEXT("非 600:300 尺度必须原子失败"),
			BuildCanonicalStructureInstances(Plan, InvalidSettings, Instances, Error));
		TestTrue(TEXT("结构展开失败不得泄漏上一次或半成品实例"), Instances.IsEmpty());
		return true;
	}

	/**
	 * 验证 Grid Solver 同输入确定性、失败输出原子清空和失败后的无状态恢复。
	 * 测试不设置布局重试预算；非法配置必须立即失败，合法请求只执行一次求解。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeLayoutDeterminismAndStateIsolationTest,
		"Demo.PCG.Unit.Layout.DeterminismAndStateIsolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeLayoutDeterminismAndStateIsolationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const int32 Seed = 54321;
		const FGridLayoutRequest Request = MakeEscapeGridRequest(Seed);
		FGridLayoutSettings Settings;
		Settings.GridSize = FIntPoint(24, 16);
		Settings.OptionalEnvelopeRadius = 0;
		Settings.MaxRequiredRouteLengthTiles = 64;
		Settings.MaxRequiredRouteExtraTiles = 24;
		FZeroEscapeWfcShapeWeights Weights;

		FZeroEscapeGeneratedLevelPlan FirstPlan;
		FZeroEscapeGenerationReport FirstReport;
		if (!TestTrue(TEXT("最小 Escape Grid 必须一次求解成功"),
			FGridLayoutSolver::Solve(Request, Settings, Weights, Seed, FirstPlan, FirstReport)))
		{
			AddError(FirstReport.Message);
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
		if (SecondPlan.Cells.Num() == FirstPlan.Cells.Num())
		{
			for (int32 Index = 0; Index < FirstPlan.Cells.Num(); ++Index)
			{
				TestTrue(TEXT("同输入必须复现每格坐标与 OpeningMask"),
					SecondPlan.Cells[Index].GridCoordinate == FirstPlan.Cells[Index].GridCoordinate
					&& SecondPlan.Cells[Index].OpeningMask == FirstPlan.Cells[Index].OpeningMask);
			}
		}

		FGridLayoutSettings InvalidSettings = Settings;
		InvalidSettings.GridSize = FIntPoint(10, 8);
		FZeroEscapeGeneratedLevelPlan FailedPlan = FirstPlan;
		FZeroEscapeGenerationReport FailedReport;
		TestFalse(TEXT("低于 Grid V3.2 安全容量的配置必须立即失败"),
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
	 * 用同一份通过资产校验的 Profile 扫描多 Seed、三档难度和三种通关规则。
	 * 该测试专门验证 V3.2 的构造性承诺：合法配置不应依赖换 Seed、布局重试或 WFC 回溯才能成功。
	 * 路口数量可以随 Seed 波动，但每次结果都必须保持全图连通、K-of-N 可完成且不变量失败为零。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeValidProfileSeedSweepTest,
		"Demo.PCG.Grid.ValidProfileSeedSweep",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeValidProfileSeedSweepTest::RunTest(const FString& Parameters)
	{
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

		int32 SuccessfulRuns = 0;
		for (const EZeroEscapeDifficulty Difficulty : Difficulties)
		{
			for (const FName FlowId : FlowIds)
			{
				for (int32 SeedOffset = 0; SeedOffset < 32; ++SeedOffset)
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
						break;
					}

					FGridLayoutRequest GridRequest;
					GridRequest.Signature = Signature;
					GridRequest.Progression = Progression;
					GridRequest.MaxOptionalSideBranches = Resolved.MaxOptionalSideBranches;
					GridRequest.MaxOptionalForwardLinks = Resolved.MaxOptionalForwardLinks;
					FZeroEscapeGeneratedLevelPlan Plan;
					if (!FGridLayoutSolver::Solve(
						GridRequest,
						GridSettings,
						Snapshot.WfcShapeWeights,
						GenerationRequest.Seed,
						Plan,
						Report))
					{
						AddError(FString::Printf(
							TEXT("合法组合不应因 Seed 失败：Difficulty=%d Flow=%s Seed=%d Stage=%d Failure=%d Message=%s"),
							static_cast<int32>(Difficulty), *FlowId.ToString(),
							GenerationRequest.Seed, static_cast<int32>(Report.Stage),
							static_cast<int32>(Report.Failure), *Report.Message));
						break;
					}

					if (!IsEntirePlanReachableFromStart(Plan)
						|| Report.Metrics.WfcInvariantFailureCount != 0)
					{
						AddError(FString::Printf(
							TEXT("Seed Sweep 成功结果违反连通/WFC 不变量：Difficulty=%d Flow=%s Seed=%d"),
							static_cast<int32>(Difficulty), *FlowId.ToString(),
							GenerationRequest.Seed));
						break;
					}
					++SuccessfulRuns;
				}
			}
		}

		TestEqual(TEXT("3 难度 x 3 Flow x 32 Seed 必须全部一次成功"),
			SuccessfulRuns, 3 * 3 * 32);
		return true;
	}

	/**
	 * 读取真正落盘的项目 DataAsset 与 HydroLab Mesh，防止纯瞬态单元测试漏掉资源路径、
	 * 旧序列化字段或 PivotCorrection 写错的问题。本测试只读供应商素材；不会生成 World、
	 * 创建 HISM 或保存任何包，因此它仍不能代替正常视口中的碰撞、接缝和通行验收。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeProjectHydroLabPipelineSmokeTest,
		"Demo.PCG.Integration.ProjectHydroLabPipelineSmoke",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeProjectHydroLabPipelineSmokeTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const UZeroEscapeLevelGenerationProfile* Profile =
			LoadObject<UZeroEscapeLevelGenerationProfile>(
				nullptr,
				TEXT("/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile.DA_LevelGenerationProfile"));
		const UZeroEscapePresentationProfile* Presentation =
			LoadObject<UZeroEscapePresentationProfile>(
				nullptr,
				TEXT("/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SciFiHydroLab.DA_Presentation_SciFiHydroLab"));
		if (!TestNotNull(TEXT("V3.2 Generation Profile 必须已落盘"), Profile)
			|| !TestNotNull(TEXT("HydroLab Presentation Profile 必须已落盘"), Presentation))
		{
			return true;
		}

		FString ConfigurationError;
		TestTrue(
			TEXT("真实 Profile 与 Presentation 必须通过联合配置校验"),
			ValidateZeroEscapeGenerationAssetSet(*Profile, *Presentation, ConfigurationError));
		if (!ConfigurationError.IsEmpty())
		{
			AddError(ConfigurationError);
		}

		TestEqual(TEXT("真实 Profile 必须使用 24x16 逻辑 Grid"),
			Profile->SharedRouteConstraints.GridSize, FIntPoint(24, 16));
		TestTrue(TEXT("真实 Profile 必须保持 600:300 两级网格契约"),
			FMath::IsNearlyEqual(Profile->SharedRouteConstraints.LogicalTileSizeCm, 600.0)
			&& FMath::IsNearlyEqual(Presentation->StructureUnitSizeCm, 300.0));
		TestEqual(TEXT("真实 Profile 必须包含三档难度"), Profile->Difficulties.Num(), 3);
		TestEqual(TEXT("真实 Profile 必须包含三种流程"), Profile->Flows.Num(), 3);

		auto TestBinding = [this](
			const TCHAR* Label,
			const FZeroEscapeStructureMeshBinding& Binding,
			const TCHAR* ExpectedMeshPath,
			const FVector& ExpectedCorrection)
		{
			if (!TestNotNull(FString::Printf(TEXT("%s Mesh 必须存在"), Label), Binding.StaticMesh.Get()))
			{
				return;
			}
			TestEqual(
				FString::Printf(TEXT("%s 必须直接绑定已审核 HydroLab Mesh"), Label),
				Binding.StaticMesh->GetPathName(),
				FString(ExpectedMeshPath));
			TestTrue(
				FString::Printf(TEXT("%s PivotCorrection 必须匹配实测 Pivot"), Label),
				Binding.PivotCorrection.GetLocation().Equals(
					ExpectedCorrection, TransformTolerance));
			TestTrue(
				FString::Printf(TEXT("%s PivotCorrection 不得暗含未审核旋转"), Label),
				Binding.PivotCorrection.GetRotation().Equals(
					FQuat::Identity, TransformTolerance));
			TestTrue(
				FString::Printf(TEXT("%s PivotCorrection 必须保持 Unit Scale"), Label),
				Binding.PivotCorrection.GetScale3D().Equals(FVector::OneVector));
		};

		TestBinding(
			TEXT("Floor"), Presentation->Floor,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Floors/SM_HydroLab_LargeFloorB.SM_HydroLab_LargeFloorB"),
			FVector(150.0, -150.0, 0.0));
		TestBinding(
			TEXT("Ceiling"), Presentation->Ceiling,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Ceiling/SM_HydroLab_CeilingC.SM_HydroLab_CeilingC"),
			FVector(-150.0, -150.0, 0.0));
		TestBinding(
			TEXT("Wall"), Presentation->Wall,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Walls/SM_HydroLab_WallH.SM_HydroLab_WallH"),
			FVector(0.0, -150.0, 0.0));
		TestBinding(
			TEXT("WallTopTrim"), Presentation->WallTopTrim,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Trims/SM_HydroLab_WallTrimG.SM_HydroLab_WallTrimG"),
			FVector(18.75, -150.0, -26.72216510772705));
		TestBinding(
			TEXT("Pillar"), Presentation->Pillar,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Pillars/SM_HydroLab_PillarC.SM_HydroLab_PillarC"),
			FVector::ZeroVector);
		return true;
	}
}

#endif
