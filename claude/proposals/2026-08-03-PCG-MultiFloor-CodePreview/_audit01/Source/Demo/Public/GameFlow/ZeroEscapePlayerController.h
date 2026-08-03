// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlayerController.h
 * 职责：声明《零号逃亡》本地玩家的自定义 Controller 扩展点。
 * 边界：当前不接管开局焦点、输入模式、Mapping Context 或普通移动输入。
 * 状态 Owner：普通移动与 Mapping Context 由玩家角色管理；UI 输入模式只在实际 UI 流程中切换。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "ZeroEscapePlayerController.generated.h"

/** 为跨 Pawn 的玩家会话功能保留稳定类型；当前不添加运行时输入行为。 */
UCLASS()
class DEMO_API AZeroEscapePlayerController final : public APlayerController
{
	GENERATED_BODY()
};
