// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAttackComponentTests.cpp
 * 职责：验证跑跳预测、固定时间抛物线、Heavy 攻击体起点与击飞速度，不依赖 PIE、动画或项目资产。
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

		const FVector Knockback = UPursuerAttackComponent::ComputeKnockbackVelocity(
			FVector::ZeroVector,
			FVector(100.0f, 0.0f, 40.0f),
			FVector::RightVector,
			700.0f,
			350.0f);
		TestTrue(TEXT("Knockback must preserve configured horizontal velocity"),
			FMath::IsNearlyEqual(Knockback.Size2D(), 700.0f, 0.01f));
		TestTrue(TEXT("Knockback must preserve configured upward velocity"),
			FMath::IsNearlyEqual(Knockback.Z, 350.0f, 0.01f));
		TestTrue(TEXT("Knockback must point away from the Pursuer"), Knockback.X > 0.0f);

		const FVector FallbackKnockback = UPursuerAttackComponent::ComputeKnockbackVelocity(
			FVector::ZeroVector,
			FVector::ZeroVector,
			FVector::RightVector,
			500.0f,
			100.0f);
		TestTrue(TEXT("Coincident actors must fall back to the Pursuer forward direction"),
			FallbackKnockback.Y > 0.0f && FMath::IsNearlyEqual(FallbackKnockback.X, 0.0f, 0.01f));

		constexpr float BodyRadius = 90.0f;
		constexpr float BodySpeed = 2200.0f;
		constexpr float ContactEta = 0.10f;
		const FVector TargetPoint(500.0f, 0.0f, 100.0f);
		const FVector BodyStart = UPursuerAttackComponent::ComputeImpactBodyStart(
			TargetPoint,
			FVector::ForwardVector,
			BodyRadius,
			BodySpeed,
			ContactEta);
		TestTrue(TEXT("Impact body start must preserve target height"),
			FMath::IsNearlyEqual(BodyStart.Z, TargetPoint.Z, 0.01f));
		TestTrue(TEXT("Impact body start must include ETA travel, radius and body clearance"),
			FMath::IsNearlyEqual(TargetPoint.X - BodyStart.X, 355.0f, 0.01f));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
