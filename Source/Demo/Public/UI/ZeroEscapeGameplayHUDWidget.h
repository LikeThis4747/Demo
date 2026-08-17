// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayHUDWidget.h
 * 职责：显示玩家局内资源与磁力操作提示；读取现有组件状态，不拥有任何玩法状态。
 * 边界：仅负责右下角 UMG 表现，使用低频 Timer 刷新，不新增能量系统或常驻玩法 Tick。
 */

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "TimerManager.h"

#include "ZeroEscapeGameplayHUDWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UHorizontalBox;

/** 右下角局内 HUD；数据权威仍由 HealthComponent 与 ElectromagneticGrabComponent 持有。 */
UCLASS()
class DEMO_API UZeroEscapeGameplayHUDWidget final : public UUserWidget
{
	GENERATED_BODY()

protected:
	/** 初始化首次显示并启动低频状态刷新。 */
	virtual void NativeConstruct() override;

	/** 清理刷新 Timer，避免 Widget 销毁后继续访问组件。 */
	virtual void NativeDestruct() override;

private:
	/** 从当前拥有 Pawn 读取生命、爆炸次数和下一次爆炸充能进度。 */
	void RefreshGameplayState();

	/** 从 GameState 读取通关目标进度并更新顶部目标行计数与配色。 */
	void RefreshObjectiveState();

	/** 蓝色爆炸充能进度条；满次数时保持满格。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> EnergyBar;

	/** 红色生命进度条。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	/** 当前可用爆炸次数/最大次数，例如 1/3。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ChargesText;

	/** 顶部通关目标："收集能量团"四字，固定黄色强调。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ObjectiveOrbText;

	/** 顶部通关目标："逃往出口"中的"出口"，固定蓝色强调。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ObjectiveExitText;

	/** 顶部通关目标计数：已收集/所需，例如 0/4；未达标红色、达标蓝色。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ObjectiveCountText;

	/** 顶部通关目标整条容器；未达所需数时整条隐藏，避免开局误导。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ObjectiveRow;

	/** 低频 UI 刷新句柄；不参与玩家移动、磁力或战斗逻辑。 */
	FTimerHandle RefreshTimer;
};
