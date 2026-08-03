// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameInstance.cpp
 * 职责：原子维护跨关卡生成请求与有限自动重试次数。
 * 边界：不打开关卡、不调用 Generator；重试是否允许由 GameMode 与纯策略共同决定。
 */

#include "GameFlow/ZeroEscapeGameInstance.h"

#include "GameFlow/ZeroEscapeGameSetupGate.h"

void UZeroEscapeGameInstance::SetPendingRequest(
	const FZeroEscapeGenerationRequest& InRequest)
{
	PendingRequest = InRequest;
	AutomaticGenerationRetryCount = 0;
}

void UZeroEscapeGameInstance::SetPendingSeed(const int32 InSeed)
{
	PendingRequest.Seed = InSeed;
	AutomaticGenerationRetryCount = 0;
}

void UZeroEscapeGameInstance::SetPendingDifficulty(
	const EZeroEscapeDifficulty InDifficulty)
{
	PendingRequest.Difficulty = InDifficulty;
	AutomaticGenerationRetryCount = 0;
}

bool UZeroEscapeGameInstance::TryAdvancePendingRequestForAutomaticRetry(
	int32& OutRetryNumber,
	int32& OutPreviousSeed,
	int32& OutNextSeed)
{
	OutRetryNumber = AutomaticGenerationRetryCount;
	OutPreviousSeed = PendingRequest.Seed;
	OutNextSeed = PendingRequest.Seed;
	if (AutomaticGenerationRetryCount >= MaxAutomaticGenerationRetryCount)
	{
		return false;
	}

	const int32 NextSeed =
		ZeroEscape::GameFlow::FGameSetupGate::DeriveAutomaticRetrySeed(
			PendingRequest.Seed);
	++AutomaticGenerationRetryCount;
	OutRetryNumber = AutomaticGenerationRetryCount;
	OutPreviousSeed = PendingRequest.Seed;
	OutNextSeed = NextSeed;
	PendingRequest.Seed = NextSeed;
	return true;
}
