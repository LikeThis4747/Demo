// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PauseMenuWidget.h
 * 职责：ESC 暂停菜单——主选择层[继续/选择关卡/返回主菜单]+内嵌 LevelSetup 组件。
 * 边界：不创建/销毁自身（由 PlayerController 管理）；不绑定 ESC 输入（由 PC 的 EnhancedInput 处理），
 *       但在暂停状态下通过 NativeOnKeyDown 捕获 ESC 等价"继续"。
 * 状态 Owner：仅拥有自身面板切换状态。
 */

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "PauseMenuWidget.generated.h"

class UButton;
class ULevelSetupWidget;
class UTextBlock;
class UWidget;

/**
 * 暂停菜单：主选择层提供继续/选择关卡/返回主菜单；内嵌 LevelSetup 处理种子+难度。
 * ESC 再按 = 等价"继续"（NativeOnKeyDown 捕获）。
 */
UCLASS(Abstract)
class DEMO_API UPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 由 PlayerController 调用：显示主选择层并初始化。 */
	void ShowPauseMenu();

protected:
	/** 返回的主菜单关卡名；WBP 类默认值指定（如 L_MainMenu）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Pause")
	FName MainMenuLevelName;

private:
	UFUNCTION()
	void HandleResumeClicked();
	UFUNCTION()
	void HandleSelectLevelClicked();
	UFUNCTION()
	void HandleMainMenuClicked();
	UFUNCTION()
	void HandleSetupBackRequested();

	void ShowMainChoice();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UWidget> ChoicePanel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> PauseTitle;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> ResumeButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> SelectLevelButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> MainMenuButton;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<ULevelSetupWidget> LevelSetup;
};
