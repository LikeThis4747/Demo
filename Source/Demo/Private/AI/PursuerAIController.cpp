// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAIController.cpp
 * 职责：启动行为树、更新黑板事实，并复用原有追击和脱困操作。
 * 边界：树资产选择普攻/大跳/追击；攻击组件独占阶段、冷却、动画和命中。
 * 状态 Owner：每个控制器只保存自己的上下文与追逐异常累计时间，节点不共享运行时状态。
 */

#include "AI/PursuerAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BrainComponent.h"
#include "Characters/PursuerCharacter.h"
#include "Components/Combat/PursuerAttackComponent.h"
#include "Data/PursuerConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogPursuerAI, Log, All);

namespace
{
	// 本项目黑板契约；不是 Actor/组件名字，也不保存第二份运行时攻击状态。
	const FName TargetActorKey(TEXT("TargetActor"));
	const FName CanCloseAttackKey(TEXT("CanCloseAttack"));
	const FName CanJumpAttackKey(TEXT("CanJumpAttack"));
}

/** 创建控制器：关闭常驻 Tick，周期更新由行为树服务负责。 */
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

/** 缓存配置并验证树和黑板；装配缺失时明确报错，不运行另一套隐藏的旧决策。 */
void APursuerAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	RecoveryConditionSeconds = 0.0f;
	bCanRequestMovement = false;

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

	UBehaviorTree* Tree = Config->BehaviorTree;
	UBlackboardComponent* BlackboardComponent = nullptr;
	if (!IsValid(Tree) || !IsValid(Tree->BlackboardAsset)
		|| !UseBlackboard(Tree->BlackboardAsset, BlackboardComponent))
	{
		UE_LOG(LogPursuerAI, Error, TEXT("%s 请在追猎者 Config 装配带黑板的 BehaviorTree，AI 未启动。"), *GetName());
		return;
	}
	if (BlackboardComponent->GetKeyType(BlackboardComponent->GetKeyID(TargetActorKey)) != UBlackboardKeyType_Object::StaticClass()
		|| BlackboardComponent->GetKeyType(BlackboardComponent->GetKeyID(CanCloseAttackKey)) != UBlackboardKeyType_Bool::StaticClass()
		|| BlackboardComponent->GetKeyType(BlackboardComponent->GetKeyID(CanJumpAttackKey)) != UBlackboardKeyType_Bool::StaticClass())
	{
		UE_LOG(LogPursuerAI, Error, TEXT("%s 黑板需要 TargetActor(Object)、CanCloseAttack(Bool)、CanJumpAttack(Bool)，AI 未启动。"), *GetName());
		return;
	}

	RefreshBehaviorContext();
	if (!RunBehaviorTree(Tree))
	{
		UE_LOG(LogPursuerAI, Error, TEXT("%s 行为树启动失败。"), *GetName());
	}
}

/** 停止树和攻击，避免在旧 Pawn 上留下任务或攻击 Timer。 */
void APursuerAIController::OnUnPossess()
{
	if (UBrainComponent* Brain = GetBrainComponent())
	{
		Brain->StopLogic(TEXT("Pursuer unpossessed"));
	}
	if (Pursuer.IsValid() && IsValid(Pursuer->GetAttackComponent()))
	{
		Pursuer->GetAttackComponent()->CancelAttack();
	}
	RecoveryConditionSeconds = 0.0f;
	bCanRequestMovement = false;

	Super::OnUnPossess();
	Pursuer.Reset();
	Config.Reset();
}

/** 只读取原有思考周期；配置失效时短周期仅供残留任务安全结束，不启动玩法。 */
float APursuerAIController::GetBehaviorUpdateInterval() const
{
	return Config.IsValid() ? Config->ThinkInterval : 0.1f;
}

