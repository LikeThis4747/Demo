// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ResultMenuWidget.cpp
 * 职责：结算界面逻辑实现——胜负标题、主按钮语义（下一把/重开）、面板切换、返回主菜单。
 *       种子/难度采集与开新局委托给内嵌 LevelSetup 组件。
 * 边界：不处理布局外观；不采集种子/难度。
 */

#include "UI/ResultMenuWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "GameFlow/ZeroEscapeGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UI/LevelSetupWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeResultMenu, Log, All);

void UResultMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	PrimaryButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandlePrimaryClicked);
	SelectLevelButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleSelectLevelClicked);
	MainMenuButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleMainMenuClicked);
	LevelSetup->OnBackRequested.AddDynamic(this, &UResultMenuWidget::HandleSetupBackRequested);

	ShowMainChoice();
}

void UResultMenuWidget::ShowResult(bool bWon)
{
	bWonResult = bWon;

	ResultTitle->SetText(bWon ? WinTitle : LoseTitle);
	ResultTitle->SetColorAndOpacity(
		bWon ? FLinearColor(0.35f, 0.85f, 0.45f, 1.0f)
			 : FLinearColor(0.90f, 0.35f, 0.35f, 1.0f));
	PrimaryButtonLabel->SetText(bWon ? NextRunLabel : RetryLabel);

	ShowMainChoice();
}

void UResultMenuWidget::ShowMainChoice()
{
	ChoicePanel->SetVisibility(ESlateVisibility::Visible);
	LevelSetup->SetVisibility(ESlateVisibility::Collapsed);
}

/** 胜=随机新种子+当前难度；负=当前种子+当前难度。委托 LevelSetup 开新局。 */
void UResultMenuWidget::HandlePrimaryClicked()
{
	const UZeroEscapeGameInstance* GI = GetGameInstance<UZeroEscapeGameInstance>();
	if (GI == nullptr)
	{
		return;
	}
	const FZeroEscapeGenerationRequest& Request = GI->GetPendingRequest();

	if (bWonResult)
	{
		LevelSetup->StartLevelWith(FMath::Rand(), Request.Difficulty);
	}
	else
	{
		LevelSetup->StartLevelWith(Request.Seed, Request.Difficulty);
	}
}

void UResultMenuWidget::HandleSelectLevelClicked()
{
	LevelSetup->InitializeFromCurrentRequest();
	ChoicePanel->SetVisibility(ESlateVisibility::Collapsed);
	LevelSetup->SetVisibility(ESlateVisibility::Visible);
}

void UResultMenuWidget::HandleMainMenuClicked()
{
	if (MainMenuLevelName.IsNone())
	{
		UE_LOG(LogZeroEscapeResultMenu, Error,
			TEXT("ZE_RESULT_MENU result=Failure reason=MainMenuLevelUnset"));
		return;
	}
	UGameplayStatics::SetGamePaused(this, false);
	UGameplayStatics::OpenLevel(this, MainMenuLevelName);
}

void UResultMenuWidget::HandleSetupBackRequested()
{
	ShowMainChoice();
}
