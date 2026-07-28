// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAIController.cpp
 * 职责：实现追猎者的定时状态机——感知玩家、寻路追击、进入距离攻击、冷却后回追。
 * 边界：不实现移动物理与攻击动画（角色负责），不做物理受击/伤害（第二步），不使用行为树/AIPerception。
 */

#include "AI/PursuerAIController.h"

#include "Characters/PursuerCharacter.h"
#include "Data/PursuerConfig.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogPursuerAI, Log, All);

/** 创建控制器：关闭常驻 Tick，全部决策靠思考 Timer。 */
APursuerAIController::APursuerAIController()
{
	PrimaryActorTick.bCanEverTick = false;
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

	Super::OnUnPossess();
}

/** 状态机核心：按距离（察觉/丢失双阈值 + 可选视线）决定追击、攻击或待机。 */
void APursuerAIController::Think()
{
	if (!Pursuer.IsValid() || !Config.IsValid())
	{
		return;
	}

	// 受击停顿：受击反应播放期间停止移动、不追不攻击，等受击结束后下一次思考自然恢复。
	if (Pursuer->IsReacting())
	{
		StopMovement();
		return;
	}

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

		if (!bIsOnAttackCooldown)
		{
			bIsOnAttackCooldown = true;
			Pursuer->PlayAttackMontage();
			GetWorldTimerManager().SetTimer(
				AttackCooldownTimerHandle, this, &APursuerAIController::OnAttackCooldownFinished,
				Config->AttackCooldown, /*bLoop=*/false);
		}
		return;
	}

	// 攻击距离外且不在冷却：解除面向锁定并追击到比攻击距离更近处（ApproachRadius < AttackRange），
	// 使追猎者一旦进入攻击距离即主动停下攻击，消除停在 AttackRange 边界的攻击抖动。
	if (!bIsOnAttackCooldown)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
		MoveToActor(PlayerPawn, Config->AttackApproachRadius);
	}
}

/** 冷却结束：清标记，下次思考即可重新攻击或继续追击。 */
void APursuerAIController::OnAttackCooldownFinished()
{
	bIsOnAttackCooldown = false;
}
