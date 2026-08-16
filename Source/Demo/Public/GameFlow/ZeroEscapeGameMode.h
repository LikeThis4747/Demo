// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameMode.h
 * 职责：锁输入并编排正式异步开局；Generator、角色、出口、Population 全部成功后才开放一局。
 * 边界：GameState 仍拥有胜负真相；本类不求解 PCG、不接管追猎者 AI 或玩法对象行为。
 * 状态 Owner：只拥有开局事务、生成对象引用与失败后的关卡切换收口。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFlow/ZeroEscapeGameState.h"
#include "PCG/ZeroEscapeGenerationTypes.h"
#include "TimerManager.h"

#include "ZeroEscapeGameMode.generated.h"

class APawn;
class APursuerCharacter;
class AZeroEscapeGameplayPopulator;
class AZeroEscapeExitVolume;
class AZeroEscapeRuntimeLevelGenerator;
class UHealthComponent;
class UResultMenuWidget;
class UWorld;

/** 正式游戏关卡的 GameMode：开局读参数生成 PCG，并完成追猎者/玩家的初始摆放。 */
UCLASS()
class DEMO_API AZeroEscapeGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** 指定与原型一致的玩家角色、Controller 与准星 HUD。 */
	AZeroEscapeGameMode();

	/** 正式奖励光团的唯一收集入口；成功即计入出口目标并尝试补充一次爆裂投掷。 */
	bool TryCollectEnergyOrb(APawn& PlayerPawn);

protected:
	/** 请求前绑定最终事件并锁输入；直接运行正式游戏关卡也走同一异步流程。 */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 按类型定位关卡中唯一的对象；缺失或多于一个都失败，不选择“第一个”。 */
	AZeroEscapeRuntimeLevelGenerator* FindLevelGenerator() const;
	AZeroEscapeGameplayPopulator* FindGameplayPopulator() const;

	/** Generator 的唯一最终回调；只接受当前 Generator 的最终报告，OperationId 负责去重。 */
	UFUNCTION()
	void HandleGenerationFinished(
		bool bSuccess,
		const FZeroEscapeGenerationReport& Report);

	/** 读取 Plan 明确给出的玩家/追猎者出生点，不再从普通格二次猜选。 */
	bool PlacePlayerAndPursuer(AZeroEscapeRuntimeLevelGenerator& Generator);

	/** 生成成功后在出口坐标放置出口体积并激活；失败记录错误并返回 false。 */
	bool PlaceExit(AZeroEscapeRuntimeLevelGenerator& Generator);

	/** 必需绑定失败也让本局回滚，避免生成一局无法判负的半成品。 */
	bool BindPlayerDeath();

	/** 出口到达回调：转发判胜给 GameState。 */
	UFUNCTION()
	void HandleExitReached();

	/** 玩家生命归零回调：转发判负给 GameState。 */
	UFUNCTION()
	void HandlePlayerDeath();

	/** 订阅 GameState 状态变化，用于分出胜负时弹结算界面。 */
	bool BindRoundStateForUI();

	/** 局状态变化回调：非进行中时创建结算界面、切 UI 输入并暂停。 */
	UFUNCTION()
	void HandleRoundStateChanged(EZeroEscapeRoundState NewState);

	void SetGameplayInputLocked(bool bLocked) const;
	void AbortSetupAndReturnToMainMenu(const TCHAR* Reason);
	void BeginSetupTransition(
		const TCHAR* Reason,
		const TSoftObjectPtr<UWorld>& TargetLevel);
	void FinalizeSetupTransition();
	void CleanupRoundActors();
	void UnbindRuntimeDelegates();

	/** 本局唯一追猎者类；由正式 GameMode 蓝图在类默认值中指定现有 BP_Pursuer。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round")
	TSubclassOf<APursuerCharacter> PursuerClass;

	/** 本局生成的唯一追猎者；仅用于开局摆放记录，不承担后续状态管理。 */
	UPROPERTY(Transient)
	TObjectPtr<APursuerCharacter> SpawnedPursuer;

	/** 出口 Actor 类；由正式 GameMode 蓝图在类默认值中指定。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round")
	TSubclassOf<AZeroEscapeExitVolume> ExitActorClass;

	/** 本局生成的出口体积；仅用于开局摆放记录。 */
	UPROPERTY(Transient)
	TObjectPtr<AZeroEscapeExitVolume> SpawnedExit;

	/** 失败时返回的主菜单软引用；由正式 GameMode 蓝图显式装配，不按关卡名猜测。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round")
	TSoftObjectPtr<UWorld> MainMenuLevel;

	/** 结算界面 Widget 类；由正式 GameMode 蓝图指定。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round")
	TSubclassOf<UResultMenuWidget> ResultMenuWidgetClass;

	/** 本局创建的结算界面实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UResultMenuWidget> ResultMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<AZeroEscapeRuntimeLevelGenerator> ActiveGenerator;

	UPROPERTY(Transient)
	TObjectPtr<AZeroEscapeGameplayPopulator> ActivePopulator;

	UPROPERTY(Transient)
	TObjectPtr<UHealthComponent> BoundHealthComponent;

	UPROPERTY(Transient)
	TObjectPtr<AZeroEscapeGameState> BoundRoundState;

	FTimerHandle SetupTransitionTimer;
	TSoftObjectPtr<UWorld> PendingTransitionLevel;
	FString PendingTransitionReason;
	int64 LastHandledGenerationOperationId = 0;
	bool bSetupTerminal = false;
	bool bRoundStarted = false;
	bool bSetupTransitionScheduled = false;
	bool bEndingPlay = false;
};
