// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MainMenuGameMode.cpp
 * 职责：主菜单关卡 GameMode 实现——创建主菜单 Widget 并进入 UI-only 输入。
 * 边界：不生成 PCG、不摆角色；Widget 类缺失时记录错误并安全返回。
 */

#include "GameFlow/MainMenuGameMode.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MainMenuWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeMainMenu, Log, All);

/** 创建主菜单并把玩家输入切换为 UI-only、显示鼠标。 */
void AMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController) || MainMenuWidgetClass == nullptr)
	{
		UE_LOG(LogZeroEscapeMainMenu, Error,
			TEXT("ZE_MENU_SETUP result=Failure reason=InvalidControllerOrWidgetClass"));
		return;
	}

	MainMenuWidget = CreateWidget<UUserWidget>(PlayerController, MainMenuWidgetClass);
	if (!IsValid(MainMenuWidget))
	{
		UE_LOG(LogZeroEscapeMainMenu, Error,
			TEXT("ZE_MENU_SETUP result=Failure reason=WidgetCreationFailed"));
		return;
	}

	MainMenuWidget->AddToViewport();

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MainMenuWidget->TakeWidget());
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);

	UE_LOG(LogZeroEscapeMainMenu, Display, TEXT("ZE_MENU_SETUP result=Success"));
}
