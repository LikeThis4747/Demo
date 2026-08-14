// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactResponseComponent.h
 * 职责：唯一拥有角色重冲击准备、真实接触提交、全身物理飞行、跟随与落地判稳状态。
 * 边界：不制造冲量、不播放受击动画；只在安全落点确定后协调共享起身动画，不依赖具体玩家、AI 或机关类型。
 * 状态 Owner：唯一写入 EHeavyImpactState 并唯一管理本组件创建的 Physics Control 记录。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Physics/HeavyImpactTypes.h"

#include "HeavyImpactResponseComponent.generated.h"

class AActor;
class ACharacter;
class UAnimMontage;
class UAnimSequenceBase;
class UCharacterMovementComponent;
class UCapsuleComponent;
class UHeavyImpactAnimInstance;
class UHeavyImpactTuningData;
class UPhysicsControlComponent;
class UPrimitiveComponent;
class USceneComponent;
class USkeletalMeshComponent;
struct FHeavyImpactControlStageTuning;
struct FPoseSnapshot;

/** 通知角色适配层物理状态发生变化。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnHeavyImpactStateChanged,
	EHeavyImpactState /* Previous */,
	EHeavyImpactState /* Current */);

/** 只在指定重物发生真实 Chaos 接触后广播一次。 */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnHeavyImpactCommitted,
	const FHeavyImpactPreparationRequest&);

/** 请求已通过 Heavy 校验、即将捕获接触前身体状态；同步订阅者必须立即释放局部身体写入。 */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnHeavyImpactPreContactCaptureRequested,
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

/** 仅供返回 Accepted 前的准备转换失败恢复；Accepted 后立即销毁。 */
struct FHeavyImpactPreparationRollback
{
	bool bValid = false;
	FVector CharacterVelocity = FVector::ZeroVector;
	TEnumAsByte<EMovementMode> MovementMode = MOVE_None;
	uint8 CustomMovementMode = 0;

	void Reset()
	{
		*this = FHeavyImpactPreparationRollback();
	}
};

/** Character shell/body configuration retained until a committed impact has recovered. */
struct FHeavyImpactRecoveryBaseline
{
	bool bValid = false;
	/** Heavy 准备前的有效 Character/Capsule 世界变换；优先作为安全搜索种子，也是最后的玩法恢复锚点。 */
	FTransform PreImpactCharacterTransform = FTransform::Identity;
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
		*this = FHeavyImpactRecoveryBaseline();
	}
};

/** 玩家和追猎者共用的权威重冲击物理响应组件。 */
enum class EHeavyImpactRecoveryPhase : uint8
{
	None,
	WaitingForSpace,
	BlendingSnapshotToMontage,
	PlayingMontage
};

