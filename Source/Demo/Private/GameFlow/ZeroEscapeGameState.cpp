// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameState.cpp
 * 职责：实现局状态机的状态转移与广播；只接受进行中→终态的首次转移。
 * 边界：不生成关卡、不放角色、不弹 UI、不决定重开。
 */

#include "GameFlow/ZeroEscapeGameState.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeRound, Log, All);

bool FZeroEscapeEnergyOrbObjective::Initialize(
	const int32 InTotalCount,
	const float RequiredFraction)
{
	if (bInitialized
		|| InTotalCount < 0
		|| !FMath::IsFinite(RequiredFraction)
		|| RequiredFraction < 0.0f
		|| RequiredFraction > 1.0f)
	{
		return false;
	}

	TotalCount = InTotalCount;
	CollectedCount = 0;
	RequiredCount = InTotalCount == 0
		? 0
		: FMath::CeilToInt(static_cast<float>(InTotalCount) * RequiredFraction);
	RequiredCount = FMath::Clamp(RequiredCount, 0, TotalCount);
	bInitialized = true;
	return true;
}

bool FZeroEscapeEnergyOrbObjective::TryCollect()
{
	if (!bInitialized || CollectedCount >= TotalCount)
	{
		return false;
	}
	++CollectedCount;
	return true;
}

void AZeroEscapeGameState::SetRoundWon()
{
	TransitionTo(EZeroEscapeRoundState::Won);
}

void AZeroEscapeGameState::SetRoundLost()
{
	TransitionTo(EZeroEscapeRoundState::Lost);
}

bool AZeroEscapeGameState::InitializeEnergyOrbObjective(
	const int32 TotalCount,
	const float RequiredFraction)
{
	if (RoundState != EZeroEscapeRoundState::InProgress
		|| !EnergyOrbObjective.Initialize(TotalCount, RequiredFraction))
	{
		return false;
	}

	UE_LOG(LogZeroEscapeRound, Display,
		TEXT("ZE_ENERGY_OBJECTIVE total=%d required=%d fraction=%.3f"),
		EnergyOrbObjective.GetTotalCount(),
		EnergyOrbObjective.GetRequiredCount(),
		RequiredFraction);

	OnEnergyOrbCountChanged.Broadcast(
		EnergyOrbObjective.GetCollectedCount(),
		EnergyOrbObjective.GetRequiredCount());
	return true;
}

bool AZeroEscapeGameState::TryCollectEnergyOrb()
{
	if (RoundState != EZeroEscapeRoundState::InProgress
		|| !EnergyOrbObjective.TryCollect())
	{
		return false;
	}

	UE_LOG(LogZeroEscapeRound, Display,
		TEXT("ZE_ENERGY_COLLECT collected=%d required=%d total=%d"),
		EnergyOrbObjective.GetCollectedCount(),
		EnergyOrbObjective.GetRequiredCount(),
		EnergyOrbObjective.GetTotalCount());

	OnEnergyOrbCountChanged.Broadcast(
		EnergyOrbObjective.GetCollectedCount(),
		EnergyOrbObjective.GetRequiredCount());
	return true;
}

/** 只处理进行中→终态的首次转移；已分胜负后忽略后续请求。 */
void AZeroEscapeGameState::TransitionTo(EZeroEscapeRoundState NewState)
{
	if (RoundState != EZeroEscapeRoundState::InProgress
		|| NewState == EZeroEscapeRoundState::InProgress)
	{
		return;
	}

	RoundState = NewState;

	UE_LOG(LogZeroEscapeRound, Display,
		TEXT("ZE_ROUND_RESULT result=%s"),
		NewState == EZeroEscapeRoundState::Won ? TEXT("Win") : TEXT("Lost"));

	OnRoundStateChanged.Broadcast(NewState);
}
