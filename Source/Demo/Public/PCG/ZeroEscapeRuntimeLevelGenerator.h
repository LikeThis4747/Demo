// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeLevelGenerator.h
 * 职责：拥有一局实时 Grid/WFC 生成事务、已提交结构/顶灯场景和空间结果查询。
 * 边界：算法留在 Private/PCG；蓝图只装配 Generation/Profile 两个 DataAsset、触发并读取结果。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeRuntimeLevelGenerator.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UZeroEscapeLevelGenerationProfile;
class UZeroEscapePresentationProfile;

/** 当前同步生成事务实际使用的公开生命周期。 */
UENUM(BlueprintType)
enum class EZeroEscapeRuntimeGenerationState : uint8
{
	Idle = 0,
	Planning = 1,
	Validating = 2,
	Instantiating = 3,
	Ready = 4,
	Failed = 5
};

/** 生成只允许在 BeginPlay 或明确函数调用发生，禁止 Construction Script 隐式改图。 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationTrigger : uint8
{
	BeginPlay = 0,
	ExplicitOnly = 1
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FZeroEscapeGenerationFinished,
	bool,
	bSuccess,
	const FZeroEscapeGenerationReport&,
	Report);

/**
 * 实时非工具型整关 PCG 的世界边界 Actor。
 * 求解器先构造完整纯数据 Plan，Actor 再一次性提交 HISM 与顶灯 Actor；
 * 任一检查点失败都会回滚本次全部表现对象。
 * 玩法对象、敌人、陷阱和奖励不会在这里生成；其他系统只读取 Ready 后的 Start/Exit/Rooms。
 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapeRuntimeLevelGenerator : public AActor
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的生成器和统一 GeneratedRoot。 */
	AZeroEscapeRuntimeLevelGenerator();

	/** 使用 DefaultRequest 同步生成；拒绝重入并通过 LastReport/Delegate 提供结果。 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool Generate();

	/** 使用显式请求同步生成；请求一旦被接受，旧场景立即失效。 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool GenerateFromRequest(const FZeroEscapeGenerationRequest& Request);

	/** 只检查线程、World、Construction Script、重入和 EndPlay 门禁，不预跑算法。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool CanAcceptGenerationRequest() const;

	/** 非生成期间幂等清空 HISM 与 Plan 并回到 Idle；忙碌时不修改状态。 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool ClearGeneratedScene();

	/** Ready 时返回玩家出生锚点世界 Transform。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedStartWorldTransform(FTransform& OutTransform) const;

	/** Ready 时返回出口锚点世界 Transform。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedExitWorldTransform(FTransform& OutTransform) const;

	/** Ready 时按 RegionId 稳定顺序返回全部中立房间世界 Transform。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedRoomWorldTransforms(TArray<FTransform>& OutTransforms) const;

	/** Ready 时按区域语义返回候选放置点世界 Transform（地板高度）；可排除 Start/Exit 及相邻格。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedCellWorldTransforms(
		EZeroEscapeGridRegionKind RegionKind,
		bool bExcludeStartExitAdjacent,
		TArray<FTransform>& OutTransforms) const;

	/** Ready 时返回本局布局 Seed，供下游放置层复现确定性随机。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	int32 GetGeneratedSeed() const;

	/** 一次同步请求结束时广播；广播期间仍保持防重入锁。 */
	UPROPERTY(BlueprintAssignable, Category = "PCG")
	FZeroEscapeGenerationFinished OnGenerationFinished;

	/** 最近一次已结束请求的结构化报告。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "PCG")
	FZeroEscapeGenerationReport LastReport;

	/** 只有完整 Plan 和全部组件成功提交后才进入 Ready。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "PCG")
	EZeroEscapeRuntimeGenerationState State = EZeroEscapeRuntimeGenerationState::Idle;

protected:
	/** 根据 TriggerMode 可选生成首局。 */
	virtual void BeginPlay() override;

	/** EndPlay 时无条件使 Plan、空间查询结果与组件失效。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 内部无条件回滚 HISM、顶灯和 Plan；调用者负责合法状态转换。 */
	void ClearGeneratedSceneInternal();

	/** 把已验证逻辑 Plan 展开并事务式提交为五类结构 HISM 与可选顶灯。 */
	bool InstantiateValidatedPlan(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		FZeroEscapeGenerationReport& InOutReport);

	/**
	 * 按四方向方格的棋盘奇偶性选择约半数有效格并生成顶灯。
	 * 每个成功 Spawn 的 Actor 会立即登记；任一失败由调用者走统一事务回滚。
	 */
	bool SpawnCeilingLights(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		FZeroEscapeGenerationReport& InOutReport);

	/** 提交最终状态与单条结构化证据后广播完成事件。 */
	void FinishGeneration(
		bool bSuccess,
		const FZeroEscapeGenerationReport& Report,
		const FZeroEscapeGenerationRequest& Request);

	/** 所有结构 HISM 与空间结果的共同局部空间。 */
	UPROPERTY(VisibleAnywhere, Category = "PCG")
	TObjectPtr<USceneComponent> GeneratedRoot;

	/** BeginPlay 自动生成或仅允许外部显式触发。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	EZeroEscapeGenerationTrigger TriggerMode = EZeroEscapeGenerationTrigger::ExplicitOnly;

	/** Generate() 每次复制的稳定请求。 */
	UPROPERTY(EditAnywhere, Category = "PCG", meta = (ShowOnlyInnerProperties))
	FZeroEscapeGenerationRequest DefaultRequest;

	/** 格网、房间、路线预算和难度 WFC 权重的唯一权威配置。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapeLevelGenerationProfile> GenerationProfile;

	/** 五类规范结构与可选顶灯到当前素材的直接表现绑定。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapePresentationProfile> PresentationProfile;

	/** 最近一次成功提交的纯数据 Plan；失败或 Clear 后为空。 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "PCG")
	FZeroEscapeGeneratedLevelPlan LastPlan;

	/** 本次实例化事务已登记的全部 HISM；创建后立即入数组以保证可回滚。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> GeneratedHismComponents;

	/** 本次实例化事务已登记的全部顶灯；由 Generator 写入并在 Clear/失败/EndPlay 清空。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> GeneratedLightActors;

	/** 同步重入锁；完成 Delegate 广播期间也保持为 true。 */
	bool bGenerationInProgress = false;

	/** EndPlay 开始后永久置 true，阻止销毁回调再次生成。 */
	bool bEndingPlay = false;
};
