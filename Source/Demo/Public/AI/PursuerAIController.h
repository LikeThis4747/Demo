// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAIController.h
 * 职责：启动配置中的行为树，提供玩家目标、攻击条件及既有追击/脱困操作。
 * 边界：普攻、大跳、追击的优先级由树资产决定；攻击阶段、冷却和命中仍属于攻击组件。
 * 状态 Owner：本控制器拥有每个 AI 的追逐异常累计时间和本次上下文是否允许移动；不拥有攻击阶段。
 */

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"

#include "PursuerAIController.generated.h"

class APursuerCharacter;
class UPursuerConfig;

/** 追猎者行为树接入；控制器无 Tick，目标始终是本局玩家。 */
UCLASS()
class DEMO_API APursuerAIController final : public AAIController
{
	GENERATED_BODY()

public:
	/** 创建无常驻 Tick 的控制器。 */
	APursuerAIController();

	/** 取消攻击与路径；空中停止保留原有竖直速度。 */
	void NotifyImpactMovementBlocked();

	/** 树服务按配置周期刷新黑板，并优先执行原有受击抑制与追逐异常脱困检查。 */
	void RefreshBehaviorContext();

	/** 攻击任务请求一种攻击；只消费对应黑板条件，不在此选择其他攻击或追击。 */
	bool TryStartBehaviorAttack(bool bJumpAttack);

	/** 追击任务按原接近半径更新移动；受击、攻击执行或本次刚脱困时不发新路径。 */
	void UpdateBehaviorChase();

	/** 树服务和任务的检查周期，单位秒；唯一配置来源为 PursuerConfig::ThinkInterval。 */
	float GetBehaviorUpdateInterval() const;

protected:
	/** 缓存角色与 Config，验证黑板契约后启动配置中的行为树。 */
	virtual void OnPossess(APawn* InPawn) override;

	/** 失去占有前停止树并取消攻击，随后清理每个 AI 的上下文。 */
	virtual void OnUnPossess() override;

private:
	/** 尝试把追猎者重放置到玩家镜头后方约三个逻辑格的有效导航位置。 */
	bool TryRelocateBehindPlayer(APawn* PlayerPawn);

	/** 被占有的追猎者，OnPossess 时缓存；失效时思考直接返回。 */
	TWeakObjectPtr<APursuerCharacter> Pursuer;

	/** 追猎者行为参数，OnPossess 时从角色缓存；失效时思考直接返回。 */
	TWeakObjectPtr<const UPursuerConfig> Config;

	/** 上下文服务写入；仅本次检查允许追击时为 true，受击/攻击/脱困时清零。 */
	bool bCanRequestMovement = false;

	/** 水平距离或高度差持续异常的累计时间，正常后立即清零。 */
	float RecoveryConditionSeconds = 0.0f;
};
