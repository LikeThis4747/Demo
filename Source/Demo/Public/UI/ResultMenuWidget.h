// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ResultMenuWidget.h
 * 职责：一局结束后的结算界面逻辑基类。主选择层提供快速操作（胜=下一把/负=重开）、
 *       选择关卡、返回主菜单；选择关卡子层采集种子+难度后开新局。
 * 边界：不判定胜负（由 GameState 决定）、不做布局外观（交给 WBP 蓝图）；只采集参数并切关卡。
 * 状态 Owner：不长期持有玩法状态；开局参数写入 UZeroEscapeGameInstance。
 */

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ResultMenuWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UWidget;

/**
 * 结算界面逻辑基类：蓝图子类做布局，本类提供胜负标题、快速操作与选择关卡。
 * 蓝图控件命名必须与 BindWidget 成员一致：
 *   主选择层：ChoicePanel / ResultTitle / PrimaryButton / PrimaryButtonLabel /
 *             SelectLevelButton / MainMenuButton
 *   选择关卡子层：SetupPanel / SeedInput / RandomButton /
 *             DiffEasyButton / DiffNormalButton / DiffHardButton / StartButton / BackButton
 */
UCLASS(Abstract)
class DEMO_API UResultMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 绑定按钮事件。 */
	virtual void NativeConstruct() override;

	/** 由 GameMode 调用：按胜/负设置标题与主按钮文字，读取本局种子/难度作为初值，显示主选择层。 */
	void ShowResult(bool bWon);

protected:
	/** 要开始/重开的游戏关卡名；WBP 类默认值指定（如 L_Game）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Result")
	FName GameLevelName;

	/** 返回的主菜单关卡名；WBP 类默认值指定（如 L_MainMenu）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Result")
	FName MainMenuLevelName;

	/** 胜利标题与主按钮文字。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Result")
	FText WinTitle = FText::FromString(TEXT("逃脱成功"));
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Result")
	FText NextRunLabel = FText::FromString(TEXT("下一把"));

	/** 失败标题与主按钮文字。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Result")
	FText LoseTitle = FText::FromString(TEXT("失败"));
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Result")
	FText RetryLabel = FText::FromString(TEXT("重开"));

private:
	/** 主按钮：胜=随机新种子+当前难度；负=当前种子+当前难度。 */
	UFUNCTION()
	void HandlePrimaryClicked();

	/** 选择关卡：显示子层，回填种子输入框与难度高亮。 */
	UFUNCTION()
	void HandleSelectLevelClicked();

	/** 子层返回：回到主选择层。 */
	UFUNCTION()
	void HandleBackClicked();

	/** 子层开始：用当前选择的种子+难度开新局。 */
	UFUNCTION()
	void HandleStartClicked();

	/** 返回主菜单。 */
	UFUNCTION()
	void HandleMainMenuClicked();

	/** 种子输入提交：解析整数，非法内容回退显示。 */
	UFUNCTION()
	void HandleSeedCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/** 随机种子并回填输入框。 */
	UFUNCTION()
	void HandleRandomClicked();

	/** 三个难度按钮入口。 */
	UFUNCTION()
	void HandleDifficultyEasyClicked();
	UFUNCTION()
	void HandleDifficultyNormalClicked();
	UFUNCTION()
	void HandleDifficultyHardClicked();

	/** 按当前难度刷新三键高亮。 */
	void RefreshDifficultyHighlight();

	/** 写入参数、取消暂停并打开游戏关卡（下一把/重开/开始共用）。 */
	void StartLevel(int32 Seed, EZeroEscapeDifficulty Difficulty);

	/** 是否为胜利结算；决定标题与主按钮语义。 */
	bool bWonResult = false;

	/** 当前采集的种子与难度；初值取自本局请求，子层可修改。 */
	int32 SelectedSeed = 0;
	EZeroEscapeDifficulty SelectedDifficulty = EZeroEscapeDifficulty::Normal;

	// —— 主选择层 ——
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UWidget> ChoicePanel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> ResultTitle;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> PrimaryButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> PrimaryButtonLabel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> SelectLevelButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> MainMenuButton;

	// —— 选择关卡子层 ——
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UWidget> SetupPanel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UEditableTextBox> SeedInput;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> RandomButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> DiffEasyButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> DiffNormalButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> DiffHardButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> StartButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> BackButton;
};
