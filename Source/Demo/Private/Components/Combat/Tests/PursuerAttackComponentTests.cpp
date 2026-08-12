// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAttackComponentTests.cpp
 * 职责：验证跑跳攻击的预测截断与固定时间抛物线计算，不依赖 PIE、动画或项目资产。
 * 边界：不验证 CharacterMovement 碰撞、Montage 对帧或玩家手感，这些留给编辑器运行验收。
 */

#include "Components/Combat/PursuerAttackComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace ZeroEscape::Combat::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FPursuerAttackPredictionAndBallisticsTest,
		"Demo.Combat.PursuerAttack.PredictionAndBallistics",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	/** 验证移动目标预判不会改变高度、会遵守可达距离，并且抛体积分能回到锁定点。 */
	bool FPursuerAttackPredictionAndBallisticsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		const FVector Origin(0.0f, 0.0f, 100.0f);
		const FVector TargetLocation(400.0f, 0.0f, 25.0f);
		const FVector TargetVelocity(0.0f, 500.0f, 200.0f);
		const FVector Predicted = UPursuerAttackComponent::ComputePredictedTargetPoint(
			Origin, TargetLocation, TargetVelocity, 0.4f, 1000.0f);

		TestTrue(TEXT("Prediction must retain the target height"),
			FMath::IsNearlyEqual(Predicted.Z, TargetLocation.Z));
		TestTrue(TEXT("Prediction must lead the target by horizontal velocity only"),
			Predicted.Equals(FVector(400.0f, 200.0f, 25.0f), KINDA_SMALL_NUMBER));

		const FVector Clamped = UPursuerAttackComponent::ComputePredictedTargetPoint(
			Origin, TargetLocation, TargetVelocity, 0.4f, 300.0f);
		FVector ClampedHorizontalDelta = Clamped - Origin;
		ClampedHorizontalDelta.Z = 0.0f;
		TestTrue(TEXT("Prediction must clamp unreachable horizontal distance"),
			FMath::IsNearlyEqual(ClampedHorizontalDelta.Size(), 300.0f, 0.01f));
		TestTrue(TEXT("Clamped prediction must still retain target height"),
			FMath::IsNearlyEqual(Clamped.Z, TargetLocation.Z));

		constexpr float FlightSeconds = 0.65f;
		constexpr float GravityZ = -980.0f;
		FVector LaunchVelocity;
		TestTrue(TEXT("Valid fixed-time trajectory must produce a launch velocity"),
			UPursuerAttackComponent::CalculateBallisticLaunchVelocity(
				Origin, Predicted, FlightSeconds, GravityZ, LaunchVelocity));

		const FVector Integrated = Origin
			+ LaunchVelocity * FlightSeconds
			+ FVector::UpVector * (0.5f * GravityZ * FMath::Square(FlightSeconds));
		TestTrue(TEXT("Integrated launch must reach the locked point at the configured time"),
			Integrated.Equals(Predicted, 0.01f));

		TestFalse(TEXT("Zero flight time must be rejected"),
			UPursuerAttackComponent::CalculateBallisticLaunchVelocity(
				Origin, Predicted, 0.0f, GravityZ, LaunchVelocity));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
