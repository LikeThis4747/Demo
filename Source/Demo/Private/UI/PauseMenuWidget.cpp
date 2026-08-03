// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PauseMenuWidget.cpp
 * 职责：暂停菜单逻辑实现——继续（关闭暂停+切游戏输入）、选择关卡（显示子组件）、返回主菜单。
 * 边界：不创建/销毁自身；ESC 关闭走 NativeOnKeyDown。
 */

#include "UI/PauseMenuWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Kismet/GameplayStatics.h"
#include "UI/LevelSetupWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePauseMenu, Log, All);

void UPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ResumeButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleResumeClicked);
	SelectLevelButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleSelectLevelClicked);
	MainMenuButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleMainMenuClicked);
	LevelSetup->OnBackRequested.AddDynamic(this, &UPauseMenuWidget::HandleSetupBackRequested);

	ShowMainChoice();
}

void UPauseMenuWidget::ShowPauseMenu()
{
	ShowMainChoice();
}

void UPauseMenuWidget::ShowMainChoice()
{
	ChoicePanel->SetVisibility(ESlateVisibility::Visible);
	LevelSetup->SetVisibility(ESlateVisibility::Collapsed);
}

void UPauseMenuWidget::HandleResumeClicked()
{
	// 关闭暂停菜单由 PlayerController 负责（它持有 Widget 实例并管理输入模式）。
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
		UGameplayStatics::SetGamePaused(this, false);
	}
	RemoveFromParent();
}

void UPauseMenuWidget::HandleSelectLevelClicked()
{
	LevelSetup->InitializeFromCurrentRequest();
	ChoicePanel->SetVisibility(ESlateVisibility::Collapsed);
	LevelSetup->SetVisibility(ESlateVisibility::Visible);
}

void UPauseMenuWidget::HandleMainMenuClicked()
{
	if (MainMenuLevelName.IsNone())
	{
		UE_LOG(LogZeroEscapePauseMenu, Error,
			TEXT("ZE_PAUSE_MENU result=Failure reason=MainMenuLevelUnset"));
		return;
	}
	UGameplayStatics::SetGamePaused(this, false);
	UGameplayStatics::OpenLevel(this, MainMenuLevelName);
}

void UPauseMenuWidget::HandleSetupBackRequested()
{
	ShowMainChoice();
}

FReply UPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// 暂停状态下再按 ESC = 等价"继续"。
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		HandleResumeClicked();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
