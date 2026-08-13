// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardProjectile.h
 * 职责：拥有从炮架预装到离膛的同一个 Chaos 胶囊刚体、一次质心冲量、接触阶段和可选 HeavyImpact 准备。
 * 边界：不使用 Thruster/ProjectileMovement，不 Tick、不追踪目标、不直接改写速度或 Transform。
 * 状态 Owner：本 Actor 唯一写入 Loaded/Ballistic/FreePhysics/Sleeping/Disabled、LaunchId 和重冲击通知去重状态。
 * 轴约定：胶囊局部 +Z 是弹体长轴；SpawnTransform 会把它对齐发射器实际炮管方向。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"

#include "ThrustGuidedHazardProjectile.generated.h"

class UCapsuleComponent;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class UThrustGuidedHazardTuningData;
struct FHeavyImpactPreparationRequest;

/** 项目内部的纯 Chaos 弹体阶段，不是 UE 官方 ProjectileMovement 状态。 */
enum class EThrustGuidedHazardProjectilePhase : uint8
{
	Uninitialized,
	Loaded,
	Ballistic,
	FreePhysics,
	Sleeping,
	Disabled
};

/** 接受一次初速度后始终由同一个 Chaos 刚体运动的一次性弹体。 */
UCLASS()
class DEMO_API AThrustGuidedHazardProjectile final : public AActor
{
	GENERATED_BODY()

public:
	/** 装配唯一物理胶囊、查询球和纯美术挂点；本 Actor 永不 Tick。 */
	AThrustGuidedHazardProjectile();

	/** 延迟生成期间注入唯一配置；BeginPlay 进入可见但无碰撞、无模拟的 Loaded 阶段。 */
	void ConfigureLoaded(UThrustGuidedHazardTuningData* InTuningData);

	/** 炮架解除挂接后调用；同一个 Actor 开启 Chaos 并获得唯一一次质心冲量。 */
	bool LaunchFromLoaded(
		const FVector& InLaunchVelocity,
		const FGuid& InLaunchId,
		FString& OutError);

	/** 供拥有它的 Launcher 校验预装生命周期，不暴露内部物理阶段写入口。 */
	bool IsLoaded() const;

protected:
	/** 校验预装合同，保持同一个真实弹体可见但不参与碰撞或物理模拟。 */
	virtual void BeginPlay() override;

	/** 清理碰撞/休眠/可选重冲击委托、Timer 和候选集合。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Blueprint 可在首个有效阻挡接触时播放撞击声光；C++ 不依赖实现。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "机关|预判抛射|表现")
	void ReceiveFirstBlockingImpact(
		AActor* OtherActor,
		FVector ImpactPoint,
		FVector NormalImpulse);

private:
	/** 将 DataAsset 的胶囊和可选预测球尺寸写入组件；关闭 HeavyImpact 时不读取其字段。 */
	void ApplyConfiguration(const UThrustGuidedHazardTuningData& Tuning);

	/** 启动 60 Hz HeavyImpact 候选预测；开关关闭或刚体休眠时不工作。 */
	void StartPreparationMonitoring();

	/** 停止 HeavyImpact 预测 Timer。 */
	void StopPreparationMonitoring();

	/** 对当前重叠的 IHeavyImpactReceiver 估算带重力 ETA，并按 Actor 独立发送一次请求。 */
	void EvaluatePreparationCandidates();

	/** 用胶囊真实表面、接收者预测组件、相对速度和世界重力构造准备请求。 */
	bool BuildPreparationRequest(
		const AActor& Receiver,
		FHeavyImpactPreparationRequest& OutRequest);

	/** 解 Gap = Closing*t + 0.5*GravityAlong*t^2，返回最早有限非负根。 */
	static bool TryEstimateGravityAwareContactTime(
		float SurfaceGap,
		float ClosingSpeed,
		float GravityAlongApproach,
		float& OutTimeSeconds);

