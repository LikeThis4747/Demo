// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MainMenuWidget.cpp
 * 职责：主菜单逻辑实现——随机 Seed、写入 GameInstance、切换到游戏关卡、退出。
 * 边界：不处理布局与控件绑定；关卡名与外观由继承本类的 WBP 蓝图提供。
 */

#include "UI/MainMenuWidget.h"

#include "GameFlow/ZeroEscapeGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeMainMenu, Log, All);

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
		UE_LOG(LogZeroEscapeMainMenu, Error,
			TEXT("ZE_MENU_START result=Failure reason=WrongGameInstanceClass"));
		return;
	}

	if (GameLevelName.IsNone())
	{
		UE_LOG(LogZeroEscapeMainMenu, Error,
			TEXT("ZE_MENU_START result=Failure reason=GameLevelNameUnset"));
		return;
	}

	FZeroEscapeGenerationRequest Request;
	Request.Seed = SelectedSeed;
	Request.Difficulty = SelectedDifficulty;
	GameInstancePtr->SetPendingRequest(Request);

	UE_LOG(LogZeroEscapeMainMenu, Display,
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
