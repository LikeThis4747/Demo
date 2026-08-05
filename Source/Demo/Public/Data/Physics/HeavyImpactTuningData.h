// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactTuningData.h
 * 职责：集中保存单套角色重冲击状态机的 PCA 引用、骨骼名和时序阈值。
 * 边界：姿态控制强度只由 Physics Control Asset Profile 管理，本资产不重复覆盖。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "HeavyImpactTuningData.generated.h"

class UPhysicsControlAsset;
class USkeletalMeshComponent;

/** 玩家或 AI 的重冲击物理响应配置；缺失或非法时功能明确停用。 */
UCLASS(BlueprintType)
class DEMO_API UHeavyImpactTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 已编译的 Physics Control Asset；硬引用保证命中前已加载。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Physics Control")
	TObjectPtr<UPhysicsControlAsset> PhysicsControlAsset = nullptr;

	/** 用于身体跟随、速度判稳和地面探测的骨盆骨骼。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Skeleton")
	FName PelvisBone = TEXT("pelvis");

	/** 正常帧率下等待指定刚体真实命中的最长秒数；严重低帧率时运行时最多临时扩到 2.5 帧/0.5 秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Timing", meta = (ClampMin = "0.03", ClampMax = "0.5"))
	float MaximumPreparationSeconds = 0.18f;

	/** 预计接触的最小提前量下限；运行时还会取当前帧时长的 1.25 倍，避免固定秒数误判。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Timing", meta = (ClampMin = "0.005", ClampMax = "0.1"))
	float MinimumPreparationLeadSeconds = 0.02f;

	/** 真实命中后允许进入稳定判定前的最短模拟秒数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Timing", meta = (ClampMin = "0.0"))
	float MinimumSimulationSeconds = 0.25f;

	/** 骨盆低于该线速度才可计入稳定时间，单位为厘米/秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Stability", meta = (ClampMin = "0.0"))
	float StableLinearSpeedCmPerSecond = 80.0f;

	/** 骨盆低于该角速度才可计入稳定时间，单位为度/秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Stability", meta = (ClampMin = "0.0"))
	float StableAngularSpeedDegPerSecond = 90.0f;

	/** 速度达标且有地面支撑必须持续的秒数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Stability", meta = (ClampMin = "0.05"))
	float RequiredStableSeconds = 0.35f;

	/** 从骨盆向下查询地面支撑的距离，单位为厘米。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Stability", meta = (ClampMin = "10.0"))
	float GroundProbeDistance = 120.0f;

	/** Capsule 外壳跟随骨盆的插值速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Follow", meta = (ClampMin = "0.1"))
	float CapsuleFollowInterpSpeed = 18.0f;

	/** 超过该时长后关闭角向控制并保持自由物理，单位为秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Safety", meta = (ClampMin = "1.0"))
	float FreeFallbackAfterSeconds = 5.0f;

	/** 物理长期不稳定时的最终停止保险，必须大于自由物理阈值，单位为秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Safety", meta = (ClampMin = "2.0"))
	float ForceDownedAfterSeconds = 10.0f;

	/** 已接受 ImpactId 与拒绝日志缓存的固定上限。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heavy Impact|Deduplication", meta = (ClampMin = "4", ClampMax = "64"))
	int32 RecentImpactHistorySize = 16;

	/** 校验资产、骨骼、编译后 PCA 数据以及所有运行时阈值。 */
	bool Validate(const USkeletalMeshComponent* Mesh, FText& OutError) const;
};
