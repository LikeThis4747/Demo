// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticObjectComponent.h
 * 职责：声明磁性道具身份，并独占一次正式投掷的碰撞快照、Hit、Tag、Timer 与恢复。
 * 边界：不保存玩家全局抓取手感、不制造投掷冲量，也不决定 Light 或破碎消费者的最终表现。
 * 状态 Owner：本组件是单个道具正式投掷碰撞事务的唯一写入者；消费者只能监听原生命中委托。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Engine/EngineTypes.h"
#include "Engine/TimerHandle.h"

#include "MagneticObjectComponent.generated.h"

class AActor;
class UCharacterImpactSourceProfile;
class UMagneticGrabTuningData;
class UMaterialInterface;
class UMeshComponent;
class UPrimitiveComponent;

/** 正式投掷事务过滤投掷者后，为墙体、道具和角色统一广播的原生阻挡命中。 */
DECLARE_MULTICAST_DELEGATE_SevenParams(
	FOnMagneticThrownBlockingHit,
	UPrimitiveComponent*,
	AActor*,
	UPrimitiveComponent*,
	const FVector&,
	const FHitResult&,
	bool,
	float);

/** 给 Actor 添加可磁吸标记，并提供选取优先级、投掷倍率与共享命中事务。 */
UCLASS(ClassGroup = (Magnetism), meta = (BlueprintSpawnableComponent))
class DEMO_API UMagneticObjectComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的事件驱动配置组件；正式投掷事务只在输入触发时存在。 */
	UMagneticObjectComponent();

	/** 检查磁性标记、刚体模拟状态和全局质量上限，决定候选组件是否可抓取。 */
	bool CanGrab(const UPrimitiveComponent* CandidateComponent, float MaxAllowedMass) const;

	/**
	 * 释放 Physics Handle 后、施加投掷冲量前，用精确 Primitive 建立一次可回滚事务。
	 * LightActiveDurationSeconds 控制角色轻受击窗口；MaximumBreakMonitoringSeconds 为 0 时不延长破碎监听。
	 */
	bool ArmThrownImpact(
		UPrimitiveComponent* ThrownPrimitive,
		AActor* Thrower,
		float LightActiveDurationSeconds,
		float MaximumBreakMonitoringSeconds = 0.0f,
		UMagneticGrabTuningData* TuningData = nullptr,
		UMagneticGrabTuningData* ExplosionTuning = nullptr);

	/** 幂等结束当前投掷事务并精确恢复该 Primitive 的全部碰撞快照。 */
	void DisarmThrownImpact();

	/** 设置或恢复精确持有/投掷网格的爆裂 Overlay；不改变原基础材质。 */
	void SetExplosionPresentationActive(
		UPrimitiveComponent* TargetPrimitive,
		UMaterialInterface* OverlayMaterial);

	/** 返回当前是否仍由本组件持有正式投掷碰撞事务。 */
	bool IsThrownImpactArmed() const { return ArmedPrimitive.IsValid(); }

	/** 返回通用阻挡命中原生委托；监听者不得在回调中改写本组件碰撞快照。 */
	FOnMagneticThrownBlockingHit& OnThrownBlockingHit() { return ThrownBlockingHit; }

	/**
	 * 对应 C++ 属性 bMagnetizable；初始值：true。
	 * 关闭后该 Actor 不进入磁力候选，但不会修改碰撞或 Chaos 模拟状态。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体")
	bool bMagnetizable = true;

	/**
	 * 对应 C++ 属性 SelectionPriority，由 FindBestCandidate 处理重叠候选，无单位。
	 * 初始值：1；编辑范围：0~10。调高更容易在重叠轮廓中胜出，调低则让其他道具优先。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0"))
	float SelectionPriority = 1.0f;

	/**
	 * 对应 C++ 属性 ThrowSpeedMultiplier，由 ThrowHeldObject 乘到全局 ThrowSpeed，无单位。
	 * 初始值：1；编辑范围：0.1~3。调高让特殊轻型道具飞得更快，调低可表现重型或吸能道具。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体", meta = (ClampMin = "0.1", ClampMax = "3.0", UIMin = "0.1", UIMax = "3.0"))
	float ThrowSpeedMultiplier = 1.0f;

	/** 本类磁力物作为正式投掷物时使用的站立轻受击来源映射。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体|轻受击")
	TObjectPtr<UCharacterImpactSourceProfile> StandingImpactSourceProfile = nullptr;

protected:
	/** Owner 或 World 结束时清除全部 Timer、委托绑定并恢复碰撞快照。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 接收唯一根刚体的真实 Hit，先广播通用命中，再按 Light 窗口提交角色请求。 */
	UFUNCTION()
	void HandleArmedPrimitiveHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** Light 窗口自然到期；若仍需破碎监听，仅恢复攻击身份并保留 Hit/CCD。 */
	void HandleLightWindowExpired();

	/** Light 已被角色消费后的 next-tick 收口，避免在受击回调同帧改写来源身份。 */
	void HandleDeferredLightResolved();

	/** 破碎监听达到硬上限后完整结束投掷事务，避免长期潜伏成随机破碎。 */
	void HandleMaximumMonitoringExpired();

	/** 对 Pawn 做一次 Actor 去重范围查询，并结算距离衰减伤害与独立径向 Heavy。 */
	void TriggerExplosion(const FVector& ExplosionOrigin);

	/** 在真实爆点生成核心、范围外圈与短时火焰烟雾，不依赖原道具继续存活。 */
	void SpawnExplosionPresentation(
		const FVector& ExplosionOrigin,
		const UMagneticGrabTuningData& ExplosionTuning);

	/** 恢复红光启用前的 Overlay，并清空唯一表现目标。 */
	void RestoreExplosionPresentation();

	/** 恢复原 ObjectType、Responses、Profile 与 Tag，同时继续临时保留 Hit 通知和 CCD。 */
	void RestoreAttackIdentityButKeepHitMonitoring();

	/** 本组件在正式投掷期间覆盖的全部可恢复碰撞与 Tag 状态。 */
	struct FThrownImpactCollisionSnapshot
	{
		bool bValid = false;
		FName CollisionProfileName = NAME_None;
		TEnumAsByte<ECollisionEnabled::Type> CollisionEnabled = ECollisionEnabled::NoCollision;
		TEnumAsByte<ECollisionChannel> ObjectType = ECC_PhysicsBody;
		FCollisionResponseContainer Responses;
		bool bNotifyRigidBodyCollision = false;
		bool bUseCCD = false;
		bool bOwnerHadAttackProjectileTag = false;

		void Reset() { *this = FThrownImpactCollisionSnapshot(); }
	};

	/** 当前由本组件临时改写并绑定 Hit 的精确刚体；完整 Disarm 后清空。 */
	TWeakObjectPtr<UPrimitiveComponent> ArmedPrimitive;

	/** 本次投掷者；只用于过滤出手瞬间和飞行中的自碰撞。 */
	TWeakObjectPtr<AActor> ActiveThrower;

	/** 当前 Light 请求的唯一标识；每次正式投掷重新生成。 */
	FGuid ActiveImpactId;

	/** 仅爆裂投掷持有现有磁力 DA；普通投掷保持为空，不读取任何爆裂参数。 */
	TWeakObjectPtr<UMagneticGrabTuningData> ActiveTuningData;
	TWeakObjectPtr<UMagneticGrabTuningData> ActiveExplosionTuning;

	/** 当前被红光 Overlay 覆盖的精确网格；持有切换与投掷事务共享同一份状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UMeshComponent> ExplosionPresentationMesh;

	/** 红光启用前的 Overlay；退出任意爆裂路径时原样恢复。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PreviousExplosionOverlayMaterial = nullptr;

	/** 正式投掷前的唯一恢复基线；Light 与破碎不得各存一份。 */
	FThrownImpactCollisionSnapshot CollisionSnapshot;

	/** Light 与破碎共同消费的同步原生命中委托；由组件生命周期管理监听。 */
	FOnMagneticThrownBlockingHit ThrownBlockingHit;

	/** Light 有效窗口 Timer；到期后恢复攻击身份或完整 Disarm。 */
	FTimerHandle ActiveDurationTimerHandle;

	/** 角色 Light 已消费后的 next-tick 收口 Timer。 */
	FTimerHandle DeferredLightResolutionTimerHandle;

	/** 从出手开始计算的破碎监听硬上限 Timer。 */
	FTimerHandle MaximumMonitoringTimerHandle;

	/** 为 true 时本次仍允许构造一次角色 Light 请求。 */
	bool bLightImpactWindowActive = false;

	/** 为 true 时第一次合格 Blocking Hit 改走爆炸，不再叠加普通 Light。 */
	bool bExplosionImpactWindowActive = false;

	/** 为 true 时 Light 结束后继续由同一 Hit/CCD 事务等待破碎消费者。 */
	bool bKeepMonitoringForBreak = false;

	/** 为 true 时本次 Light 已经提交，不允许同一投掷重复提交角色响应。 */
	bool bImpactConsumed = false;
};
