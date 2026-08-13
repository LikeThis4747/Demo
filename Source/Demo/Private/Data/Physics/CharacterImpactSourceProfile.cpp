// Copyright Epic Games, Inc. All Rights Reserved.

/** @file CharacterImpactSourceProfile.cpp 校验来源映射并归一化该来源自己的真实物理冲量。 */

#include "Data/Physics/CharacterImpactSourceProfile.h"

const FStandingImpactReactionSpec& UCharacterImpactSourceProfile::GetReaction(
	const EImpactReceiverCategory Category) const
{
	return Category == EImpactReceiverCategory::Pursuer ? PursuerReaction : PlayerReaction;
}

float UCharacterImpactSourceProfile::NormalizePhysicalImpulse(const float ImpulseMagnitude) const
{
	if (!FMath::IsFinite(ImpulseMagnitude)
		|| ImpulseMagnitude < MinimumPhysicalImpulse)
	{
		return 0.0f;
	}

	return FMath::GetMappedRangeValueClamped(
		FVector2D(MinimumPhysicalImpulse, FullStrengthPhysicalImpulse),
		FVector2D(MinimumResponseStrength, 1.0f),
		ImpulseMagnitude);
}

bool UCharacterImpactSourceProfile::IsConfigured(FString& OutError) const
{
	OutError.Reset();
	if (!PlayerReaction.IsConfigured(TEXT("PlayerReaction"), OutError)
		|| !PursuerReaction.IsConfigured(TEXT("PursuerReaction"), OutError))
	{
		return false;
	}
	if (!FMath::IsFinite(MinimumPhysicalImpulse)
		|| !FMath::IsFinite(FullStrengthPhysicalImpulse)
		|| !FMath::IsFinite(MinimumResponseStrength)
		|| MinimumPhysicalImpulse < 0.0f
		|| FullStrengthPhysicalImpulse <= MinimumPhysicalImpulse
		|| MinimumResponseStrength < 0.0f
		|| MinimumResponseStrength > 1.0f)
	{
		OutError = TEXT("Physical impulse range and MinimumResponseStrength must be finite; the range must increase and strength must be within 0..1.");
		return false;
	}
	return true;
}
