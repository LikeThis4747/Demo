// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeLevelGenerator.h
 * 职责：拥有一局实时 PCG 的同步生成状态、已提交场景实例和对玩法开放的逻辑 Anchor 查询。
 * 边界：算法实现留在 Private/PCG；蓝图只装配三类 DataAsset、触发生成并读取结果。
 * 状态 Owner：场景中唯一的 AZeroEscapeRuntimeLevelGenerator 实例；Clear/EndPlay 负责完整回滚。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeRuntimeLevelGenerator.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UZeroEscapeLevelGenerationProfile;
class UZeroEscapeModuleCatalog;
class UZeroEscapePresentationProfile;

UENUM(BlueprintType)
enum class EZeroEscapeRuntimeGenerationState : uint8
{
	/** 没有已提交 Plan；公开 Clear 的稳定终态。 */
	Idle = 0,
	/** 正在构建资产快照与抽象拓扑。 */
	Planning = 1,
	/** 纯算法已返回，正在核对 Signature 与规范 Hash。 */
	Validating = 2,
	/** 正在把完整 Plan 事务式提交为场景组件。 */
	Instantiating = 3,
	/** 为后续异步导航扩展预留；首版同步 HISM 路径不会停留在此状态。 */
	WaitingForNavigation = 4,
	/** 场景已完整提交，Anchor 查询才在此状态有效。 */
	Ready = 5,
	/** 最近一次请求失败且没有半成品场景。 */
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
 * 运行时整关生成的世界边界 Actor。
 *
 * 生成分为“纯数据计划”和“场景提交”两段：求解器先返回完整 Plan，Actor 验证其签名后
 * 才创建 HISM。任何检查点失败都会销毁本次登记的全部对象，不会留下半张地图。
 * 该 Actor 不直接生成任务、陷阱或敌人；玩法系统在 Ready 后通过 Anchor 查询进行装配。
 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapeRuntimeLevelGenerator : public AActor
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的生成器和统一 GeneratedRoot。 */
	AZeroEscapeRuntimeLevelGenerator();

	/** 使用 DefaultRequest 同步生成；拒绝重入，结果通过返回值、LastReport 和 Delegate 给出。 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool Generate();

	/** 使用显式请求同步生成；请求被接受后旧场景立即失效，失败不会保留半成品。 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool GenerateFromRequest(const FZeroEscapeGenerationRequest& Request);

	/** 非生成期间幂等清空所有实例和 Plan，并回到 Idle；忙碌时返回 false 且不改状态。 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool ClearGeneratedScene();

	/** Ready 时返回 PlayerSpawn 逻辑 Anchor 的世界 Transform。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedStartWorldTransform(FTransform& OutTransform) const;

	/** Ready 时返回 Exit 逻辑 Anchor 的世界 Transform。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedExitWorldTransform(FTransform& OutTransform) const;

	/** Ready 时按 StableAnchorInstanceId 顺序返回所有 Objective Anchor；EscapeOnly 可返回空数组和 true。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedObjectiveWorldTransforms(TArray<FTransform>& OutTransforms) const;

	/** 一次同步请求结束时广播；广播期间仍处于防重入保护。 */
	UPROPERTY(BlueprintAssignable, Category = "PCG")
	FZeroEscapeGenerationFinished OnGenerationFinished;

	/** 最近一次已结束请求的结构化报告；Clear 不伪造新的失败报告。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "PCG")
	FZeroEscapeGenerationReport LastReport;

	/** 当前生命周期状态；只有完整实例化提交后才进入 Ready。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "PCG")
	EZeroEscapeRuntimeGenerationState State = EZeroEscapeRuntimeGenerationState::Idle;

protected:
	/** 按 TriggerMode 可选执行首局生成。 */
	virtual void BeginPlay() override;

	/** 使所有运行时实例失效并清理，不依赖 Tick 或异步回调。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 内部无条件回滚入口；调用者负责外部状态转换。 */
	void ClearGeneratedSceneInternal();

	/** 事务式实例化已验证 Plan；首版仅接受 HISM，任一失败由上层统一回滚已登记对象。 */
	bool InstantiateValidatedPlan(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		FZeroEscapeGenerationReport& InOutReport);

	/** 提交最终报告、状态并广播；不会清理或再次生成。 */
	void FinishGeneration(bool bSuccess, const FZeroEscapeGenerationReport& Report);

	/** 按 Stable Anchor Id 查询并转换到世界；仅供三个公开查询复用。 */
	bool GetGeneratedAnchorWorldTransform(
		int32 StableAnchorInstanceId,
		EZeroEscapeGameplayAnchorType ExpectedType,
		FTransform& OutTransform) const;

	/** 所有 HISM 与逻辑 Anchor 的父空间；运行时必须保持有限 Unit Scale。 */
	UPROPERTY(VisibleAnywhere, Category = "PCG")
	TObjectPtr<USceneComponent> GeneratedRoot;

	/** BeginPlay 自动生成或只允许外部显式触发。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	EZeroEscapeGenerationTrigger TriggerMode = EZeroEscapeGenerationTrigger::ExplicitOnly;

	/** Generate() 每次复制的稳定请求；难度和 Seed 在一局内不变。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	FZeroEscapeGenerationRequest DefaultRequest;

	/** 难度、Flow、路线和求解预算的唯一权威配置。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapeLevelGenerationProfile> GenerationProfile;

	/** 与具体素材无关的结构模块、Portal、Anchor 和逻辑 Bounds。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapeModuleCatalog> ModuleCatalog;

	/** 当前素材集到逻辑 Module 的可替换表现绑定；首套将装配 SFCorridors。 */
	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapePresentationProfile> PresentationProfile;

	/** 最近一次成功提交的纯数据 Plan；失败或 Clear 后必须为空。 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "PCG")
	FZeroEscapeGeneratedLevelPlan LastPlan;

	/** 当前实例化事务已经登记的 HISM；创建后立即入数组以保证可回滚。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> GeneratedHismComponents;

	/** 为未来受控包装 Actor 预留的回滚集合；首版 Actor Policy fail-closed，不会写入。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> GeneratedActors;

	/** 同步重入锁；广播回调中也保持为 true，拒绝递归 Generate/Clear。 */
	bool bGenerationInProgress = false;

	/** EndPlay 一旦开始永久置 true；阻止销毁回调重新生成或广播完成事件。 */
	bool bEndingPlay = false;
};
