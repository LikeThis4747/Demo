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

public:
	/** 显示/隐藏出口能量不足红字闪烁；只操作表现，不裁决能量门槛。 */
	void SetExitLockedWarningVisible(bool bVisible);

	/** 镜头切回玩家后显示一次很短的“开始逃亡！”提示；只操作 UMG 表现。 */
	void ShowEscapeStartMessage();

	void SetFloorGuidanceTargets(
		const TArray<FVector>& TargetWorldLocations,
		int32 InFloorCount,
		float InFloorTopZCm,
		float InFloorHeightCm);

protected:
	/** 初始化首次显示并启动低频状态刷新。 */
	virtual void NativeConstruct() override;

	/** 清理刷新 Timer，避免 Widget 销毁后继续访问组件。 */
	virtual void NativeDestruct() override;

private:
	/** 从当前拥有 Pawn 读取生命、爆炸次数和下一次爆炸充能进度。 */
	void RefreshGameplayState();

	/** 从 GameState 读取通关目标进度并更新顶部目标行计数与配色；仅在数值变化时写 UI。 */
	void RefreshObjectiveState();

	/** 把顶部目标行按当前视口宽度水平居中（分辨率无关，仅布局变化时调用）。 */
	void CenterObjectiveRow();

	void CenterGuideRow();

	void RefreshFloorGuidance();

	/** 组件未被标记变量时按名字兜底解析提示 TextBlock，避免 Widget 默认可视残留。 */
	void ResolveMessageTexts();

	/** 能量光团计数变化回调；订阅 GameState 委托以事件驱动刷新。 */
	UFUNCTION()
	void HandleEnergyOrbCountChanged(int32 CollectedCount, int32 RequiredCount);

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

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> GuideRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GuideFloorText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GuideTargetText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GuideArrowText;

	/** 出口能量不足提示；由 WBP_GameplayHUD 可选装配，缺失时不显示。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ExitLockedWarningText;

	/** 画面中央偏上的“开始逃亡！”提示；由 WBP_GameplayHUD 可选装配，缺失时不显示。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EscapeStartText;

	/** 低频 UI 刷新句柄；不参与玩家移动、磁力或战斗逻辑。 */
	FTimerHandle RefreshTimer;

	/** 上次写入的能量团计数，避免每帧重复 SetText/SetColor。 */
	int32 LastOrbCollected = -1;
	int32 LastOrbRequired = -1;

	/** 出口能量不足提示当前是否应由玩家位置触发显示。 */
	bool bExitLockedWarningVisible = false;

	/** “开始逃亡！”提示的截止世界时间；0 表示不显示。 */
	double EscapeStartMessageUntilTimeSeconds = 0.0;

	/** 上次居中时所用的视口宽度，分辨率不变则不重复布局。 */
	float LastCenteredViewportX = -1.0f;

	TArray<FVector> FloorGuidanceTargetWorldLocations;
	int32 GuidanceFloorCount = 0;
	float GuidanceFloorTopZCm = 0.0f;
	float GuidanceFloorHeightCm = 0.0f;
	int32 LastGuidanceFloorIndex = INDEX_NONE;
	bool bFloorGuidanceReady = false;
};
