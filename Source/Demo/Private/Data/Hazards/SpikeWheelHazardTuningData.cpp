// Copyright Epic Games, Inc. All Rights Reserved.

/** @file SpikeWheelHazardTuningData.cpp Validates spike-wheel tuning and provides editable starter routes. */

#include "Data/Hazards/SpikeWheelHazardTuningData.h"

#include "Data/Physics/CharacterImpactSourceProfile.h"

#include <initializer_list>

USpikeWheelHazardTuningData::USpikeWheelHazardTuningData()
{
	const auto AddPattern = [this](const FName Id, const int32 Span, const bool bClosed, std::initializer_list<FVector2D> Points)
	{
		FSpikeWheelRoutePattern& Pattern = RoutePatterns.AddDefaulted_GetRef();
		Pattern.PatternId = Id;
		Pattern.FootprintSpanTiles = Span;
		Pattern.bClosedLoop = bClosed;
		Pattern.ControlPointsInTileUnits.Append(Points.begin(), static_cast<int32>(Points.size()));
	};

	AddPattern(TEXT("OneCell_OffsetLoop"), 1, true,
		{ {-0.30f, -0.18f}, {-0.14f, 0.30f}, {0.08f, -0.28f}, {0.30f, 0.18f}, {0.14f, 0.31f}, {-0.28f, 0.06f} });
	AddPattern(TEXT("TwoCell_OffsetW"), 2, false,
		{ {-0.30f, -0.25f}, {-0.05f, 0.28f}, {0.25f, -0.28f}, {0.55f, 0.30f}, {0.85f, -0.25f}, {1.15f, 0.28f}, {1.30f, -0.10f} });
	AddPattern(TEXT("TwoCell_LongDiagonal"), 2, true,
		{ {-0.30f, -0.30f}, {0.24f, 0.28f}, {0.64f, -0.12f}, {1.28f, 0.30f}, {1.15f, -0.30f}, {0.34f, -0.28f} });
	AddPattern(TEXT("ThreeCell_Serpentine"), 3, false,
		{ {-0.30f, -0.26f}, {0.05f, 0.29f}, {0.38f, -0.28f}, {0.72f, 0.30f}, {1.08f, -0.27f}, {1.44f, 0.28f}, {1.82f, -0.29f}, {2.30f, 0.18f} });
}

