// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerCharacter.cpp
 * 职责：装配追猎者角色，应用移动参数并播放攻击蒙太奇。
 * 边界：不做 AI 决策与物理受击；不加载除攻击蒙太奇外的资源；不替代 AI 控制器的状态管理。
 */

#include "Characters/PursuerCharacter.h"

#include "AI/PursuerAIController.h"
#include "Animation/AnimMontage.h"
#include "Data/PursuerConfig.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogPursuer, Log, All);

/** 创建追猎者：关闭常驻 Tick，绑定专用 AI 控制器，放置或生成即被 AI 占有；朝向由移动方向驱动。 */
APursuerCharacter::APursuerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	AIControllerClass = APursuerAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->bOrientRotationToMovement = true;
	MovementComponent->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
}

/** 按 Config 应用移动速度；Config 缺失或非法时保留引擎默认并记录错误，不阻断游戏。 */
void APursuerCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	FString ConfigurationError;
	if (!IsValid(Config) || !Config->IsConfigured(ConfigurationError))
	{
		if (!IsValid(Config))
		{
			ConfigurationError = TEXT("BP_Pursuer 尚未指定 Config。");
		}
		UE_LOG(LogPursuer, Error, TEXT("%s 追猎者配置无效：%s"), *GetName(), *ConfigurationError);
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = Config->MaxWalkSpeed;
}

/** 同步加载并播放攻击蒙太奇；缺失 Config 或蒙太奇时记录错误并安全返回。 */
void APursuerCharacter::PlayAttackMontage()
{
	if (!IsValid(Config))
	{
		UE_LOG(LogPursuer, Error, TEXT("%s 无 Config，无法播放攻击。"), *GetName());
		return;
	}

	UAnimMontage* Montage = Config->AttackMontage.LoadSynchronous();
	if (!IsValid(Montage))
	{
		// [临时-A] 项目暂无攻击动画：未配蒙太奇时仅记录攻击时机以验证追击闭环；有动画后此处应改回 Error 并直接返回。
		UE_LOG(LogPursuer, Warning, TEXT("%s 触发攻击（暂无攻击蒙太奇，仅记录时机）。"), *GetName());
		return;
	}

	PlayAnimMontage(Montage);
}