struct FHeavyImpactRecoveryPlan
{
	bool bFaceUp = true;
	FVector CapsuleLocation = FVector::ZeroVector;
	FRotator CapsuleRotation = FRotator::ZeroRotator;
	TObjectPtr<UAnimSequenceBase> Animation = nullptr;
	float AnimationStartTimeSeconds = 0.0f;
};

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

	/** 仅在有效 Heavy 请求捕获身体前广播；Busy、Duplicate 和 Invalid 请求不会触发。 */
	FOnHeavyImpactPreContactCaptureRequested OnPreContactCaptureRequested;

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
	/** 只用真实 Mesh Hit 提交预期机关接触；Downed 时拒绝同一来源再次把恢复打断。 */
	UFUNCTION()
	void HandleMeshHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** Abort a recovery immediately if the Mesh rebuilds its AnimInstance mid-transaction. */
	UFUNCTION()
	void HandleAnimInitialized();

	/** 从已编译 PCA 创建并记录独占 Controls/BodyModifiers。 */
	bool InitializePhysicsControlAuthority(FText& OutError);

	/** 校验请求能在准备窗口内与 Mesh 发生真实 PhysicsBody 阻挡。 */
	bool ValidatePreparationRequest(
		const FHeavyImpactPreparationRequest& Request,
		FString& OutReason,
		float& OutAllowedMaximumSeconds) const;

	/** 捕获 Prepared 误判时必须完整恢复的角色、碰撞和刚体属性。 */
	bool CapturePreContactState(FString& OutReason);

	/** 在同一调用栈停止角色驱动、脱离 Mesh 并启用全身物理。 */
	bool EnterPrepared(
		const FHeavyImpactPreparationRequest& Request,
		float AllowedMaximumSeconds,
		FString& OutReason);

	/** 将预期刚体的真实接触提交为物理飞行，不补第二份冲量。 */
	void CommitRealImpact(const FHitResult& Hit, const FVector& NormalImpulse);
	/** Accepted 后的统一提交入口；无 Hit 的准备超时也继续 Heavy，但不伪造接触冲量。 */
	void CommitAcceptedImpact(const FHitResult* Hit, const FVector& NormalImpulse);
	/** 准备超时已进入 Heavy 后，补记同一预期源随后到达的唯一真实接触。 */
	void CommitLateAcceptedContact(const FHitResult& Hit, const FVector& NormalImpulse);

	/** 从 Downed 的另一真实动态刚体接触恢复物理求解，不补冲量。 */
	void ResumeFromDownedHit(
		AActor* SourceActor,
		const FHitResult& Hit,
		const FVector& NormalImpulse);

	/** 首次真实冲量保留短暂接触后，只放开 PhysicsBody；墙和地面碰撞保持不变。 */
	void ReleasePhysicsBodyCollisionIfDue();

	/** Inactive 时拒绝并刷新上一次命中来源的保护期；其他来源不受影响。 */
	bool RefreshSameSourceProtectionIfActive(AActor* RequestSourceActor);

	/** 起身事务完成时从本次真实命中来源建立保护期。 */
	void BeginSameSourceProtection();

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

	/** 调用阶段 Profile 后应用 DataAsset 倍率；A/B 纯布娃娃模式则在相同 BodyModifier 条件下关闭全部 Controls。 */
	bool ApplyPhysicalStage(
		FName ProfileName,
		const FHeavyImpactControlStageTuning& StageTuning);

	/**
	 * 只更新 ParentSpace 关节倍率，不重新调用 Profile。
	 * StrengthAndTorqueScale 只缩放 AngularStrength 与 MaxTorque，阶段阻尼比保持不变。
	 */
	bool ApplyParentSpaceControlMultipliers(
		const FHeavyImpactControlStageTuning& StageTuning,
		float StrengthAndTorqueScale = 1.0f);

	/** 在坏物理超时或 Profile 失败时关闭姿态控制，保持自由物理。 */
	void EnterFreeFallback(const TCHAR* Reason);

	/** 对齐真实落点并排队到下一帧完成无 Tick 的 Downed 睡眠。 */
	void EnterDowned(const TCHAR* Reason);

	/** 在 FreeFallback 已经过一次 PrePhysics 后睡眠，并关闭两个物理 Tick。 */
	void FinishPendingDownedSleep();

	void ScheduleRecoveryAttempt(float DelaySeconds);
	void TryBeginRecovery(uint32 ExpectedTransactionSerial);
	bool BuildRecoveryPlan(FHeavyImpactRecoveryPlan& OutPlan, FString& OutReason);
	bool BuildPreImpactFallbackRecoveryPlan(
		FHeavyImpactRecoveryPlan& OutPlan,
		FString& OutReason);
	bool PopulateRecoveryAnimation(FHeavyImpactRecoveryPlan& OutPlan, FString& OutReason);
	bool DetermineRecoveryOrientation(bool& bOutFaceUp, FRotator& OutRotation, FString& OutReason);
	bool TryFindRecoveryCapsuleLocation(
		const FRotator& UprightRotation,
		FVector& OutLocation,
		FString& OutReason) const;
	bool TryResolveRecoveryCandidate(
		const FVector& PelvisAnchor,
		const FVector2D& HorizontalOffset,
		const FRotator& UprightRotation,
		float MaximumAdjustment,
		FVector& OutLocation) const;
	bool BeginPhysicalToAnimationHandoff(
		const FHeavyImpactRecoveryPlan& Plan,
		FString& OutReason);
	/** 在物理交接提交前同步创建动态 Montage；失败时调用方仍可保持当前物理状态。 */
	bool StartRecoveryMontageNow(
		const FHeavyImpactRecoveryPlan& Plan,
		FString& OutReason);
	/** 在短生命周期 PostPhysics Tick 中推进唯一的 Snapshot-to-Slot 混合。 */
	void UpdateRecoverySnapshotBlend(float DeltaTime);
	void ValidateRecoverySlotEvaluation(uint32 ExpectedTransactionSerial);
	void HandleRecoveryMontageWatchdog(uint32 ExpectedTransactionSerial);
	void HandleRecoveryMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void CompleteRecovery(const TCHAR* Reason, bool bStopActiveMontage = false);
	void CancelRecoveryAsync(bool bStopActiveMontage);
	void ClearRecoveryAnimationState(bool bStopActiveMontage);
	bool IsCurrentRecoveryTransaction(uint32 ExpectedTransactionSerial) const;
	bool CaptureRelocatedRecoveryPose(
		const FHeavyImpactRecoveryPlan& Plan,
		FPoseSnapshot& OutPose,
		FString& OutReason) const;

	/** 仅恢复返回 Accepted 前失败的原子准备转换；正常 Prepared 超时不得调用。 */
	void RestoreFailedPreparationTransition();

	/** 恢复每个 Physics Asset Body 的原始属性。 */
	void RestoreBodyBaseline(const FHeavyImpactRecoveryBaseline& Baseline);
	bool RestoreCharacterShell(const FHeavyImpactRecoveryBaseline& Baseline);

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

	/** 当前已提交物理事务的真实来源；从 Commit 保留到恢复完成。 */
	TWeakObjectPtr<AActor> CommittedSourceActor;

	/** 最近一次完成恢复后受保护的机关来源；弱引用不延长机关生命周期。 */
	TWeakObjectPtr<AActor> ProtectedSourceActor;

	FHeavyImpactPreparationRequest ActiveRequest;
	FHeavyImpactPreparationRollback PreparationRollback;
	FHeavyImpactRecoveryBaseline RecoveryBaseline;
	TArray<FName> OwnedControlNames;
	TArray<FName> OwnedBodyModifierNames;
	TArray<FGuid> RecentImpactIds;
	TArray<FGuid> LoggedRejectedImpactIds;

	UPROPERTY(Transient)
	TObjectPtr<UHeavyImpactAnimInstance> HeavyImpactAnimInstance = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveRecoveryMontage = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> ActiveRecoverySequence = nullptr;

	FTimerHandle RecoveryRetryTimer;
	FTimerHandle RecoverySlotValidationTimer;
	FTimerHandle RecoveryMontageWatchdogTimer;

	EHeavyImpactState State = EHeavyImpactState::Inactive;
	EHeavyImpactRecoveryPhase RecoveryPhase = EHeavyImpactRecoveryPhase::None;
	float StateElapsedSeconds = 0.0f;
	float TotalCommittedSeconds = 0.0f;
	float StableElapsedSeconds = 0.0f;
	float ActorToPelvisZ = 0.0f;
	float ActivePreparationTimeoutSeconds = 0.0f;
	/** Downed 睡眠完成后首次起身尝试的世界时间；负值表示尚未开始阻塞计时。 */
	double RecoveryBlockedStartTimeSeconds = -1.0;
	/** 同一来源保护的游戏世界截止时间；同一来源持续请求时刷新。 */
	float SameSourceProtectionUntilSeconds = 0.0f;
	float BodyFrontCalibrationSign = 1.0f;
	/** 动态 Montage 的起始时间，单位秒。 */
	float ActiveRecoveryAnimationStartTimeSeconds = 0.0f;
	/** 当前 Snapshot-to-Montage 混合已累计的 PostPhysics 秒数。 */
	float RecoveryBlendElapsedSeconds = 0.0f;
	uint64 PreparedEntryFrame = 0;
	uint32 RecoveryTransactionSerial = 0;
	bool bConfigured = false;
	bool bInitialized = false;
	bool bFreeFallbackInvoked = false;
	bool bPendingDownedSleep = false;
	/** 当前已提交事务是否已经放开 Mesh 对 PhysicsBody 的阻挡。 */
	bool bPhysicsBodyCollisionReleased = false;
	bool bPureRagdollComparisonActive = false;
	bool bBodyFrontCalibrationValid = false;
	bool bRecoveryOrientationWarningLogged = false;
};
