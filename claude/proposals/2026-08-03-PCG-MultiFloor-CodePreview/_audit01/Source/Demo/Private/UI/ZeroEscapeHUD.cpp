// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeHUD.cpp
 * 职责：实现不依赖额外资源的 Canvas 准星，使首个可玩原型无需 UMG 资产。
 */

#include "UI/ZeroEscapeHUD.h"

#include "Engine/Canvas.h"

/** 设置中性高亮颜色，后续可根据磁力候选状态切换表现。 */
AZeroEscapeHUD::AZeroEscapeHUD()
	: ReticleColor(0.88f, 0.96f, 1.0f, 0.95f)
{
}

/** 围绕精确的中心瞄准点绘制四组对角圆弧。 */
void AZeroEscapeHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!IsValid(Canvas))
	{
		return;
	}

	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const float HalfArcDegrees = ArcDegrees * 0.5f;
	constexpr float ArcCenters[] = {45.0f, 135.0f, 225.0f, 315.0f};
	for (const float ArcCenterDegrees : ArcCenters)
	{
		DrawArc(Center, ArcCenterDegrees - HalfArcDegrees, ArcCenterDegrees + HalfArcDegrees);
	}

	DrawCenterDot(Center);
}

/** 将角度采样转换为短 Canvas 线段，避免引入纹理和额外 UI 依赖。 */
void AZeroEscapeHUD::DrawArc(
	const FVector2D& Center,
	const float StartDegrees,
	const float EndDegrees)
{
	const int32 SafeSegmentCount = FMath::Max(2, SegmentsPerArc);
	const float AngleStep = (EndDegrees - StartDegrees) / static_cast<float>(SafeSegmentCount);
	FVector2D PreviousPoint(
		Center.X + FMath::Cos(FMath::DegreesToRadians(StartDegrees)) * ArcRadius,
		Center.Y + FMath::Sin(FMath::DegreesToRadians(StartDegrees)) * ArcRadius);

	for (int32 SegmentIndex = 1; SegmentIndex <= SafeSegmentCount; ++SegmentIndex)
	{
		const float CurrentDegrees = StartDegrees + AngleStep * static_cast<float>(SegmentIndex);
		const FVector2D CurrentPoint(
			Center.X + FMath::Cos(FMath::DegreesToRadians(CurrentDegrees)) * ArcRadius,
			Center.Y + FMath::Sin(FMath::DegreesToRadians(CurrentDegrees)) * ArcRadius);
		DrawLine(PreviousPoint.X, PreviousPoint.Y, CurrentPoint.X, CurrentPoint.Y, ReticleColor, LineThickness);
		PreviousPoint = CurrentPoint;
	}
}

/** 按扫描线使用圆方程绘制真正的圆点，避免中心点呈现为方块。 */
void AZeroEscapeHUD::DrawCenterDot(const FVector2D& Center)
{
	const int32 VerticalRadius = FMath::CeilToInt(CenterDotRadius);
	for (int32 YOffset = -VerticalRadius; YOffset <= VerticalRadius; ++YOffset)
	{
		const float RemainingSquared = FMath::Max(0.0f, FMath::Square(CenterDotRadius) - FMath::Square(static_cast<float>(YOffset)));
		const float HorizontalRadius = FMath::Sqrt(RemainingSquared);
		DrawLine(
			Center.X - HorizontalRadius,
			Center.Y + static_cast<float>(YOffset),
			Center.X + HorizontalRadius,
			Center.Y + static_cast<float>(YOffset),
			ReticleColor,
			1.0f);
	}
}
