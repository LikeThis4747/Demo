// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcLayoutTests.cpp
 *
 * 职责：验证 V6 二维 WFC 的 16 状态契约、Count/MaxConsecutive/Connected 传播、
 *       带界时间顺序回溯、同 Seed 重放与纯值输出原子性。
 * 边界：测试只操作纯值 Domain/Plan，不创建 World、Actor、HISM，也不执行 PIE。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/ArrayView.h"
#include "Containers/Queue.h"
#include "Containers/StaticArray.h"
#include "Misc/AutomationTest.h"

#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeGridLayoutSolver.h"
#include "PCG/ZeroEscapeWfcConstraints.h"
#include "PCG/ZeroEscapeWfcSolver.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace WfcLayoutTestsPrivate
	{
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
			Settings.PreferredMaxConsecutiveStraightTiles = 0;
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

	/** 验证直线长度偏好只改变抽样顺序，不会拒绝唯一合法的过长直线。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeWfcSoftStraightPreferenceTest,
		"Demo.PCG.WFC.SoftStraightPreference",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeWfcSoftStraightPreferenceTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		auto AcceptEveryLeaf = [](const TConstArrayView<uint8>)
		{
			return FWfcCollapsedCandidateEvaluation::Accept();
		};
		const TArray<FTileVariant> Variants =
			MakeCanonicalVariantArray(FZeroEscapeWfcShapeWeights());

		const FIntPoint RequiredLineGrid(6, 3);
		TArray<FGridCellConstraint> RequiredLineConstraints =
			MakeDenseConstraints(RequiredLineGrid, EGridCellDomain::Outside);
		AddRequiredEdge(RequiredLineGrid, FIntPoint(1, 1), FIntPoint(2, 1), RequiredLineConstraints);
		AddRequiredEdge(RequiredLineGrid, FIntPoint(2, 1), FIntPoint(3, 1), RequiredLineConstraints);
		AddRequiredEdge(RequiredLineGrid, FIntPoint(3, 1), FIntPoint(4, 1), RequiredLineConstraints);
		FZeroEscapeWfcSolveSettings RequiredLineSettings =
			MakeWfcSettings(FIntPoint(1, 1), 4, 4, 4);
		RequiredLineSettings.PreferredMaxConsecutiveStraightTiles = 1;
		FRandomStream RequiredLineRandom(112233);
		TArray<uint8> RequiredLineOutput;
		FZeroEscapeGenerationReport RequiredLineReport;
		const bool bRequiredLineSolved = FWfcSolver::Solve(
			RequiredLineGrid,
			RequiredLineConstraints,
			RequiredLineSettings,
			Variants,
			RequiredLineRandom,
			AcceptEveryLeaf,
			RequiredLineOutput,
			RequiredLineReport);
		TestTrue(TEXT("超过软偏好长度的唯一 Required 直线仍必须成功"), bRequiredLineSolved);
		if (bRequiredLineSolved)
		{
			TestEqual(TEXT("软偏好不得删除唯一的水平贯通候选"),
				RequiredLineOutput[Grid::ToIndex(FIntPoint(2, 1), RequiredLineGrid)],
				static_cast<uint8>(Grid::DirectionBit(1) | Grid::DirectionBit(3)));
		}

		// 固定两格水平贯通后，第三格可收尾或继续直行；跨固定 Seed 比较候选排序倾向。
		const FIntPoint ChoiceGrid(4, 1);
		TArray<FGridCellConstraint> ChoiceConstraints =
			MakeDenseConstraints(ChoiceGrid, EGridCellDomain::Optional);
		AddRequiredEdge(ChoiceGrid, FIntPoint(0, 0), FIntPoint(1, 0), ChoiceConstraints);
		AddRequiredEdge(ChoiceGrid, FIntPoint(1, 0), FIntPoint(2, 0), ChoiceConstraints);
		FZeroEscapeWfcShapeWeights ChoiceWeights;
		ChoiceWeights.EmptyWeight = 20;
		ChoiceWeights.DeadEndWeight = 35;
		ChoiceWeights.StraightWeight = 90;
		const TArray<FTileVariant> ChoiceVariants = MakeCanonicalVariantArray(ChoiceWeights);
		auto CountStraightExtensions = [this,
			&ChoiceConstraints,
			&ChoiceVariants,
			AcceptEveryLeaf,
			ChoiceGrid](const int32 PreferredMaximum)
		{
			int32 ExtensionCount = 0;
			for (int32 Seed = 0; Seed < 128; ++Seed)
			{
				FZeroEscapeWfcSolveSettings Settings =
					MakeWfcSettings(FIntPoint(0, 0), 3, 4, 4);
				Settings.PreferredMaxConsecutiveStraightTiles = PreferredMaximum;
				FRandomStream Random(Seed);
				TArray<uint8> Output;
				FZeroEscapeGenerationReport Report;
				if (!FWfcSolver::Solve(
						ChoiceGrid,
						ChoiceConstraints,
						Settings,
						ChoiceVariants,
						Random,
						AcceptEveryLeaf,
						Output,
						Report))
				{
					AddError(FString::Printf(TEXT("软直线偏好比较 Seed=%d 求解失败。"), Seed));
					continue;
				}
				const uint8 ThirdMask = Output[Grid::ToIndex(FIntPoint(2, 0), ChoiceGrid)];
				const uint8 HorizontalMask = static_cast<uint8>(
					Grid::DirectionBit(1) | Grid::DirectionBit(3));
				ExtensionCount += (ThirdMask & HorizontalMask) == HorizontalMask ? 1 : 0;
			}
			return ExtensionCount;
		};

		const int32 UnbiasedExtensionCount = CountStraightExtensions(0);
		const int32 PreferredExtensionCount = CountStraightExtensions(1);
		TestTrue(TEXT("启用软偏好后固定 Seed 集合中的长直线首选次数必须下降"),
			PreferredExtensionCount < UnbiasedExtensionCount);
		return true;
	}

	/** 验证软路线提示只改变正权重，并锁定方向感知代价对长直和锯齿的排序。 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeWfcRouteOpeningPreferenceTest,
		"Demo.PCG.WFC.RouteOpeningPreference",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeWfcRouteOpeningPreferenceTest::RunTest(const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		FWfcCellOpeningPreference Preference;
		Preference.PreferredOpenMask = Grid::DirectionBit(1);
		Preference.PreferredClosedMask = Grid::DirectionBit(3);
		const int32 PerfectWeight = Testing::ApplyOpeningPreferenceWeight(
			Grid::DirectionBit(1), 100, Preference, 1.5f);
		const int32 NeutralWeight = Testing::ApplyOpeningPreferenceWeight(
			static_cast<uint8>(Grid::DirectionBit(1) | Grid::DirectionBit(3)),
			100, Preference, 1.5f);
		const int32 OppositeWeight = Testing::ApplyOpeningPreferenceWeight(
			Grid::DirectionBit(3), 100, Preference, 1.5f);
		TestTrue(TEXT("完全匹配、半匹配和完全相反必须保持严格权重顺序"),
			PerfectWeight > NeutralWeight && NeutralWeight > OppositeWeight);
		TestTrue(TEXT("完全相反的合法 OpeningMask 仍必须保留正权重"),
			OppositeWeight > 0);

		TArray<uint8> FiveStraight;
		FiveStraight.Init(1, 5);
		TArray<uint8> EightStraight;
		EightStraight.Init(1, 8);
		const TArray<uint8> Sawtooth = {1, 0, 1, 0};
		const TArray<uint8> ReadableTurns = {1, 1, 0, 0};
		TestTrue(TEXT("8 格直线必须严格比 5 格直线昂贵"),
			Testing::MeasurePreferredPathDirectionCost(EightStraight, 4)
				> Testing::MeasurePreferredPathDirectionCost(FiveStraight, 4));
		TestTrue(TEXT("每格转向的锯齿必须比两格一转的路线昂贵"),
			Testing::MeasurePreferredPathDirectionCost(Sawtooth, 4)
				> Testing::MeasurePreferredPathDirectionCost(ReadableTurns, 4));

		FZeroEscapeWfcSolveSettings Settings = MakeWfcSettings(
			FIntPoint(0, 0), 1, 4, 2);
		Settings.OpeningPreferenceLog2Strength = 1.5f;
		Settings.OpeningPreferencesByCell.Init(FWfcCellOpeningPreference(), 4);
		FString Error;
		TestTrue(TEXT("合法稠密软提示必须通过设置校验"),
			FWfcConstraints::ValidateSolveSettings(FIntPoint(2, 2), Settings, Error));
		Settings.OpeningPreferencesByCell[0].PreferredOpenMask = Grid::DirectionBit(0);
		Settings.OpeningPreferencesByCell[0].PreferredClosedMask = Grid::DirectionBit(0);
		TestFalse(TEXT("同一 bit 同时偏好开关必须拒绝为配置错误"),
			FWfcConstraints::ValidateSolveSettings(FIntPoint(2, 2), Settings, Error));
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
		const FIntPoint GridSize(4, 1);
		TArray<FGridCellConstraint> Constraints =
			MakeDenseConstraints(GridSize, EGridCellDomain::Optional);
		AddRequiredEdge(GridSize, FIntPoint(0, 0), FIntPoint(1, 0), Constraints);
		AddRequiredEdge(GridSize, FIntPoint(1, 0), FIntPoint(2, 0), Constraints);
		FZeroEscapeWfcSolveSettings Settings =
			MakeWfcSettings(FIntPoint(0, 0), 3, 4, 4);
		Settings.PreferredMaxConsecutiveStraightTiles = 1;
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
				Seed, ERandomDomain::WfcLayout);
			FRandomStream SecondRandom = FGenerationCore::MakeRandomStream(
				Seed, ERandomDomain::WfcLayout);
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

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeDeterministicFloorFallbackTest,
		"Demo.PCG.Unit.Grid.DeterministicFallback",
		EAutomationTestFlags_ApplicationContextMask
			| EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeDeterministicFloorFallbackTest::RunTest(
		const FString& Parameters)
	{
		using namespace WfcLayoutTestsPrivate;
		(void)Parameters;
		FZeroEscapeConstrainedFloorInput Input;
		Input.Signature.Seed = 240813;
		Input.Signature.Difficulty = EZeroEscapeDifficulty::Hard;
		Input.GridSize = FIntPoint(6, 6);
		Input.RequiredEnterCoordinate = FIntPoint(0, 0);
		Input.RequiredLeaveCoordinate = FIntPoint(5, 5);
		Input.Constraints = MakeDenseConstraints(
			Input.GridSize, EGridCellDomain::Optional);
		Input.StructureWalkableByCell.Init(0, Input.Constraints.Num());
		Input.Constraints[Grid::ToIndex(
			Input.RequiredEnterCoordinate, Input.GridSize)].Domain =
			EGridCellDomain::Required;
		Input.Constraints[Grid::ToIndex(
			Input.RequiredLeaveCoordinate, Input.GridSize)].Domain =
			EGridCellDomain::Required;
		Input.MinTotalWalkableCellCount = 2;
		Input.MaxTotalWalkableCellCount = Input.Constraints.Num();
		Input.MinOrdinaryWalkableCellCount = 2;
		Input.MaxConsecutiveStraightTiles = 6;
		Input.MaxSolveAttemptsForThisFloor = 3;
		Input.PreferredTotalWalkableCellCount = 20;
		Input.PreferredOrdinaryWalkableCellCount = 20;
		Input.PreferredMaxConsecutiveStraightTiles = 4;
		Input.PreferredRouteCoverageRatio = 0.75;
		Input.RouteOpeningPreferenceLog2Strength = 1.5f;
		Input.MinimumRewardBranchLengthTiles = 3;
		Input.MaximumPreferredRewardBranchLengthTiles = 6;
		Input.PreferredRewardBranchCount = 2;

		FZeroEscapeSharedWfcBudget FirstBudget;
		FZeroEscapeSharedWfcBudget ReplayBudget;
		FZeroEscapeConstrainedFloorResult First;
		FZeroEscapeConstrainedFloorResult Replay;
		FZeroEscapeGenerationReport FirstReport;
		FZeroEscapeGenerationReport ReplayReport;
		const FZeroEscapeWfcShapeWeights Weights;
		const bool bFirstSolved = FGridLayoutSolver::SolveConstrainedFloor(
			Input, Weights, FirstBudget, First, FirstReport);
		const bool bReplaySolved = FGridLayoutSolver::SolveConstrainedFloor(
			Input, Weights, ReplayBudget, Replay, ReplayReport);
		TestTrue(TEXT("WFC 工作预算为零时必须提交确定性硬合法楼层兜底"),
			bFirstSolved && bReplaySolved);
		TestEqual(TEXT("兜底必须保持同 Seed OpeningMask 重放"),
			First.OpeningMaskByCell, Replay.OpeningMaskByCell);
		TestEqual(TEXT("兜底应向软密度目标生长"),
			First.TotalWalkableCellCount, 20);
		TestTrue(TEXT("兜底必须连接进入点和离开点"),
			First.RequiredRouteLengthTiles > 0);
		TestEqual(TEXT("兜底不得伪造 WFC 搜索树消耗"),
			FirstReport.Metrics.WfcSolveAttemptCount, 0);

		FZeroEscapeConstrainedFloorInput ChangedAttemptLimit = Input;
		ChangedAttemptLimit.MaxSolveAttemptsForThisFloor = 1;
		FZeroEscapeSharedWfcBudget ChangedBudget;
		FZeroEscapeConstrainedFloorResult ChangedResult;
		FZeroEscapeGenerationReport ChangedReport;
		TestTrue(TEXT("调整真实 WFC 树上限后零预算兜底仍必须成功"),
			FGridLayoutSolver::SolveConstrainedFloor(
				ChangedAttemptLimit,
				Weights,
				ChangedBudget,
				ChangedResult,
				ChangedReport));
		TestEqual(TEXT("独立兜底 salt 不得随真实 WFC 树上限改变布局"),
			First.OpeningMaskByCell, ChangedResult.OpeningMaskByCell);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
