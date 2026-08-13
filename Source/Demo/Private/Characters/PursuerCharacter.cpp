// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerCharacter.cpp
 * 职责：装配追猎者攻击、官方 Physics Control 与轻/重冲击组件，并应用 DataAsset 移动参数。
 * 边界：不做 AI 决策、攻击阶段或物理受击状态管理；所有运行时状态交给专用组件。
 */

#include "Characters/PursuerCharacter.h"

#include "AI/PursuerAIController.h"
#include "Animation/HeavyImpactAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/Combat/PursuerAttackComponent.h"
#include "Components/Physics/CharacterImpactResponseComponent.h"
#include "Components/Physics/HeavyImpactResponseComponent.h"
#include "Components/Physics/PhysicsControlHitResponseComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Data/PursuerConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsControlComponent.h"
#include "Physics/DemoCollisionChannels.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogPursuer, Log, All);

/** 创建追猎者：关闭常驻 Tick，绑定专用 AI 控制器，放置或生成即被 AI 占有；朝向由移动方向驱动。 */
APursuerCharacter::APursuerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	PursuerAttack = CreateDefaultSubobject<UPursuerAttackComponent>(TEXT("PursuerAttack"));
	AttackImpactBody = CreateDefaultSubobject<USphereComponent>(TEXT("AttackImpactBody"));
	AttackImpactBody->SetupAttachment(GetRootComponent());
	AttackImpactBody->SetMobility(EComponentMobility::Movable);
	AttackImpactBody->SetCanEverAffectNavigation(false);
	AttackImpactBody->SetSphereRadius(90.0f);
	AttackImpactBody->SetCollisionObjectType(ECC_WorldDynamic);
	AttackImpactBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AttackImpactBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	AttackImpactBody->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	AttackImpactBody->SetGenerateOverlapEvents(false);
	AttackImpactBody->SetNotifyRigidBodyCollision(true);
	AttackImpactBody->SetUseCCD(true);
	AttackImpactBody->SetEnableGravity(false);
	AttackImpactBody->SetHiddenInGame(true);
	AttackImpactBody->SetVisibility(false);
	PhysicsControl = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("PhysicsControl"));
	PhysicsControl->SetupAttachment(GetRootComponent());
	HeavyImpactResponse = CreateDefaultSubobject<UHeavyImpactResponseComponent>(TEXT("HeavyImpactResponse"));
	CharacterImpactResponse = CreateDefaultSubobject<UCharacterImpactResponseComponent>(TEXT("CharacterImpactResponse"));
	LightPhysicalAnimation = CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("LightPhysicalAnimation"));
	LightPhysicalAnimation->SetAutoActivate(false);
	LightPhysicalAnimation->PrimaryComponentTick.bStartWithTickEnabled = false;

	AIControllerClass = APursuerAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->bOrientRotationToMovement = true;
	MovementComponent->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
}

/** 机关按准备后仍会参与真实刚体接触的 Mesh 计算距离，不能使用稍后被降为 QueryOnly 的 Capsule。 */
UPrimitiveComponent* APursuerCharacter::GetHeavyImpactPredictionPrimitive_Implementation() const
{
	return GetMesh();
}

/** 把接口请求交给唯一重冲击状态 Owner；组件缺失时明确拒绝，不恢复旧局部受击兜底。 */
EHeavyImpactPrepareResult APursuerCharacter::PrepareForHeavyImpact_Implementation(
	const FHeavyImpactPreparationRequest& Request)
{
	return IsValid(HeavyImpactResponse)
		? HeavyImpactResponse->PrepareForImpact(Request)
		: EHeavyImpactPrepareResult::Invalid;
}

/** 应用 Light 后由追猎者适配层精确中断攻击；Stop 还取消 PathFollowing，并在空中保留 Z。 */
EStandingImpactSubmitResult APursuerCharacter::SubmitStandingImpact_Implementation(
	const FStandingImpactRequest& Request)
{
	if (!IsValid(CharacterImpactResponse))
	{
		return EStandingImpactSubmitResult::Invalid;
	}

	const EStandingImpactSubmitResult Result = CharacterImpactResponse->SubmitImpact(Request);
	if (Result != EStandingImpactSubmitResult::Applied)
	{
		return Result;
	}

	InterruptActiveAttackMontage();
	if (CharacterImpactResponse->IsMovementBlocked())
	{
		if (APursuerAIController* AIController = Cast<APursuerAIController>(GetController()))
		{
			AIController->NotifyImpactMovementBlocked();
		}
	}
	return Result;
}

/** 旧蒙太奇反应与新重冲击任一忙碌时都暂停 AI，旧布尔状态保留用于无损回退。 */
bool APursuerCharacter::IsReacting() const
{
	return bIsReacting
		|| (IsValid(HeavyImpactResponse) && HeavyImpactResponse->IsBusy());
}

bool APursuerCharacter::IsImpactMovementBlocked() const
{
	return (IsValid(HeavyImpactResponse) && HeavyImpactResponse->IsBusy())
		|| (IsValid(CharacterImpactResponse) && CharacterImpactResponse->IsMovementBlocked());
}

bool APursuerCharacter::IsImpactAttackSuppressed() const
{
	return (IsValid(HeavyImpactResponse) && HeavyImpactResponse->IsBusy())
		|| (IsValid(CharacterImpactResponse) && CharacterImpactResponse->IsAttackSuppressed());
}

/** 按 Config 应用移动速度；Config 缺失或非法时保留引擎默认并记录错误，不阻断游戏。 */
void APursuerCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Blueprint 组件模板应用完成后再建立碰撞职责：Capsule 管移动，Manny Physics Asset 接收物理道具命中。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(
		Demo::CollisionChannels::AttackProjectileBody, ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(
		Demo::CollisionChannels::AttackProjectileBody, ECR_Ignore);
	// 敌人不触发玩家第三人称弹簧臂的相机回缩：对 Camera 通道 Ignore（保留相机对世界几何防穿墙，且与 PhysicsBody 受击独立）。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	if (IsValid(HeavyImpactResponse))
	{
		HeavyImpactResponse->Configure(
			this,
			GetMesh(),
			GetCapsuleComponent(),
			GetCharacterMovement(),
			PhysicsControl,
			HeavyImpactTuningData);
	}

	if (IsValid(CharacterImpactResponse))
	{
		CharacterImpactResponse->Configure(
			this,
			GetMesh(),
			GetCharacterMovement(),
			LightPhysicalAnimation,
			EImpactReceiverCategory::Pursuer,
			CharacterImpactTuningData,
			HeavyImpactResponse);
	}

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
	PursuerAttack->Configure(this, Config);
}

/** 受击系统只请求取消；精确 Montage 与 Timer 清理由攻击组件统一完成。 */
void APursuerCharacter::InterruptActiveAttackMontage()
{
	if (IsValid(PursuerAttack))
	{
		PursuerAttack->CancelAttack();
	}
}

void APursuerCharacter::SetChargeAnimationActive(const bool bActive)
{
	if (UHeavyImpactAnimInstance* AnimInstance =
		Cast<UHeavyImpactAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AnimInstance->SetChargeGuardActive(bActive);
	}
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
