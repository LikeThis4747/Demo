// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlayerController.cpp
 * 职责：为原型建立唯一且可复现的游戏视口输入模式，替代第三人称内容包蓝图中的隐式输入管理。
 * 边界：不读取输入 DataAsset，不注册动作，也不修改角色移动或磁力组件状态。
 * 状态 Owner：视口捕获与光标状态由本类管理，Enhanced Input 映射仍由 AZeroEscapeCharacter 管理。
 */

#include "GameFlow/ZeroEscapePlayerController.h"

/** 在控制器正式进入游戏状态后统一建立输入模式，避免 PIE 启动点击遗留为持续按键。 */
void AZeroEscapePlayerController::BeginPlayingState()
{
	Super::BeginPlayingState();
	ApplyGameplayInputMode();
}

/** 应用纯游戏输入并刷新按键缓存；false 使首次用于捕获视口的鼠标按下仍能传递给游戏。 */
void AZeroEscapePlayerController::ApplyGameplayInputMode()
{
	FInputModeGameOnly InputMode;
	InputMode.SetConsumeCaptureMouseDown(false);
	SetInputMode(InputMode);

	SetShowMouseCursor(false);
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	FlushPressedKeys();
}
