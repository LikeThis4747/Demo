// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactResponseComponent.h
 * 职责：唯一拥有角色重冲击准备、真实接触提交、全身物理飞行、跟随与落地判稳状态。
 * 边界：不制造冲量、不播放受击或起身动画，不依赖具体玩家、AI 或机关类型。
 * 状态 Owner：唯一写入 EHeavyImpactState 并唯一管理本组件创建的 Physics Control 记录。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Physics/HeavyImpactTypes.h"

#include "HeavyImpactResponseComponent.generated.h"

class AActor;
class ACharacter;
class UCharacterMovementComponent;
class UCapsuleComponent;
class UHeavyImpactTuningData;
class UPhysicsControlComponent;
class UPrimitiveComponent;
class USceneComponent;
class USkeletalMeshComponent;

/** 通知角色适配层物理状态发生变化。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnHeavyImpactStateChanged,
	EHeavyImpactState /* Previous */,
	EHeavyImpactState /* Current */);

/** 只在指定重物发生真实 Chaos 接触后广播一次。 */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnHeavyImpactCommitted,
	const FHeavyImpactPreparationRequest&);

/** 单个 Physics Asset Body 在进入 Prepared 前的可恢复属性。 */
struct FHeavyImpactBodySnapshot
{
	FName BoneName = NAME_None;
	bool bWasSimulating = false;
	bool bNotifyRigidBodyCollision = false;
	bool bUseCCD = false;
	float PhysicsBlendWeight = 0.0f;
	TEnumAsByte<ECollisionEnabled::Type> CollisionEnabled = ECollisionEnabled::NoCollision;
};

/** 仅供“预测到了但没有命中”事务恢复的角色快照。 */
struct FHeavyImpactPreContactSnapshot
{
	bool bValid = false;
	FTransform ActorTransform = FTransform::Identity;
	FVector CharacterVelocity = FVector::ZeroVector;
	TEnumAsByte<EMovementMode> MovementMode = MOVE_None;
	uint8 CustomMovementMode = 0;

	TWeakObjectPtr<USceneComponent> MeshAttachParent;
	FName MeshAttachSocket = NAME_None;
	FTransform MeshRelativeTransform = FTransform::Identity;
	bool bMeshPauseAnims = false;
	TEnumAsByte<ECollisionEnabled::Type> MeshCollisionEnabled = ECollisionEnabled::NoCollision;
	TEnumAsByte<ECollisionChannel> MeshObjectType = ECC_Pawn;
	FCollisionResponseContainer MeshResponses;
	bool bMeshBodyInstanceNotify = false;
	bool bMeshBodyInstanceCCD = false;

	TEnumAsByte<ECollisionEnabled::Type> CapsuleCollisionEnabled = ECollisionEnabled::NoCollision;
	FCollisionResponseContainer CapsuleResponses;

	TArray<FHeavyImpactBodySnapshot> Bodies;

	/** 清除全部临时引用和值。 */
	void Reset()
	{
		*this = FHeavyImpactPreContactSnapshot();
	}
};

/** 玩家和追猎者共用的权威重冲击物理响应组件。 */
UCLASS(ClassGroup = (Physics))
class DEMO_API UHeavyImpactResponseComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建仅在物理事务期间启用的 PostPhysics Tick。 */
	UHeavyImpactResponseComponent();

	/**
	 * 由角色显式注入真实组件和配置；禁止按名字查找 Mesh、Capsule 或 Movement。
	 * 必须在角色 PostInitializeComponents 中调用，早于 BeginPlay。
	 */
	void Configure(
		ACharacter* InCharacter,
		USkeletalMeshComponent* InMesh,
		UCapsuleComponent* InCapsule,
		UCharacterMovementComponent* InMovement,
		UPhysicsControlComponent* InPhysicsControl,
		UHeavyImpactTuningData* InTuning);

	/** 接受机关预测请求；外部不能通过本组件直接击飞角色。 */
	EHeavyImpactPrepareResult PrepareForImpact(const FHeavyImpactPreparationRequest& Request);

	/** 返回当前项目重冲击状态。 */
	EHeavyImpactState GetState() const { return State; }

	/** Prepared 到 Recovering 均会阻止角色身体输入或 AI 行为。 */
	bool IsBusy() const { return State != EHeavyImpactState::Inactive; }

	/** 指定重物已真实命中后返回 true；Prepared 误判不算提交。 */
	bool IsCommitted() const
	{
		return State == EHeavyImpactState::Simulating
			|| State == EHeavyImpactState::Settling
			|| State == EHeavyImpactState::Downed
			|| State == EHeavyImpactState::Recovering;
	}

	/** 原生状态变化通知；组件仍是唯一状态写入者。 */
	FOnHeavyImpactStateChanged OnStateChanged;

	/** 真实接触提交通知；玩家用它释放磁力物体。 */
	FOnHeavyImpactCommitted OnImpactCommitted;

