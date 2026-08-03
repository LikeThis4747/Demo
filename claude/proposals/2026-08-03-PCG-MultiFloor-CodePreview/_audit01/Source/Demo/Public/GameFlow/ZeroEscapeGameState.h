// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameState.h
 * 职责：持有当前一局的胜负状态（进行中/胜/负），作为唯一真相源并广播状态变化。
 * 边界：只裁决并广播状态；不生成关卡、不放角色、不弹 UI、不决定重开。
 * 状态 Owner：本局 RoundState 的唯一持有者。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"

#include "ZeroEscapeGameState.generated.h"

/** 一局的运行状态。 */
UENUM(BlueprintType)
enum class EZeroEscapeRoundState : uint8
{
	InProgress = 0,
	Won = 1,
	Lost = 2
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnRoundStateChanged, EZeroEscapeRoundState, NewState);

/** 正式一局的状态机：承接胜/负裁决，广播给 UI 与 GameMode。 */
UCLASS()
class DEMO_API AZeroEscapeGameState final : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** 读取当前局状态。 */
	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Round")
	EZeroEscapeRoundState GetRoundState() const { return RoundState; }

	/** 判胜：仅在进行中时生效，置为 Won 并广播。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Round")
	void SetRoundWon();

	/** 判负：仅在进行中时生效，置为 Lost 并广播。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Round")
	void SetRoundLost();

	/** 局状态变化事件；结算 UI 与 GameMode 订阅。 */
	UPROPERTY(BlueprintAssignable, Category = "ZeroEscape|Round")
	FOnRoundStateChanged OnRoundStateChanged;

private:
	/** 只允许 InProgress→Won/Lost 的一次性转移，防止重复或胜负互覆盖。 */
	void TransitionTo(EZeroEscapeRoundState NewState);

	/** 当前局状态；开局默认进行中。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ZeroEscape|Round", meta = (AllowPrivateAccess = "true"))
	EZeroEscapeRoundState RoundState = EZeroEscapeRoundState::InProgress;
};
