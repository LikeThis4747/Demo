// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ResultMenuWidget.cpp
 * 职责：结算界面逻辑实现——胜负标题与主按钮、双层面板切换、种子/难度采集、取消暂停并切关卡。
 * 边界：不处理布局与外观；关卡名、标题与按钮文字由 WBP 蓝图默认值提供。
 */

#include "UI/ResultMenuWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "GameFlow/ZeroEscapeGameInstance.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeResultMenu, Log, All);

/** 难度按钮选中/未选中背景色，与主菜单保持一致。 */
namespace ZeroEscapeResultMenu
{
	const FLinearColor SelectedColor(0.12f, 0.55f, 0.62f, 1.0f);
	const FLinearColor NormalColor(0.16f, 0.21f, 0.28f, 1.0f);
}

void UResultMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	PrimaryButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandlePrimaryClicked);
	SelectLevelButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleSelectLevelClicked);
	MainMenuButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleMainMenuClicked);
	StartButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleStartClicked);
	BackButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleBackClicked);
	RandomButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleRandomClicked);
	DiffEasyButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleDifficultyEasyClicked);
	DiffNormalButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleDifficultyNormalClicked);
	DiffHardButton->OnClicked.AddDynamic(this, &UResultMenuWidget::HandleDifficultyHardClicked);
	SeedInput->OnTextCommitted.AddDynamic(this, &UResultMenuWidget::HandleSeedCommitted);
}

/** 设置胜/负标题与主按钮文字，读取本局参数作初值，显示主选择层、隐藏子层。 */
void UResultMenuWidget::ShowResult(bool bWon)
{
	bWonResult = bWon;

	ResultTitle->SetText(bWon ? WinTitle : LoseTitle);
	ResultTitle->SetColorAndOpacity(
		bWon ? FLinearColor(0.35f, 0.85f, 0.45f, 1.0f)
			 : FLinearColor(0.90f, 0.35f, 0.35f, 1.0f));
	PrimaryButtonLabel->SetText(bWon ? NextRunLabel : RetryLabel);

	if (const UZeroEscapeGameInstance* GameInstancePtr = GetGameInstance<UZeroEscapeGameInstance>())
	{
		const FZeroEscapeGenerationRequest& Request = GameInstancePtr->GetPendingRequest();
		SelectedSeed = Request.Seed;
		SelectedDifficulty = Request.Difficulty;
	}

	ChoicePanel->SetVisibility(ESlateVisibility::Visible);
	SetupPanel->SetVisibility(ESlateVisibility::Collapsed);
}

/** 胜=随机新种子+当前难度快速继续；负=当前种子+当前难度重来。 */
void UResultMenuWidget::HandlePrimaryClicked()
{
	if (bWonResult)
	{
		StartLevel(FMath::Rand(), SelectedDifficulty);
	}
	else
	{
		StartLevel(SelectedSeed, SelectedDifficulty);
	}
}

/** 进入选择关卡子层：回填种子输入框与难度高亮。 */
void UResultMenuWidget::HandleSelectLevelClicked()
{
	SeedInput->SetText(FText::AsNumber(SelectedSeed));
	RefreshDifficultyHighlight();
	ChoicePanel->SetVisibility(ESlateVisibility::Collapsed);
	SetupPanel->SetVisibility(ESlateVisibility::Visible);
}

/** 返回主选择层。 */
void UResultMenuWidget::HandleBackClicked()
{
	SetupPanel->SetVisibility(ESlateVisibility::Collapsed);
	ChoicePanel->SetVisibility(ESlateVisibility::Visible);
}

/** 用子层当前选择的种子+难度开新局。 */
void UResultMenuWidget::HandleStartClicked()
{
	StartLevel(SelectedSeed, SelectedDifficulty);
}

/** 返回主菜单。 */
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

/** 解析整数种子；非法内容回退显示。 */
void UResultMenuWidget::HandleSeedCommitted(const FText& Text, ETextCommit::Type /*CommitMethod*/)
{
	const FString SeedString = Text.ToString();
	if (SeedString.IsNumeric())
	{
		SelectedSeed = FCString::Atoi(*SeedString);
	}
	else
	{
		SeedInput->SetText(FText::AsNumber(SelectedSeed));
	}
}

/** 随机种子并回填输入框。 */
void UResultMenuWidget::HandleRandomClicked()
{
	SelectedSeed = FMath::Rand();
	SeedInput->SetText(FText::AsNumber(SelectedSeed));
}

void UResultMenuWidget::HandleDifficultyEasyClicked()
{
	SelectedDifficulty = EZeroEscapeDifficulty::Easy;
	RefreshDifficultyHighlight();
}

void UResultMenuWidget::HandleDifficultyNormalClicked()
{
	SelectedDifficulty = EZeroEscapeDifficulty::Normal;
	RefreshDifficultyHighlight();
}

void UResultMenuWidget::HandleDifficultyHardClicked()
{
	SelectedDifficulty = EZeroEscapeDifficulty::Hard;
	RefreshDifficultyHighlight();
}

/** 选中难度按钮高亮，其余恢复普通色。 */
void UResultMenuWidget::RefreshDifficultyHighlight()
{
	DiffEasyButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Easy
			? ZeroEscapeResultMenu::SelectedColor : ZeroEscapeResultMenu::NormalColor);
	DiffNormalButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Normal
			? ZeroEscapeResultMenu::SelectedColor : ZeroEscapeResultMenu::NormalColor);
	DiffHardButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Hard
			? ZeroEscapeResultMenu::SelectedColor : ZeroEscapeResultMenu::NormalColor);
}

/** 写入本局请求、取消暂停并打开游戏关卡。 */
void UResultMenuWidget::StartLevel(int32 Seed, EZeroEscapeDifficulty Difficulty)
{
	UZeroEscapeGameInstance* GameInstancePtr = GetGameInstance<UZeroEscapeGameInstance>();
	if (GameInstancePtr == nullptr || GameLevelName.IsNone())
	{
		UE_LOG(LogZeroEscapeResultMenu, Error,
			TEXT("ZE_RESULT_START result=Failure reason=InstanceOrLevelUnset"));
		return;
	}

	FZeroEscapeGenerationRequest Request;
	Request.Seed = Seed;
	Request.Difficulty = Difficulty;
	GameInstancePtr->SetPendingRequest(Request);

	UGameplayStatics::SetGamePaused(this, false);

	UE_LOG(LogZeroEscapeResultMenu, Display,
		TEXT("ZE_RESULT_START result=Success seed=%d difficulty=%d"),
		Seed, static_cast<int32>(Difficulty));

	UGameplayStatics::OpenLevel(this, GameLevelName);
}
