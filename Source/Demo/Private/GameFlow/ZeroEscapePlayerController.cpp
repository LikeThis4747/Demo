// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlayerController.cpp
 * 职责：ESC 暂停菜单的创建/销毁与输入模式切换。
 * 边界：不处理角色移动/Mapping Context（由角色管理）。
 */

#include "GameFlow/ZeroEscapePlayerController.h"

#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "UI/PauseMenuWidget.h"
#include "UI/ZeroEscapeGameplayHUDWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePlayerController, Log, All);

/** 创建一次局内 HUD；所有资源与玩法状态仍由 Widget 读取现有组件。 */
void AZeroEscapePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController() || GameplayHUDWidgetClass == nullptr)
	{
		return;
	}

	GameplayHUDWidget = CreateWidget<UUserWidget>(this, GameplayHUDWidgetClass);
	if (IsValid(GameplayHUDWidget))
	{
		GameplayHUDWidget->AddToViewport(0);
	}
}

void AZeroEscapePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (PauseAction != nullptr)
		{
			EnhancedInput->BindAction(PauseAction, ETriggerEvent::Triggered,
				this, &AZeroEscapePlayerController::HandlePausePressed);
		}
	}
}

void AZeroEscapePlayerController::HandlePausePressed()
{
	if (!bPauseMenuEnabled)
	{
		return;
	}

	// 已有暂停菜单则忽略（防重复弹）。
	if (PauseMenuWidget != nullptr)
	{
		return;
	}

	if (PauseMenuWidgetClass == nullptr)
	{
		UE_LOG(LogZeroEscapePlayerController, Warning,
			TEXT("ZE_PAUSE result=Failure reason=PauseMenuWidgetClassUnset"));
		return;
	}

	PauseMenuWidget = CreateWidget<UPauseMenuWidget>(this, PauseMenuWidgetClass);
	if (!IsValid(PauseMenuWidget))
	{
		return;
	}

	PauseMenuWidget->ShowPauseMenu();
	PauseMenuWidget->AddToViewport();

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(PauseMenuWidget->TakeWidget());
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	UGameplayStatics::SetGamePaused(this, true);
}

void AZeroEscapePlayerController::SetPauseMenuEnabled(const bool bEnabled)
{
	bPauseMenuEnabled = bEnabled;
}

void AZeroEscapePlayerController::ClosePauseMenu()
{
	if (PauseMenuWidget != nullptr)
	{
		PauseMenuWidget->RemoveFromParent();
		PauseMenuWidget = nullptr;
	}

	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
	UGameplayStatics::SetGamePaused(this, false);
}

void AZeroEscapePlayerController::SetExitLockedWarningVisible(const bool bVisible)
{
	if (UZeroEscapeGameplayHUDWidget* ZeroEscapeHUDWidget =
		Cast<UZeroEscapeGameplayHUDWidget>(GameplayHUDWidget))
	{
		ZeroEscapeHUDWidget->SetExitLockedWarningVisible(bVisible);
	}
}

void AZeroEscapePlayerController::ShowEscapeStartMessage()
{
	if (UZeroEscapeGameplayHUDWidget* ZeroEscapeHUDWidget =
		Cast<UZeroEscapeGameplayHUDWidget>(GameplayHUDWidget))
	{
		ZeroEscapeHUDWidget->ShowEscapeStartMessage();
	}
}

void AZeroEscapePlayerController::SetFloorGuidanceTargets(
	const TArray<FVector>& TargetWorldLocations,
	const int32 FloorCount,
	const float FloorTopZCm,
	const float FloorHeightCm)
{
	if (UZeroEscapeGameplayHUDWidget* ZeroEscapeHUDWidget =
		Cast<UZeroEscapeGameplayHUDWidget>(GameplayHUDWidget))
	{
		ZeroEscapeHUDWidget->SetFloorGuidanceTargets(
			TargetWorldLocations,
			FloorCount,
			FloorTopZCm,
			FloorHeightCm);
	}
}
