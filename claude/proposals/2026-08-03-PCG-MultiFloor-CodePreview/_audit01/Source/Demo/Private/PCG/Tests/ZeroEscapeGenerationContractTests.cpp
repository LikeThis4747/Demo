// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationContractTests.cpp
 *
 * 职责：验证 V5 空间 PCG 的配置、确定性输入、布局 Hash 与结构展开契约。
 * 边界：本文件不验证玩法目标，也不驱动 WFC 搜索；WFC 与完整 Grid 测试位于
 *       ZeroEscapeWfcLayoutTests.cpp。除末尾项目资产烟测外，UObject 均为瞬态对象。
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

		/** 构造只包含空间参数和三档 WFC 权重的最小合法 Profile。 */
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

		/** 构造不依赖磁盘素材的合法 Presentation；本测试只校验绑定契约。 */
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

		/** 构造 2x2 Tile 环路，其中底部内部边关闭，用于验证共享墙和柱子去重。 */
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
				Cell.RegionKind = EZeroEscapeGridRegionKind::Corridor;
			}
			return Plan;
		}

		/** 构造最小合法 Hash 夹具；表现版本和 Transform 均不应改变纯布局 Hash。 */
		FZeroEscapeGeneratedLevelPlan MakeHashFixturePlan()
		{
			FZeroEscapeGeneratedLevelPlan Plan;
			Plan.Signature.Seed = 24680;
			Plan.Signature.Difficulty = EZeroEscapeDifficulty::Normal;
			Plan.Signature.AlgorithmVersion = GAlgorithmVersion;
			Plan.Signature.GenerationProfileVersion = 5;
			Plan.Signature.PresentationVersion = 2;
			Plan.GridSize = FIntPoint(2, 1);
			Plan.LogicalTileSizeCm = 600.0;
			Plan.StartCoordinate = FIntPoint(0, 0);
			Plan.ExitCoordinate = FIntPoint(1, 0);

			FZeroEscapeCollapsedTile& Start = Plan.Cells.AddDefaulted_GetRef();
			Start.StableCellId = 0;
			Start.GridCoordinate = Plan.StartCoordinate;
			Start.OpeningMask = Grid::DirectionBit(1);
			Start.RegionKind = EZeroEscapeGridRegionKind::Start;

			FZeroEscapeCollapsedTile& Exit = Plan.Cells.AddDefaulted_GetRef();
			Exit.StableCellId = 1;
			Exit.GridCoordinate = Plan.ExitCoordinate;
			Exit.OpeningMask = Grid::DirectionBit(3);
			Exit.RegionKind = EZeroEscapeGridRegionKind::Exit;
			return Plan;
		}

		/** 统计某一类规范结构件数量；测试不依赖具体 StaticMesh。 */
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
		TestFalse(TEXT("夹具必须能识别 Pivot 与规范结构顺序被交换"),
			WrongOrder.Equals(PresentationWorld.TransformPosition(Probe), TransformTolerance));
		TestTrue(TEXT("合法组合必须保持有限 Unit Scale"),
			FGenerationCore::IsFiniteUnitScaleTransform(PresentationWorld));
		TestFalse(TEXT("表现绑定不得通过 Scale 适配素材尺寸"),
			FGenerationCore::IsFiniteUnitScaleTransform(
				FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0, 1.0, 1.0))));
		return true;
	}

	/** 验证空间 Profile、三档权重与 600:300 Presentation 的 fail-closed 契约。 */
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
		TestTrue(TEXT("合法 V5 Profile 必须通过配置校验"), Profile->IsConfigured(Error));
		TestTrue(TEXT("瞬态必填 Mesh 足以验证 Presentation 结构契约"),
			Presentation->IsConfigured(600.0, Error));
		TestTrue(TEXT("Presentation 必须接受 Profile 的 600 cm 逻辑格"),
			Presentation->IsConfigured(
				Profile->SharedRouteConstraints.LogicalTileSizeCm,
				Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.RoomCount = GenerationLimits::MaxRoomCount + 1;
		TestFalse(TEXT("房间数超过运行时安全上限必须失败"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.GridSize = FIntPoint(14, 10);
		TestFalse(TEXT("无法容纳三间房和安全间距的 Grid 必须失败"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.MaxWalkableCellCount = 13;
		Profile->SharedRouteConstraints.MinWalkableCellCount = 13;
		TestFalse(TEXT("Start、Exit 与三间 2x2 房超过非空上限时必须失败"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.MaxWfcBacktrackCount = 0;
		TestFalse(TEXT("WFC 回溯预算不得为零"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[0].WfcShapeWeights.CrossWeight = 0;
		TestFalse(TEXT("任一 16-mask 形态权重为零必须失败"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		++Profile->Difficulties[1].WfcShapeWeights.EmptyWeight;
		TestFalse(TEXT("三档难度的 EmptyWeight 必须一致"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		++Profile->Difficulties[2].WfcShapeWeights.CrossWeight;
		TestFalse(TEXT("三档难度的非空 Variant 总权重必须一致"),
			Profile->IsConfigured(Error));

		BuildValidTransientPresentation(*Presentation);
		Presentation->StructureUnitSizeCm = 299.0;
		TestFalse(TEXT("Presentation 不满足 600:300 固定 2:1 契约时必须失败"),
			Presentation->IsConfigured(600.0, Error));

		BuildValidTransientPresentation(*Presentation);
		Presentation->Floor.StaticMesh = nullptr;
		TestFalse(TEXT("Floor、Wall、Ceiling 任一必填 Mesh 缺失必须失败"),
			Presentation->IsConfigured(600.0, Error));
		return true;
	}

	/** 验证 Request 只解析 Seed、难度与版本，且不依赖 DataAsset 数组作者顺序。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeResolvedInputContractTest,
		"Demo.PCG.Unit.Core.ResolvedInputContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeResolvedInputContractTest::RunTest(const FString& Parameters)
	{
		using namespace ContractTestsPrivate;
		(void)Parameters;
		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		// 2 个 Straight mask 各减 20、4 个 Corner mask 各加 10，总权重保持不变。
		Profile->Difficulties[1].WfcShapeWeights.StraightWeight = 80;
		Profile->Difficulties[1].WfcShapeWeights.CornerWeight = 90;
		Profile->Difficulties.Swap(0, 2);

		FZeroEscapeGenerationRequest Request;
		Request.Seed = 13579;
		Request.Difficulty = EZeroEscapeDifficulty::Normal;
		FResolvedGenerationInput Input;
		FZeroEscapeGenerationReport Report;
		TestTrue(TEXT("打乱作者顺序的合法 Profile 仍必须能解析"),
			FGenerationCore::ResolveGenerationInput(*Profile, Request, 7, Input, Report));
		TestTrue(TEXT("解析结果必须完整冻结 Seed、难度与三个版本"),
			Input.Signature.Seed == 13579
				&& Input.Signature.Difficulty == EZeroEscapeDifficulty::Normal
				&& Input.Signature.AlgorithmVersion == GAlgorithmVersion
				&& Input.Signature.GenerationProfileVersion == 5
				&& Input.Signature.PresentationVersion == 7);
		TestTrue(TEXT("解析结果必须选择 Normal 权重而不是数组下标"),
			Input.WfcShapeWeights.StraightWeight == 80
				&& Input.WfcShapeWeights.CornerWeight == 90);

		FResolvedGenerationInput InvalidInput;
		TestFalse(TEXT("非法 PresentationVersion 必须在消费随机数前失败"),
			FGenerationCore::ResolveGenerationInput(*Profile, Request, 0, InvalidInput, Report));
		TestTrue(TEXT("失败解析不得泄漏部分 Signature"),
			InvalidInput.Signature.AlgorithmVersion == 0);
		return true;
	}

	/** 验证房间抽样和 WFC 使用互相隔离、可重放的确定性随机子流。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeRandomDomainIsolationTest,
		"Demo.PCG.Unit.Core.RandomDomainIsolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeRandomDomainIsolationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const TStaticArray<ERandomDomain, 2> Domains = {
			ERandomDomain::RoomPlacement, ERandomDomain::WfcLayout };
		TStaticArray<TArray<uint32>, 2> Sequences;
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
		TestFalse(TEXT("房间抽样与 WFC 不得共享同一派生序列"),
			Sequences[0] == Sequences[1]);
		return true;
	}

	/** 验证 Hash 只描述纯布局，表现版本和 Transform 不影响结果。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeCanonicalLayoutHashTest,
		"Demo.PCG.Unit.Core.CanonicalLayoutHash",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeCanonicalLayoutHashTest::RunTest(const FString& Parameters)
	{
		using namespace ContractTestsPrivate;
		(void)Parameters;
		const FZeroEscapeGeneratedLevelPlan Baseline = MakeHashFixturePlan();
		const int64 BaselineHash = FGenerationCore::ComputeCanonicalLayoutHash(Baseline);
		TestTrue(TEXT("合法布局 Hash 必须非零"), BaselineHash != 0);

		FZeroEscapeGeneratedLevelPlan PresentationOnly = Baseline;
		PresentationOnly.Signature.PresentationVersion = 99;
		PresentationOnly.PlayerStartLocalTransform.SetLocation(FVector(1.0, 2.0, 3.0));
		TestEqual(TEXT("表现版本与 Transform 不得改变纯布局 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(PresentationOnly), BaselineHash);

		FZeroEscapeGeneratedLevelPlan DifferentSeed = Baseline;
		++DifferentSeed.Signature.Seed;
		TestNotEqual(TEXT("Seed 改变必须进入布局 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(DifferentSeed), BaselineHash);

		FZeroEscapeGeneratedLevelPlan InvalidOrder = Baseline;
		InvalidOrder.Cells.Swap(0, 1);
		TestEqual(TEXT("非递增 StableCellId 的 Plan 必须拒绝计算 Hash"),
			FGenerationCore::ComputeCanonicalLayoutHash(InvalidOrder), static_cast<int64>(0));
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
		TestEqual(TEXT("四个非 Empty Tile 必须展开 16 块 Floor"),
			CountStructureKind(Instances, EStructurePieceKind::Floor), 16);
		TestEqual(TEXT("四个非 Empty Tile 必须展开 16 块 Ceiling"),
			CountStructureKind(Instances, EStructurePieceKind::Ceiling), 16);
		TestEqual(TEXT("共享闭边去重后必须生成 18 块 Wall"),
			CountStructureKind(Instances, EStructurePieceKind::Wall), 18);
		TestEqual(TEXT("WallTopTrim 必须与去重后的 Wall 一一对应"),
			CountStructureKind(Instances, EStructurePieceKind::WallTopTrim), 18);
		TestEqual(TEXT("端点、转角和 T/Cross 墙图顶点共六根 Pillar"),
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
	 * 读取真实 DataAsset 与 HydroLab Mesh，防止纯瞬态测试漏掉资源路径或 PivotCorrection。
	 * 本测试只读，不能代替正常视口中的碰撞、接缝、照明和通行验收。
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
		if (!TestNotNull(TEXT("V5 Generation Profile 必须已落盘"), Profile)
			|| !TestNotNull(TEXT("HydroLab Presentation Profile 必须已落盘"), Presentation))
		{
			return true;
		}

		FString ConfigurationError;
		TestTrue(TEXT("真实 Generation Profile 必须通过配置校验"),
			Profile->IsConfigured(ConfigurationError));
		TestTrue(TEXT("真实 Presentation 必须匹配 Profile 的逻辑格尺寸"),
			Presentation->IsConfigured(
				Profile->SharedRouteConstraints.LogicalTileSizeCm,
				ConfigurationError));
		if (!ConfigurationError.IsEmpty())
		{
			AddError(ConfigurationError);
		}

		TestEqual(TEXT("真实 Profile 必须使用 24x16 逻辑 Grid"),
			Profile->SharedRouteConstraints.GridSize, FIntPoint(24, 16));
		TestEqual(TEXT("真实 Profile 必须已经迁移到 V5"), Profile->ProfileVersion, 5);
		TestTrue(TEXT("真实 Profile 必须保持 600:300 两级网格契约"),
			FMath::IsNearlyEqual(Profile->SharedRouteConstraints.LogicalTileSizeCm, 600.0)
				&& FMath::IsNearlyEqual(Presentation->StructureUnitSizeCm, 300.0));
		const FZeroEscapeSharedRouteConstraints& Route = Profile->SharedRouteConstraints;
		TestTrue(TEXT("真实 Profile 必须保存房间、数量和直线约束"),
			Route.RoomSizeTiles == 2
				&& Route.RoomCount == 3
				&& Route.MinWalkableCellCount == 48
				&& Route.MaxWalkableCellCount == 72
				&& Route.MaxConsecutiveStraightTiles == 4);
		TestTrue(TEXT("真实 Profile 必须保存路线与 WFC 预算"),
			Route.MaxRequiredRouteLengthTiles == 64
				&& Route.MaxWfcCandidateAttempts == 100000
				&& Route.MaxWfcBacktrackCount == 25000
				&& Route.MaxWfcSolveAttempts == 10);
		TestEqual(TEXT("真实 Profile 必须只包含三档难度"), Profile->Difficulties.Num(), 3);
		for (const FZeroEscapeDifficultyDefinition& Definition : Profile->Difficulties)
		{
			const FZeroEscapeWfcShapeWeights& Weights = Definition.WfcShapeWeights;
			TestTrue(TEXT("每档难度必须保存完整 16-mask 权重"),
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
			TestEqual(FString::Printf(TEXT("%s 必须直接绑定已审核 HydroLab Mesh"), Label),
				Binding.StaticMesh->GetPathName(), FString(ExpectedMeshPath));
			TestTrue(FString::Printf(TEXT("%s PivotCorrection 必须匹配实测 Pivot"), Label),
				Binding.PivotCorrection.GetLocation().Equals(ExpectedCorrection, TransformTolerance));
			TestTrue(FString::Printf(TEXT("%s PivotCorrection 必须保持 Unit Scale"), Label),
				Binding.PivotCorrection.GetScale3D().Equals(FVector::OneVector));
		};

		TestBinding(TEXT("Floor"), Presentation->Floor,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Floors/SM_HydroLab_LargeFloorB.SM_HydroLab_LargeFloorB"),
			FVector(150.0, -150.0, 0.0));
		TestBinding(TEXT("Ceiling"), Presentation->Ceiling,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Ceiling/SM_HydroLab_CeilingC.SM_HydroLab_CeilingC"),
			FVector(-150.0, -150.0, 0.0));
		TestBinding(TEXT("Wall"), Presentation->Wall,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Walls/SM_HydroLab_WallH.SM_HydroLab_WallH"),
			FVector(0.0, -150.0, 0.0));
		TestBinding(TEXT("WallTopTrim"), Presentation->WallTopTrim,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Trims/SM_HydroLab_WallTrimG.SM_HydroLab_WallTrimG"),
			FVector(18.75, -150.0, -26.72216510772705));
		TestBinding(TEXT("Pillar"), Presentation->Pillar,
			TEXT("/Game/Assets/SciFiHydroLab/Meshes/Pillars/SM_HydroLab_PillarC.SM_HydroLab_PillarC"),
			FVector::ZeroVector);
		return true;
	}
}

#endif
