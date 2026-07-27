// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeGenerationTestHarness.h
 * 职责：只在独立 PCG 测试关卡中协调安全出生、运行时生成结果与受控重生成。
 * 边界：不参与空间求解或结构实例化，不进入最终游戏流程，也不按名称查找依赖。
 * 状态 Owner：Generator 拥有生成事务；Harness 只拥有玩家传送等待与测试入口的短生命周期状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeRuntimeGenerationTestHarness.generated.h"

class APawn;
class APlayerController;
class AZeroEscapeRuntimeLevelGenerator;
class USceneComponent;

/**
 * 运行时整关生成的 PIE 装配 Harness。
 *
 * PlayerStart 负责把玩家放到不会随生成场景一起销毁的 Staging；Harness 等 Generator 真正进入
 * Ready 后再读取动态 PlayerSpawn Anchor。重生成时顺序反过来：先回 Staging，确认安全后才允许
 * Generator 以 replace 语义清掉旧 HISM，避免玩家脚下地板先消失。
 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapeRuntimeGenerationTestHarness final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的测试协调 Actor；Actor Transform 本身不充当隐式配置。 */
	AZeroEscapeRuntimeGenerationTestHarness();

	/** 先把当前玩家安全送回 Staging，再使用 Generator 的默认 Request 进行一次完整替换生成。 */
	UFUNCTION(BlueprintCallable, Category = "PCG|Test", meta = (UnsafeDuringActorConstruction = "true"))
	bool Regenerate();

	/** 先安全回 Staging，再以显式 Request 重生成；用于以后做固定 Seed/难度的 PIE 对照。 */
	UFUNCTION(BlueprintCallable, Category = "PCG|Test", meta = (UnsafeDuringActorConstruction = "true"))
	bool RegenerateFromRequest(const FZeroEscapeGenerationRequest& Request);

protected:
	/** 绑定两个事件源，并兼容 Generator 与 Harness 任意 BeginPlay 先后顺序。 */
	virtual void BeginPlay() override;

	/** 先关闭入口、解绑 Delegate 并清 Timer，避免 World 销毁期间重新传送或生成。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Generator 对外提交终态后的唯一回调；这里只查询 Anchor/排队传送，绝不在回调内重入生成。 */
	UFUNCTION()
	void HandleGenerationFinished(bool bSuccess, const FZeroEscapeGenerationReport& Report);

	/** Pawn 晚于关卡 Actor 出现时立即补做待处理传送；Timer 仍作为有限兜底。 */
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	/** 找到 PlayerIndex 对应 Controller，并以幂等方式维护 OnPossessedPawnChanged 绑定。 */
	void BindPlayerControllerIfAvailable();

	/** 标记“生成 Start 已可用”，立即尝试一次；Pawn 未就绪时启动有限 Timer。 */
	void QueueTransferToGeneratedStart();

	/** 同时要求 Ready Anchor、PlayerController/Pawn 和安全 Teleport 成功；失败不会强制穿模。 */
	void TryTransferToGeneratedStart();

	/** 无 Tick 的 Pawn 发现兜底；达到固定次数后停止，不留下无限定时器。 */
	void HandlePlayerDiscoveryTimer();

	/** 幂等清除 Pawn 发现 Timer；EndPlay 和所有传送终态统一调用。 */
	void ClearPlayerDiscoveryTimer();

	/** 重生成前检查依赖/状态并把玩家送回显式 StagingAnchor；失败时不清旧场景。 */
	bool PrepareForRegeneration();

	/** 停止 Pawn 当前移动，以带碰撞检查的 TeleportTo 移动并同步本地 Controller 朝向。 */
	bool MoveCurrentPlayerTo(const FTransform& TargetTransform, const TCHAR* MoveReason);

	/** 仅用于承载可放置 Actor；不从该组件推导 Staging 或生成坐标。 */
	UPROPERTY(VisibleAnywhere, Category = "PCG|Test")
	TObjectPtr<USceneComponent> SceneRoot;

	/**
	 * 关卡实例显式引用的运行时 Generator。
	 * 不提供按名称或“取第一个 Actor”的回退，避免以后多个生成区时悄悄连错 Owner。
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "PCG|Test", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AZeroEscapeRuntimeLevelGenerator> Generator;

	/**
	 * 指向不会被 Generator 清理的 PlayerStart_Staging（或未来专用 Anchor Actor）。
	 * Harness 只读取它的世界 Transform，不依赖对象名、Label 或文件夹。
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "PCG|Test", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AActor> StagingAnchor;

	/** 当前单机 Demo 使用玩家 0；保留显式索引，避免把获取入口散落在多处。 */
	UPROPERTY(EditInstanceOnly, Category = "PCG|Test", meta = (ClampMin = "0"))
	int32 PlayerIndex = 0;

	/** Generator 仍为 Idle 时由 Harness 发起首局；可关闭以专测外部触发。 */
	UPROPERTY(EditInstanceOnly, Category = "PCG|Test")
	bool bGenerateIfIdleOnBeginPlay = true;

	/** 当前已绑定的 PlayerController；Pawn 可以在其生命周期内变化。 */
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> BoundPlayerController;

	/** Pawn 尚未可用时的有限重试 Timer，不启用 Actor Tick。 */
	FTimerHandle PlayerDiscoveryTimerHandle;

	/** 包含立即尝试在内的累计次数；只用于确定停止时机，不参与生成确定性。 */
	int32 PlayerDiscoveryAttempts = 0;

	/** Ready 已发生但玩家尚未成功传送；Delegate 和 Timer 都只消费这个状态。 */
	bool bPendingGeneratedStartTransfer = false;

	/** EndPlay 单向关闭闩；置位后所有回调均不得再产生副作用。 */
	bool bEndingPlay = false;
};
