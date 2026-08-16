// Copyright Epic Games, Inc. All Rights Reserved.

/** 纯值测试：异步开局忽略旧操作、重复终态和 EndPlay 后回调。 */

#include "GameFlow/ZeroEscapeGameSetupGate.h"
#include "GameFlow/ZeroEscapeGameState.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Magnetism/MagneticGrabTuningData.h"
#include "Misc/AutomationTest.h"

namespace ZeroEscape::GameFlow::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeGameSetupGateTest,
		"Demo.GameFlow.AsyncSetupGate",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeGameSetupGateTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FGameSetupGateSnapshot State;
		State.ActiveOperationId = 12;
		TestFalse(TEXT("旧操作报告不得启动当前一局"),
			FGameSetupGate::AcceptFinalReport(State, 11));
		TestTrue(TEXT("当前操作的首个最终报告必须被接受"),
			FGameSetupGate::AcceptFinalReport(State, 12));

		State.LastHandledOperationId = 12;
		TestFalse(TEXT("同一最终报告重复广播不得再次摆放"),
			FGameSetupGate::AcceptFinalReport(State, 12));
		State.LastHandledOperationId = 0;
		State.bTerminal = true;
		TestFalse(TEXT("开局已进入成功或失败终态后不得重入"),
			FGameSetupGate::AcceptFinalReport(State, 12));
		State.bTerminal = false;
		State.bEndingPlay = true;
		TestFalse(TEXT("EndPlay 后回调必须被忽略"),
			FGameSetupGate::AcceptFinalReport(State, 12));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeEnergyOrbObjectiveTest,
		"Demo.GameFlow.EnergyOrbObjective",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeEnergyOrbObjectiveTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FZeroEscapeEnergyOrbObjective Objective;
		TestTrue(TEXT("6 个光团、50% 目标可初始化"),
			Objective.Initialize(6, 0.5f));
		TestEqual(TEXT("50% 使用实际总数计算"), Objective.GetRequiredCount(), 3);
		TestFalse(TEXT("尚未收集时不满足出口"), Objective.IsRequirementMet());
		TestTrue(TEXT("第一个光团可计入"), Objective.TryCollect());
		TestTrue(TEXT("第二个光团可计入"), Objective.TryCollect());
		TestFalse(TEXT("2/3 时仍不满足出口"), Objective.IsRequirementMet());
		TestTrue(TEXT("第三个光团可计入"), Objective.TryCollect());
		TestTrue(TEXT("3/3 时满足出口"), Objective.IsRequirementMet());
		TestFalse(TEXT("同一目标不得重复初始化"), Objective.Initialize(6, 0.5f));

		FZeroEscapeEnergyOrbObjective EasyObjective;
		TestTrue(TEXT("5 个光团、35% 目标可初始化"),
			EasyObjective.Initialize(5, 0.35f));
		TestEqual(TEXT("35% 必须向上取整"), EasyObjective.GetRequiredCount(), 2);

		FZeroEscapeEnergyOrbObjective HardObjective;
		TestTrue(TEXT("5 个光团、75% 目标可初始化"),
			HardObjective.Initialize(5, 0.75f));
		TestEqual(TEXT("75% 必须向上取整"), HardObjective.GetRequiredCount(), 4);

		FZeroEscapeEnergyOrbObjective EmptyObjective;
		TestTrue(TEXT("零光团地图仍可初始化"), EmptyObjective.Initialize(0, 0.75f));
		TestTrue(TEXT("零光团地图不得锁死出口"), EmptyObjective.IsRequirementMet());
		TestFalse(TEXT("零光团地图不能虚构收集"), EmptyObjective.TryCollect());

		FZeroEscapeEnergyOrbObjective InvalidObjective;
		TestFalse(TEXT("负总数必须拒绝"), InvalidObjective.Initialize(-1, 0.5f));
		TestFalse(TEXT("大于 100% 的目标必须拒绝"),
			InvalidObjective.Initialize(1, 1.01f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeExplosionChargeConfigurationTest,
		"Demo.GameFlow.ExplosionChargeConfiguration",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeExplosionChargeConfigurationTest::RunTest(
		const FString& Parameters)
	{
		(void)Parameters;
		UMagneticGrabTuningData* Tuning = NewObject<UMagneticGrabTuningData>();
		FString Error;
		TestTrue(TEXT("默认 1/3/30 秒配置合法"), Tuning->IsConfigured(Error));
		TestEqual(TEXT("默认开局一次"), Tuning->InitialExplosionCharges, 1);
		TestEqual(TEXT("默认上限三次"), Tuning->MaximumExplosionCharges, 3);
		TestTrue(TEXT("默认每次恢复 30 秒"),
			FMath::IsNearlyEqual(Tuning->ExplosionRechargeSecondsPerCharge, 30.0f));

		Tuning->InitialExplosionCharges = 4;
		TestFalse(TEXT("开局次数超过上限必须拒绝"), Tuning->IsConfigured(Error));
		Tuning->InitialExplosionCharges = 1;
		Tuning->ExplosionRechargeSecondsPerCharge = 0.0f;
		TestFalse(TEXT("零恢复时长必须拒绝"), Tuning->IsConfigured(Error));
		return true;
	}
}

#endif
