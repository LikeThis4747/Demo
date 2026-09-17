// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerBehaviorTreeNodes.h
 * 职责：本项目追猎者行为树的上下文服务、攻击任务和持续追击任务。
 * 边界：不保存攻击阶段、不修改攻击数值、不生成树资产；由编辑器装配独立分支。
 * 状态 Owner：上下文属于控制器，攻击属于攻击组件；节点检查间隔存于 UE 每个 AI 的节点内存。
 */

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"

#include "PursuerBehaviorTreeNodes.generated.h"

/** 挂在根选择器上，沿用 DA 的 ThinkInterval 刷新黑板和既有优先检查。 */
UCLASS(meta = (DisplayName = "Pursuer: Update Context"))
class UBTService_PursuerContext final : public UBTService
{
	GENERATED_BODY()

public:
	/** 启用定时检查；首次上下文由控制器在启动树前填充。 */
	UBTService_PursuerContext();

protected:
	/** 更新本 AI 的上下文，再安排下一次检查，不直接选择攻击任务。 */
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/** 检查周期读取该 AI 的配置，不修改由多个 AI 共享的节点属性。 */
	virtual void ScheduleNextTick(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/** 在编辑器说明实际周期来源，避免误以为节点面板 Interval 是另一份玩法配置。 */
	virtual FString GetStaticServiceDescription() const override;
};

/** 放置两次分别作为普攻和大跳叶节点；启动失败返回 Failed，让选择器继续右侧分支。 */
UCLASS(meta = (DisplayName = "Pursuer: Attack"))
class UBTTask_PursuerAttack final : public UBTTaskNode
{
	GENERATED_BODY()

public:
	/** 启用按配置间隔检查原攻击完成状态，不新增攻击计时器或恢复时长。 */
	UBTTask_PursuerAttack();

	/** 编辑器装配：false 为普攻，true 为大跳；不是运行时阶段。 */
	UPROPERTY(EditAnywhere, Category = "Pursuer", meta = (DisplayName = "Jump Attack"))
	bool bJumpAttack = false;

	/** 调用既有攻击入口，成功后保持任务运行直到原组件空闲。 */
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/** 编辑器显示当前实例选择的攻击种类。 */
	virtual FString GetStaticDescription() const override;

protected:
	/** 仅检查结束；距离离开起手范围不会取消正在执行的攻击。 */
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/** 树被停止或明确中断时通过现有接口取消，冷却保留。 */
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

/** 默认叶节点持续追击；不以攻击冷却或未感知玩家为理由进入待机。 */
UCLASS(meta = (DisplayName = "Pursuer: Chase Player"))
class UBTTask_PursuerChase final : public UBTTaskNode
{
	GENERATED_BODY()

public:
	/** 按原思考间隔更新路径；不启用逐帧寻路。 */
	UBTTask_PursuerChase();

	/** 首次提交原有追击请求，然后持续运行，等待更高优先级攻击条件。 */
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
	/** 受击或刚脱困的周期由控制器阻止新路径，下一正常周期自行恢复。 */
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/** 退出追击不 StopMovement，保留大跳前段原有助跑；受击取消由控制器负责。 */
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
