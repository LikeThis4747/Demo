// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlayerController.h
 * 职责：本地玩家的会话级 Controller——ESC 暂停输入与暂停菜单的创建/销毁。
 * 边界：不接管角色移动/Mapping Context（由角色管理）；只在游戏进行中处理暂停。
 * 状态 Owner：暂停菜单 Widget 实例的创建与销毁。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "ZeroEscapePlayerController.generated.h"

class UPauseMenuWidget;
class UInputAction;

/**
 * 玩家会话级 Controller：ESC 弹暂停菜单，再次 ESC 关闭。
 * 暂停时切 UI-only 输入并 SetGamePaused(true)，继续时恢复。
 */
UCLASS()
class DEMO_API AZeroEscapePlayerController final : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;

	/** Enables or blocks opening the pause menu while the round is being set up. */
	void SetPauseMenuEnabled(bool bEnabled);

	/** 关闭暂停菜单：移除界面+切回游戏输入+取消暂停。供 PauseMenuWidget 的"继续"调用。 */
	void ClosePauseMenu();

private:
	/** ESC 触发：若无暂停菜单则弹出。 */
	UFUNCTION()
	void HandlePausePressed();

	/** ESC 的 InputAction；由 BP 在类默认值中指定。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Pause")
	TObjectPtr<UInputAction> PauseAction;

	/** 暂停菜单 Widget 类；由 BP 在类默认值中指定。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Pause")
	TSubclassOf<UPauseMenuWidget> PauseMenuWidgetClass;

	/** 当前暂停菜单实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UPauseMenuWidget> PauseMenuWidget;

	bool bPauseMenuEnabled = true;
};
