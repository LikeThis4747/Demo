// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationContractTests.cpp
 *
 * 职责：验证 V4 运行时 PCG 的资产、快照、流程语义、稳定签名与规范结构展开契约。
 * 边界：本文件不驱动 WFC 搜索或回溯；WFC/Grid 状态机测试位于 ZeroEscapeWfcLayoutTests.cpp。
 *       除末尾的项目资产烟测外，所有 UObject/StaticMesh 均为瞬态对象，不保存包。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/StaticArray.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeGridLayoutSolver.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace ContractTestsPrivate
	{
		/** Transform 点位比较只用于揭示乘法顺序错误，不参与运行时容差。 */
		constexpr double TransformTolerance = 0.01;

		/**
		 * 构造包含三档难度和三种 Flow 的最小合法 Profile。
		 *
		 * 三档难度故意使用同一套默认权重，以满足 V4 “Empty 和非空总权重不随难度改变”
		 * 契约。跨难度不一致的失败情况由独立断言覆盖，不隐藏在基础夹具中。
		 */
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

	/** 冻结素材 Pivot -> 规范结构 -> Generator Root 的组合顺序。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeTransformCompositionTest,
		"Demo.PCG.Unit.TransformComposition",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeTransformCompositionTest::RunTest(const FString& Parameters)
	{
		using namespace ContractTestsPrivate;
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

	/** 验证 Profile/Grid 容量、K/N、分难度 WFC 权重和 600:300 Presentation 的前置失败契约。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeProfileAndPresentationContractsTest,
		"Demo.PCG.Unit.Assets.ProfileAndPresentationContracts",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeProfileAndPresentationContractsTest::RunTest(const FString& Parameters)
	{
		using namespace ContractTestsPrivate;
		(void)Parameters;
		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		UZeroEscapePresentationProfile* Presentation = NewObject<UZeroEscapePresentationProfile>();
		FString Error;
		BuildValidProfile(*Profile);
		BuildValidTransientPresentation(*Presentation);
		TestTrue(TEXT("合法 V4 Profile 必须通过配置校验"), Profile->IsConfigured(Error));
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
		TestFalse(TEXT("无法容纳 2x2 房间安全间距的 Grid 应在 Profile 阶段失败"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.MinWalkableCellCount = 73;
		TestFalse(TEXT("非空 Cell 下限超过上限必须失败"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.ObjectiveProgressBandCount =
			ZeroEscape::GenerationLimits::MaxObjectiveProgressBands + 1;
		TestFalse(TEXT("纯值校验不得只依赖编辑器 Clamp，进度带超过六个必须失败"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.MaxWfcBacktrackCount = 0;
		TestFalse(TEXT("WFC 回溯预算不得为零"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.MaxWfcSolveAttempts = 0;
		TestFalse(TEXT("WFC 尝试次数不得为零"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.MaxWfcBacktrackCount = 9;
		Profile->SharedRouteConstraints.MaxWfcSolveAttempts = 10;
		TestFalse(TEXT("总回溯预算必须至少覆盖每次 WFC 尝试"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[0].WfcShapeWeights.CrossWeight = 0;
		TestFalse(TEXT("任何难度的形态权重为零都会移除状态，必须被拒绝"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		++Profile->Difficulties[1].WfcShapeWeights.EmptyWeight;
		TestFalse(TEXT("三档难度的 EmptyWeight 不一致必须失败"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		++Profile->Difficulties[2].WfcShapeWeights.CrossWeight;
		TestFalse(TEXT("三档难度的非空 Variant 总权重不一致必须失败"),
			Profile->IsConfigured(Error));

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
		using namespace ContractTestsPrivate;
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

	/** 验证 Escape/CollectAll/K-of-N 的轻量 Intent 语义、0-based 进度槽和同 Seed 确定性。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeProgressionIntentContractsAndDeterminismTest,
		"Demo.PCG.Unit.Core.ProgressionIntentContractsAndDeterminism",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeProgressionIntentContractsAndDeterminismTest::RunTest(const FString& Parameters)
	{
		using namespace ContractTestsPrivate;
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
		TestEqual(TEXT("解析结果必须带出当前难度权重"),
			KOfNSettings.WfcShapeWeights.EmptyWeight,
			Snapshot.Difficulties[1].WfcShapeWeights.EmptyWeight);

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

	/** 验证三个稳定随机职责互相隔离，且同 Seed/版本/Domain 复现完全相同序列。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeRandomDomainIsolationTest,
		"Demo.PCG.Unit.Core.RandomDomainIsolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeRandomDomainIsolationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const TStaticArray<ERandomDomain, 3> Domains = {
			ERandomDomain::Landmark, ERandomDomain::WfcLayout, ERandomDomain::Presentation };
		TStaticArray<TArray<uint32>, 3> Sequences;
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

	/** 用 2x2 环路和一条内部闭边验证 600 Tile -> 300 构件展开。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeExpansionDedupAndPillarsTest,
		"Demo.PCG.Structure.ExpansionDedupAndPillars",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeExpansionDedupAndPillarsTest::RunTest(const FString& Parameters)
	{
		using namespace ContractTestsPrivate;
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
	 * 读取真正落盘的项目 DataAsset 与 HydroLab Mesh，防止纯瞬态单元测试漏掉资源路径、
	 * 旧序列化字段或 PivotCorrection 写错。本测试只读，不能代替正常视口的碰撞、接缝和通行验收。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeProjectHydroLabPipelineSmokeTest,
		"Demo.PCG.Integration.ProjectHydroLabPipelineSmoke",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeProjectHydroLabPipelineSmokeTest::RunTest(const FString& Parameters)
	{
		using namespace ContractTestsPrivate;
		(void)Parameters;
		const UZeroEscapeLevelGenerationProfile* Profile =
			LoadObject<UZeroEscapeLevelGenerationProfile>(
				nullptr,
				TEXT("/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile.DA_LevelGenerationProfile"));
		const UZeroEscapePresentationProfile* Presentation =
			LoadObject<UZeroEscapePresentationProfile>(
				nullptr,
				TEXT("/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SciFiHydroLab.DA_Presentation_SciFiHydroLab"));
		if (!TestNotNull(TEXT("V4 Generation Profile 必须已落盘"), Profile)
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
		TestEqual(TEXT("真实 Profile 必须已经迁移到 V4"), Profile->ProfileVersion, 4);
		TestTrue(TEXT("真实 Profile 必须保持 600:300 两级网格契约"),
			FMath::IsNearlyEqual(Profile->SharedRouteConstraints.LogicalTileSizeCm, 600.0)
				&& FMath::IsNearlyEqual(Presentation->StructureUnitSizeCm, 300.0));
		const FZeroEscapeSharedRouteConstraints& Route = Profile->SharedRouteConstraints;
		TestTrue(TEXT("真实 Profile 必须保存 V4 房间、进度带、数量与直线约束"),
			Route.RoomSizeTiles == 2
				&& Route.ObjectiveProgressBandCount == 3
				&& Route.MinWalkableCellCount == 48
				&& Route.MaxWalkableCellCount == 72
				&& Route.MaxConsecutiveStraightTiles == 4);
		TestTrue(TEXT("真实 Profile 必须保存路线完成态约束"),
			Route.MaxRequiredRouteLengthTiles == 64
				&& Route.MaxRequiredRouteExtraTiles == 24);
		TestTrue(TEXT("真实 Profile 必须保存整局 WFC 预算与确定性尝试数"),
			Route.MaxWfcCandidateAttempts == 100000
				&& Route.MaxWfcBacktrackCount == 25000
				&& Route.MaxWfcSolveAttempts == 10);
		TestEqual(TEXT("真实 Profile 必须包含三档难度"), Profile->Difficulties.Num(), 3);
		TestEqual(TEXT("真实 Profile 必须包含三种流程"), Profile->Flows.Num(), 3);
		for (const FZeroEscapeDifficultyDefinition& Definition : Profile->Difficulties)
		{
			const FZeroEscapeWfcShapeWeights& Weights = Definition.WfcShapeWeights;
			TestTrue(TEXT("每档真实难度必须保存经过 Seed Sweep 校准的完整 16-mask 权重"),
				Weights.EmptyWeight == 12000
					&& Weights.DeadEndWeight == 100
					&& Weights.StraightWeight == 100
					&& Weights.CornerWeight == 80
					&& Weights.TJunctionWeight == 25
					&& Weights.CrossWeight == 5);
		}

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
				Binding.PivotCorrection.GetLocation().Equals(ExpectedCorrection, TransformTolerance));
			TestTrue(
				FString::Printf(TEXT("%s PivotCorrection 不得暗含未审核旋转"), Label),
				Binding.PivotCorrection.GetRotation().Equals(FQuat::Identity, TransformTolerance));
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