protected:
	/** 验证配置并提前创建本组件独占的 PCA 运行时记录。 */
	virtual void BeginPlay() override;

	/** 恢复未提交事务、解绑命中并精确销毁本组件拥有的记录。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 在 PostPhysics 跟随骨盆、判稳并完成延迟一帧的 Downed 睡眠。 */
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 只用真实 Mesh Hit 提交预期机关接触，Downed 时允许真实动态刚体再次唤醒。 */
	UFUNCTION()
	void HandleMeshHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** 从已编译 PCA 创建并记录独占 Controls/BodyModifiers。 */
	bool InitializePhysicsControlAuthority(FText& OutError);

	/** 校验请求能在准备窗口内与 Mesh 发生真实 PhysicsBody 阻挡。 */
	bool ValidatePreparationRequest(
		const FHeavyImpactPreparationRequest& Request,
		FString& OutReason,
		float& OutAllowedMaximumSeconds) const;

	/** 捕获 Prepared 误判时必须完整恢复的角色、碰撞和刚体属性。 */
	bool CapturePreContactSnapshot(FString& OutReason);

	/** 在同一调用栈停止角色驱动、脱离 Mesh 并启用全身物理。 */
	bool EnterPrepared(
		const FHeavyImpactPreparationRequest& Request,
		float AllowedMaximumSeconds,
		FString& OutReason);

	/** 将预期刚体的真实接触提交为物理飞行，不补第二份冲量。 */
	void CommitRealImpact(const FHitResult& Hit, const FVector& NormalImpulse);

	/** 从 Downed 的真实动态刚体接触恢复物理求解，不补冲量。 */
	void ResumeFromDownedHit(const FHitResult& Hit, const FVector& NormalImpulse);

	/** 取消未提交的 Prepared，并恢复唯一允许使用受击前 Transform 的快照。 */
	void CancelUncommittedPreparation(const TCHAR* Reason);

	/** 让直立 Capsule/Actor 外壳跟随脱离后的真实骨盆位置和水平朝向。 */
	void UpdatePhysicalFollow(
		float DeltaTime,
		bool bSnap = false,
		bool bUseGroundedCapsulePlacement = false);

	/** 用骨盆速度、可行走地面和持续时间判断落地稳定。 */
	void UpdateStability(float DeltaTime);

	/** 从骨盆向下查找可行走地面，拒绝墙面和机关自身。 */
	bool TryGetGroundSupport(FHitResult& OutGroundHit) const;

	/** 调用必须存在的项目 PCA Profile，并明确记录失败。 */
	bool InvokeRequiredProfile(FName ProfileName);

	/** 在坏物理超时或 Profile 失败时关闭姿态控制，保持自由物理。 */
	void EnterFreeFallback(const TCHAR* Reason);

	/** 对齐真实落点并排队到下一帧完成无 Tick 的 Downed 睡眠。 */
	void EnterDowned(const TCHAR* Reason);

	/** 在 FreeFallback 已经过一次 PrePhysics 后睡眠，并关闭两个物理 Tick。 */
	void FinishPendingDownedSleep();

	/** 恢复 Prepared 前的全部角色外壳和 Body 属性。 */
	void RestoreSnapshotAfterFalsePositive();

	/** 恢复每个 Physics Asset Body 的原始属性。 */
	void RestoreBodySnapshot();

	/** 只销毁本组件创建并记录的 Controls/BodyModifiers。 */
	void DestroyOwnedPhysicsControlRecords();

	/** 唯一状态写入口。 */
	void SetState(EHeavyImpactState NewState);

	/** 将已接受 ID 写入固定长度去重缓存。 */
	void RecordRecentImpactId(const FGuid& Id);

	/** 查询 ID 是否已经通过一次有效准备校验。 */
	bool HasSeenImpactId(const FGuid& Id) const;

	/** 为同一无效 ID 的重复预测只记录一次 Warning。 */
	bool ShouldLogRejectedImpact(const FGuid& Id);

	/** 显式注入的角色 Owner。 */
	UPROPERTY(Transient)
	TObjectPtr<ACharacter> Character = nullptr;

	/** 脱离 Capsule 后承受真实 Chaos 接触的 Skeletal Mesh。 */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh = nullptr;

	/** 仅作为查询和游戏逻辑外壳、跟随骨盆但不复制翻滚。 */
	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> Capsule = nullptr;

	/** Prepared 起停止，首版 Downed 后不恢复。 */
	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> Movement = nullptr;

	/** 创建 PCA 记录并在 PrePhysics 应用项目 Profile。 */
	UPROPERTY(Transient)
	TObjectPtr<UPhysicsControlComponent> PhysicsControl = nullptr;

	/** 角色专属 PCA 与时序阈值。 */
	UPROPERTY(Transient)
	TObjectPtr<UHeavyImpactTuningData> Tuning = nullptr;

	/** Prepared 期间唯一允许提交状态的真实机关 Actor。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> ExpectedSourceActor = nullptr;

	/** Prepared 期间唯一允许提交状态的真实机关刚体。 */
	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> ExpectedSourceComponent = nullptr;

	FHeavyImpactPreparationRequest ActiveRequest;
	FHeavyImpactPreContactSnapshot Snapshot;
	TArray<FName> OwnedControlNames;
	TArray<FName> OwnedBodyModifierNames;
	TArray<FGuid> RecentImpactIds;
	TArray<FGuid> LoggedRejectedImpactIds;

	EHeavyImpactState State = EHeavyImpactState::Inactive;
	float StateElapsedSeconds = 0.0f;
	float TotalCommittedSeconds = 0.0f;
	float StableElapsedSeconds = 0.0f;
	float ActorToPelvisZ = 0.0f;
	float ActivePreparationTimeoutSeconds = 0.0f;
	uint64 PreparedEntryFrame = 0;
	bool bConfigured = false;
	bool bInitialized = false;
	bool bFreeFallbackInvoked = false;
	bool bPendingDownedSleep = false;
	bool bHardTimeoutReported = false;
};
