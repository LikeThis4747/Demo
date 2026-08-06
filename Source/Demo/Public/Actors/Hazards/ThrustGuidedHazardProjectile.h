// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardProjectile.h
 * 职责：拥有一次发射的真实物理弹体、尾部 Thruster、短时制导、碰撞失导和重冲击准备预测。
 * 边界：不直接修改速度/Transform，不额外 AddTorque/AddImpulse，不决定玩家或追猎者受击结果。
 * 状态 Owner：本 Actor 唯一写入推进阶段、LaunchId、首次有效碰撞和每接收者通知集合。
 * 轴约定：胶囊局部 +Z 是弹体前向；UE5.8 UPhysicsThrusterComponent 局部 -X 是实际施力轴。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"

#include "ThrustGuidedHazardProjectile.generated.h"

class UCapsuleComponent;
class UPhysicsThrusterComponent;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class UThrustGuidedHazardTuningData;
struct FHeavyImpactPreparationRequest;

/** 项目内部的物理制导弹体阶段，不是 UE 官方 ProjectileMovement 状态。 */
enum class EThrustGuidedHazardProjectilePhase : uint8
{
	Uninitialized,
	PoweredGuided,
	PoweredUnguided,
	Coasting,
	Sleeping,
	Disabled
};

/** 用尾部真实施力完成短时制导，并在碰撞/计时后交给 Chaos 的一次性弹体。 */
UCLASS()
class DEMO_API AThrustGuidedHazardProjectile final : public AActor
{
	GENERATED_BODY()

public:
	/** 装配胶囊刚体、直接子级 Thruster、预测球和纯美术挂点；默认不模拟、不 Tick。 */
	AThrustGuidedHazardProjectile();

	/**
	 * 延迟生成期间注入本次唯一配置、弱目标和 LaunchId。
	 * 只保存输入，不启动物理；FinishSpawningActor 后由 BeginPlay 统一校验和启动。
	 */
	void ConfigureLaunch(
		UThrustGuidedHazardTuningData* InTuningData,
		USceneComponent* InTargetComponent,
		const FGuid& InLaunchId);

protected:
	/** 校验延迟生成合同，配置并启动刚体、Thruster、碰撞事件和预测 Timer。 */
	virtual void BeginPlay() override;

	/** 仅在 PoweredGuided/PoweredUnguided 更新推进计时和 Thruster 朝向。 */
	virtual void Tick(float DeltaSeconds) override;

	/** 清理碰撞/休眠/重叠委托、预测 Timer、目标和候选集合。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 将 DataAsset 的胶囊、预测球和 Thruster 尾部位置写入组件。 */
	void ApplyConfiguration(const UThrustGuidedHazardTuningData& Tuning);

	/** 检查锁定目标及其组件仍属于同一有效 Actor。 */
	bool IsLockedTargetUsable() const;

	/** 首次碰撞或目标失效时永久停止本次制导，但不停止推进计时。 */
	void StopGuidance(const TCHAR* Reason);

	/** 推进计时结束时关闭 Thruster、恢复重力、关闭 Actor Tick 并进入 Coasting。 */
	void FinishPoweredPhase();

	/**
	 * 用目标短时前置、朝向误差和角速度阻尼计算期望世界推力方向。
	 * 输出已经满足喷口最大偏角；失败时调用者转入 PoweredUnguided。
	 */
	bool TryCalculateGuidedForceDirection(FVector& OutWorldForceDirection) const;

	/**
	 * 以最大喷口转速把当前 Thruster 局部 -X 力轴转向期望世界方向。
	 * 只改变子组件相对旋转，不改刚体 Transform、速度或角速度。
	 */
	void AimThrusterAtWorldForceDirection(
		const FVector& DesiredWorldForceDirection,
		float DeltaSeconds);

	/** 启动 60 Hz 重冲击候选预测；Actor Tick 关闭后仍可在刚体醒着时工作。 */
	void StartPreparationMonitoring();

	/** 停止重冲击预测 Timer，但保留候选和已通知集合以支持后续物理唤醒。 */
	void StopPreparationMonitoring();

	/** 对当前重叠的 IHeavyImpactReceiver 估算 ETA，并按 Actor 独立发送一次准备请求。 */
	void EvaluatePreparationCandidates();

	/** 用胶囊真实表面、接收者真实预测组件和相对速度构造准备请求。 */
	bool BuildPreparationRequest(
		const AActor& Receiver,
		FHeavyImpactPreparationRequest& OutRequest);

