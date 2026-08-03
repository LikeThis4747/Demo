// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file LevelSetupWidget.h
 * 职责：可复用的"选择关卡"组件——采集种子+难度并开新局，提供返回委托。
 * 边界：不决定何时显示/隐藏（由宿主控制）；不弹 UI；只采集参数、写 GameInstance、切关卡。
 * 复用者：ResultMenuWidget（结算）、PauseMenuWidget（暂停）。
 * 状态 Owner：当前采集的种子与难度初值（由宿主 InitializeFromCurrentRequest 设置）。
 */

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "LevelSetupWidget.generated.h"

class UButton;
class UEditableTextBox;

/** 返回请求；宿主订阅后决定切回自己的主选择层。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSetupBackRequested);

/**
 * 选择关卡组件：种子输入+随机+难度三键+开始按钮+返回按钮。
 * 宿主通过 InitializeFromCurrentRequest 设置初值，通过 StartLevelWith 直接开新局，
 * 或显示本组件让玩家自行调整后点"开始"。
 */
UCLASS(Abstract)
class DEMO_API ULevelSetupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	/** 用本局请求初始化种子/难度初值（宿主打开子层时调）。 */
	void InitializeFromCurrentRequest();

	/** 直接开新局（宿主的"下一把/重开"快捷按钮调这个）。 */
	void StartLevelWith(int32 Seed, EZeroEscapeDifficulty Difficulty);

	/** 返回请求事件；宿主订阅后切回自己的主选择层。 */
	UPROPERTY(BlueprintAssignable, Category = "ZeroEscape|LevelSetup")
	FOnSetupBackRequested OnBackRequested;

protected:
	/** 要开始的游戏关卡名；WBP 类默认值指定（如 L_Game）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|LevelSetup")
	FName GameLevelName;

private:
	UFUNCTION()
	void HandleSeedCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION()
	void HandleRandomClicked();
	UFUNCTION()
	void HandleStartClicked();
	UFUNCTION()
	void HandleBackClicked();
	UFUNCTION()
	void HandleDifficultyEasyClicked();
	UFUNCTION()
	void HandleDifficultyNormalClicked();
	UFUNCTION()
	void HandleDifficultyHardClicked();

	void RefreshDifficultyHighlight();
	void StartLevel(int32 Seed, EZeroEscapeDifficulty Difficulty);

	int32 SelectedSeed = 0;
	EZeroEscapeDifficulty SelectedDifficulty = EZeroEscapeDifficulty::Normal;

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
