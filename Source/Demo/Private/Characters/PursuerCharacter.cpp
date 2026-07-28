// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerCharacter.cpp
 * 职责：装配追猎者角色、官方 Physics Control 与局部受击状态组件，应用移动参数并播放攻击蒙太奇。
 * 边界：不做 AI 决策或物理受击状态管理；不加载除攻击蒙太奇外的资源；不替代专用状态 Owner。
 */

#include "Characters/PursuerCharacter.h"

#include "AI/PursuerAIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/Physics/PhysicsControlHitResponseComponent.h"
#include "Data/PursuerConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsControlComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogPursuer, Log, All);

/** 创建追猎者：关闭常驻 Tick，绑定专用 AI 控制器，放置或生成即被 AI 占有；朝向由移动方向驱动。 */
APursuerCharacter::APursuerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	PhysicsControl = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("PhysicsControl"));
	PhysicsControl->SetupAttachment(GetRootComponent());
	PhysicsControlHitResponse = CreateDefaultSubobject<UPhysicsControlHitResponseComponent>(
		TEXT("PhysicsControlHitResponse"));

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

	// Blueprint 组件模板应用完成后再建立碰撞职责：Capsule 管移动，Manny Physics Asset 接收物理道具命中。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	// 敌人不触发玩家第三人称弹簧臂的相机回缩：对 Camera 通道 Ignore（保留相机对世界几何防穿墙，且与 PhysicsBody 受击独立）。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	PhysicsControlHitResponse->Configure(GetMesh(), PhysicsControl, PhysicsControlHitTuning);
	PhysicsControlHitResponse->OnPhysicsHit.AddDynamic(this, &APursuerCharacter::HandleHitReact);

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

/** 按命中方向从 Config 加载对应受击蒙太奇并播放；Config 缺失或蒙太奇为空时安全返回。 */
void APursuerCharacter::HandleHitReact(EPhysicsHitDirection HitDirection)
{
	if (!IsValid(Config))
	{
		return;
	}

	TSoftObjectPtr<UAnimMontage> MontagePtr;
	switch (HitDirection)
	{
	case EPhysicsHitDirection::Left:
		MontagePtr = Config->HitReactFromLeft;
		break;
	case EPhysicsHitDirection::Right:
		MontagePtr = Config->HitReactFromRight;
		break;
	default:
		MontagePtr = Config->HitReactFromFront;
		break;
	}

	UAnimMontage* Montage = MontagePtr.LoadSynchronous();
	if (!IsValid(Montage))
	{
		return;
	}

	// 受击期间进入停顿：置位受击状态并在蒙太奇结束时清除，AI 依此暂停追击与攻击。
	if (PlayAnimMontage(Montage) > 0.0f)
	{
		bIsReacting = true;
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &APursuerCharacter::OnHitReactMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);
		}
	}
}

/** 受击蒙太奇结束（含被打断）清除受击状态，让 AI 下一次思考恢复追击。 */
void APursuerCharacter::OnHitReactMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	bIsReacting = false;
}
