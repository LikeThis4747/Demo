// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticThrowBreakComponent.h
 * 职责：声明磁力物正式投掷后首次合格命中的破碎替换状态与配置。
 * 边界：不绑定 Primitive Hit、不控制 CCD/碰撞快照、不计算投掷速度，也不让 Geometry Collection 参与抓取。
 * 状态 Owner：本组件只独占 Ready、BreakQueued、Consumed、BreakFailed 替换状态。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/TimerHandle.h"

#include "MagneticThrowBreakComponent.generated.h"

class AActor;
class AMagneticFractureActor;
class UMagneticObjectComponent;
class UPrimitiveComponent;

/** 项目内部破碎替换状态；不代表 UE Geometry Collection 的官方状态。 */
UENUM()
enum class EMagneticThrowBreakState : uint8
{
	Ready,
	BreakQueued,
	Consumed,
	BreakFailed
};

/** 订阅共享正式投掷 Hit，并把完整磁力物安全替换为短命 Geometry Collection。 */
UCLASS(ClassGroup = (Magnetism), meta = (BlueprintSpawnableComponent))
class DEMO_API UMagneticThrowBreakComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的事件驱动破碎消费者。 */
	UMagneticThrowBreakComponent();

	/**
	 * 正式投掷前返回本物体需要的最长破碎监听时间，单位 s。
	 * 配置为空、Body 不匹配或状态不可用时返回 0，不影响既有 Light 投掷。
	 */
	float GetFormalThrowMonitoringSeconds(const UPrimitiveComponent* CandidateBody) const;

	/** BreakQueued/Consumed 时拒绝重新抓取；其他状态允许抓取入口完整 Disarm。 */
	bool CanBeginGrab(const UPrimitiveComponent* CandidateBody) const;

protected:
	/** 校验根刚体和配置，并向 MagneticObject 的原生命中委托注册一次监听。 */
	virtual void BeginPlay() override;

	/** 结束时清 next-tick Timer 并移除原生委托，防止跨生命周期回调。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 接收共享事务已过滤投掷者的 Blocking Hit，并按冲量门槛只排队一次。 */
	void HandleThrownBlockingHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		const FVector& NormalImpulse,
		const FHitResult& Hit);

	/** 在 Hit 后 next-tick 继承运动、生成替身，并仅在成功后销毁完整 Actor。 */
	void ProcessQueuedBreak();

	/** 清空只属于当前待处理命中的瞬时刚体数据，防止失败或 EndPlay 后误复用。 */
	void ResetPendingBreakData();

	/** 记录可定位错误、保留完整 Actor、结束共享投掷事务并禁止本实例重复失败。 */
	void FailAndPreserveOriginal(const TCHAR* Reason);

	/** 统一写入项目内部状态并输出低噪声 Verbose 调试信息。 */
	void SetState(EMagneticThrowBreakState NewState, const TCHAR* Context);

	/**
	 * 命中后生成的项目破碎替身 Blueprint 类；为空表示该磁力物有意不破碎。
	 * Blueprint 只应选择 AMagneticFractureActor 派生类，不在 EventGraph 重复实现替换。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AMagneticFractureActor> FractureActorClass;

	/**
	 * 首次合格 Blocking Hit 的最小 NormalImpulse 大小，单位 kg*cm/s。
	 * 初始值 5000；调高减少轻擦误碎，调低让弱投掷更容易消耗。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0"))
	float MinimumBreakNormalImpulse = 5000.0f;

	/**
	 * 将 NormalImpulse / 质量反推的碰撞法向速度保留比例；初始值 0.6。
	 * 该速度在真实 Hit 回调中冻结，避免 next-tick 读取到已被 Chaos 阻挡归零的速度。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float ImpactVelocityRetention = 0.6f;

	/**
	 * 替身可继承的最大线速度，单位 cm/s；初始值 5000。
	 * 只作为异常冲量兜底，不会主动把正常投掷加速到该值。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true", ClampMin = "100.0", UIMin = "100.0", Units = "cm/s"))
	float MaximumInheritedLinearSpeed = 5000.0f;

	/**
	 * 从正式出手开始计算的破碎监听硬上限，单位 s；初始值 8。
	 * 必须大于 Light 窗口，避免未命中的物体在很久以后随机碎裂。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true", ClampMin = "0.1", UIMin = "0.1", Units = "s"))
	float MaximumMonitoringSeconds = 8.0f;

	/** BeginPlay 找到的唯一正式投掷事务组件；只读取委托并在失败时请求 Disarm。 */
	TWeakObjectPtr<UMagneticObjectComponent> MagneticObject;

	/** Owner 的模拟物理根刚体；只有这个 Body 的共享命中可以触发破碎。 */
	TWeakObjectPtr<UPrimitiveComponent> MonitoredBody;

	/** BreakQueued 后保留到 next-tick 的精确刚体弱引用。 */
	TWeakObjectPtr<UPrimitiveComponent> PendingBody;

	/** 首次合格 Hit 当帧冻结的替身线速度；只由 HandleThrownBlockingHit 写入并由替换事务消费。 */
	FVector PendingLinearVelocity = FVector::ZeroVector;

	/** 首次合格 Hit 当帧冻结的弧度制角速度；只由 HandleThrownBlockingHit 写入并由替换事务消费。 */
	FVector PendingAngularVelocityRadians = FVector::ZeroVector;

	/** 注册到 MagneticObject 原生委托的句柄；EndPlay 精确移除。 */
	FDelegateHandle ThrownHitDelegateHandle;

	/** 合格命中安排的唯一 next-tick 替换 Timer。 */
	FTimerHandle DeferredBreakTimerHandle;

	/** 当前替换状态，只由 SetState 和构造期默认值写入。 */
	EMagneticThrowBreakState State = EMagneticThrowBreakState::Ready;

	/** BeginPlay 完成根刚体、类、阈值和时间校验后为 true。 */
	bool bConfigurationValid = false;
};
