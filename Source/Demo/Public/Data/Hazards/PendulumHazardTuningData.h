// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PendulumHazardTuningData.h
 * 职责：保存摆锤几何、刚体、约束、预测准备窗口与每次中线穿越的最大补能量。
 * 边界：不引用关卡、网格或具体角色，不保存运行时相位，也不定义角色受击求解规则。
 * 状态 Owner：本 DataAsset 是摆锤可调参数的唯一来源；APendulumHazard 只读取并执行。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PendulumHazardTuningData.generated.h"

/** Level0 物理摆锤的独立调参资产；补能默认关闭，必须先测纯自由摆损失再标定。 */
UCLASS(BlueprintType)
class DEMO_API UPendulumHazardTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 校验公开范围和跨属性几何约束；失败时返回具体属性名与原因，不偷偷钳制资产。 */
	bool IsConfigured(FString& OutError) const;

	/**
	 * APendulumHazard::ApplyGeometry 读取的地面根到支点高度，单位 cm；初始 650，编辑范围 100~2000。
	 * 调高会整体抬高支点和锤头，调低会压缩离地净空；必须继续满足 PivotHeight > PendulumLength + BobHalfExtents.Z。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|几何",
		meta = (ClampMin = "100.0", ClampMax = "2000.0", UIMin = "300.0", UIMax = "1000.0", Units = "cm"))
	float PivotHeight = 650.0f;

	/**
	 * APendulumHazard::ApplyGeometry / CalculateTargetCenterSpeed 读取的支点到锤头中心距离，单位 cm；初始 520，编辑范围 100~1500。
	 * 调高会扩大扫掠并延长自然周期、降低最低点，调低则缩小扫掠并加快节奏；改动后必须同步蓝图摆杆网格。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|几何",
		meta = (ClampMin = "100.0", ClampMax = "1500.0", UIMin = "200.0", UIMax = "800.0", Units = "cm"))
	float PendulumLength = 520.0f;

	/**
	 * APendulumHazard::ApplyGeometry / CalculateTargetCenterSpeed 读取的盒形碰撞半尺寸，单位 cm；
	 * X 是跨走廊宽度、Y 是最低点运动方向厚度、Z 是高度。默认 110/40/75，即完整尺寸 220x80x150。
	 * 增大 X/Z 会扩大正面命中范围，增大 Y 会让锤头更早接触；改动后必须同步蓝图锤头网格。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|几何",
		meta = (ClampMin = "10.0", ClampMax = "250.0", UIMin = "20.0", UIMax = "150.0", Units = "cm"))
	FVector BobHalfExtents = FVector(110.0f, 40.0f, 75.0f);

	/**
	 * APendulumHazard::ApplyPhysicsProperties 读取的锤头刚体质量，单位 kg；初始 1000，编辑范围 1~5000。
	 * 调高会让同等外部冲量更难改变摆锤且反向冲击更强，调低则更易被互动；Chaos 始终使用双方真实质量和速度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|物理",
		meta = (ClampMin = "1.0", ClampMax = "5000.0", UIMin = "50.0", UIMax = "2000.0", Units = "kg"))
	float BobMassKilograms = 1000.0f;

	/**
	 * APendulumHazard::SetInitialReleasePose / CalculateTargetCenterSpeed 读取的目标摆幅，单位 degree；初始 18，编辑范围 1~80。
	 * 调高会提高释放速度与扫掠范围，调低则减弱威胁；必须小于 MainAxisLimitDegrees 并重新核对墙面净距。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|约束",
		meta = (ClampMin = "1.0", ClampMax = "80.0", UIMin = "5.0", UIMax = "45.0", Units = "deg"))
	float TargetAmplitudeDegrees = 18.0f;

	/**
	 * APendulumHazard::ConfigureConstraint 读取的 Twist 双侧硬角限位，单位 degree；初始 23，编辑范围 2~85。
	 * 调高会允许碰撞注入更大摆幅但增加碰墙风险，调低会更早碰到机械边界；必须大于 TargetAmplitudeDegrees。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|约束",
		meta = (ClampMin = "2.0", ClampMax = "85.0", UIMin = "5.0", UIMax = "50.0", Units = "deg"))
	float MainAxisLimitDegrees = 23.0f;

	/**
	 * APendulumHazard::ConfigureConstraint 读取的两个副轴侧摆角，单位 degree；初始 5，编辑范围 0~15。
	 * 调高会保留更多斜向碰撞但扩大空间包络，调低会抑制侧摆；0 会锁定两个副轴。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|约束",
		meta = (ClampMin = "0.0", ClampMax = "15.0", UIMin = "0.0", UIMax = "10.0", Units = "deg"))
	float SecondaryAxisLimitDegrees = 5.0f;

	/**
	 * APendulumHazard::ApplyPhysicsProperties 读取的 UE 线性阻尼系数，无物理单位；初始 0.01，编辑范围 0~10。
	 * 调高会同时衰减主摆速度和侧向漂移，调低会让所有线性运动保留更久；必须用纯自由摆实测，不能只按现有物体参数照搬。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|物理",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "1.0"))
	float LinearDamping = 0.01f;

	/**
	 * APendulumHazard::ApplyPhysicsProperties 读取的 UE 角阻尼系数，无物理单位；初始 0.02，编辑范围 0~10。
	 * 调高会更快衰减主摆旋转、球体自转与侧摆，调低会让碰撞扰动保持更久；必须在 PIE 中与线性阻尼联合标定。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|物理",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "1.0"))
	float AngularDamping = 0.02f;

	/**
	 * 独立 PreparationVolume 在锤头包围球外增加的预测距离，单位 cm；初始 500，编辑范围 10~1000。
	 * 调高可更早登记高速目标但增加 Overlap 候选，调低会缩短角色切物理的可靠时间。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|重冲击预测",
		meta = (ClampMin = "10.0", ClampMax = "1000.0", UIMin = "100.0", UIMax = "750.0", Units = "cm"))
	float PreparationLookAheadDistance = 500.0f;

	/**
	 * 发送重冲击准备请求的最低相对接近速度，单位 cm/s；初始 120，编辑范围 1~5000。
	 * 低于该值只保留普通物理接触，不把慢速推挤升级为重冲击。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|重冲击预测",
		meta = (ClampMin = "1.0", ClampMax = "5000.0", UIMin = "50.0", UIMax = "1000.0", Units = "cm/s"))
	float MinimumHeavyImpactClosingSpeed = 120.0f;

	/**
	 * 预测体积尺寸要覆盖的接收者最大运动速度，单位 cm/s；初始 600。
	 * 它不限制角色速度，只是为了让玩家/AI 迎面移动时仍能在低帧率准备窗口前被登记。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|重冲击预测",
		meta = (ClampMin = "0.0", ClampMax = "2000.0", UIMin = "0.0", UIMax = "1000.0", Units = "cm/s"))
	float MaximumExpectedReceiverSpeed = 600.0f;

	/**
	 * 正常帧率下机关允许发送的最大预计接触时间，单位 s；初始 0.16，编辑范围 0.08~0.5。
	 * 严重低帧率时运行时会与接收端同步临时扩到最多 2.5 帧/0.5 秒；资产值仍应位于接收端正常窗口内。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|重冲击预测",
		meta = (ClampMin = "0.08", ClampMax = "0.5", UIMin = "0.08", UIMax = "0.25", Units = "s"))
	float MaximumPreparationLeadTime = 0.16f;

	/**
	 * APendulumHazard::AssistAtCenterCrossing 读取的单次最大补速，单位 cm/s；初始 0，编辑范围 0~200。
	 * 0 表示纯自由摆标定；调高会更快恢复自然损失和碰撞减速，调低会更完整保留扰动但可能逐渐耗停。
	 * Level0 正式值必须取“3 分钟实测被动损失 + 少量余量”，不能预设 25，也不构成外物交互门槛。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|摆锤|补能",
		meta = (ClampMin = "0.0", ClampMax = "200.0", UIMin = "0.0", UIMax = "100.0", Units = "cm/s"))
	float MaximumAssistSpeedDeltaPerPass = 0.0f;
};
