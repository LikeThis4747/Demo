// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlayerController.cpp
 * 职责：ESC 暂停菜单的创建/销毁与输入模式切换。
 * 边界：不处理角色移动/Mapping Context（由角色管理）。
 */

#include "GameFlow/ZeroEscapePlayerController.h"

#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "UI/PauseMenuWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePlayerController, Log, All);

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
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	UGameplayStatics::SetGamePaused(this, true);
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
