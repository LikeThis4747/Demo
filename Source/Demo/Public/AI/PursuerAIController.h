// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAIController.h
 * 职责：以纯 C++ 定时状态机驱动追猎者的感知、追击与攻击节奏，不使用行为树。
 * 边界：只做决策与移动指令，不实现移动物理、攻击表现（交给角色）与物理受击（第二步）。
 * 状态 Owner：本控制器拥有追击/冷却运行时标记与思考 Timer 生命周期。
 */

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"

#include "PursuerAIController.generated.h"

class APursuerCharacter;
class UPursuerConfig;

/** 追猎者 AI：Timer 驱动的 察觉→追击→攻击→冷却→回追 状态机，默认关 Tick。 */
UCLASS()
class DEMO_API APursuerAIController final : public AAIController
{
	GENERATED_BODY()

public:
	/** 创建无常驻 Tick 的控制器。 */
	APursuerAIController();

protected:
	/** 占有追猎者后缓存角色与 Config，并按 ThinkInterval 启动思考 Timer。 */
	virtual void OnPossess(APawn* InPawn) override;

	/** 失去占有前清理全部 Timer 与运行时标记，避免悬挂回调。 */
	virtual void OnUnPossess() override;

private:
	/** 单次思考：按距离与视线在追击/攻击/待机之间转移；由 Timer 周期调用。 */
	void Think();

	/** 冷却结束回调，仅清除冷却标记。 */
	void OnAttackCooldownFinished();

	/** 被占有的追猎者，OnPossess 时缓存；失效时思考直接返回。 */
	TWeakObjectPtr<APursuerCharacter> Pursuer;

	/** 追猎者行为参数，OnPossess 时从角色缓存；失效时思考直接返回。 */
	TWeakObjectPtr<const UPursuerConfig> Config;

	/** 思考 Timer 句柄。 */
	FTimerHandle ThinkTimerHandle;

	/** 攻击冷却 Timer 句柄。 */
	FTimerHandle AttackCooldownTimerHandle;

	/** 是否处于追击态（已察觉玩家）；配合察觉/丢失双阈值防抖。 */
	bool bIsChasing = false;

	/** 是否处于攻击冷却中；冷却期间不发起新攻击、不追击移动。 */
	bool bIsOnAttackCooldown = false;
};
