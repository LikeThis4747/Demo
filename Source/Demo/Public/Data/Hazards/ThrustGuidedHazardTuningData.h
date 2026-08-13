// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardTuningData.h
 * 职责：保存壁挂式一次性预判抛射机关的触发、机械瞄准、弹体和可选重冲击准备参数。
 * 边界：不保存关卡摆位、Muzzle/TriggerAnchor 变换、美术资源、目标状态或运行时弹道解。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ThrustGuidedHazardTuningData.generated.h"

class UCharacterImpactSourceProfile;

/** 壁挂式一次性预判抛射机关的唯一运行时调参来源。 */
UCLASS(BlueprintType)
class DEMO_API UThrustGuidedHazardTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 校验全部启用参数及其关系；失败时返回具体原因，运行时不会偷偷钳制非法资产。 */
	bool IsConfigured(FString& OutError) const;

	/** TriggerVolume 半尺寸，单位 cm；TriggerAnchor 单独决定空间位置和旋转。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|触发",
		meta = (ClampMin = "10.0", ClampMax = "2000.0", Units = "cm"))
	FVector TriggerHalfExtent = FVector(500.0f, 260.0f, 140.0f);

	/** 首个角色被锁定后的预警时长，单位 s。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|触发",
		meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.2", UIMax = "2.0", Units = "s"))
	float WarningSeconds = 0.55f;

	/**
	 * 与 PreferredLaunchAngleDegrees 一起推导唯一设计初速，单位 cm；
	 * 它不是 Trigger 距离、最大射程或运行时发射门槛。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹道",
		meta = (ClampMin = "300.0", ClampMax = "3000.0", UIMin = "400.0", UIMax = "1500.0", Units = "cm"))
	float ReferenceRange = 900.0f;

	/** 同高参考条件下用于推导初速的低抛角，单位 deg；每一发的实际仰角由目标几何决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹道",
		meta = (ClampMin = "5.0", ClampMax = "35.0", UIMin = "8.0", UIMax = "25.0", Units = "deg"))
	float PreferredLaunchAngleDegrees = 18.0f;

	/** 从目标胶囊中心向世界上方抬高的瞄准偏移，单位 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹道",
		meta = (ClampMin = "-100.0", ClampMax = "200.0", UIMin = "0.0", UIMax = "100.0", Units = "cm"))
	float TargetAimHeightOffset = 50.0f;

	/** 预警期间更新目标状态和机械瞄准的 Timer 周期，单位 s；Actor Tick 保持关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|瞄准",
		meta = (ClampMin = "0.02", ClampMax = "0.2", UIMin = "0.02", UIMax = "0.1", Units = "s"))
	float AimUpdateIntervalSeconds = 0.05f;

	/** 相对初始炮轴允许的最大水平偏航，单位 deg。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|瞄准",
		meta = (ClampMin = "0.0", ClampMax = "60.0", UIMin = "10.0", UIMax = "40.0", Units = "deg"))
	float MaximumAimYawDegrees = 30.0f;

	/** 相对初始炮轴允许的最大向上仰角，单位 deg；低顶房不得用高抛候选绕过它。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|瞄准",
		meta = (ClampMin = "1.0", ClampMax = "30.0", UIMin = "8.0", UIMax = "22.0", Units = "deg"))
	float MaximumAimPitchUpDegrees = 18.0f;

	/** 相对初始炮轴允许的最大向下俯角，单位 deg。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|瞄准",
		meta = (ClampMin = "0.0", ClampMax = "45.0", UIMin = "0.0", UIMax = "25.0", Units = "deg"))
	float MaximumAimPitchDownDegrees = 15.0f;

	/** AimPivot 在固定中性轴 Yaw/Pitch 平面内的最大合成转速，单位 deg/s。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|瞄准",
		meta = (ClampMin = "1.0", ClampMax = "360.0", UIMin = "30.0", UIMax = "180.0", Units = "deg/s"))
	float AimTurnSpeedDegreesPerSecond = 90.0f;

	/** 所有数学候选失败时仍采用的固定向上角度，单位 deg。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|瞄准",
		meta = (ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "15.0", Units = "deg"))
	float FallbackElevationDegrees = 8.0f;

	/** Muzzle 出口平面到弹体尾端的额外净空，单位 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|发射",
		meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "30.0", Units = "cm"))
	float SpawnClearanceMargin = 10.0f;

	/** 唯一物理胶囊半径，单位 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹体",
		meta = (ClampMin = "5.0", ClampMax = "100.0", UIMin = "15.0", UIMax = "50.0", Units = "cm"))
	float ProjectileRadius = 22.0f;

	/** 唯一物理胶囊半高，单位 cm，且必须不小于半径；胶囊局部 +Z 是长轴。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹体",
		meta = (ClampMin = "5.0", ClampMax = "250.0", UIMin = "30.0", UIMax = "100.0", Units = "cm"))
	float ProjectileHalfHeight = 45.0f;

	/** Chaos 刚体质量，单位 kg；只影响动量、接触和反弹，不参与弹道轨迹公式。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹体",
		meta = (ClampMin = "1.0", ClampMax = "500.0", UIMin = "10.0", UIMax = "100.0", Units = "kg"))
	float ProjectileMassKilograms = 50.0f;

	/** 从弹体正式进入飞行状态起计算的最长存活时间，单位 s；到期由 Actor LifeSpan 自动清除。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹体",
		meta = (ClampMin = "1.0", ClampMax = "30.0", UIMin = "3.0", UIMax = "15.0", Units = "s"))
	float ProjectileLifetimeSeconds = 8.0f;

	/** 离膛到首次有效阻挡接触之间的角阻尼。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float BallisticAngularDamping = 0.05f;

	/** 首次有效阻挡接触后的线性阻尼；不改写当前速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float PostImpactLinearDamping = 0.08f;

	/** 首次有效阻挡接触后的角阻尼；不改写当前角速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float PostImpactAngularDamping = 0.15f;

	/** 第一次有效角色阻挡命中使用的可选 StandingImpact 来源配置；为空时弹体只保留原有碰撞行为。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|轻受击")
	TObjectPtr<UCharacterImpactSourceProfile> StandingImpactSourceProfile = nullptr;

	/**
	 * 制导弹体作为已创作 Light 攻击时的最低响应强度。
	 * NormalImpulse 可用时仍可向上增加；该值避免扫掠命中返回零冲量时完全没有反馈。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|轻受击",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumStandingImpactStrength = 0.8f;

	/**
	 * 是否启用现有 HeavyImpact 提前准备链；首轮默认关闭以独立验收弹道。
	 * 关闭时 Projectile 不读取下方三项、不绑定预测球 Overlap，也不启动准备 Timer。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|重冲击预测")
	bool bEnableHeavyImpactPreparation = false;

	/** 启用 HeavyImpact 时的 Query-only 准备球半径，单位 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|重冲击预测",
		meta = (EditCondition = "bEnableHeavyImpactPreparation", ClampMin = "50.0", ClampMax = "2000.0", Units = "cm"))
	float PreparationLookAheadDistance = 700.0f;

	/** 启用 HeavyImpact 时要求的最小相对接近速度，单位 cm/s。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|重冲击预测",
		meta = (EditCondition = "bEnableHeavyImpactPreparation", ClampMin = "1.0", ClampMax = "5000.0", Units = "cm/s"))
	float MinimumHeavyImpactClosingSpeed = 120.0f;

	/** 启用 HeavyImpact 时正常帧率允许的最大准备时间，单位 s。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|预判抛射|重冲击预测",
		meta = (EditCondition = "bEnableHeavyImpactPreparation", ClampMin = "0.08", ClampMax = "0.5", Units = "s"))
	float MaximumPreparationLeadTime = 0.35f;
};
