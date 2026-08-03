// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameState.cpp
 * 职责：实现局状态机的状态转移与广播；只接受进行中→终态的首次转移。
 * 边界：不生成关卡、不放角色、不弹 UI、不决定重开。
 */

#include "GameFlow/ZeroEscapeGameState.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeRound, Log, All);

void AZeroEscapeGameState::SetRoundWon()
{
	TransitionTo(EZeroEscapeRoundState::Won);
}

void AZeroEscapeGameState::SetRoundLost()
{
	TransitionTo(EZeroEscapeRoundState::Lost);
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
