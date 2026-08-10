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
	 * 默认 0.9，范围 0.05~5。调高增加观察和躲避时间，调低提高突然性。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|触发",
		meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.2", UIMax = "2.0", Units = "s"))
	float WarningSeconds = 0.9f;

	/**
	 * AThrustGuidedHazardLauncher::CalculateInitialLaunchDirection 允许目标相对 Muzzle +X 的最大初始偏角，
	 * 单位 deg；默认 30，范围 0~60。它只补偿走廊内横移，不允许弹体用大转角掩盖错误摆位。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|发射",
		meta = (ClampMin = "0.0", ClampMax = "60.0", UIMin = "10.0", UIMax = "35.0", Units = "deg"))
	float MaximumInitialAimAngleDegrees = 30.0f;

	/** Muzzle 表示炮管出口平面；弹体尾端与该平面之间额外保留的净空，单位 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|发射",
		meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "30.0", Units = "cm"))
	float SpawnClearanceMargin = 10.0f;

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
	 * 默认 60，范围 1~500。质量只决定真实接触、惯量和反弹；目标加速度不随质量改变。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "1.0", ClampMax = "500.0", UIMin = "20.0", UIMax = "150.0", Units = "kg"))
	float ProjectileMassKilograms = 60.0f;

	/** 满油门期望加速度；运行时按实际刚体质量换算为 Thruster 力，单位 cm/s²。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进",
		meta = (ClampMin = "10.0", ClampMax = "5000.0", UIMin = "100.0", UIMax = "2000.0", Units = "cm/s^2"))
	float MaximumPoweredAcceleration = 900.0f;

	/** 正常受控飞行追求的有符号前向速度，单位 cm/s。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进",
		meta = (ClampMin = "50.0", ClampMax = "3000.0", UIMin = "200.0", UIMax = "1200.0", Units = "cm/s"))
	float TargetPoweredSpeed = 650.0f;

	/** 前向/侧向分量安全线；组合总速另加 SpeedControlBand 后关机，不直接钳制刚体速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进",
		meta = (ClampMin = "50.0", ClampMax = "5000.0", UIMin = "300.0", UIMax = "1500.0", Units = "cm/s"))
	float MaximumPoweredSpeed = 800.0f;

	/** 目标速度下方多大范围内逐渐收小油门；同时作为组合总速安全线的额外余量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进",
		meta = (ClampMin = "1.0", ClampMax = "2000.0", UIMin = "50.0", UIMax = "500.0", Units = "cm/s"))
	float SpeedControlBand = 150.0f;

	/**
	 * AThrustGuidedHazardProjectile::Tick 读取的持续推进时间，单位 s；
	 * 默认 1.4，范围 0.05~5。首次阻挡碰撞会立即结束推进，不再继续累计能量。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进",
		meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.3", UIMax = "2.0", Units = "s"))
	float PoweredDurationSeconds = 1.4f;

	/**
	 * 受控阶段线性阻尼；用于抑制初段速度积累，不直接写速度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float PoweredLinearDamping = 0.35f;

	/**
	 * 受控阶段角阻尼；与真实惯量转矩共同抑制追踪过冲。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float PoweredAngularDamping = 1.5f;

	/** 首碰或计时结束后的 Chaos 自由运动线性阻尼。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float CoastingLinearDamping = 0.08f;

	/** 首碰或计时结束后的 Chaos 自由运动角阻尼。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|弹体",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))
	float CoastingAngularDamping = 0.15f;

	/**
	 * AThrustGuidedHazardProjectile::BeginPlay/FinishPoweredPhase 读取；
	 * 默认 true。开启时推进阶段关闭重力，计时结束后恢复，避免低推力弹体在短走廊内先坠地。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|推进")
	bool bDisableGravityWhilePowered = true;

	/**
	 * 距离除以目标速度所得动态前置时间的上限；它不是完整弹道解，只补偿角色短时间移动。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.5", Units = "s"))
	float MaximumTargetLeadTimeSeconds = 0.35f;

	/**
	 * 期望角加速度的朝向误差比例增益；调高会更积极转向，过高会产生过冲和脚本感。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "0.0", ClampMax = "30.0", UIMin = "0.0", UIMax = "15.0"))
	float OrientationGain = 10.0f;

	/**
	 * 对完整角速度（包括滚转）的阻尼增益；调高会压制过冲，过高会让转向迟钝。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "10.0"))
	float AngularVelocityDampingGain = 4.0f;

	/**
	 * 姿态控制允许请求的最大角加速度；运行时通过真实惯量换算成物理转矩。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|制导",
		meta = (ClampMin = "0.1", ClampMax = "50.0", UIMin = "1.0", UIMax = "20.0"))
	float MaximumAngularAcceleration = 8.0f;

	/**
	 * AThrustGuidedHazardProjectile::ApplyConfiguration 读取的球形准备查询半径，单位 cm；
	 * 默认 700，范围 50~2000。它只发现 IHeavyImpactReceiver，不阻挡、不施力。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|重冲击预测",
		meta = (ClampMin = "50.0", ClampMax = "2000.0", UIMin = "200.0", UIMax = "1000.0", Units = "cm"))
	float PreparationLookAheadDistance = 700.0f;

	/**
	 * AThrustGuidedHazardProjectile::BuildPreparationRequest 要求的最小相对接近速度，单位 cm/s；
	 * 默认 120，范围 1~5000。只过滤无法形成有效预计接触的慢速远离状态，不判定最终受击强弱。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|重冲击预测",
		meta = (ClampMin = "1.0", ClampMax = "5000.0", UIMin = "50.0", UIMax = "1000.0", Units = "cm/s"))
	float MinimumHeavyImpactClosingSpeed = 120.0f;

	/**
	 * AThrustGuidedHazardProjectile::BuildPreparationRequest 读取的正常帧率最大准备时间，单位 s；
	 * 默认 0.35，范围 0.08~0.5。严重掉帧时仍允许按共享 HeavyImpact 规则扩展到最多 0.5 s。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|物理制导|重冲击预测",
		meta = (ClampMin = "0.08", ClampMax = "0.5", UIMin = "0.08", UIMax = "0.25", Units = "s"))
	float MaximumPreparationLeadTime = 0.35f;
};
