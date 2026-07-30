// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MainMenuWidget.h
 * 职责：主菜单的逻辑基类——采集玩家选择的 Seed 与难度、写入 GameInstance、开始游戏或退出。
 * 边界：不负责按钮布局与外观（交给继承本类的 WBP 蓝图）；不驱动 PCG、不管胜负。
 * 状态 Owner：本类不长期持有玩法状态，选择结果统一写入 UZeroEscapeGameInstance。
 */

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "MainMenuWidget.generated.h"

/** 主菜单逻辑基类：蓝图子类做布局，本类提供开始/退出与参数写入。 */
UCLASS(Abstract)
class DEMO_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	/** 记录本局要用的 Seed；蓝图输入框变化时调用。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|MainMenu")
	void SetSeed(int32 InSeed) { SelectedSeed = InSeed; }

	/** 记录本局难度；蓝图难度选择控件变化时调用。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|MainMenu")
	void SetDifficulty(EZeroEscapeDifficulty InDifficulty) { SelectedDifficulty = InDifficulty; }

	/** 生成一个随机 Seed 并返回，便于蓝图刷新输入框显示。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|MainMenu")
	int32 RollRandomSeed();

	/** 把当前选择写入 GameInstance 并打开游戏关卡；关卡名由蓝图默认值指定。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|MainMenu")
	void StartGame();

	/** 退出游戏。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|MainMenu")
	void QuitGame();

	/** 要打开的游戏关卡名；在 WBP 蓝图的类默认值中指定（例如 L_Game）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|MainMenu")
	FName GameLevelName;

	/** 当前选择的 Seed；默认沿用生成请求的默认 Seed。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ZeroEscape|MainMenu")
	int32 SelectedSeed = FZeroEscapeGenerationRequest().Seed;

	/** 当前选择的难度；默认 Normal。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ZeroEscape|MainMenu")
	EZeroEscapeDifficulty SelectedDifficulty = EZeroEscapeDifficulty::Normal;
};
