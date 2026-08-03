// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ResultMenuWidget.h
 * 职责：一局结束后的结算界面——主选择层提供快速操作（胜=下一把/负=重开）、选择关卡、返回主菜单；
 *       选择关卡子层由内嵌 ULevelSetupWidget 组件提供。
 * 边界：不判定胜负（由 GameState 决定）；不采集种子/难度（委托给 LevelSetup 组件）。
 * 状态 Owner：仅拥有主选择层与子组件的切换状态。
 */

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "ResultMenuWidget.generated.h"

class UButton;
class ULevelSetupWidget;
class UTextBlock;
class UWidget;

/**
 * 结算界面：主选择层标题(胜/负)+主按钮(下一把/重开)+选择关卡+返回主菜单；
 * 内嵌 LevelSetup 组件处理种子/难度采集与开新局。
 */
UCLASS(Abstract)
class DEMO_API UResultMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	/** 由 GameMode 调用：按胜/负设置标题与主按钮文字，显示主选择层。 */
	void ShowResult(bool bWon);

protected:
	/** 返回的主菜单关卡名。 */
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
	UFUNCTION()
	void HandlePrimaryClicked();
	UFUNCTION()
	void HandleSelectLevelClicked();
	UFUNCTION()
	void HandleMainMenuClicked();
	UFUNCTION()
	void HandleSetupBackRequested();

	void ShowMainChoice();

	/** 是否为胜利结算；决定标题与主按钮语义。 */
	bool bWonResult = false;

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

	// —— 内嵌选择关卡组件 ——
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<ULevelSetupWidget> LevelSetup;
};
