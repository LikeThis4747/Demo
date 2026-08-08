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
struct FHeavyImpactFalsePositiveRollback
{
	bool bValid = false;
	FTransform ActorTransform = FTransform::Identity;
	FVector CharacterVelocity = FVector::ZeroVector;
	TEnumAsByte<EMovementMode> MovementMode = MOVE_None;
	uint8 CustomMovementMode = 0;

	void Reset()
	{
		*this = FHeavyImpactFalsePositiveRollback();
	}
};

/** Character shell/body configuration retained until a committed impact has recovered. */
struct FHeavyImpactRecoveryBaseline
{
	bool bValid = false;
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
	PreparingPose,
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
	/** 捕获当前物理姿势并启动全物理身体向起身开头姿势的渐进关节追随。 */
	bool BeginRecoveryPosePreparation(
		const FHeavyImpactRecoveryPlan& Plan,
		FString& OutReason);
	/** 在 PostPhysics 更新目标姿势和控制强度；完成后尝试安全交接。 */
	void UpdateRecoveryPosePreparation(
		float DeltaTime,
		bool bSlowEnough,
		bool bSupported);
	/** 身体重新失稳时冻结当前动画目标、清理准备数据并恢复 Flight 控制。 */
	void CancelRecoveryPosePreparation(const TCHAR* Reason);
	/** 当前是否仍由 Chaos 模拟全身、同时执行起身姿势准备。 */
	bool IsRecoveryPosePreparationActive() const;
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
	void StartRecoveryMontage(uint32 ExpectedTransactionSerial);
	void ReleaseRecoveryPoseSnapshot(uint32 ExpectedTransactionSerial);
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

	/** 恢复 Prepared 前的全部角色外壳和 Body 属性。 */
	void RestoreSnapshotAfterFalsePositive();

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

	FHeavyImpactPreparationRequest ActiveRequest;
	FHeavyImpactFalsePositiveRollback FalsePositiveRollback;
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
	FTimerHandle RecoveryMontageStartTimer;
	FTimerHandle RecoveryPoseReleaseTimer;
	FTimerHandle RecoverySlotValidationTimer;
	FTimerHandle RecoveryMontageWatchdogTimer;

	EHeavyImpactState State = EHeavyImpactState::Inactive;
	EHeavyImpactRecoveryPhase RecoveryPhase = EHeavyImpactRecoveryPhase::None;
	float StateElapsedSeconds = 0.0f;
	float TotalCommittedSeconds = 0.0f;
	float StableElapsedSeconds = 0.0f;
	float ActorToPelvisZ = 0.0f;
	float ActivePreparationTimeoutSeconds = 0.0f;
	float RecoveryBlockedElapsedSeconds = 0.0f;
	float BodyFrontCalibrationSign = 1.0f;
	/** Sequence Evaluator 与动态 Montage 共用的起始时间，单位秒。 */
	float ActiveRecoveryAnimationStartTimeSeconds = 0.0f;
	/** 当前全物理姿势准备已累计的 PostPhysics 秒数。 */
	float RecoveryPosePreparationElapsedSeconds = 0.0f;
	/** Alpha 首次达到一时的 PostPhysics 帧；后续帧才允许关闭物理。 */
	uint64 RecoveryPoseFinalTargetFrame = 0;
	uint64 PreparedEntryFrame = 0;
	uint32 RecoveryTransactionSerial = 0;
	bool bConfigured = false;
	bool bInitialized = false;
	bool bFreeFallbackInvoked = false;
	bool bPendingDownedSleep = false;
	bool bHardTimeoutReported = false;
	bool bPureRagdollComparisonActive = false;
	/** EnterPrepared 锁存的 A/B 路线；同一真实冲击的 Downed 重试继续沿用。 */
	bool bUseRecoveryPosePreparationForTransaction = false;
	/** 防止提交最终目标的同一 PostPhysics 帧立即关闭 Chaos。 */
	bool bRecoveryPoseFinalTargetSubmitted = false;
	bool bBodyFrontCalibrationValid = false;
	bool bRecoveryOrientationWarningLogged = false;
	bool bRecoveryBlockedWarningLogged = false;
};
