// Copyright Epic Games, Inc. All Rights Reserved.

/** @file CharacterImpactTypes.cpp 实现站立轻受击数据合同的无副作用校验。 */

#include "Physics/CharacterImpactTypes.h"

#include "Components/PrimitiveComponent.h"
#include "Data/Physics/CharacterImpactSourceProfile.h"
#include "GameFramework/Actor.h"

bool FStandingImpactReactionSpec::IsConfigured(
	const TCHAR* PropertyPrefix,
	FString& OutError) const
{
	const FString Prefix = PropertyPrefix ? PropertyPrefix : TEXT("Reaction");
	if (!FMath::IsFinite(DurationSeconds) || !FMath::IsFinite(SpeedMultiplier))
	{
		OutError = FString::Printf(TEXT("%s contains a non-finite numeric value."), *Prefix);
		return false;
	}

	switch (Result)
	{
	case EStandingImpactResult::None:
		if (!FMath::IsNearlyZero(DurationSeconds)
			|| !FMath::IsNearlyEqual(SpeedMultiplier, 1.0f)
			|| bPlayReactionAnimation)
		{
			OutError = FString::Printf(
				TEXT("%s None requires DurationSeconds=0, SpeedMultiplier=1 and no animation."),
				*Prefix);
			return false;
		}
		break;

	case EStandingImpactResult::Slow:
		if (DurationSeconds <= 0.0f
			|| SpeedMultiplier <= 0.0f
			|| SpeedMultiplier >= 1.0f
			|| bPlayReactionAnimation)
		{
			OutError = FString::Printf(
				TEXT("%s Slow requires DurationSeconds>0, 0<SpeedMultiplier<1 and no full-body animation in V1."),
				*Prefix);
			return false;
		}
		break;

	case EStandingImpactResult::Stop:
		if (DurationSeconds <= 0.0f || !FMath::IsNearlyZero(SpeedMultiplier))
		{
			OutError = FString::Printf(
				TEXT("%s Stop requires DurationSeconds>0 and SpeedMultiplier=0."),
				*Prefix);
			return false;
		}
		break;

	default:
		OutError = FString::Printf(TEXT("%s has an unsupported Result value."), *Prefix);
		return false;
	}

	return true;
}

bool FStandingImpactRequest::IsStructurallyValid(
	const AActor* Receiver,
	FString& OutError) const
{
	OutError.Reset();
	if (!ImpactId.IsValid())
	{
		OutError = TEXT("ImpactId is zero or invalid.");
		return false;
	}
	if (!IsValid(Receiver)
		|| !IsValid(SourceActor)
		|| SourceActor == Receiver
		|| !IsValid(SourceComponent)
		|| SourceComponent->GetOwner() != SourceActor
		|| !IsValid(SourceProfile))
	{
		OutError = TEXT("Receiver, source actor/component relationship, or SourceProfile is invalid.");
		return false;
	}
	if (WorldDirection.ContainsNaN()
		|| WorldDirection.IsNearlyZero()
		|| ImpactPoint.ContainsNaN()
		|| RawNormalImpulse.ContainsNaN()
		|| !FMath::IsFinite(NormalizedStrength)
		|| NormalizedStrength < 0.0f
		|| NormalizedStrength > 1.0f)
	{
		OutError = TEXT("Direction, point, impulse or NormalizedStrength is invalid.");
		return false;
	}
	return true;
}
