// Copyright Epic Games, Inc. All Rights Reserved.

/** 纯值测试：0 候选/密度取整为 0 均成功，非法规则和超网格预算必须拒绝。 */

#include "PCG/Population/ZeroEscapePopulationPlacementPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePopulationPlacementPolicyTest,
		"Demo.PCG.Population.PlacementBudget",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePopulationPlacementPolicyTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		int32 Targets = INDEX_NONE;
		int32 Actors = INDEX_NONE;
		TestTrue(
			TEXT("没有普通候选格是合法的零放置结果"),
			FPopulationPlacementPolicy::Evaluate(0, 4, 8, 2, Targets, Actors)
				== EPopulationPlacementBudgetResult::Success);
		TestEqual(TEXT("零候选目标数"), Targets, 0);
		TestEqual(TEXT("零候选 Actor 数"), Actors, 0);

		TestTrue(
			TEXT("候选少于密度分母时沿用旧语义并成功跳过"),
			FPopulationPlacementPolicy::Evaluate(3, 4, 8, 2, Targets, Actors)
				== EPopulationPlacementBudgetResult::Success);
		TestEqual(TEXT("整数密度取整为零"), Targets, 0);

		TestTrue(
			TEXT("普通规则按整数密度和横向数量计算"),
			FPopulationPlacementPolicy::Evaluate(20, 4, 8, 2, Targets, Actors)
				== EPopulationPlacementBudgetResult::Success);
		TestEqual(TEXT("普通规则目标格"), Targets, 5);
		TestEqual(TEXT("普通规则 Actor 数"), Actors, 10);

		TestTrue(
			TEXT("非法横向数量必须拒绝"),
			FPopulationPlacementPolicy::Evaluate(20, 4, 8, 0, Targets, Actors)
				== EPopulationPlacementBudgetResult::InvalidRule);

		constexpr int32 MaxAddresses = GenerationLimits::MaxGridCells
			* GenerationLimits::MaxFloorCount;
		TestTrue(
			TEXT("超过全四层最大地址数的 Actor 请求必须拒绝"),
			FPopulationPlacementPolicy::Evaluate(
				MaxAddresses, 1, MaxAddresses, 2, Targets, Actors)
				== EPopulationPlacementBudgetResult::SpawnBudgetExceeded);
		return true;
	}
}

#endif
