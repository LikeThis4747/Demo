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

/** 本局能量光团目标的纯值状态；由 GameState 独占，拆出后可在无 World 测试中验证边界。 */
struct DEMO_API FZeroEscapeEnergyOrbObjective
{
public:
	/** 仅允许初始化一次；比例必须为有限的 0~1，最低数量按实际总数向上取整。 */
	bool Initialize(int32 InTotalCount, float RequiredFraction);

	/** 接受一个尚未计数的光团；达到实际总数后拒绝重复增加。 */
	bool TryCollect();

	bool IsInitialized() const { return bInitialized; }
	bool IsRequirementMet() const { return bInitialized && CollectedCount >= RequiredCount; }
	int32 GetTotalCount() const { return TotalCount; }
	int32 GetCollectedCount() const { return CollectedCount; }
	int32 GetRequiredCount() const { return RequiredCount; }

private:
	int32 TotalCount = 0;
	int32 CollectedCount = 0;
	int32 RequiredCount = 0;
	bool bInitialized = false;
};

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

	/** Population 成功后初始化本局实际光团总数和出口比例；只允许一次。 */
	bool InitializeEnergyOrbObjective(int32 TotalCount, float RequiredFraction);

	/** 由正式光团收集事务调用；返回是否首次计入本局。 */
	bool TryCollectEnergyOrb();

	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Energy Orb")
	int32 GetTotalEnergyOrbCount() const { return EnergyOrbObjective.GetTotalCount(); }

	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Energy Orb")
	int32 GetCollectedEnergyOrbCount() const { return EnergyOrbObjective.GetCollectedCount(); }

	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Energy Orb")
	int32 GetRequiredEnergyOrbCount() const { return EnergyOrbObjective.GetRequiredCount(); }

	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Energy Orb")
	bool IsEnergyOrbRequirementMet() const { return EnergyOrbObjective.IsRequirementMet(); }

	/** 局状态变化事件；结算 UI 与 GameMode 订阅。 */
	UPROPERTY(BlueprintAssignable, Category = "ZeroEscape|Round")
	FOnRoundStateChanged OnRoundStateChanged;

private:
	/** 只允许 InProgress→Won/Lost 的一次性转移，防止重复或胜负互覆盖。 */
	void TransitionTo(EZeroEscapeRoundState NewState);

	/** 当前局状态；开局默认进行中。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ZeroEscape|Round", meta = (AllowPrivateAccess = "true"))
	EZeroEscapeRoundState RoundState = EZeroEscapeRoundState::InProgress;

	/** 本局出口光团条件的唯一状态；不参与 PCG，也不改变爆裂次数。 */
	FZeroEscapeEnergyOrbObjective EnergyOrbObjective;
};
