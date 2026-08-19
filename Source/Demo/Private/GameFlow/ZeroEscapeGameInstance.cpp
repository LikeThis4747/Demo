// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameInstance.cpp
 * 职责：原子维护跨关卡生成请求；只有显式 Setter 可以改变公开 Seed 或难度。
 * 边界：不打开关卡、不调用 Generator；运行时生成失败不得改写请求。
 */

#include "GameFlow/ZeroEscapeGameInstance.h"

#include "Kismet/GameplayStatics.h"

void UZeroEscapeGameInstance::SetPendingRequest(
	const FZeroEscapeGenerationRequest& InRequest)
{
	PendingRequest = InRequest;
}

void UZeroEscapeGameInstance::SetPendingSeed(const int32 InSeed)
{
	PendingRequest.Seed = InSeed;
}

void UZeroEscapeGameInstance::SetPendingDifficulty(
	const EZeroEscapeDifficulty InDifficulty)
{
	PendingRequest.Difficulty = InDifficulty;
}

float UZeroEscapeGameInstance::GetSfxVolumeFor(const UObject* WorldContextObject)
{
	if (const UZeroEscapeGameInstance* GameInstancePtr =
			Cast<UZeroEscapeGameInstance>(UGameplayStatics::GetGameInstance(WorldContextObject)))
	{
		return GameInstancePtr->GetSfxVolume();
	}
	return 1.0f;
}