	/** 返回从胶囊中心沿世界方向射线到当前胶囊表面的精确距离，单位 cm。 */
	static float CalculateCapsuleRaySurfaceDistance(
		const UCapsuleComponent& Capsule,
		const FVector& WorldDirection);

	/** 关闭碰撞/模拟和全部本地能力并记录明确错误。 */
	void DisableProjectile(const FString& Reason);

	/** 第一次有效角色阻挡命中复用 LaunchId 提交现有 StandingImpact；返回是否已识别并通知接收者。 */
	bool TrySubmitStandingImpact(
		AActor* ContactOwner,
		const FVector& ContactLinearVelocity,
		const FVector& NormalImpulse,
		const FHitResult& Hit);

	/** 记录每个 Chaos Hit；首次有效阻挡只切阶段/阻尼/表现，不改写速度。 */
	UFUNCTION()
	void HandleProjectileHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** 休眠刚体被真实外力唤醒时恢复 FreePhysics 和可选准备采样。 */
	UFUNCTION()
	void HandleProjectileWake(UPrimitiveComponent* WakingComponent, FName BoneName);

	/** Ballistic/FreePhysics 刚体休眠时停止准备 Timer，不销毁弹体。 */
	UFUNCTION()
	void HandleProjectileSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);

	/** HeavyImpact 开启时登记进入 Query-only 预测球的接收者。 */
	UFUNCTION()
	void HandlePreparationVolumeBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** Actor 的最后一个组件离开预测球后移除候选。 */
	UFUNCTION()
	void HandlePreparationVolumeEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	/** 唯一模拟物理的根胶囊；局部 +Z 是长轴，阻挡世界、PhysicsBody 和 Pawn。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCapsuleComponent> ProjectileBody;

	/** ProjectileBody 下的纯美术挂点；Blueprint 网格碰撞必须关闭。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> BodyVisualRoot;

	/** ProjectileBody 下的尾迹/烟迹挂点；仅辅助阅读方向，不代表持续推进。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ExhaustVisualRoot;

	/** 可选 HeavyImpact 的 Query-only Pawn 预测球；关闭开关时始终 NoCollision。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|重冲击",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> PreparationVolume;

	/** 预装生成注入并在整个弹体生命周期持有的唯一配置资产。 */
	UPROPERTY(Transient)
	TObjectPtr<UThrustGuidedHazardTuningData> RuntimeTuningData;

	/** 发射器冻结的最终世界初速度；LaunchFromLoaded 施加一次后不再由代码修改。 */
	FVector PendingLaunchVelocity = FVector::ZeroVector;

	/** 本次发射稳定 ID，用于日志和可选 HeavyImpact 接收者去重。 */
	FGuid LaunchId;

	/** 当前阶段，只由生命周期、Hit 和 Wake/Sleep 回调写入。 */
	EThrustGuidedHazardProjectilePhase Phase =
		EThrustGuidedHazardProjectilePhase::Uninitialized;

	/** ConfigureLoaded 是否在 FinishSpawningActor 前完整注入。 */
	bool bLoadedConfigured = false;

	/** 是否已按 HeavyImpact 开关绑定预测球委托；EndPlay 只清理真实绑定。 */
	bool bPreparationBindingsActive = false;

	/** 每个 OnComponentHit 回调的递增序号，只用于诊断 Chaos 接触序列。 */
	uint32 ContactSequence = 0;

	/** 当前与预测球重叠且实现 IHeavyImpactReceiver 的 Actor。 */
	TSet<TWeakObjectPtr<AActor>> PreparationCandidates;

	/** 本次 LaunchId 已返回 Accepted/Duplicate 的接收者；Busy/Invalid 会继续重试。 */
	TSet<TWeakObjectPtr<AActor>> NotifiedReceiversThisLaunch;

	/** 醒着且 HeavyImpact 开启时以 60 Hz 采样的准备 Timer。 */
	FTimerHandle PreparationTimerHandle;
};
