// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerBehaviorTreeNodes.cpp
 * 职责：把行为树叶节点接到现有追击与攻击接口，不复制整个旧 Think。
 * 边界：树决定任务优先级；节点只执行一种动作，攻击阶段与时序仍归攻击组件。
 * 状态 Owner：共享节点无可变实例状态；检查时间由 UE 节点内存按 AI 隔离。
 */

#include "AI/BehaviorTree/PursuerBehaviorTreeNodes.h"

#include "AI/PursuerAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Characters/PursuerCharacter.h"
#include "Components/Combat/PursuerAttackComponent.h"

namespace
{
	/** 从当前树的 Pawn 获取唯一攻击执行器；不按组件名查找。 */
	UPursuerAttackComponent* GetPursuerAttack(UBehaviorTreeComponent& OwnerComp)
	{
		const AAIController* Controller = OwnerComp.GetAIOwner();
		const APursuerCharacter* Character = IsValid(Controller)
			? Cast<APursuerCharacter>(Controller->GetPawn()) : nullptr;
		return IsValid(Character) ? Character->GetAttackComponent() : nullptr;
	}
}

/** 不在每次分支搜索时额外累计脱困计时；持续根服务按配置周期运行。 */
UBTService_PursuerContext::UBTService_PursuerContext()
{
	NodeName = TEXT("Pursuer: Update Context");
	bNotifyTick = true;
	bNotifyOnSearch = false;
	bCallTickOnSearchStart = false;
	RandomDeviation = 0.0f;
}

/** 原受击与脱困检查先于攻击条件，且在攻击任务执行期间仍按周期运行。 */
void UBTService_PursuerContext::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (APursuerAIController* Controller = Cast<APursuerAIController>(OwnerComp.GetAIOwner()))
	{
		Controller->RefreshBehaviorContext();
	}
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
}

/** 不写共享 Interval；直接把各自 DA 的检查间隔写入对应节点内存。 */
void UBTService_PursuerContext::ScheduleNextTick(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (const APursuerAIController* Controller = Cast<APursuerAIController>(OwnerComp.GetAIOwner()))
	{
		SetNextTickTime(NodeMemory, Controller->GetBehaviorUpdateInterval());
		return;
	}
	Super::ScheduleNextTick(OwnerComp, NodeMemory);
}

/** 说明数值唯一来源。 */
FString UBTService_PursuerContext::GetStaticServiceDescription() const
{
	return TEXT("Refresh TargetActor / CanCloseAttack / CanJumpAttack at PursuerConfig.ThinkInterval");
}

/** 使用 UE 的间隔 Tick，只读取既有攻击完成状态。 */
UBTTask_PursuerAttack::UBTTask_PursuerAttack()
{
	NodeName = TEXT("Pursuer: Attack");
	bNotifyTick = true;
	bTickIntervals = true;
}

/** 攻击条件由当前分支和控制器核实；失败允许同一选择器尝试后续分支。 */
EBTNodeResult::Type UBTTask_PursuerAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	APursuerAIController* Controller = Cast<APursuerAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(Controller) || !Controller->TryStartBehaviorAttack(bJumpAttack))
	{
		return EBTNodeResult::Failed;
	}
	SetNextTickTime(NodeMemory, Controller->GetBehaviorUpdateInterval());
	return EBTNodeResult::InProgress;
}

/** 沿用组件已有的全部恢复阶段；没有额外 Wait 或第二份冷却。 */
void UBTTask_PursuerAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	const APursuerAIController* Controller = Cast<APursuerAIController>(OwnerComp.GetAIOwner());
	const UPursuerAttackComponent* Attack = GetPursuerAttack(OwnerComp);
	if (!IsValid(Controller) || !IsValid(Attack))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	if (!Attack->IsBusy())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}
	SetNextTickTime(NodeMemory, Controller->GetBehaviorUpdateInterval());
}

/** 真正中断时复用攻击组件清理，避免树停止后仍有攻击回调。 */
EBTNodeResult::Type UBTTask_PursuerAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (UPursuerAttackComponent* Attack = GetPursuerAttack(OwnerComp))
	{
		Attack->CancelAttack();
	}
	return EBTNodeResult::Aborted;
}

/** 同一任务类的两个实例在树上可明确区分。 */
FString UBTTask_PursuerAttack::GetStaticDescription() const
{
	return bJumpAttack ? TEXT("Jump Smash - keep running until the existing attack finishes")
		: TEXT("Close Swing - keep running until the existing attack finishes");
}

/** 连续追击且限频，不因 MoveTo 已到接近半径而结束并逐帧重发路径。 */
UBTTask_PursuerChase::UBTTask_PursuerChase()
{
	NodeName = TEXT("Pursuer: Chase Player");
	bNotifyTick = true;
	bTickIntervals = true;
}

/** 冷却、缺少攻击动画或距离不合适都落到这个持续任务。 */
EBTNodeResult::Type UBTTask_PursuerChase::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	APursuerAIController* Controller = Cast<APursuerAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(Controller))
	{
		return EBTNodeResult::Failed;
	}
	Controller->UpdateBehaviorChase();
	SetNextTickTime(NodeMemory, Controller->GetBehaviorUpdateInterval());
	return EBTNodeResult::InProgress;
}

/** 只更新追击；攻击条件由根服务和两个黑板装饰器决定。 */
void UBTTask_PursuerChase::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	APursuerAIController* Controller = Cast<APursuerAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(Controller))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	Controller->UpdateBehaviorChase();
	SetNextTickTime(NodeMemory, Controller->GetBehaviorUpdateInterval());
}

/** 路径取消不属于任务切换；普攻起手与受击各自通过原入口停止移动。 */
EBTNodeResult::Type UBTTask_PursuerChase::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}
