// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeLevelGenerator.h
 * 职责：拥有一局多层 PCG 从纯数据、场景事务到正式导航验收的完整生命周期。
 * 边界：二维/多层求解和表现展开留在 Private/PCG；玩法对象只读取最终 Ready 结果。
 */

#pragma once

#include "AI/Navigation/NavigationTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ZeroEscapeGenerationTypes.h"
#include "TimerManager.h"

#include "ZeroEscapeRuntimeLevelGenerator.generated.h"

class ANavigationData;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UZeroEscapeLevelGenerationProfile;
class UZeroEscapePresentationProfile;
struct FZeroEscapeSharedGenerationBudget;

/** WaitingForNavigation 是跨帧状态；GenerateFromRequest 返回 true 时不代表已经 Ready。 */
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
 * 正式运行时整关生成器；只有导航路径验收成功才广播成功并进入 Ready。
 * 每个 Actor 生命周期只接受一次生成请求；需要重试时由 GameMode 重载关卡创建新 World。
 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapeRuntimeLevelGenerator : public AActor
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的生成器和统一 GeneratedRoot。 */
	AZeroEscapeRuntimeLevelGenerator();

	/** 使用 DefaultRequest 发起流程；true 只表示请求被接受。 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool Generate();

	/** 发起显式请求；最终成功/失败只通过 OnGenerationFinished 和 Report.OperationId 判断。 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool GenerateFromRequest(const FZeroEscapeGenerationRequest& Request);

	/** GameMode 在发起请求前提供真实追猎者 CDO 的导航代理尺寸。 */
	bool ConfigurePursuerNavigationAgent(const FNavAgentProperties& AgentProperties);

	/** 检查线程、World、Construction Script、单局一次、持久等待、重入和 EndPlay 门禁。 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool CanAcceptGenerationRequest() const;

	/**
	 * 非生成/等待期间幂等清空场景和 Plan。
	 * 此操作不会解除单次请求门禁；再次生成必须重载关卡并创建新的生成器 Actor。
	 */
	UFUNCTION(BlueprintCallable, Category = "PCG", meta = (UnsafeDuringActorConstruction = "true"))
	bool ClearGeneratedScene();

	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedPlayerSpawnWorldTransform(FTransform& OutTransform) const;

	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedPursuerSpawnWorldTransform(FTransform& OutTransform) const;

	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedExitWorldTransform(FTransform& OutTransform) const;

	/**
	 * 返回普通二维 WFC 格的玩法放置候选；完整结构、实体和净空从不进入此查询。
	 * 保护区按同层曼哈顿距离计算，Transform 贴当前层地面且 X 轴沿走廊。
	 * 返回 true 时，全部输出都已通过有限值、规范旋转与 Unit Scale 校验。
	 */
	UFUNCTION(BlueprintPure, Category = "PCG")
	bool GetGeneratedOrdinaryGameplayCellWorldTransforms(
		bool bAvoidSpawnExitNeighbors,
		bool bStraightCorridorOnly,
		TArray<FTransform>& OutTransforms) const;

	UFUNCTION(BlueprintPure, Category = "PCG")
	int32 GetGeneratedSeed() const;

	UFUNCTION(BlueprintPure, Category = "PCG")
	int64 GetActiveOperationId() const { return ActiveOperationId; }

	/** 只在最终成功或确定失败时广播一次；导航等待中不广播。 */
	UPROPERTY(BlueprintAssignable, Category = "PCG")
	FZeroEscapeGenerationFinished OnGenerationFinished;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "PCG")
	FZeroEscapeGenerationReport LastReport;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "PCG")
	EZeroEscapeRuntimeGenerationState State = EZeroEscapeRuntimeGenerationState::Idle;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 销毁全部已登记 HISM/灯，清空 Plan，并取消导航等待。 */
	void ClearGeneratedSceneInternal();

	/** 委托 StructureBuilder 提交 HISM，再原样生成普通格顶灯。 */
	bool InstantiateValidatedPlan(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		FZeroEscapeGenerationReport& InOutReport);

	bool SpawnCeilingLights(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		FZeroEscapeGenerationReport& InOutReport);

	/** 在实例化前解析本次追猎者对应的 Dynamic RecastNavMesh 并先绑定完成事件。 */
	bool PrepareNavigationWait(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const FZeroEscapeSharedGenerationBudget& Budget,
		FZeroEscapeGenerationReport& InOutReport);

	/** HISM 全部提交后启动事件+Timer 等待；AddInstance 自己通知导航系统。 */
	void StartNavigationWait();

	UFUNCTION()
	void HandleNavigationGenerationFinished(ANavigationData* NavigationData);

	void HandleNavigationBuildTimeout(int64 CallbackOperationId);
	void TryCompleteNavigationWait(int64 CallbackOperationId);

	/** 最多 20 个点投射，再从追猎者点执行最多 19 次 Regular TestPathSync。 */
	bool ValidateNavigationEndpoints(FZeroEscapeGenerationReport& InOutReport);

	bool BuildNavigationValidationAddresses(
		TArray<FIntVector>& OutAddresses,
		FZeroEscapeGenerationReport& InOutReport) const;

	/** 清理事件和 Timer；不销毁场景，也不改变最终 State。 */
	void CancelNavigationWait();

	/** 写最终状态、日志和唯一完成广播；广播期间保持防重入。 */
	void FinishGeneration(bool bSuccess, const FZeroEscapeGenerationReport& Report);

	FVector AddressToLocalLocation(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const FIntVector& Address,
		bool bUseAnchorHeight) const;
	bool AddressToWorldTransform(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const FIntVector& Address,
		bool bUseAnchorHeight,
		FTransform& OutTransform) const;

	UPROPERTY(VisibleAnywhere, Category = "PCG")
	TObjectPtr<USceneComponent> GeneratedRoot;

	UPROPERTY(EditAnywhere, Category = "PCG")
	EZeroEscapeGenerationTrigger TriggerMode = EZeroEscapeGenerationTrigger::ExplicitOnly;

	UPROPERTY(EditAnywhere, Category = "PCG", meta = (ShowOnlyInnerProperties))
	FZeroEscapeGenerationRequest DefaultRequest;

	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapeLevelGenerationProfile> GenerationProfile;

	UPROPERTY(EditAnywhere, Category = "PCG")
	TObjectPtr<UZeroEscapePresentationProfile> PresentationProfile;

	UPROPERTY(VisibleAnywhere, Transient, Category = "PCG")
	FZeroEscapeGeneratedLevelPlan LastPlan;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> GeneratedHismComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> GeneratedLightActors;

	/** 本次等待的目标导航数据；仅在 WaitingForNavigation 有效。 */
	UPROPERTY(Transient)
	TObjectPtr<ANavigationData> ExpectedNavigationData;

	FNavAgentProperties PursuerNavigationAgentProperties;
	bool bHasPursuerNavigationAgent = false;

	FZeroEscapeGenerationRequest ActiveRequest;
	FZeroEscapeGenerationReport PendingReport;
	double ActiveGenerationStartSeconds = 0.0;
	int64 ActiveCanonicalLayoutHash = 0;
	int32 ActiveOrdinaryCellCount = 0;
	int32 ActiveStructureCount = 0;
	double ActiveNavigationTimeoutSeconds = 10.0;
	int32 ActiveMaxNavigationValidationPoints = 20;

	FTimerHandle NavigationTimeoutTimer;
	int64 NextOperationId = 1;
	int64 ActiveOperationId = 0;
	bool bNavigationWaitActive = false;
	bool bNavigationGeometryRegistrationStarted = false;
	bool bNavigationGeometrySubmitted = false;
	bool bObservedNavigationBuild = false;
	bool bReceivedTargetNavigationCompletion = false;
	bool bNavigationWaitTerminal = false;
	bool bGenerationInProgress = false;
	/** 正式游戏关卡中一次 Actor 生命周期只接受一局；重开通过关卡切换创建新 World。 */
	bool bHasAcceptedGenerationRequest = false;
	bool bEndingPlay = false;
};
