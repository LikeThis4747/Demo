// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameMode.h
 * 职责：正式一局的开局编排——读取本局请求、驱动 PCG 生成、把追猎者放在起点、玩家放在起点两格外、放置出口。
 * 边界：不做局状态机（由 GameState 承接胜负真相）；不撒放陷阱/奖励、不接管追猎者 AI 或磁力手感；
 *       只在开局摆好人和出口，把胜负触发源（出口到达、玩家死亡）转发给 GameState，并在分出胜负时弹结算界面。
 * 状态 Owner：本类只在开局临时持有对 Generator、本局追猎者与出口的引用，不长期拥有局状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFlow/ZeroEscapeGameState.h"

#include "ZeroEscapeGameMode.generated.h"

class APawn;
class APursuerCharacter;
class AZeroEscapeExitVolume;
class AZeroEscapeRuntimeLevelGenerator;
class UResultMenuWidget;

/** 正式游戏关卡的 GameMode：开局读参数生成 PCG，并完成追猎者/玩家的初始摆放。 */
UCLASS()
class DEMO_API AZeroEscapeGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** 指定与原型一致的玩家角色、Controller 与准星 HUD。 */
	AZeroEscapeGameMode();

protected:
	/** 读取 GameInstance 请求、驱动本局生成并摆放角色。 */
	virtual void BeginPlay() override;

private:
	/** 按类型定位关卡中唯一的空间 Generator；缺失或多于一个都记为错误。 */
	AZeroEscapeRuntimeLevelGenerator* FindLevelGenerator() const;

	/** 生成成功后：追猎者放起点、玩家放两格外；任一步失败记录错误并返回 false。 */
	bool PlacePlayerAndPursuer(AZeroEscapeRuntimeLevelGenerator& Generator);

	/** 从走廊候选中选出与起点直线距离达到下限、且额外距离最小的玩家出生点。 */
	bool FindPlayerSpawnTransform(
		AZeroEscapeRuntimeLevelGenerator& Generator,
		const FTransform& StartTransform,
		FTransform& OutPlayerTransform) const;

	/** 生成成功后在出口坐标放置出口体积并激活；失败记录错误并返回 false。 */
	bool PlaceExit(AZeroEscapeRuntimeLevelGenerator& Generator);

	/** 绑定玩家生命归零事件，用于判负转发。 */
	void BindPlayerDeath();

	/** 出口到达回调：转发判胜给 GameState。 */
	UFUNCTION()
	void HandleExitReached();

	/** 玩家生命归零回调：转发判负给 GameState。 */
	UFUNCTION()
	void HandlePlayerDeath();

	/** 订阅 GameState 状态变化，用于分出胜负时弹结算界面。 */
	void BindRoundStateForUI();

	/** 局状态变化回调：非进行中时创建结算界面、切 UI 输入并暂停。 */
	UFUNCTION()
	void HandleRoundStateChanged(EZeroEscapeRoundState NewState);

	/** 本局唯一追猎者类；由正式 GameMode 蓝图在类默认值中指定现有 BP_Pursuer。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round")
	TSubclassOf<APursuerCharacter> PursuerClass;

	/** 玩家与起点（追猎者所在）的最小二维距离；1200 cm 等于当前两个 600 cm 逻辑格。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round", meta = (ClampMin = "600.0", Units = "cm"))
	double PlayerStartSeparationCm = 1200.0;

	/** 本局生成的唯一追猎者；仅用于开局摆放记录，不承担后续状态管理。 */
	UPROPERTY(Transient)
	TObjectPtr<APursuerCharacter> SpawnedPursuer;

	/** 出口 Actor 类；由正式 GameMode 蓝图在类默认值中指定。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round")
	TSubclassOf<AZeroEscapeExitVolume> ExitActorClass;

	/** 本局生成的出口体积；仅用于开局摆放记录。 */
	UPROPERTY(Transient)
	TObjectPtr<AZeroEscapeExitVolume> SpawnedExit;

	/** 结算界面 Widget 类；由正式 GameMode 蓝图指定。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round")
	TSubclassOf<UResultMenuWidget> ResultMenuWidgetClass;

	/** 本局创建的结算界面实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UResultMenuWidget> ResultMenuWidget;
};
