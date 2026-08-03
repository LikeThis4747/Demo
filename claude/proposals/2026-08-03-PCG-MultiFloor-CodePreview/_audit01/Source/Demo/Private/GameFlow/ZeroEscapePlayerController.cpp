// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlayerController.cpp
 * 职责：为 AZeroEscapePlayerController 保留独立翻译单元，便于后续增加玩家会话级功能。
 * 边界：当前不实现开局焦点、输入模式、按键清理、玩法动作或 Mapping Context 管理。
 * 状态 Owner：普通移动与 Enhanced Input 映射由 AZeroEscapeCharacter 管理。
 */

#include "GameFlow/ZeroEscapePlayerController.h"