/** 尝试三个固定的镜头后方候选点；只要求落在同层附近的 NavMesh，不重复验证迷宫连通性。 */
bool APursuerAIController::TryRelocateBehindPlayer(
	APawn* PlayerPawn)
{
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!Pursuer.IsValid()
		|| !IsValid(PlayerPawn)
		|| !IsValid(PlayerController)
		|| !IsValid(NavigationSystem))
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	constexpr float RelocationDistance = 1800.0f;
	const FVector ProjectionExtent(300.0f, 300.0f, 200.0f);
	constexpr float RearYawOffsets[] = {180.0f, 135.0f, 225.0f};
	const FVector PlayerNavLocation = PlayerPawn->GetNavAgentLocation();
	const float NavAgentOffsetZ = Pursuer->GetActorLocation().Z - Pursuer->GetNavAgentLocation().Z;

	for (const float YawOffset : RearYawOffsets)
	{
		const FVector CandidateDirection =
			FRotator(0.0f, ViewRotation.Yaw + YawOffset, 0.0f).Vector();
		const FVector CandidatePoint = PlayerNavLocation + CandidateDirection * RelocationDistance;
		FNavLocation ProjectedLocation;
		if (!NavigationSystem->ProjectPointToNavigation(
				CandidatePoint,
				ProjectedLocation,
				ProjectionExtent,
				&GetNavAgentPropertiesRef()))
		{
			continue;
		}

		FVector TeleportLocation = ProjectedLocation.Location;
		TeleportLocation.Z += NavAgentOffsetZ;
		FRotator FacingRotation = (PlayerPawn->GetActorLocation() - TeleportLocation).Rotation();
		FacingRotation.Pitch = 0.0f;
		FacingRotation.Roll = 0.0f;
		if (!Pursuer->TeleportTo(TeleportLocation, FacingRotation, /*bIsATest=*/false, /*bNoCheck=*/false))
		{
			continue;
		}

		StopMovement();
		if (UCharacterMovementComponent* Movement = Pursuer->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		UE_LOG(LogPursuerAI, Log, TEXT("%s 追逐异常持续超时，已隐藏重放置到玩家后方。"), *GetName());
		return true;
	}

	return false;
}

/** 服务只更新条件并执行原有优先检查；真正选择并启动攻击/追击的是树的各个任务。 */
void APursuerAIController::RefreshBehaviorContext()
{
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!IsValid(BlackboardComponent))
	{
		return;
	}
	bool bCloseOpportunity = false;
	bool bJumpOpportunity = false;
	// 每个周期只写最终值，避免同一次刷新产生 false -> true 的虚假条件变化。
	ON_SCOPE_EXIT
	{
		BlackboardComponent->SetValueAsBool(CanCloseAttackKey, bCloseOpportunity);
		BlackboardComponent->SetValueAsBool(CanJumpAttackKey, bJumpOpportunity);
	};
	bCanRequestMovement = false;
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
	const bool bAttackSuppressed =
		Pursuer->IsImpactAttackSuppressed() || !Config->bEnableAttacks;
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
	BlackboardComponent->SetValueAsObject(TargetActorKey, PlayerPawn);
	if (!IsValid(PlayerPawn))
	{
		return;
	}

	const FVector PursuerLocation = Pursuer->GetActorLocation();
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	const float Distance = FVector::Dist(PursuerLocation, PlayerLocation);
	const float VerticalDistance = FMath::Abs(PlayerLocation.Z - PursuerLocation.Z);
	constexpr float RecoveryHorizontalDistance = 3600.0f;
	constexpr float RecoveryVerticalDifference = 225.0f;
	const bool bRecoveryCondition = FVector::Dist2D(PursuerLocation, PlayerLocation) > RecoveryHorizontalDistance
		|| VerticalDistance > RecoveryVerticalDifference;
	RecoveryConditionSeconds = bRecoveryCondition
		? FMath::Min(RecoveryConditionSeconds + Config->ThinkInterval, Config->RecoveryDelaySeconds)
		: 0.0f;

	if (RecoveryConditionSeconds >= Config->RecoveryDelaySeconds)
	{
		RecoveryConditionSeconds = 0.0f;
		if (TryRelocateBehindPlayer(PlayerPawn))
		{
			return;
		}
	}

	constexpr float FlatFloorNormalZ = 0.99f;
	constexpr float CloseAttackMaxVerticalDifference = 70.0f;
	const UCharacterMovementComponent* Movement = Pursuer->GetCharacterMovement();
	const bool bOnInclinedFloor = IsValid(Movement)
		&& Movement->CurrentFloor.IsWalkableFloor()
		&& Movement->CurrentFloor.HitResult.ImpactNormal.Z < FlatFloorNormalZ;
	const bool bNeedsVerticalTraversal = IsValid(Movement)
		&& VerticalDistance > Movement->MaxStepHeight;
	const bool bCanStartCloseSwing = VerticalDistance <= CloseAttackMaxVerticalDifference;
	const bool bCanStartJumpSmash = !bOnInclinedFloor && !bNeedsVerticalTraversal;

	// 攻击组件忙碌时不重复 StopMovement，避免空中 Launch 的水平速度被路径系统反复清理。
	if (AttackComponent->IsBusy())
	{
		SetFocus(PlayerPawn);
		return;
	}

	const bool bAttackReady = !bAttackSuppressed && AttackComponent->CanStartAttack();
	bCloseOpportunity = bAttackReady && bCanStartCloseSwing && Distance <= Config->AttackRange;
	bJumpOpportunity = bAttackReady && bCanStartJumpSmash
		&& Distance >= Config->JumpAttackMinRange && Distance <= Config->JumpAttackMaxRange;
	bCanRequestMovement = true;
}

