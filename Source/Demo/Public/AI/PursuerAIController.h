// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAIController.h
 * 职责：以纯 C++ 定时状态机驱动追猎者持续追击，并选择近战/跑跳攻击，不使用行为树。
 * 边界：只做目标、距离和移动决策；攻击阶段、冷却、命中、恢复与中断交给 UPursuerAttackComponent。
 * 状态 Owner：本控制器只拥有思考 Timer；不拥有感知丢失或攻击冷却状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"

#include "PursuerAIController.generated.h"

class APursuerCharacter;
class UPursuerConfig;

/** 追猎者 AI：Timer 驱动的 持续追击→攻击→冷却→回追 状态机，默认关 Tick。 */
UCLASS()
class DEMO_API APursuerAIController final : public AAIController
{
	GENERATED_BODY()

public:
	/** 创建无常驻 Tick 的控制器。 */
	APursuerAIController();

	/** 受击适配层已取消当前路径；若攻击冷却尚未结束，记录恢复后仍需继续追击。 */
	void NotifyImpactMovementBlocked();

protected:
	/** 占有追猎者后缓存角色与 Config，并按 ThinkInterval 启动思考 Timer。 */
	virtual void OnPossess(APawn* InPawn) override;

	/** 失去占有前清理思考 Timer，避免悬挂回调。 */
	virtual void OnUnPossess() override;

private:
	/** 单次思考：玩家有效时始终追击，并按距离选择移动或攻击；由 Timer 周期调用。 */
	void Think();

	/** 被占有的追猎者，OnPossess 时缓存；失效时思考直接返回。 */
	TWeakObjectPtr<APursuerCharacter> Pursuer;

	/** 追猎者行为参数，OnPossess 时从角色缓存；失效时思考直接返回。 */
	TWeakObjectPtr<const UPursuerConfig> Config;

	/** 思考 Timer 句柄。 */
	FTimerHandle ThinkTimerHandle;
};
