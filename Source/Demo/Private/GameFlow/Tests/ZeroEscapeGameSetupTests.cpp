// Copyright Epic Games, Inc. All Rights Reserved.

/** 纯值测试：异步开局忽略旧操作、重复终态和 EndPlay 后回调。 */

#include "GameFlow/ZeroEscapeGameSetupGate.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameFlow/ZeroEscapeGameInstance.h"
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
		FZeroEscapeAutomaticRetryPolicyTest,
		"Demo.GameFlow.AutomaticRetryPolicy",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeAutomaticRetryPolicyTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FZeroEscapeGenerationReport Report;
		Report.Stage = EZeroEscapeGenerationStage::Configuration;
		Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
		TestFalse(TEXT("配置错误不得靠换 Seed 重试"),
			FGameSetupGate::IsRecoverableGenerationFailure(Report));
		Report.Stage = EZeroEscapeGenerationStage::StructurePlacement;
		Report.Failure = EZeroEscapeGenerationFailure::CapacityInsufficient;
		TestTrue(TEXT("Seed 影响楼层与结构占用时，容量失败允许有限重试"),
			FGameSetupGate::IsRecoverableGenerationFailure(Report));
		Report.Failure = EZeroEscapeGenerationFailure::StructurePlacementFailed;
		TestTrue(TEXT("结构候选失败允许换 Seed"),
			FGameSetupGate::IsRecoverableGenerationFailure(Report));
		Report.Stage = EZeroEscapeGenerationStage::WfcLayout;
		Report.Failure = EZeroEscapeGenerationFailure::SolverBudgetExhausted;
		TestTrue(TEXT("搜索长尾耗尽允许有限换 Seed"),
			FGameSetupGate::IsRecoverableGenerationFailure(Report));
		Report.Stage = EZeroEscapeGenerationStage::NavigationBuild;
		Report.Failure = EZeroEscapeGenerationFailure::NavigationValidationFailed;
		TestFalse(TEXT("导航配置或准备失败不得伪装成随机失败"),
			FGameSetupGate::IsRecoverableGenerationFailure(Report));
		Report.Stage = EZeroEscapeGenerationStage::NavigationValidation;
		TestTrue(TEXT("布局相关的最终导航不连通允许换 Seed"),
			FGameSetupGate::IsRecoverableGenerationFailure(Report));

		UZeroEscapeGameInstance* GameInstance =
			NewObject<UZeroEscapeGameInstance>();
		if (!TestNotNull(TEXT("测试 GameInstance 必须创建成功"), GameInstance))
		{
			return false;
		}
		FZeroEscapeGenerationRequest Request;
		Request.Seed = 24680;
		Request.Difficulty = EZeroEscapeDifficulty::Hard;
		GameInstance->SetPendingRequest(Request);

		TArray<int32> ExpectedRetrySeeds;
		int32 ExpectedPreviousSeed = Request.Seed;
		for (int32 ExpectedRetryNumber = 1;
			ExpectedRetryNumber <= UZeroEscapeGameInstance::MaxAutomaticGenerationRetryCount;
			++ExpectedRetryNumber)
		{
			int32 RetryNumber = 0;
			int32 PreviousSeed = 0;
			int32 NextSeed = 0;
			TestTrue(TEXT("上限内必须允许推进重试请求"),
				GameInstance->TryAdvancePendingRequestForAutomaticRetry(
					RetryNumber, PreviousSeed, NextSeed));
			TestEqual(TEXT("重试序号必须连续"),
				RetryNumber, ExpectedRetryNumber);
			TestEqual(TEXT("日志用旧 Seed 必须准确"),
				PreviousSeed, ExpectedPreviousSeed);
			TestNotEqual(TEXT("下一 Seed 不得等于旧 Seed"),
				NextSeed, PreviousSeed);
			TestTrue(TEXT("自动重试 Seed 保持非负"), NextSeed >= 0);
			TestEqual(TEXT("重试不得修改难度"),
				GameInstance->GetPendingRequest().Difficulty,
				Request.Difficulty);
			ExpectedRetrySeeds.Add(NextSeed);
			ExpectedPreviousSeed = NextSeed;
		}

		const FZeroEscapeGenerationRequest RequestAtLimit =
			GameInstance->GetPendingRequest();
		int32 RetryNumber = 0;
		int32 PreviousSeed = 0;
		int32 NextSeed = 0;
		TestFalse(TEXT("达到上限后必须拒绝继续重试"),
			GameInstance->TryAdvancePendingRequestForAutomaticRetry(
				RetryNumber, PreviousSeed, NextSeed));
		TestEqual(TEXT("拒绝重试不得修改 Seed"),
			GameInstance->GetPendingRequest().Seed, RequestAtLimit.Seed);

		GameInstance->SetPendingRequest(Request);
		TestTrue(TEXT("显式新请求必须重置重试次数"),
			GameInstance->TryAdvancePendingRequestForAutomaticRetry(
				RetryNumber, PreviousSeed, NextSeed));
		TestEqual(TEXT("相同初始 Seed 的重试序列必须可复现"),
			NextSeed, ExpectedRetrySeeds[0]);
		return true;
	}
}

#endif
