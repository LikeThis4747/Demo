// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeHUD.h
 * 职责：声明用于磁力选取和瞄准的轻量级、分辨率无关中心准星。
 * 状态边界：HUD 只持有表现配置，不保存任何权威玩法状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "ZeroEscapeHUD.generated.h"

/** 绘制四段分离圆弧与中心点，形成类似散弹枪准星的样式。 */
UCLASS()
class DEMO_API AZeroEscapeHUD final : public AHUD
{
	GENERATED_BODY()

public:
	/** 使用适合原型关卡、保持清晰可见的紧凑默认值初始化准星。 */
	AZeroEscapeHUD();

	/** 每个 HUD 绘制帧在当前画布中心渲染准星。 */
	virtual void DrawHUD() override;

private:
	/** 使用数量受限的 HUD 线段近似绘制一段圆弧。 */
	void DrawArc(const FVector2D& Center, float StartDegrees, float EndDegrees);

	/** 使用水平 HUD 线段栅格化一个实心圆形中心点。 */
	void DrawCenterDot(const FVector2D& Center);

	/** 外侧圆弧与中心点共用的显示颜色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle")
	FLinearColor ReticleColor;

	/** 屏幕中心到四段圆弧的半径，单位为像素。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "4.0"))
	float ArcRadius = 15.0f;

	/** 每段分离圆弧的角宽度，单位为度。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "5.0", ClampMax = "80.0", Units = "deg"))
	float ArcDegrees = 48.0f;

	/** 每段圆弧使用的直线数量；限制上限以控制 HUD 绘制开销。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "2", ClampMax = "32"))
	int32 SegmentsPerArc = 8;

	/** 每条准星线段的粗细，单位为像素。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "0.5"))
	float LineThickness = 1.8f;

	/** 屏幕正中心实心圆点的半径，单位为像素。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "1.0"))
	float CenterDotRadius = 2.2f;
};
