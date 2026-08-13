// Copyright Epic Games, Inc. All Rights Reserved.

/** 纯值测试：异步开局忽略旧操作、重复终态和 EndPlay 后回调。 */

#include "GameFlow/ZeroEscapeGameSetupGate.h"

#if WITH_DEV_AUTOMATION_TESTS

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
}

#endif
