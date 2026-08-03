// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MainMenuGameMode.h
 * 职责：主菜单关卡的 GameMode——进入时创建主菜单 Widget 并切换为 UI-only 输入。
 * 边界：不生成 PCG、不摆角色、不管胜负；只负责把主菜单 UI 呈现出来。
 * 状态 Owner：本类临时持有当前主菜单 Widget 实例引用。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "MainMenuGameMode.generated.h"

class UMainMenuWidget;
class UUserWidget;

/** 主菜单关卡 GameMode：显示主菜单并进入 UI-only 输入模式。 */
UCLASS()
class DEMO_API AMainMenuGameMode final : public AGameModeBase
{
	GENERATED_BODY()

protected:
	/** 创建主菜单 Widget、加入视口并设置 UI-only 输入与鼠标显示。 */
	virtual void BeginPlay() override;

private:
	/** 主菜单 Widget 蓝图类；在主菜单 GameMode 蓝图的类默认值中指定 WBP_MainMenu。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|MainMenu")
	TSubclassOf<UMainMenuWidget> MainMenuWidgetClass;

	/** 当前主菜单 Widget 实例；仅本关卡生命周期内持有。 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MainMenuWidget;
};
