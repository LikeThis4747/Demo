// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardTuningData.h
 * 职责：保存壁挂式一次性物理制导机关的触发、弹体、推进、制导和重冲击预测参数。
 * 边界：不保存关卡摆位、Muzzle/TriggerAnchor 变换、美术资源、目标状态或受击差异。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ThrustGuidedHazardTuningData.generated.h"

/** 壁挂式一次性物理制导机关 V1 的唯一运行时调参来源。 */
UCLASS(BlueprintType)
class DEMO_API UThrustGuidedHazardTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 校验全部公开参数；失败时返回具体原因，运行时不会偷偷钳制非法资产。 */
	bool IsConfigured(FString& OutError) const;

	/**
	 * AThrustGuidedHazardLauncher::ApplyTriggerGeometry 读取的触发盒半尺寸，单位 cm；
	 * 默认 500/260/140，范围 10~2000。TriggerAnchor 单独决定位置和旋转。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|触发",
		meta = (ClampMin = "10.0", ClampMax = "2000.0", Units = "cm"))
	FVector TriggerHalfExtent = FVector(500.0f, 260.0f, 140.0f);

	/**
	 * AThrustGuidedHazardLauncher::EnterWarning 读取的预警时间，单位 s；
	 * 默认 0.7，范围 0.05~5。调高增加观察和躲避时间，调低提高突然性。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|触发",
		meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.2", UIMax = "2.0", Units = "s"))
	float WarningSeconds = 0.7f;

	/**
	 * AThrustGuidedHazardLauncher::CalculateInitialLaunchDirection 允许目标相对 Muzzle +X 的最大初始偏角，
	 * 单位 deg；默认 25，范围 0~60。它只补偿走廊内横移，不允许弹体用大转角掩盖错误摆位。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|发射",
		meta = (ClampMin = "0.0", ClampMax = "60.0", UIMin = "10.0", UIMax = "35.0", Units = "deg"))
	float MaximumInitialAimAngleDegrees = 25.0f;

	/**
	 * AThrustGuidedHazardProjectile::ApplyConfiguration 读取的胶囊半径，单位 cm；
	 * 默认 25，范围 5~100。增大会提高碰撞稳定性，也更难穿过狭窄物件缝隙。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "5.0", ClampMax = "100.0", UIMin = "15.0", UIMax = "50.0", Units = "cm"))
	float ProjectileRadius = 25.0f;

	/**
	 * AThrustGuidedHazardProjectile::ApplyConfiguration 读取的胶囊半高，单位 cm；
	 * 默认 60，范围 5~250，且必须不小于半径。弹体局部 +Z 是纵轴和推进基准。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "5.0", ClampMax = "250.0", UIMin = "30.0", UIMax = "100.0", Units = "cm"))
	float ProjectileHalfHeight = 60.0f;

	/**
	 * AThrustGuidedHazardProjectile::BeginPlay 写入 ProjectileBody 的质量，单位 kg；
	 * 默认 60，范围 1~500。质量越大，在相同推力下加速越慢，但真实接触的动量通常更大。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "1.0", ClampMax = "500.0", UIMin = "20.0", UIMax = "150.0", Units = "kg"))
	float ProjectileMassKilograms = 60.0f;

	/**
	 * AThrustGuidedHazardProjectile::BeginPlay 写入 UPhysicsThrusterComponent::ThrustStrength；
	 * 默认 120000，范围 1000~2000000，使用 UE 物理力单位。调高会同时增强直线加速和尾部转矩。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进",
		meta = (ClampMin = "1000.0", ClampMax = "2000000.0", UIMin = "50000.0", UIMax = "300000.0"))
	float ThrustStrength = 120000.0f;

	/**
	 * AThrustGuidedHazardProjectile::Tick 读取的持续推进时间，单位 s；
	 * 默认 1.0，范围 0.05~5。首次碰撞只停止制导，不改变该计时。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进",
		meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.3", UIMax = "2.0", Units = "s"))
	float PoweredDurationSeconds = 1.0f;

	/**
	 * AThrustGuidedHazardProjectile::BeginPlay 写入刚体线性阻尼；
	 * 默认 0.05，范围 0~10。调高会缩短推进后滑行和多次反弹距离。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float LinearDamping = 0.05f;

	/**
	 * AThrustGuidedHazardProjectile::BeginPlay 写入刚体角阻尼；
	 * 默认 0.1，范围 0~10。调高会减少碰撞后的翻滚，过高则削弱不可控感。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float AngularDamping = 0.1f;

	/**
	 * AThrustGuidedHazardProjectile::BeginPlay/FinishPoweredPhase 读取；
	 * 默认 true。开启时推进阶段关闭重力，计时结束后恢复，避免低推力弹体在短走廊内先坠地。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进")
	bool bDisableGravityWhilePowered = true;

	/**
	 * AThrustGuidedHazardProjectile::TryCalculateGuidedForceDirection 读取的固定目标前置时间，单位 s；
	 * 默认 0.15，范围 0~0.5。它不是完整弹道解，只补偿角色短时间移动。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "0.0", ClampMax = "0.5", UIMin = "0.0", UIMax = "0.3", Units = "s"))
	float TargetLeadTimeSeconds = 0.15f;

	/**
	 * AThrustGuidedHazardProjectile::TryCalculateGuidedForceDirection 读取的朝向误差比例增益；
	 * 默认 1.4，范围 0~10。调高会更积极转向，过高会产生过冲和脚本感。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "4.0"))
	float OrientationGain = 1.4f;

	/**
	 * AThrustGuidedHazardProjectile::TryCalculateGuidedForceDirection 读取的角速度阻尼增益；
	 * 默认 0.25，范围 0~10。调高会压制过冲，过高会让转向迟钝。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float AngularVelocityDampingGain = 0.25f;

	/**
	 * AThrustGuidedHazardProjectile 的尾部推力矢量相对弹体 +Z 的最大偏角，单位 deg；
	 * 默认 18，范围 0~45。它同时限定可产生的最大转向转矩和横向推力。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "0.0", ClampMax = "45.0", UIMin = "5.0", UIMax = "30.0", Units = "deg"))
	float MaximumGimbalAngleDegrees = 18.0f;

	/**
	 * AThrustGuidedHazardProjectile::AimThrusterAtWorldForceDirection 读取的喷口转动速度，单位 deg/s；
	 * 默认 120，范围 1~720。该限制让追踪方向通过连续物理施力形成，而不是瞬间改向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "1.0", ClampMax = "720.0", UIMin = "30.0", UIMax = "240.0", Units = "deg/s"))
	float MaximumGimbalRateDegreesPerSecond = 120.0f;

	/**
	 * AThrustGuidedHazardProjectile::ApplyConfiguration 读取的球形准备查询半径，单位 cm；
	 * 默认 600，范围 50~2000。它只发现 IHeavyImpactReceiver，不阻挡、不施力。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|重冲击预测",
		meta = (ClampMin = "50.0", ClampMax = "2000.0", UIMin = "200.0", UIMax = "1000.0", Units = "cm"))
	float PreparationLookAheadDistance = 600.0f;

	/**
	 * AThrustGuidedHazardProjectile::BuildPreparationRequest 要求的最小相对接近速度，单位 cm/s；
	 * 默认 120，范围 1~5000。只过滤无法形成有效预计接触的慢速远离状态，不判定最终受击强弱。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|重冲击预测",
		meta = (ClampMin = "1.0", ClampMax = "5000.0", UIMin = "50.0", UIMax = "1000.0", Units = "cm/s"))
	float MinimumHeavyImpactClosingSpeed = 120.0f;

	/**
	 * AThrustGuidedHazardProjectile::BuildPreparationRequest 读取的正常帧率最大准备时间，单位 s；
	 * 默认 0.16，范围 0.08~0.5。严重掉帧时仍允许按共享 HeavyImpact 规则扩展到最多 0.5 s。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|重冲击预测",
		meta = (ClampMin = "0.08", ClampMax = "0.5", UIMin = "0.08", UIMax = "0.25", Units = "s"))
	float MaximumPreparationLeadTime = 0.16f;
};