bool USpikeWheelHazardTuningData::IsConfigured(FString& OutError) const
{
	OutError.Reset();
	const auto Reject = [&OutError](const FString& Message) { OutError = Message; return false; };

	if (!FMath::IsFinite(TileSizeCm) || !FMath::IsFinite(MoveSpeedCmPerSecond)
		|| !FMath::IsFinite(WheelRadiusCm) || !FMath::IsFinite(WheelCenterHeightCm)
		|| !FMath::IsFinite(HurtRadiusCm) || !FMath::IsFinite(Damage)
		|| !FMath::IsFinite(RehitLockSeconds) || !FMath::IsFinite(StandingImpactStrength))
	{
		return Reject(TEXT("Spike-wheel tuning contains a non-finite scalar."));
	}
	if (TileSizeCm < 100.0f || TileSizeCm > 2000.0f
		|| MoveSpeedCmPerSecond < 1.0f || MoveSpeedCmPerSecond > 2000.0f)
	{
		return Reject(TEXT("TileSizeCm or MoveSpeedCmPerSecond is outside its supported range."));
	}
	if (WheelRadiusCm < 5.0f || WheelRadiusCm >= TileSizeCm * 0.5f
		|| HurtRadiusCm < 5.0f || HurtRadiusCm >= TileSizeCm * 0.5f
		|| WheelCenterHeightCm < 0.0f || WheelCenterHeightCm > 500.0f)
	{
		return Reject(TEXT("Spike-wheel geometry is outside its supported range."));
	}
	if (Damage < 0.0f || RehitLockSeconds < 0.05f || RehitLockSeconds > 5.0f
		|| StandingImpactStrength < 0.0f || StandingImpactStrength > 1.0f)
	{
		return Reject(TEXT("Spike-wheel contact tuning is outside its supported range."));
	}

	if (!IsValid(StandingImpactSourceProfile))
	{
		return Reject(TEXT("StandingImpactSourceProfile is required."));
	}
	FString ProfileError;
	if (!StandingImpactSourceProfile->IsConfigured(ProfileError))
	{
		return Reject(FString::Printf(TEXT("StandingImpactSourceProfile is invalid: %s"), *ProfileError));
	}
	if (StandingImpactSourceProfile->PlayerReaction.Result != EStandingImpactResult::Stop
		|| StandingImpactSourceProfile->PursuerReaction.Result != EStandingImpactResult::None)
	{
		return Reject(TEXT("Spike wheel requires Player=Stop and Pursuer=None."));
	}
	if (RehitLockSeconds < StandingImpactSourceProfile->PlayerReaction.DurationSeconds)
	{
		return Reject(TEXT("RehitLockSeconds cannot be shorter than the Player Stop duration."));
	}

	if (RoutePatterns.IsEmpty())
	{
		return Reject(TEXT("At least one spike-wheel route pattern is required."));
	}

	TSet<FName> PatternIds;
	const float Clearance = HurtRadiusCm / TileSizeCm;
	for (int32 PatternIndex = 0; PatternIndex < RoutePatterns.Num(); ++PatternIndex)
	{
		const FSpikeWheelRoutePattern& Pattern = RoutePatterns[PatternIndex];
		if (Pattern.PatternId.IsNone() || PatternIds.Contains(Pattern.PatternId))
		{
			return Reject(FString::Printf(TEXT("RoutePatterns[%d] has a missing or duplicate PatternId."), PatternIndex));
		}
		PatternIds.Add(Pattern.PatternId);
		if (Pattern.FootprintSpanTiles < 1 || Pattern.FootprintSpanTiles > 3
			|| Pattern.ControlPointsInTileUnits.Num() < (Pattern.bClosedLoop ? 3 : 2))
		{
			return Reject(FString::Printf(TEXT("Route '%s' has an invalid span or point count."), *Pattern.PatternId.ToString()));
		}

		const float MinX = -0.5f + Clearance;
		const float MaxX = Pattern.FootprintSpanTiles - 0.5f - Clearance;
		const float MaxAbsY = 0.5f - Clearance;
		float RouteLengthInTiles = 0.0f;
		for (int32 PointIndex = 0; PointIndex < Pattern.ControlPointsInTileUnits.Num(); ++PointIndex)
		{
			const FVector2D& Point = Pattern.ControlPointsInTileUnits[PointIndex];
			if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y)
				|| Point.X < MinX || Point.X > MaxX || FMath::Abs(Point.Y) > MaxAbsY)
			{
				return Reject(FString::Printf(TEXT("Route '%s' leaves its declared footprint."), *Pattern.PatternId.ToString()));
			}
			if (PointIndex > 0)
			{
				const float Segment = FVector2D::Distance(Pattern.ControlPointsInTileUnits[PointIndex - 1], Point);
				if (Segment <= KINDA_SMALL_NUMBER)
				{
					return Reject(FString::Printf(TEXT("Route '%s' has adjacent duplicate points."), *Pattern.PatternId.ToString()));
				}
				RouteLengthInTiles += Segment;
			}
		}
		if (Pattern.bClosedLoop)
		{
			const float ClosingSegment = FVector2D::Distance(Pattern.ControlPointsInTileUnits.Last(), Pattern.ControlPointsInTileUnits[0]);
			if (ClosingSegment <= KINDA_SMALL_NUMBER)
			{
				return Reject(FString::Printf(TEXT("Closed route '%s' repeats its first point at the end."), *Pattern.PatternId.ToString()));
			}
			RouteLengthInTiles += ClosingSegment;
		}
		if (RouteLengthInTiles * TileSizeCm <= WheelRadiusCm * 2.0f)
		{
			return Reject(FString::Printf(TEXT("Route '%s' is too short to create visible motion."), *Pattern.PatternId.ToString()));
		}
	}

	return true;
}
