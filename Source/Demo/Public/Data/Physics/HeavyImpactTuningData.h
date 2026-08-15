// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactTuningData.h
 * 职责：集中保存单套角色重冲击状态机的 PCA 引用、分阶段控制倍率、骨骼名和时序阈值。
 * 边界：Physics Control Asset 定义控制拓扑和基础 Profile；本资产只缩放既有 ParentSpace 控制，不创建世界空间位移权威。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "HeavyImpactTuningData.generated.h"

class UPhysicsControlAsset;
class UAnimSequenceBase;
class USkeletalMeshComponent;

/** 项目内的重冲击阶段倍率；只改变关节姿态驱动力，不约束骨盆的世界空间位移。 */
USTRUCT(BlueprintType)
struct DEMO_API FHeavyImpactControlStageTuning
{
	GENERATED_BODY()

	/** 乘到 PCA Profile 基础 AngularStrength 上。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heavy Impact|Physics Control",
		meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float AngularStrengthMultiplier = 1.0f;

	/** 乘到 PCA Profile 基础 AngularDampingRatio 上。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heavy Impact|Physics Control",
		meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float AngularDampingRatioMultiplier = 1.0f;

	/** 乘到 PCA Profile 基础 MaxTorque 上；不产生额外线性冲量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heavy Impact|Physics Control",
		meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float MaxTorqueMultiplier = 1.0f;
};

/** 玩家或 AI 的重冲击物理响应配置；缺失或非法时功能明确停用。 */
UCLASS(BlueprintType)
class DEMO_API UHeavyImpactTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	UHeavyImpactTuningData();

