// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeHUD.h
 * Declares a lightweight, resolution-independent center reticle for magnetic selection and aiming.
 * The HUD owns presentation configuration only and never stores authoritative gameplay state.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "ZeroEscapeHUD.generated.h"

/** Draws four separated circular arcs and a center dot in the style of a shotgun reticle. */
UCLASS()
class DEMO_API AZeroEscapeHUD final : public AHUD
{
	GENERATED_BODY()

public:
	/** Initializes the reticle with compact defaults that remain legible over the prototype level. */
	AZeroEscapeHUD();

	/** Draws the reticle at the current canvas center every HUD frame. */
	virtual void DrawHUD() override;

private:
	/** Approximates one circular arc with a bounded number of HUD line segments. */
	void DrawArc(const FVector2D& Center, float StartDegrees, float EndDegrees);

	/** Rasterizes a small filled circular center point using horizontal HUD lines. */
	void DrawCenterDot(const FVector2D& Center);

	/** Display color used by both outer arcs and the central point. */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle")
	FLinearColor ReticleColor;

	/** Radius from screen center to the four arc segments, in pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "4.0"))
	float ArcRadius = 15.0f;

	/** Angular width of each of the four separated circular segments, in degrees. */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "5.0", ClampMax = "80.0", Units = "deg"))
	float ArcDegrees = 48.0f;

	/** Number of straight lines used per arc; bounded to keep HUD drawing inexpensive. */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "2", ClampMax = "32"))
	int32 SegmentsPerArc = 8;

	/** Thickness of every reticle stroke, in pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "0.5"))
	float LineThickness = 1.8f;

	/** Radius of the filled point at exact screen center, in pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "Reticle", meta = (ClampMin = "1.0"))
	float CenterDotRadius = 2.2f;
};
