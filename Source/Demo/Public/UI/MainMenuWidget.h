// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MainMenuWidget.h
 * 职责：主菜单的逻辑基类——采集玩家选择的 Seed 与难度、写入 GameInstance、开始游戏或退出；
 *       同时通过 BindWidget 绑定蓝图中的同名控件，处理按钮点击与设置面板显隐。
 * 边界：不负责按钮布局与外观（交给继承本类的 WBP 蓝图）；不驱动 PCG、不管胜负。
 * 状态 Owner：本类不长期持有玩法状态，选择结果统一写入 UZeroEscapeGameInstance。
 */

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "MainMenuWidget.generated.h"

class UBorder;
class UButton;
class UEditableTextBox;
class USlider;

/**
 * 主菜单逻辑基类：蓝图子类做布局，本类提供开始/退出与参数写入。
 * 蓝图控件命名必须与下方 BindWidget 成员一致：
 * SeedInput / RandomButton / SettingsButton / StartButton / QuitButton /
 * SettingsPanel / DiffEasyButton / DiffNormalButton / DiffHardButton / BackButton /
 * SensitivitySlider / MusicSlider / SfxSlider。
 */
UCLASS(Abstract)
class DEMO_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 绑定按钮事件并刷新难度高亮。 */
	virtual void NativeConstruct() override;

protected:
	/** 记录本局要用的 Seed；供蓝图或输入框提交时调用。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|MainMenu")
	void SetSeed(int32 InSeed) { SelectedSeed = InSeed; }

	/** 记录本局难度并刷新按钮高亮。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|MainMenu")
	void SetDifficulty(EZeroEscapeDifficulty InDifficulty);

	/** 生成一个非负随机 Seed 并返回，便于刷新输入框显示。 */
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

private:
	/** 输入框提交（回车/失焦）时解析整数写入 Seed。 */
	UFUNCTION()
	void HandleSeedCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/** 随机按钮：生成随机 Seed 并回填输入框。 */
	UFUNCTION()
	void HandleRandomClicked();

	/** 设置按钮：显示设置面板。 */
	UFUNCTION()
	void HandleSettingsClicked();

	/** 返回按钮：隐藏设置面板。 */
	UFUNCTION()
	void HandleBackClicked();

	/** 三个难度按钮的点击入口。 */
	UFUNCTION()
	void HandleDifficultyEasyClicked();
	UFUNCTION()
	void HandleDifficultyNormalClicked();
	UFUNCTION()
	void HandleDifficultyHardClicked();

	/** 按当前难度刷新三个难度按钮的选中高亮。 */
	void RefreshDifficultyHighlight();

	/** Seed 输入框。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UEditableTextBox> SeedInput;

	/** 随机 Seed 按钮。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> RandomButton;

	/** 打开设置面板按钮。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> SettingsButton;

	/** 开始游戏按钮。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> StartButton;

	/** 退出游戏按钮。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> QuitButton;

	/** 设置面板容器（默认 Collapsed）。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UBorder> SettingsPanel;

	/** 难度三键。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> DiffEasyButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> DiffNormalButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> DiffHardButton;

	/** 设置面板返回按钮。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> BackButton;

	/** 鼠标灵敏度滑条（0.1~3.0）。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<USlider> SensitivitySlider;

	/** 音乐音量滑条（0~1）。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<USlider> MusicSlider;

	/** 音效音量滑条（0~1）。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<USlider> SfxSlider;

	/** 灵敏度滑条拖动：写入 GameInstance。 */
	UFUNCTION()
	void HandleSensitivityChanged(float Value);

	/** 音乐滑条拖动：写入 GameInstance。 */
	UFUNCTION()
	void HandleMusicChanged(float Value);

	/** 音效滑条拖动：写入 GameInstance。 */
	UFUNCTION()
	void HandleSfxChanged(float Value);
};