	/** 已编译的 Physics Control Asset；硬引用保证命中前已加载。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Physics Control")
	TObjectPtr<UPhysicsControlAsset> PhysicsControlAsset = nullptr;

	/** 命中前短暂准备：强控制、零重力，避免身体先于锤头接触而软倒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Physics Control")
	FHeavyImpactControlStageTuning PreparedControl;

	/** 飞行阶段：保留完整重力与自由骨盆，只给内部关节中等肌肉张力。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Physics Control")
	FHeavyImpactControlStageTuning FlightControl;

	/** 落地判稳阶段：提高关节强度和阻尼，减少尸体式松散与持续抖动。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Physics Control")
	FHeavyImpactControlStageTuning LandingControl;

	/** 用于身体跟随、速度判稳和地面探测的骨盆骨骼。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Skeleton")
	FName PelvisBone = TEXT("pelvis");

	/** Get-up sequence selected when the calibrated chest normal faces upward. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Animation")
	TObjectPtr<UAnimSequenceBase> GetUpFaceUpAnimation = nullptr;

	/** Get-up sequence selected when the calibrated chest normal faces downward. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Animation")
	TObjectPtr<UAnimSequenceBase> GetUpFaceDownAnimation = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Skeleton")
	FName HeadBone = TEXT("head");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Skeleton")
	FName LeftShoulderBone = TEXT("upperarm_l");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Skeleton")
	FName RightShoulderBone = TEXT("upperarm_r");

	/** Retry interval while no safe standing capsule can be found. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Timing",
		meta = (ClampMin = "0.1", ClampMax = "1.0", Units = "s"))
	float RecoveryRetrySeconds = 0.20f;

	/** 起身安全站位允许阻塞的最长时间；到期后停止重试，不再回到受击前位置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Timing",
		meta = (ClampMin = "0.1", ClampMax = "10.0", Units = "s"))
	float MaximumRecoveryBlockedSeconds = 1.5f;

	/** Minimum Chaos contact impulse that may interrupt Downed recovery as a genuine second hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Interruption",
		meta = (ClampMin = "1.0"))
	float MinimumDownedReimpactImpulse = 1000.0f;

	/**
	 * Seconds that every committed Heavy keeps blocking PhysicsBody before releasing dynamic impact sources.
	 * Once released, the Mesh ignores PhysicsBody until recovery;
	 * WorldStatic and WorldDynamic remain blocking, so walls and floors still participate.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Protection",
		meta = (ClampMin = "0.0", ClampMax = "0.5", UIMin = "0.05", UIMax = "0.25", Units = "s"))
	float PhysicsBodyReleaseDelaySeconds = 0.15f;

	/**
	 * Protection duration after recovery from the Actor that committed the last impact.
	 * A valid repeated request from that same source refreshes the deadline; other sources remain eligible.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Protection",
		meta = (ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.5", UIMax = "4.0", Units = "s"))
	float SameSourceProtectionSeconds = 0.75f;

	/** Hard-bounded horizontal adjustment from the final physical pelvis position. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Placement",
		meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float MaxRecoveryHorizontalAdjustmentCm = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Placement",
		meta = (ClampMin = "5.0", ClampMax = "60.0"))
	float RecoverySearchStepCm = 20.0f;

	/** Duration of the single explicit blend from the relocated physical Snapshot to the get-up Slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Animation",
		meta = (ClampMin = "0.05", ClampMax = "0.50", UIMin = "0.10", UIMax = "0.35", Units = "s"))
	float RecoverySnapshotBlendSeconds = 0.30f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Animation",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RecoveryMontageBlendOutSeconds = 0.30f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Animation",
		meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float RecoveryPlayRate = 1.0f;

	/** Face-up dynamic Montage start time, in seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "Heavy Impact|Recovery|Animation",
		meta = (ClampMin = "0.0", Units = "s"))
	float FaceUpAnimationStartTimeSeconds = 0.0f;

	/** Face-down dynamic Montage start time, in seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "Heavy Impact|Recovery|Animation",
		meta = (ClampMin = "0.0", Units = "s"))
	float FaceDownAnimationStartTimeSeconds = 0.0f;

	/** Asset-specific yaw correction applied after the face-up body direction is derived. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Animation")
	float FaceUpYawOffsetDegrees = 0.0f;

	/** Asset-specific yaw correction applied after the face-down body direction is derived. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Recovery|Animation")
	float FaceDownYawOffsetDegrees = 0.0f;

	/** 正常帧率下等待指定刚体真实命中的最长秒数；严重低帧率时运行时最多临时扩到 2.5 帧/0.5 秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Timing", meta = (ClampMin = "0.03", ClampMax = "0.5"))
	float MaximumPreparationSeconds = 0.18f;

	/** 预计接触的最小提前量下限；运行时还会取当前帧时长的 1.25 倍，避免固定秒数误判。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Timing", meta = (ClampMin = "0.005", ClampMax = "0.1"))
	float MinimumPreparationLeadSeconds = 0.02f;

	/** 真实命中后允许进入稳定判定前的最短模拟秒数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Timing", meta = (ClampMin = "0.0"))
	float MinimumSimulationSeconds = 0.25f;

	/** 真实 Heavy 提交后允许持续 Chaos 模拟的最长时间；到期后忽略速度阈值并进入有界起身。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Timing",
		meta = (ClampMin = "0.5", ClampMax = "5.0", Units = "s"))
	float MaximumSimulationSeconds = 1.5f;

	/** 骨盆低于该线速度才可计入稳定时间，单位为厘米/秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Stability", meta = (ClampMin = "0.0"))
	float StableLinearSpeedCmPerSecond = 80.0f;

	/** 骨盆低于该角速度才可计入稳定时间，单位为度/秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Stability", meta = (ClampMin = "0.0"))
	float StableAngularSpeedDegPerSecond = 90.0f;

	/** 自然低能量状态必须连续保持的秒数；最长等待仍由 MaximumSimulationSeconds 统一限制。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Stability", meta = (ClampMin = "0.05"))
	float RequiredStableSeconds = 0.35f;

	/** 从骨盆向下查询地面支撑的距离，单位为厘米。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Stability", meta = (ClampMin = "10.0"))
	float GroundProbeDistance = 120.0f;

	/** Capsule 外壳跟随骨盆的插值速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Follow", meta = (ClampMin = "0.1"))
	float CapsuleFollowInterpSpeed = 18.0f;

	/** 已接受 ImpactId 与拒绝日志缓存的固定上限。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Deduplication", meta = (ClampMin = "4", ClampMax = "64"))
	int32 RecentImpactHistorySize = 16;

	/** 校验资产、骨骼、编译后 PCA 数据以及所有运行时阈值。 */
	bool Validate(const USkeletalMeshComponent* Mesh, FText& OutError) const;
};
