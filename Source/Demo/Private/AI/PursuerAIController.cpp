// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAIController.cpp
 * 职责：实现追猎者的定时决策——感知、追击、中距离预判跑跳与近距离斧击选择。
 * 边界：不拥有攻击阶段、冷却、动画、位移或命中；这些统一交给 UPursuerAttackComponent。
 */

#include "AI/PursuerAIController.h"

#include "Characters/PursuerCharacter.h"
#include "Components/Combat/PursuerAttackComponent.h"
#include "Data/PursuerConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogPursuerAI, Log, All);

/** 创建控制器：关闭常驻 Tick，全部决策靠思考 Timer。 */
APursuerAIController::APursuerAIController()
{
	PrimaryActorTick.bCanEverTick = false;
}

/** 受击取消 PathFollowing，并取消当前攻击事务；空中的 Z 速度仍由现有受击合同保留。 */
void APursuerAIController::NotifyImpactMovementBlocked()
{
	if (Pursuer.IsValid() && IsValid(Pursuer->GetAttackComponent()))
	{
		Pursuer->GetAttackComponent()->CancelAttack();
	}

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
	bIsChasing = false;

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
	UPursuerAttackComponent* AttackComponent = Pursuer->GetAttackComponent();
	if (!IsValid(AttackComponent))
	{
		return;
	}
	if (bAttackSuppressed && AttackComponent->IsBusy())
	{
		AttackComponent->CancelAttack();
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

	// 攻击组件忙碌时不重复 StopMovement，避免空中 Launch 的水平速度被路径系统反复清理。
	if (AttackComponent->IsBusy())
	{
		SetFocus(PlayerPawn);
		return;
	}

	// 近距离优先斧击；只有组件真正启动成功才停在攻击态，资产缺失时仍继续追击。
	if (!bAttackSuppressed
		&& Distance <= Config->AttackRange
		&& AttackComponent->TryStartCloseSwing(PlayerPawn))
	{
		SetFocus(PlayerPawn);
		return;
	}

	// 中距离用一次性预测跑跳封锁玩家前路；落点锁定后空中不再持续追踪。
	if (!bAttackSuppressed
		&& Distance >= Config->JumpAttackMinRange
		&& Distance <= Config->JumpAttackMaxRange
		&& AttackComponent->TryStartJumpSmash(PlayerPawn))
	{
		SetFocus(PlayerPawn);
		return;
	}

	// 冷却中、资产暂缺或目标在攻击距离外都继续追击，不再站在旧 AttackRange 等待。
	ClearFocus(EAIFocusPriority::Gameplay);
	MoveToActor(PlayerPawn, Config->AttackApproachRadius);
}