	/** 返回从胶囊中心沿世界方向射线到当前胶囊表面的精确距离，单位 cm。 */
	static float CalculateCapsuleRaySurfaceDistance(
		const UCapsuleComponent& Capsule,
		const FVector& WorldDirection);

	/** 关闭所有动力和碰撞并记录明确错误；仅用于无法安全运行的生命周期/配置失败。 */
	void DisableProjectile(const FString& Reason);

	/** 记录每个 Chaos Hit；首次非出生穿透、非 Owner 的阻挡碰撞会永久停止制导。 */
	UFUNCTION()
	void HandleProjectileHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** 刚体从 Sleeping 被真实物理交互唤醒时恢复 Coasting 预测。 */
	UFUNCTION()
	void HandleProjectileWake(UPrimitiveComponent* WakingComponent, FName BoneName);

	/** Coasting 刚体休眠时停止预测 Timer，不销毁弹体。 */
	UFUNCTION()
	void HandleProjectileSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);

	/** 只登记进入预测球且实现 IHeavyImpactReceiver 的 Actor。 */
	UFUNCTION()
	void HandlePreparationVolumeBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** Actor 的最后一个组件离开预测球后移除候选，避免 Capsule/Mesh 交替造成漏通知。 */
	UFUNCTION()
	void HandlePreparationVolumeEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	/** 唯一模拟物理的根胶囊；局部 +Z 是弹体纵轴，阻挡世界和 PhysicsBody。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCapsuleComponent> ProjectileBody;

	/**
	 * 必须直接附着 ProjectileBody；UE5.8 Thruster 只向直接父 UPrimitiveComponent 施力。
	 * 组件位于局部 -Z 尾部，局部 -X 是实际世界推力轴。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsThrusterComponent> Thruster;

	/** ProjectileBody 下的纯美术挂点；Blueprint 装配气罐/导弹网格且网格碰撞必须关闭。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> BodyVisualRoot;

	/** Thruster 下的纯美术尾焰挂点；随喷口偏转，C++ 只控制可见性。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ExhaustVisualRoot;

	/** 随弹体移动的 Query-only Pawn 预测球；不阻挡、不施力、不影响导航。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导|重冲击",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> PreparationVolume;

	/** 延迟生成注入并在整个弹体生命周期持有的唯一配置资产。 */
	UPROPERTY(Transient)
	TObjectPtr<UThrustGuidedHazardTuningData> RuntimeTuningData;

	/** 推进阶段弱引用的目标组件；首次碰撞、目标失效或推进结束时清空。 */
	TWeakObjectPtr<USceneComponent> LockedTargetComponent;

	/** 与 LockedTargetComponent 对应的 Actor，用于拒绝组件易主或销毁。 */
	TWeakObjectPtr<AActor> LockedTargetActor;

	/** 本次发射的稳定 ID；用于日志和每个 HeavyImpact 接收者的独立去重。 */
	FGuid LaunchId;

	/** 当前阶段，只由本 Actor 生命周期、Tick、Hit 和 Wake/Sleep 回调写入。 */
	EThrustGuidedHazardProjectilePhase Phase =
		EThrustGuidedHazardProjectilePhase::Uninitialized;

	/** 推进开始后累计秒数；首次碰撞不会重置或提前结束。 */
	float PoweredElapsedSeconds = 0.0f;

	/** ConfigureLaunch 已被延迟生成调用；BeginPlay 仍会分别校验每个输入。 */
	bool bLaunchConfigured = false;

	/** 本次是否已经出现首次有效阻挡碰撞；之后仍完整记录并允许真实重复接触。 */
	bool bHadMeaningfulBlockingContact = false;

	/** 本次弹体每个 OnComponentHit 回调的递增序号，只用于诊断 Chaos 接触序列。 */
	uint32 ContactSequence = 0;

	/** 当前仍与预测球重叠、且实现 IHeavyImpactReceiver 的 Actor。 */
	TSet<TWeakObjectPtr<AActor>> PreparationCandidates;

	/** 本次 LaunchId 已返回 Accepted/Duplicate 的接收者；Busy/Invalid 会继续重试。 */
	TSet<TWeakObjectPtr<AActor>> NotifiedReceiversThisLaunch;

	/** 醒着时以 60 Hz 采样的重冲击预测 Timer；不负责推进或制导。 */
	FTimerHandle PreparationTimerHandle;
};
