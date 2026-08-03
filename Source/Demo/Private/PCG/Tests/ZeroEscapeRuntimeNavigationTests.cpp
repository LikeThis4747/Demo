// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeNavigationTests.cpp
 * 职责：验证正式导航等待状态对旧操作、错误导航数据、重复回调和超时的过滤合同。
 * 边界：不生成临时坡面或探针；实际动态 RecastNavMesh 仍由正式楼梯 HISM 的 PIE 验收证明。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PCG/ZeroEscapeRuntimeNavigationGate.h"
#include "UObject/UObjectGlobals.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeRuntimeNavigationGateTest,
		"Demo.PCG.Runtime.NavigationOperationGate",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeRuntimeNavigationGateTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UObject* ExpectedNavigationDataIdentity = NewObject<UZeroEscapeLevelGenerationProfile>();
		UObject* OtherNavigationDataIdentity = NewObject<UZeroEscapeLevelGenerationProfile>();
		TestNotNull(TEXT("测试导航身份对象必须存在"), ExpectedNavigationDataIdentity);
		TestNotNull(TEXT("错误导航身份对象必须存在"), OtherNavigationDataIdentity);

		FRuntimeNavigationGateSnapshot State;
		State.OperationId = 7;
		State.ExpectedNavigationDataKey = FObjectKey(ExpectedNavigationDataIdentity);
		State.bWaiting = true;

		TestFalse(
			TEXT("本轮几何注册开始前不得接受完成事件"),
			FRuntimeNavigationGate::AcceptCompletion(
				State, 7, FObjectKey(ExpectedNavigationDataIdentity)));

		State.bGeometryRegistrationStarted = true;
		TestFalse(
			TEXT("最后一组 HISM 提交前到达的目标事件不得被接受"),
			FRuntimeNavigationGate::AcceptCompletion(
				State, 7, FObjectKey(ExpectedNavigationDataIdentity)));

		State.bGeometrySubmitted = true;
		TestTrue(
			TEXT("全部 HISM 提交后的目标完成事件必须被接受"),
			FRuntimeNavigationGate::AcceptCompletion(
				State, 7, FObjectKey(ExpectedNavigationDataIdentity)));
		State.bReceivedTargetCompletion = true;
		TestFalse(
			TEXT("未观察到本轮导航构建时不得执行路径验收"),
			FRuntimeNavigationGate::CanValidate(State, 7, false));
		State.bReceivedTargetCompletion = false;

		State.bObservedNavigationBuild = true;
		TestFalse(
			TEXT("旧操作编号不得接管当前导航事件"),
			FRuntimeNavigationGate::AcceptCompletion(
				State, 6, FObjectKey(ExpectedNavigationDataIdentity)));
		TestFalse(
			TEXT("其他导航数据完成不得接管当前操作"),
			FRuntimeNavigationGate::AcceptCompletion(
				State, 7, FObjectKey(OtherNavigationDataIdentity)));
		TestTrue(
			TEXT("当前操作的目标导航数据完成事件必须被接受"),
			FRuntimeNavigationGate::AcceptCompletion(
				State, 7, FObjectKey(ExpectedNavigationDataIdentity)));

		State.bReceivedTargetCompletion = true;
		TestFalse(
			TEXT("导航仍在构建时不得执行路径验收"),
			FRuntimeNavigationGate::CanValidate(State, 7, true));
		TestTrue(
			TEXT("目标先完成后，其他 NavData 的完成事件必须触发再次检查"),
			FRuntimeNavigationGate::ShouldRetryAfterAnyCompletion(State, 7));
		TestTrue(
			TEXT("目标事件完成且导航静止后必须执行一次路径验收"),
			FRuntimeNavigationGate::CanValidate(State, 7, false));

		State.bTerminal = true;
		TestFalse(
			TEXT("进入终态后重复完成回调必须被忽略"),
			FRuntimeNavigationGate::AcceptCompletion(
				State, 7, FObjectKey(ExpectedNavigationDataIdentity)));
		TestFalse(
			TEXT("进入终态后重复路径验收必须被忽略"),
			FRuntimeNavigationGate::CanValidate(State, 7, false));

		FRuntimeNavigationGateSnapshot TimeoutState;
		TimeoutState.OperationId = 9;
		TimeoutState.ExpectedNavigationDataKey = FObjectKey(ExpectedNavigationDataIdentity);
		TimeoutState.bWaiting = true;
		TimeoutState.bGeometrySubmitted = true;
		TestFalse(
			TEXT("旧操作的超时 Timer 不得终止当前操作"),
			FRuntimeNavigationGate::AcceptTimeout(TimeoutState, 8));
		TestTrue(
			TEXT("当前操作的十秒超时必须被接受"),
			FRuntimeNavigationGate::AcceptTimeout(TimeoutState, 9));
		TimeoutState.bTerminal = true;
		TestFalse(
			TEXT("重复超时回调必须被忽略"),
			FRuntimeNavigationGate::AcceptTimeout(TimeoutState, 9));

		return true;
	}
}

#endif
