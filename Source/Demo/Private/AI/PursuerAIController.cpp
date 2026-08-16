// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAIController.cpp
 * 职责：实现追猎者的定时决策——持续追击、中距离预判跑跳与近距离斧击选择。
 * 边界：不拥有攻击阶段、冷却、动画、位移或命中；这些统一交给 UPursuerAttackComponent。
 */

#include "AI/PursuerAIController.h"

#include "Characters/PursuerCharacter.h"
#include "Components/Combat/PursuerAttackComponent.h"
#include "Data/PursuerConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"

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
	RecoveryConditionSeconds = 0.0f;

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

/** 失去占有前清理思考 Timer，避免悬挂回调访问失效对象。 */
void APursuerAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(ThinkTimerHandle);
	RecoveryConditionSeconds = 0.0f;

	Super::OnUnPossess();
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

/** 状态机核心：只要玩家有效就持续追击，并按距离选择移动或攻击。 */
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

	// 近距离优先斧击；只有组件真正启动成功才停在攻击态，资产缺失时仍继续追击。
	if (!bAttackSuppressed
		&& bCanStartCloseSwing
		&& Distance <= Config->AttackRange
		&& AttackComponent->TryStartCloseSwing(PlayerPawn))
	{
		SetFocus(PlayerPawn);
		return;
	}

	// 中距离用一次性预测跑跳封锁玩家前路；落点锁定后空中不再持续追踪。
	if (!bAttackSuppressed
		&& bCanStartJumpSmash
		&& Distance >= Config->JumpAttackMinRange
		&& Distance <= Config->JumpAttackMaxRange
		&& AttackComponent->TryStartJumpSmash(PlayerPawn))
	{
		SetFocus(PlayerPawn);
		return;
	}

	// 冷却中、资产暂缺或目标在攻击距离外都继续追击，不再站在旧 AttackRange 等待。
	ClearFocus(EAIFocusPriority::Gameplay);
	if (!bCanStartCloseSwing)
	{
		// 隔层时按位置严格追逐，避免双方胶囊重叠让 MoveToActor 提前判定到达。
		MoveToLocation(PlayerLocation, 0.0f, /*bStopOnOverlap=*/false);
		return;
	}
	MoveToActor(PlayerPawn, Config->AttackApproachRadius);
}
