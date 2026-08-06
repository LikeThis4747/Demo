// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeCharacter.cpp
 * 职责：装配第三人称角色，并把输入与重冲击接口转发给各自唯一状态 Owner。
 * 边界：不加载具体资源，不实现磁力或重冲击物理规则，也不替代 PlayerController 的输入模式管理。
 * 状态 Owner：本类只拥有 Mapping Context 生命周期；磁力与重冲击状态属于各自组件。
 */

#include "Characters/ZeroEscapeCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/Attributes/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Magnetism/ElectromagneticGrabComponent.h"
#include "Components/Physics/HeavyImpactResponseComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/Input/ZeroEscapeInputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeInput, Log, All);

/** 创建无常驻 Tick 的第三人称角色，并给过肩相机提供可直接试玩的初始构图。 */
AZeroEscapeCharacter::AZeroEscapeCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->bOrientRotationToMovement = true;
	MovementComponent->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	MovementComponent->JumpZVelocity = 700.0f;
	MovementComponent->AirControl = 0.35f;
	MovementComponent->MaxWalkSpeed = 500.0f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 55.0f, 65.0f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	PhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandle"));
	ElectromagneticGrab = CreateDefaultSubobject<UElectromagneticGrabComponent>(TEXT("ElectromagneticGrab"));
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	PhysicsControl = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("PhysicsControl"));
	PhysicsControl->SetupAttachment(GetRootComponent());
	PhysicsControl->SetAutoActivate(false);
	HeavyImpactResponse = CreateDefaultSubobject<UHeavyImpactResponseComponent>(TEXT("HeavyImpactResponse"));

	// HeavyImpact 先在 PostPhysics 更新角色根节点，弹簧臂再读取同帧的最终位置。
	CameraBoom->AddTickPrerequisiteComponent(HeavyImpactResponse);
}

/** 机关按准备后仍会参与真实刚体接触的 Mesh 计算距离，不能使用稍后被降为 QueryOnly 的 Capsule。 */
UPrimitiveComponent* AZeroEscapeCharacter::GetHeavyImpactPredictionPrimitive_Implementation() const
{
	return GetMesh();
}

/** 把接口请求交给唯一重冲击状态 Owner；组件缺失时明确拒绝，不创建角色侧兜底。 */
EHeavyImpactPrepareResult AZeroEscapeCharacter::PrepareForHeavyImpact_Implementation(
	const FHeavyImpactPreparationRequest& Request)
{
	return IsValid(HeavyImpactResponse)
		? HeavyImpactResponse->PrepareForImpact(Request)
		: EHeavyImpactPrepareResult::Invalid;
}

/** 重新占有时以幂等方式应用输入 DataAsset 声明的上下文。 */
void AZeroEscapeCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	ApplyInputMappingContexts();
}

/** 失去控制器前撤销本角色的输入副作用，并确保磁力物体不会因换 Pawn 被遗留在持有状态。 */
void AZeroEscapeCharacter::UnPossessed()
{
	RemoveInputMappingContexts();
	EndMagneticGrab();
	StopJumping();

	Super::UnPossessed();
}

/** 完成磁力与重冲击接线；两个状态 Owner 都自行校验注入引用和独立 Tuning DataAsset。 */
void AZeroEscapeCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (IsValid(ElectromagneticGrab))
	{
		ElectromagneticGrab->Configure(PhysicsHandle, FollowCamera);
	}

	if (IsValid(HeavyImpactResponse))
	{
		HeavyImpactResponse->Configure(
			this,
			GetMesh(),
			GetCapsuleComponent(),
			GetCharacterMovement(),
			PhysicsControl,
			HeavyImpactTuningData);
		HeavyImpactResponse->OnImpactCommitted.AddUObject(
			this,
			&AZeroEscapeCharacter::HandleHeavyImpactCommitted);
	}
}

/** 校验输入 DataAsset 后集中注册动作；普通移动直接采用 Enhanced Input 的逐帧 Triggered 值。 */
void AZeroEscapeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(EnhancedInput))
	{
		UE_LOG(LogZeroEscapeInput, Error, TEXT("%s 未使用 UEnhancedInputComponent，无法绑定玩家输入。"), *GetName());
		return;
	}

	FString ConfigurationError;
	if (!IsValid(InputConfig) || !InputConfig->IsConfigured(ConfigurationError))
	{
		if (!IsValid(InputConfig))
		{
			ConfigurationError = TEXT("BP_ZeroEscapeCharacter 尚未指定 InputConfig。");
		}
		UE_LOG(LogZeroEscapeInput, Error, TEXT("%s 输入配置无效：%s"), *GetName(), *ConfigurationError);
		return;
	}

	EnhancedInput->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AZeroEscapeCharacter::Move);
	EnhancedInput->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this, &AZeroEscapeCharacter::Look);
	EnhancedInput->BindAction(InputConfig->MouseLookAction, ETriggerEvent::Triggered, this, &AZeroEscapeCharacter::Look);
	EnhancedInput->BindAction(InputConfig->JumpAction, ETriggerEvent::Started, this, &AZeroEscapeCharacter::TryJump);
	EnhancedInput->BindAction(InputConfig->JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	EnhancedInput->BindAction(InputConfig->JumpAction, ETriggerEvent::Canceled, this, &ACharacter::StopJumping);
	EnhancedInput->BindAction(InputConfig->MagneticGrabAction, ETriggerEvent::Started, this, &AZeroEscapeCharacter::BeginMagneticGrab);
	EnhancedInput->BindAction(InputConfig->MagneticGrabAction, ETriggerEvent::Completed, this, &AZeroEscapeCharacter::EndMagneticGrab);
	EnhancedInput->BindAction(InputConfig->MagneticGrabAction, ETriggerEvent::Canceled, this, &AZeroEscapeCharacter::EndMagneticGrab);
	EnhancedInput->BindAction(InputConfig->MagneticThrowAction, ETriggerEvent::Started, this, &AZeroEscapeCharacter::ThrowMagneticObject);
}

