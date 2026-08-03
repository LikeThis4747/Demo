// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file LevelSetupWidget.cpp
 * 职责：选择关卡组件实现——种子采集、随机、难度高亮、开新局、返回广播。
 * 边界：不处理布局外观；不决定显示/隐藏；关卡名由 WBP 蓝图默认值提供。
 */

#include "UI/LevelSetupWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "GameFlow/ZeroEscapeGameInstance.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeLevelSetup, Log, All);

namespace ZeroEscapeLevelSetup
{
	const FLinearColor SelectedColor(0.12f, 0.55f, 0.62f, 1.0f);
	const FLinearColor NormalColor(0.16f, 0.21f, 0.28f, 1.0f);
}

ULevelSetupWidget::ULevelSetupWidget()
{
	// 默认隐藏；宿主显示时设 Visible。
	SetVisibility(ESlateVisibility::Collapsed);
}

void ULevelSetupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RandomButton->OnClicked.AddDynamic(this, &ULevelSetupWidget::HandleRandomClicked);
	StartButton->OnClicked.AddDynamic(this, &ULevelSetupWidget::HandleStartClicked);
	BackButton->OnClicked.AddDynamic(this, &ULevelSetupWidget::HandleBackClicked);
	DiffEasyButton->OnClicked.AddDynamic(this, &ULevelSetupWidget::HandleDifficultyEasyClicked);
	DiffNormalButton->OnClicked.AddDynamic(this, &ULevelSetupWidget::HandleDifficultyNormalClicked);
	DiffHardButton->OnClicked.AddDynamic(this, &ULevelSetupWidget::HandleDifficultyHardClicked);
	SeedInput->OnTextCommitted.AddDynamic(this, &ULevelSetupWidget::HandleSeedCommitted);
}

void ULevelSetupWidget::InitializeFromCurrentRequest()
{
	if (const UZeroEscapeGameInstance* GI = GetGameInstance<UZeroEscapeGameInstance>())
	{
		const FZeroEscapeGenerationRequest& Request = GI->GetPendingRequest();
		SelectedSeed = Request.Seed;
		SelectedDifficulty = Request.Difficulty;
	}
	SeedInput->SetText(FText::AsNumber(SelectedSeed));
	RefreshDifficultyHighlight();
}

void ULevelSetupWidget::StartLevelWith(int32 Seed, EZeroEscapeDifficulty Difficulty)
{
	StartLevel(Seed, Difficulty);
}

void ULevelSetupWidget::HandleSeedCommitted(const FText& Text, ETextCommit::Type /*CommitMethod*/)
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

void ULevelSetupWidget::HandleRandomClicked()
{
	SelectedSeed = FMath::Rand();
	SeedInput->SetText(FText::AsNumber(SelectedSeed));
}

void ULevelSetupWidget::HandleStartClicked()
{
	StartLevel(SelectedSeed, SelectedDifficulty);
}

void ULevelSetupWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

void ULevelSetupWidget::HandleDifficultyEasyClicked()
{
	SelectedDifficulty = EZeroEscapeDifficulty::Easy;
	RefreshDifficultyHighlight();
}

void ULevelSetupWidget::HandleDifficultyNormalClicked()
{
	SelectedDifficulty = EZeroEscapeDifficulty::Normal;
	RefreshDifficultyHighlight();
}

void ULevelSetupWidget::HandleDifficultyHardClicked()
{
	SelectedDifficulty = EZeroEscapeDifficulty::Hard;
	RefreshDifficultyHighlight();
}

void ULevelSetupWidget::RefreshDifficultyHighlight()
{
	DiffEasyButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Easy
			? ZeroEscapeLevelSetup::SelectedColor : ZeroEscapeLevelSetup::NormalColor);
	DiffNormalButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Normal
			? ZeroEscapeLevelSetup::SelectedColor : ZeroEscapeLevelSetup::NormalColor);
	DiffHardButton->SetBackgroundColor(
		SelectedDifficulty == EZeroEscapeDifficulty::Hard
			? ZeroEscapeLevelSetup::SelectedColor : ZeroEscapeLevelSetup::NormalColor);
}

void ULevelSetupWidget::StartLevel(int32 Seed, EZeroEscapeDifficulty Difficulty)
{
	UZeroEscapeGameInstance* GI = GetGameInstance<UZeroEscapeGameInstance>();
	if (GI == nullptr || GameLevelName.IsNone())
	{
		UE_LOG(LogZeroEscapeLevelSetup, Error,
			TEXT("ZE_LEVEL_SETUP_START result=Failure reason=InstanceOrLevelUnset"));
		return;
	}

	FZeroEscapeGenerationRequest Request;
	Request.Seed = Seed;
	Request.Difficulty = Difficulty;
	GI->SetPendingRequest(Request);

	UGameplayStatics::SetGamePaused(this, false);

	UE_LOG(LogZeroEscapeLevelSetup, Display,
		TEXT("ZE_LEVEL_SETUP_START result=Success seed=%d difficulty=%d"),
		Seed, static_cast<int32>(Difficulty));

	UGameplayStatics::OpenLevel(this, GameLevelName);
}