/** 启动失败就让选择器尝试右侧分支；清除本次机会，下个服务周期可重新尝试。 */
bool APursuerAIController::TryStartBehaviorAttack(bool bJumpAttack)
{
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	const FName OpportunityKey = bJumpAttack ? CanJumpAttackKey : CanCloseAttackKey;
	if (!Pursuer.IsValid() || !Config.IsValid() || !IsValid(BlackboardComponent)
		|| !BlackboardComponent->GetValueAsBool(OpportunityKey)
		|| !Config->bEnableAttacks || Pursuer->IsImpactAttackSuppressed()
		|| Pursuer->IsImpactMovementBlocked())
	{
		return false;
	}
	APawn* PlayerPawn = Cast<APawn>(BlackboardComponent->GetValueAsObject(TargetActorKey));
	UPursuerAttackComponent* AttackComponent = Pursuer->GetAttackComponent();
	const bool bStarted = IsValid(AttackComponent) && IsValid(PlayerPawn)
		&& (bJumpAttack ? AttackComponent->TryStartJumpSmash(PlayerPawn)
			: AttackComponent->TryStartCloseSwing(PlayerPawn));
	BlackboardComponent->SetValueAsBool(OpportunityKey, false);
	if (bStarted)
	{
		BlackboardComponent->SetValueAsBool(CanCloseAttackKey, false);
		BlackboardComponent->SetValueAsBool(CanJumpAttackKey, false);
		bCanRequestMovement = false;
		SetFocus(PlayerPawn);
	}
	return bStarted;
}

/** 原有追击路径参数原样保留；任务退出时不清速度，让跳跃起手继续既有助跑。 */
void APursuerAIController::UpdateBehaviorChase()
{
	if (!bCanRequestMovement || !Pursuer.IsValid() || !Config.IsValid()
		|| Pursuer->IsImpactMovementBlocked())
	{
		return;
	}
	UPursuerAttackComponent* AttackComponent = Pursuer->GetAttackComponent();
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	APawn* PlayerPawn = IsValid(BlackboardComponent)
		? Cast<APawn>(BlackboardComponent->GetValueAsObject(TargetActorKey)) : nullptr;
	if (!IsValid(AttackComponent) || AttackComponent->IsBusy() || !IsValid(PlayerPawn))
	{
		return;
	}
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	const float VerticalDistance = FMath::Abs(PlayerLocation.Z - Pursuer->GetActorLocation().Z);

	// 冷却中、资产暂缺或目标在攻击距离外都继续追击，不再站在旧 AttackRange 等待。
	ClearFocus(EAIFocusPriority::Gameplay);
	if (VerticalDistance > 70.0f)
	{
		// 隔层时按位置严格追逐，避免双方胶囊重叠让 MoveToActor 提前判定到达。
		MoveToLocation(PlayerLocation, 0.0f, /*bStopOnOverlap=*/false);
		return;
	}
	MoveToActor(PlayerPawn, Config->AttackApproachRadius);
}