/** 重冲击从 Prepared 到倒地期间独占身体控制；未装配组件时不阻断原玩家输入。 */
bool AZeroEscapeCharacter::CanAcceptBodyInput() const
{
	return !IsValid(HeavyImpactResponse) || !HeavyImpactResponse->IsBusy();
}

/** 查找本地玩家输入子系统；服务器、AI 控制或占有尚未完成时安全返回空。 */
UEnhancedInputLocalPlayerSubsystem* AZeroEscapeCharacter::FindInputSubsystem() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	ULocalPlayer* LocalPlayer = IsValid(PlayerController) ? PlayerController->GetLocalPlayer() : nullptr;
	return IsValid(LocalPlayer)
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer)
		: nullptr;
}

/** 先验证完整配置，再重建本角色负责的上下文；忽略添加瞬间仍按住的旧键以防产生幽灵输入。 */
void AZeroEscapeCharacter::ApplyInputMappingContexts()
{
	FString ConfigurationError;
	if (!IsValid(InputConfig) || !InputConfig->IsConfigured(ConfigurationError))
	{
		if (!IsValid(InputConfig))
		{
			ConfigurationError = TEXT("BP_ZeroEscapeCharacter 尚未指定 InputConfig。");
		}
		UE_LOG(LogZeroEscapeInput, Error, TEXT("%s 无法应用输入上下文：%s"), *GetName(), *ConfigurationError);
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = FindInputSubsystem();
	if (!IsValid(InputSubsystem))
	{
		return;
	}

	FModifyContextOptions ModifyOptions;
	ModifyOptions.bIgnoreAllPressedKeysUntilRelease = true;

	for (const FZeroEscapeInputMappingContextConfig& ContextConfig : InputConfig->MappingContexts)
	{
		InputSubsystem->RemoveMappingContext(ContextConfig.MappingContext, ModifyOptions);
		InputSubsystem->AddMappingContext(ContextConfig.MappingContext, ContextConfig.Priority, ModifyOptions);
	}
}

/** 移除本角色声明的上下文；即使其他必填 Action 尚未配置，也尽力清理每个有效上下文。 */
void AZeroEscapeCharacter::RemoveInputMappingContexts()
{
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = FindInputSubsystem();
	if (!IsValid(InputSubsystem) || !IsValid(InputConfig))
	{
		return;
	}

	for (const FZeroEscapeInputMappingContextConfig& ContextConfig : InputConfig->MappingContexts)
	{
		if (IsValid(ContextConfig.MappingContext.Get()))
		{
			InputSubsystem->RemoveMappingContext(ContextConfig.MappingContext);
		}
	}
}

/** 依据控制器水平朝向计算前后与左右方向；相机俯仰不会令角色产生垂直移动。 */
void AZeroEscapeCharacter::Move(const FInputActionValue& Value)
{
	if (!CanAcceptBodyInput())
	{
		return;
	}

	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (!IsValid(Controller))
	{
		return;
	}

	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

/** 物理受击期间拒绝新的跳跃，Completed/Canceled 仍由 StopJumping 清理已有跳跃输入。 */
void AZeroEscapeCharacter::TryJump()
{
	if (CanAcceptBodyInput())
	{
		Jump();
	}
}

/** 把鼠标或手柄的二维视角值转发给控制器，不在角色中重复实现灵敏度。 */
void AZeroEscapeCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	AddControllerYawInput(LookAxisVector.X);
	AddControllerPitchInput(LookAxisVector.Y);
}

/** 请求磁力组件在当前准星附近执行一次候选选取。 */
void AZeroEscapeCharacter::BeginMagneticGrab()
{
	if (CanAcceptBodyInput() && IsValid(ElectromagneticGrab))
	{
		ElectromagneticGrab->BeginGrabInput();
	}
}

/** 请求磁力组件放下物体，并解除投掷或安全释放后的再次抓取锁。 */
void AZeroEscapeCharacter::EndMagneticGrab()
{
	if (IsValid(ElectromagneticGrab))
	{
		ElectromagneticGrab->EndGrabInput();
	}
}

/** 请求磁力组件将当前持有物体朝准星方向投掷。 */
void AZeroEscapeCharacter::ThrowMagneticObject()
{
	if (CanAcceptBodyInput() && IsValid(ElectromagneticGrab))
	{
		ElectromagneticGrab->ThrowHeldObject();
	}
}

/** 只在真实接触提交后中断磁力；组件内部保证空手调用不锁住下一次抓取。 */
void AZeroEscapeCharacter::HandleHeavyImpactCommitted(
	const FHeavyImpactPreparationRequest& /*Request*/)
{
	if (IsValid(ElectromagneticGrab))
	{
		ElectromagneticGrab->InterruptAndRelease();
	}
}
