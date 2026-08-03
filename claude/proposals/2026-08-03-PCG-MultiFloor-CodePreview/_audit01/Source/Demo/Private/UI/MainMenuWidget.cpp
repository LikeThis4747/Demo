// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MainMenuWidget.cpp
 * 职责：主菜单逻辑实现——按钮事件绑定、随机 Seed、写入 GameInstance、切换关卡、退出、
 *       设置面板显隐与难度按钮高亮。
 * 边界：不处理布局与控件外观；关卡名与外观由继承本类的 WBP 蓝图提供。
 */

#include "UI/MainMenuWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "GameFlow/ZeroEscapeGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeMainMenuWidget, Log, All);

/** 难度按钮选中/未选中的背景色（贴合深蓝底 + 青色强调的初版配色）。 */
namespace ZeroEscapeMainMenu
{
	const FLinearColor SelectedColor(0.12f, 0.55f, 0.62f, 1.0f);
	const FLinearColor NormalColor(0.16f, 0.21f, 0.28f, 1.0f);
}

/** 绑定全部按钮事件，初始隐藏设置面板并刷新难度高亮。 */
void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	StartButton->OnClicked.AddDynamic(this, &UMainMenuWidget::StartGame);
	QuitButton->OnClicked.AddDynamic(this, &UMainMenuWidget::QuitGame);
	RandomButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleRandomClicked);
	SettingsButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleSettingsClicked);
	BackButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleBackClicked);
	DiffEasyButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleDifficultyEasyClicked);
	DiffNormalButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleDifficultyNormalClicked);
	DiffHardButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleDifficultyHardClicked);
	SeedInput->OnTextCommitted.AddDynamic(this, &UMainMenuWidget::HandleSeedCommitted);

	// 初始把当前 Seed 显示进输入框，设置面板默认收起。
	SeedInput->SetText(FText::AsNumber(SelectedSeed));
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	RefreshDifficultyHighlight();
}

/** 记录难度并刷新高亮。 */
void UMainMenuWidget::SetDifficulty(EZeroEscapeDifficulty InDifficulty)
{
	SelectedDifficulty = InDifficulty;
	RefreshDifficultyHighlight();
}

/** 生成一个非负随机 Seed 并记录，供蓝图刷新输入框显示。 */
int32 UMainMenuWidget::RollRandomSeed()
{
	SelectedSeed = FMath::Rand();
	return SelectedSeed;
}

/** 把当前选择写入 GameInstance，然后打开配置的游戏关卡。 */
void UMainMenuWidget::StartGame()
{
	UZeroEscapeGameInstance* GameInstancePtr = GetGameInstance<UZeroEscapeGameInstance>();
	if (GameInstancePtr == nullptr)
	{
		UE_LOG(LogZeroEscapeMainMenuWidget, Error,
			TEXT("ZE_MENU_START result=Failure reason=WrongGameInstanceClass"));
		return;
	}

	if (GameLevelName.IsNone())
	{
		UE_LOG(LogZeroEscapeMainMenuWidget, Error,
			TEXT("ZE_MENU_START result=Failure reason=GameLevelNameUnset"));
		return;
	}

	FZeroEscapeGenerationRequest Request;
	Request.Seed = SelectedSeed;
	Request.Difficulty = SelectedDifficulty;
	GameInstancePtr->SetPendingRequest(Request);

	UE_LOG(LogZeroEscapeMainMenuWidget, Display,
		TEXT("ZE_MENU_START result=Success seed=%d level=\"%s\""),
		SelectedSeed,
		*GameLevelName.ToString());

	UGameplayStatics::OpenLevel(this, GameLevelName);
}

/** 退出到桌面。 */
void UMainMenuWidget::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
}

/** 输入框提交：解析整数；非数字内容回退显示当前 Seed。 */
void UMainMenuWidget::HandleSeedCommitted(const FText& Text, ETextCommit::Type CommitMethod)
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

/** 随机 Seed 并回填输入框显示。 */
void UMainMenuWidget::HandleRandomClicked()
{
	SeedInput->SetText(FText::AsNumber(RollRandomSeed()));
}

/** 显示设置面板。 */
void UMainMenuWidget::HandleSettingsClicked()
{
	SettingsPanel->SetVisibility(ESlateVisibility::Visible);
}

/** 隐藏设置面板。 */
void UMainMenuWidget::HandleBackClicked()
{
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
}

/** 难度三键入口。 */
void UMainMenuWidget::HandleDifficultyEasyClicked()
{
	SetDifficulty(EZeroEscapeDifficulty::Easy);
}

void UMainMenuWidget::HandleDifficultyNormalClicked()
{
	SetDifficulty(EZeroEscapeDifficulty::Normal);
}

void UMainMenuWidget::HandleDifficultyHardClicked()
{
	SetDifficulty(EZeroEscapeDifficulty::Hard);
}

/** 选中难度按钮高亮，其余恢复普通色。 */
void UMainMenuWidget::RefreshDifficultyHighlight()
{
	DiffEasyButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Easy
			? ZeroEscapeMainMenu::SelectedColor
			: ZeroEscapeMainMenu::NormalColor);
	DiffNormalButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Normal
			? ZeroEscapeMainMenu::SelectedColor
			: ZeroEscapeMainMenu::NormalColor);
	DiffHardButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Hard
			? ZeroEscapeMainMenu::SelectedColor
			: ZeroEscapeMainMenu::NormalColor);
}
