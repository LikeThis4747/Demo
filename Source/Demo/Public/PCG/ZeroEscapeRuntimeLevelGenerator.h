// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeLevelGenerator.h
 * 职责：拥有一局实时 Grid/WFC 生成事务、已提交 HISM 场景和对玩法开放的 Anchor 查询。
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

/** 运行时同步事务的公开生命周期；保留 WaitingForNavigation 供未来导航阶段使用。 */
UENUM(BlueprintType)
enum class EZeroEscapeRuntimeGenerationState : uint8
{
	Idle = 0,
	Planning = 1,
	Validating = 2,
	Instantiating = 3,
	WaitingForNavigation = 4,
	Ready = 5,
	Failed = 6
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
 * 求解器先构造完整纯数据 Plan，Actor 再一次性提交 HISM；任一检查点失败都会回滚本次全部组件。
 * 玩法对象、敌人、陷阱和奖励不会在这里生成，只通过 Ready 后的稳定 Anchor 供其他系统装配。
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

	/** Ready 时按 StableAnchorInstanceId 返回所有候选 Objective Anchor。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedObjectiveWorldTransforms(TArray<FTransform>& OutTransforms) const;

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

	/** EndPlay 时无条件使 Plan、Anchor 与组件失效。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 内部无条件回滚入口；调用者负责合法状态转换。 */
	void ClearGeneratedSceneInternal();

	/** 把已验证逻辑 Plan 展开并事务式提交为五类结构 HISM。 */
	bool InstantiateValidatedPlan(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		FZeroEscapeGenerationReport& InOutReport);

	/** 提交最终状态与单条结构化证据后广播完成事件。 */
	void FinishGeneration(
		bool bSuccess,
		const FZeroEscapeGenerationReport& Report,
		const FZeroEscapeGenerationRequest& Request);

	/** 按稳定 Id 和类型查询单个 Anchor 并转换到世界空间。 */
	bool GetGeneratedAnchorWorldTransform(
		int32 StableAnchorInstanceId,
		EZeroEscapeGameplayAnchorType ExpectedType,
		FTransform& OutTransform) const;

	/** 所有结构 HISM 与逻辑 Anchor 的共同局部空间。 */
	UPROPERTY(VisibleAnywhere, Category = "PCG")
	TObjectPtr<USceneComponent> GeneratedRoot;

	/** BeginPlay 自动生成或仅允许外部显式触发。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	EZeroEscapeGenerationTrigger TriggerMode = EZeroEscapeGenerationTrigger::ExplicitOnly;

	/** Generate() 每次复制的稳定请求。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	FZeroEscapeGenerationRequest DefaultRequest;

	/** 难度、流程、格网、路线和 WFC 权重的唯一权威配置。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapeLevelGenerationProfile> GenerationProfile;

	/** 五类规范结构到当前素材的直接绑定；不包含 Module Catalog。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapePresentationProfile> PresentationProfile;

	/** 最近一次成功提交的纯数据 Plan；失败或 Clear 后为空。 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "PCG")
	FZeroEscapeGeneratedLevelPlan LastPlan;

	/** 本次实例化事务已登记的全部 HISM；创建后立即入数组以保证可回滚。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> GeneratedHismComponents;

	/** 同步重入锁；完成 Delegate 广播期间也保持为 true。 */
	bool bGenerationInProgress = false;

	/** EndPlay 开始后永久置 true，阻止销毁回调再次生成。 */
	bool bEndingPlay = false;
};
