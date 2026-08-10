// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAIController.cpp
 * 职责：实现追猎者的定时状态机——感知玩家、寻路追击、进入距离攻击、冷却后回追。
 * 边界：不实现移动物理与攻击动画（角色负责），不做物理受击/伤害（第二步），不使用行为树/AIPerception。
 */

#include "AI/PursuerAIController.h"

#include "Characters/PursuerCharacter.h"
#include "Data/PursuerConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogPursuerAI, Log, All);

/** 创建控制器：关闭常驻 Tick，全部决策靠思考 Timer。 */
APursuerAIController::APursuerAIController()
{
	PrimaryActorTick.bCanEverTick = false;
}

/** 受击取消 PathFollowing 时记住恢复意图；攻击冷却仍照常限制下一次攻击。 */
void APursuerAIController::NotifyImpactMovementBlocked()
{
	bResumeChaseDuringAttackCooldown |= bIsOnAttackCooldown;

	UCharacterMovementComponent* Movement = Pursuer.IsValid()
		? Pursuer->GetCharacterMovement()
		: nullptr;
	const bool bWasFalling = IsValid(Movement) && Movement->IsFalling();
	const float PreservedVerticalSpeed = IsValid(Movement) ? Movement->Velocity.Z : 0.0f;
	StopMovement();
	if (bWasFalling && IsValid(Movement) && Movement->IsFalling())
	{
		Movement->Velocity.Z = PreservedVerticalSpeed;
		Movement->UpdateComponentVelocity();
	}
}

/** 缓存追猎者与 Config，校验通过后按 ThinkInterval 周期启动思考。 */
void APursuerAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	Pursuer = Cast<APursuerCharacter>(InPawn);
	if (!Pursuer.IsValid())
	{
		UE_LOG(LogPursuerAI, Error, TEXT("%s 占有的不是 APursuerCharacter，AI 不启动。"), *GetName());
		return;
	}

	Config = Pursuer->GetConfig();
	if (!Config.IsValid())
	{
		UE_LOG(LogPursuerAI, Error, TEXT("%s 追猎者无有效 Config，AI 不启动。"), *GetName());
		return;
	}

	GetWorldTimerManager().SetTimer(
		ThinkTimerHandle, this, &APursuerAIController::Think,
		Config->ThinkInterval, /*bLoop=*/true, /*FirstDelay=*/0.0f);
}

/** 失去占有前清理全部 Timer 与标记，避免悬挂回调访问失效对象。 */
void APursuerAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(ThinkTimerHandle);
	GetWorldTimerManager().ClearTimer(AttackCooldownTimerHandle);
	bIsChasing = false;
	bIsOnAttackCooldown = false;
	bResumeChaseDuringAttackCooldown = false;

	Super::OnUnPossess();
}

/** 状态机核心：按距离（察觉/丢失双阈值 + 可选视线）决定追击、攻击或待机。 */
void APursuerAIController::Think()
{
	if (!Pursuer.IsValid() || !Config.IsValid())
	{
		return;
	}

	// Heavy 或 Light Stop 阻断移动；Light Slow 不取消 PathFollowing。
	if (Pursuer->IsImpactMovementBlocked())
	{
		NotifyImpactMovementBlocked();
		return;
	}
	const bool bAttackSuppressed = Pursuer->IsImpactAttackSuppressed();

	// GetPlayerPawn 本就返回非 const APawn*；此处保持非 const，供下方 SetFocus/MoveToActor 直接使用，避免多余 const_cast。
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		return;
	}

	const float Distance = FVector::Dist(Pursuer->GetActorLocation(), PlayerPawn->GetActorLocation());

	// 察觉/丢失双阈值：未追击时需进入 SenseRadius（且视线通过）才开始；已追击时超过 LoseSightRadius 才放弃。
	if (!bIsChasing)
	{
		const bool bInSenseRange = Distance <= Config->SenseRadius;
		const bool bCanSee = !Config->bUseLineOfSight || LineOfSightTo(PlayerPawn);
		bIsChasing = bInSenseRange && bCanSee;
	}
	else if (Distance > Config->LoseSightRadius)
	{
		bIsChasing = false;
		StopMovement();
	}

	if (!bIsChasing)
	{
		return;
	}

	// 攻击距离内：停下、面向玩家；不在冷却则发起一次攻击并进入冷却。
	if (Distance <= Config->AttackRange)
	{
		StopMovement();
		SetFocus(PlayerPawn);

		if (!bIsOnAttackCooldown && !bAttackSuppressed)
		{
			bResumeChaseDuringAttackCooldown = false;
			bIsOnAttackCooldown = true;
			Pursuer->PlayAttackMontage();
			GetWorldTimerManager().SetTimer(
				AttackCooldownTimerHandle, this, &APursuerAIController::OnAttackCooldownFinished,
				Config->AttackCooldown, /*bLoop=*/false);
		}
		return;
	}

	// 攻击距离外且不在冷却时继续追击。Light Slow 会中断攻击但不取消路径，
	// 因此即使旧攻击冷却尚未结束也要重新发出 MoveTo，避免原地等待完整冷却。
	if (!bIsOnAttackCooldown || bAttackSuppressed || bResumeChaseDuringAttackCooldown)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
		MoveToActor(PlayerPawn, Config->AttackApproachRadius);
	}
}

/** 冷却结束：清标记，下次思考即可重新攻击或继续追击。 */
void APursuerAIController::OnAttackCooldownFinished()
{
	bIsOnAttackCooldown = false;
	bResumeChaseDuringAttackCooldown = false;
}
