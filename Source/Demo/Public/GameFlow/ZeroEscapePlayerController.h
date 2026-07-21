// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlayerController.h
 * 职责：管理《零号逃亡》本地玩家的游戏输入模式、鼠标捕获与按键状态清理。
 * 边界：不添加 Mapping Context、不绑定玩法动作，也不持有磁力或移动玩法状态。
 * 状态 Owner：PlayerController 只拥有视口输入模式；动作与上下文由玩家角色管理。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "ZeroEscapePlayerController.generated.h"

/** 为纯游戏视口提供确定的鼠标捕获策略，并清除进入游戏前遗留的按键状态。 */
UCLASS()
class DEMO_API AZeroEscapePlayerController final : public APlayerController
{
	GENERATED_BODY()

protected:
	/** 玩家进入 Playing 状态时应用 GameOnly 输入模式，确保鼠标按键既能捕获视口也能触发玩法。 */
	virtual void BeginPlayingState() override;

private:
	/** 隐藏光标、关闭点击事件并清空旧按键状态；不触碰 Enhanced Input 上下文。 */
	void ApplyGameplayInputMode();
};
