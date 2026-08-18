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

class ACameraActor;
class APawn;
class APursuerCharacter;
class AZeroEscapeGameplayPopulator;
class AZeroEscapeExitVolume;
class AZeroEscapeRuntimeLevelGenerator;
class UHealthComponent;
class UResultMenuWidget;
class UWorld;

/** 正式难度生命与临时调试覆盖；全部可在 GameMode 蓝图默认值中调整。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapePlayerHealthTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Health", meta = (ClampMin = "1.0"))
	float EasyMaxHealth = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Health", meta = (ClampMin = "1.0"))
	float NormalMaxHealth = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Health", meta = (ClampMin = "1.0"))
	float HardMaxHealth = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Health")
	bool bUseDebugOverride = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Health",
		meta = (EditCondition = "bUseDebugOverride", ClampMin = "1.0"))
	float DebugMaxHealth = 1000.0f;

	bool IsConfigured(FString& OutError) const;
	float Resolve(EZeroEscapeDifficulty Difficulty) const;
};

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
	/** 按类型定位关卡中唯一的对象；缺失或多于一个都失败，不选择"第一个"。 */
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

	/** 玩家摆放完成后，按本局难度初始化最大生命和当前生命。 */
	bool InitializePlayerHealth();

	/** 出口到达回调：转发判胜给 GameState。 */
	UFUNCTION()
	void HandleExitReached();

	/** 玩家生命归零回调：转发判负给 GameState。 */
	UFUNCTION()
	void HandlePlayerDeath();

	/** 订阅 GameState 状态变化，用于分出胜负时弹结算界面。 */
	bool BindRoundStateForUI();

	/** 局状态变化回调：非进行中时先入局末过渡，延迟后弹结算。 */
	UFUNCTION()
	void HandleRoundStateChanged(EZeroEscapeRoundState NewState);

	/** 局末过渡：胜利淡出消失 / 失败布娃娃定格，延迟后由 ShowResultMenu 弹结算。 */
	void BeginWinSequence();
	void BeginLoseSequence();
	void TickWinFadeOut();
	void ShowResultMenu(bool bVictory);

	void SetGameplayInputLocked(bool bLocked) const;
	void AbortSetupAndReturnToMainMenu(const TCHAR* Reason);
	void BeginSetupTransition(
		const TCHAR* Reason,
		const TSoftObjectPtr<UWorld>& TargetLevel);
	void FinalizeSetupTransition();
	void CleanupRoundActors();
	void UnbindRuntimeDelegates();

	/** 开局运镜：出口→追猎者→玩家（均硬切静止 1s），落地后再隔 1s 解锁。 */
	void PlayIntroSequence();
	void ShowExitView();
	void ShowPursuerView();
	void ShowPlayerViewAndUnlock();
	void UnlockGameplay();
	void SetRoundFrozen(bool bFrozen);

	/** 生成一台看向目标的临时运镜相机。
	 *  在"目标→参考点"方向附近做多方向、多距离探测，选出平视能看到目标的空位；找不到则向玩家侧收敛。 */
	ACameraActor* SpawnIntroCamera(
		const FVector& TargetLocation,
		const FVector& ReferenceLocation,
		float CameraHeightOffset,
		float LookAtHeightOffset);
	void DestroyIntroCameras();

	/** 本局唯一追猎者类；由正式 GameMode 蓝图在类默认值中指定现有 BP_Pursuer。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round")
	TSubclassOf<APursuerCharacter> PursuerClass;

	/** 正式难度生命和当前调试覆盖，集中在 GameMode 蓝图默认值中调整。 */
	UPROPERTY(EditDefaultsOnly, Category = "ZeroEscape|Round|Player Health")
	FZeroEscapePlayerHealthTuning PlayerHealthTuning;

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
	FTimerHandle IntroExitViewTimer;
	FTimerHandle IntroPursuerViewTimer;
	FTimerHandle IntroPlayerViewTimer;
	FTimerHandle IntroUnlockTimer;
	FTimerHandle WinFadeTimer;
	FTimerHandle ResultShowTimer;
	TSoftObjectPtr<UWorld> PendingTransitionLevel;
	FString PendingTransitionReason;
	int64 LastHandledGenerationOperationId = 0;
	EZeroEscapeDifficulty ActiveDifficulty = EZeroEscapeDifficulty::Normal;
	bool bSetupTerminal = false;
	bool bRoundStarted = false;
	bool bSetupTransitionScheduled = false;
	bool bEndSequenceStarted = false;
	bool bWinFadeMaterialParamWorks = false;
	bool bPendingResultVictory = false;
	float WinFadeElapsed = 0.0f;
	bool bEndingPlay = false;
	bool bIntroSequencePlaying = false;

	/** 开局运镜临时相机；序列结束或关卡退出时销毁。 */
	TWeakObjectPtr<ACameraActor> IntroExitCamera;
	TWeakObjectPtr<ACameraActor> IntroPursuerCamera;
};
