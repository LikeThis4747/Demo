// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PhysicsControlHitResponseComponent.h
 * 职责：拥有追猎者短时局部 Physics Control 受击状态、接触批次合并和恢复 Timer。
 * 边界：Capsule 继续作为移动权威；本组件不管理 AI、攻击、伤害、死亡、倒地或全身 Ragdoll。
 * 状态 Owner：唯一拥有待处理最强接触和当前活动语义区域。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Data/Physics/PhysicsControlHitTuningData.h"
#include "Engine/EngineTypes.h"
#include "Engine/TimerHandle.h"

#include "PhysicsControlHitResponseComponent.generated.h"

class AActor;
class UPhysicsControlComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;

/** 无自定义 Tick 的事件驱动局部受击组件。 */
UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class DEMO_API UPhysicsControlHitResponseComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建默认关闭 Tick 的局部受击组件。 */
	UPhysicsControlHitResponseComponent();

	/**
	 * 由 Owner 显式注入受控 Mesh、官方 Physics Control 组件和唯一调参资产。
	 * 任一输入无效时 BeginPlay 记录错误并停用，不按名称或路径搜索兜底。
	 */
	void Configure(
		USkeletalMeshComponent* InControlledMesh,
		UPhysicsControlComponent* InPhysicsControl,
		UPhysicsControlHitTuningData* InTuningData);

protected:
	/** 验证注入依赖、创建肢体 Controls/Modifiers，并绑定 Mesh 命中事件。 */
	virtual void BeginPlay() override;

	/** 解绑命中事件、清理 Timer、恢复活动区域并销毁本组件创建的 Controls/Modifiers。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 单个启用区域的运行时 Set 名、倍率和 Physics Body 骨骼列表。 */
	struct FRegionRuntime
	{
		FName LimbName;
		FName ParentControlSet;
		float ImpulseScale = 1.0f;
		TArray<FName> BodyBones;
	};

	/** 同一 Chaos 分发批次中按缩放前分数保留的最强有效身体接触。 */
	struct FPendingHit
	{
		EPhysicsControlHitRegion Region = EPhysicsControlHitRegion::Torso;
		FName BodyBone;
		FVector ImpactPoint = FVector::ZeroVector;
		FVector WorldImpulse = FVector::ZeroVector;
		float Score = 0.0f;
	};

	/** 从调参资产建立 limb、Control/Modifier Set 和 Physics Body 到语义区域映射。 */
	bool InitializePhysicsControl();

	/** 销毁本组件创建的 Physics Control 对象并清空全部运行态。 */
	void ResetRuntimeSetup();

	/** 下一帧处理当前 Chaos 批次最强命中，切换活动区域、施加冲量并刷新恢复 Timer。 */
	void ProcessPendingHit();

	/** 将指定区域切换为 Simulated 并启用其 ParentSpace Controls。 */
	void ActivateRegion(EPhysicsControlHitRegion Region, const FRegionRuntime& Runtime);

	/** 关闭当前区域控制与模拟、清除残余速度并恢复动画权威。 */
	void RestoreActiveRegion();

	/** 只接收其他真实模拟物理组件对 Manny Physics Body 的有效 Chaos 命中。 */
	UFUNCTION()
	void HandleMeshHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** 唯一调参资产；由 Owner 在组件初始化后注入，BeginPlay 起只读。 */
	UPROPERTY(Transient)
	TObjectPtr<UPhysicsControlHitTuningData> TuningData;

	/** 接收 Physics Body 命中并执行局部模拟的 Skeletal Mesh；由 Owner 注入。 */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ControlledMesh;

	/** 创建和更新 Controls/Body Modifiers 的官方组件；由 Owner 注入。 */
	UPROPERTY(Transient)
	TObjectPtr<UPhysicsControlComponent> PhysicsControl;

	/** BeginPlay 建立、EndPlay 清空的启用区域运行时数据。 */
	TMap<EPhysicsControlHitRegion, FRegionRuntime> RegionRuntimes;

	/** Physics Asset Body 骨骼到启用语义区域的唯一映射。 */
	TMap<FName, EPhysicsControlHitRegion> BodyBoneToRegion;

	/** 当前唯一处于 Simulated 的区域；恢复完成后为空。 */
	TOptional<EPhysicsControlHitRegion> ActiveRegion;

	/** 当前 Chaos 批次的最强有效接触；下一帧处理后重置。 */
	FPendingHit PendingHit;

	/** 将同一 Chaos 分发批次合并到下一帧处理的单次 Timer。 */
	FTimerHandle PendingHitTimer;

	/** 每次有效命中刷新；到期后恢复动画权威。 */
	FTimerHandle RecoveryTimer;
};
