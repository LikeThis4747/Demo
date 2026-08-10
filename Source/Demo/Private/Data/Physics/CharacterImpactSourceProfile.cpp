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
	return FMath::GetMappedRangeValueClamped(
		FVector2D(MinimumPhysicalImpulse, FullStrengthPhysicalImpulse),
		FVector2D(0.0f, 1.0f),
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
		|| MinimumPhysicalImpulse < 0.0f
		|| FullStrengthPhysicalImpulse <= MinimumPhysicalImpulse)
	{
		OutError = TEXT("MinimumPhysicalImpulse must be finite and non-negative; FullStrengthPhysicalImpulse must be larger.");
		return false;
	}
	return true;
}
