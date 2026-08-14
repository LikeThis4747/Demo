// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticGrabTuningData.h
 * 职责：集中保存玩家电磁抓取基线的选取、吸取曲线、持有、安全、投掷与 Physics Handle 手感参数。
 * 边界：不引用输入 DataAsset，不保存单个道具身份，也不持有任何运行时抓取状态。
 * 状态 Owner：本资产是全局磁力手感参数的唯一来源；UElectromagneticGrabComponent 只读取并执行。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "MagneticGrabTuningData.generated.h"

/** 玩家磁力抓取基线的独立调参资产；所有属性初值与编辑范围均可在创建资产后直接查看。 */
UCLASS(BlueprintType)
class DEMO_API UMagneticGrabTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 校验数值范围及参数间约束；失败时返回具体 C++ 属性名与原因。 */
	bool IsConfigured(FString& OutError) const;

	/**
	 * 对应 C++ 属性 GrabRange，由 UElectromagneticGrabComponent::FindBestCandidate 读取，单位 cm。
	 * 初始值：1200；编辑范围：100~3000。调高可从更远处抓取但扩大查询体积，调低则要求玩家更靠近。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|选取", meta = (ClampMin = "100.0", ClampMax = "3000.0", UIMin = "100.0", UIMax = "3000.0", Units = "cm"))
	float GrabRange = 1200.0f;

	/**
	 * 对应 C++ 属性 ScreenSelectionRadiusRatio，由 FindBestCandidate 计算准星容错半径，无单位。
	 * 初始值：0.08；编辑范围：0.01~0.50。调高更容易选中准星附近物体但歧义增加，调低则更精确。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|选取", meta = (ClampMin = "0.01", ClampMax = "0.5", UIMin = "0.01", UIMax = "0.25"))
	float ScreenSelectionRadiusRatio = 0.08f;

	/**
	 * 对应 C++ 属性 MaximumGrabMass，由 UMagneticObjectComponent::CanGrab 作为质量上限，单位 kg。
	 * 初始值：80；编辑范围：1~300。调高允许抓更重物体，调低可强化资源选择和重量等级差异。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|选取", meta = (ClampMin = "1.0", ClampMax = "300.0", UIMin = "1.0", UIMax = "300.0", Units = "kg"))
	float MaximumGrabMass = 80.0f;

	/**
	 * 对应 C++ 属性 MaximumCandidateChecks，由 FindBestCandidate 限制单次按键评分数量。
	 * 初始值：32；编辑范围：1~128。调高可处理更密集场景但增加单次选取成本，调低更稳定但可能漏选。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|选取", meta = (ClampMin = "1", ClampMax = "128", UIMin = "1", UIMax = "128"))
	int32 MaximumCandidateChecks = 32;

	/**
	 * 对应 C++ 属性 HoldDistance，由 CalculateDesiredHoldLocation 计算物体前向目标点，单位 cm。
	 * 初始值：220；编辑范围：80~600。调高让物体离玩家更远，调低更贴近身体且更易发生角色周边碰撞。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|持有", meta = (ClampMin = "80.0", ClampMax = "600.0", UIMin = "80.0", UIMax = "600.0", Units = "cm"))
	float HoldDistance = 220.0f;

	/**
	 * 对应 C++ 属性 HoldSideOffset，由 CalculateDesiredHoldLocation 控制相机右方向偏移，单位 cm。
	 * 初始值：60；编辑范围：-300~300。正值移向右侧、负值移向左侧；绝对值过大会偏离投掷瞄准线。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|持有", meta = (ClampMin = "-300.0", ClampMax = "300.0", UIMin = "-300.0", UIMax = "300.0", Units = "cm"))
	float HoldSideOffset = 60.0f;

	/**
	 * 对应 C++ 属性 HoldHeight，由 CalculateDesiredHoldLocation 控制角色原点上方偏移，单位 cm。
	 * 初始值：65；编辑范围：-100~300。调高使物体进入上半屏，调低更接近地面并可能增加擦碰。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|持有", meta = (ClampMin = "-100.0", ClampMax = "300.0", UIMin = "-100.0", UIMax = "300.0", Units = "cm"))
	float HoldHeight = 65.0f;

	/**
	 * 对应 C++ 属性 HeldAngularDamping，由 GrabCandidate 在持有期间临时写入刚体角阻尼。
	 * 初始值：2.5；编辑范围：0~50。调高更快停止自然旋转，调低保留更多碰撞后的摆动和重量感。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|持有", meta = (ClampMin = "0.0", ClampMax = "50.0", UIMin = "0.0", UIMax = "20.0"))
	float HeldAngularDamping = 2.5f;

	/**
	 * 对应 C++ 属性 PullReferenceSpeed，由 UElectromagneticGrabComponent::BeginPull 计算吸取时长，单位 cm/s。
	 * 初始值：1600；编辑范围：500~5000。调高会缩短同距离吸取时间，调低会拉长；它不直接设置真实刚体速度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|吸取曲线", meta = (ClampMin = "500.0", ClampMax = "5000.0", UIMin = "800.0", UIMax = "3000.0", Units = "cm/s"))
	float PullReferenceSpeed = 1600.0f;

	/**
	 * 对应 C++ 属性 MinimumPullDuration，由 BeginPull 限制近距离吸取的最短持续时间，单位 s。
	 * 初始值：0.35；编辑范围：0.1~1。调高可避免近物瞬吸但降低响应，调低更迅速但可能显得机械。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|吸取曲线", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.15", UIMax = "0.75", Units = "s"))
	float MinimumPullDuration = 0.35f;

	/**
	 * 对应 C++ 属性 PullArcHeightRatio，由 BeginPull 按初始移动距离计算弧线高度，无单位。
	 * 初始值：0.06；编辑范围：0~0.2。调高会增强上拱曲线，设为 0 则使用纯直线路径。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|吸取曲线", meta = (ClampMin = "0.0", ClampMax = "0.2", UIMin = "0.0", UIMax = "0.12"))
	float PullArcHeightRatio = 0.06f;

	/**
	 * 对应 C++ 属性 MaximumPullArcHeight，由 BeginPull 限制远距离吸取的最大弧高，单位 cm。
	 * 初始值：90；编辑范围：0~200。调高允许远物形成更明显弧线，调低让远近路径都更直接。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|吸取曲线", meta = (ClampMin = "0.0", ClampMax = "200.0", UIMin = "0.0", UIMax = "120.0", Units = "cm"))
	float MaximumPullArcHeight = 90.0f;

	/**
	 * 对应 C++ 属性 MinimumHoldDistance，由 ResolveSafeHoldLocation 保留最小锚点距离，单位 cm。
	 * 初始值：90；编辑范围：20~300，且不得大于 HoldDistance。调高更不易贴身，调低更容易缩到狭窄空间。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|安全", meta = (ClampMin = "20.0", ClampMax = "300.0", UIMin = "20.0", UIMax = "300.0", Units = "cm"))
	float MinimumHoldDistance = 90.0f;

	/**
	 * 对应 C++ 属性 ObstructionClearance，由 ResolveSafeHoldLocation 保持锚点与阻挡面的间隙，单位 cm。
	 * 初始值：18；编辑范围：0~100。调高更不易穿墙但离障碍更远，调低更贴墙但碰撞抖动风险增加。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|安全", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0", Units = "cm"))
	float ObstructionClearance = 18.0f;

	/**
	 * 对应 C++ 属性 ObstructionReleaseDelay，由 TickComponent 判断连续阻挡多久后自动释放，单位 s。
	 * 初始值：0.35；编辑范围：0.05~3。调高更容忍短暂卡墙，调低更快解除但可能让玩家感觉物体易掉落。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|安全", meta = (ClampMin = "0.05", ClampMax = "3.0", UIMin = "0.05", UIMax = "3.0", Units = "s"))
	float ObstructionReleaseDelay = 0.35f;

	/**
	 * 对应 C++ 属性 PullGracePeriod，由 TickComponent 在曲线结束后延迟启用稳定误差断开，单位 s。
	 * 初始值：0.75；编辑范围：0~3。调高给重物更多追上持有锚点的时间，调低会更早判定抓取不稳定。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|安全", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "3.0", Units = "s"))
	float PullGracePeriod = 0.75f;

	/**
	 * 对应 C++ 属性 MaximumHoldError，由 TickComponent 限制质心与安全锚点的稳定误差，单位 cm。
	 * 初始值：700；编辑范围：100~2000。调高更不易自动断开但可能拖拽物体过远，调低更安全但重物易脱手。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|安全", meta = (ClampMin = "100.0", ClampMax = "2000.0", UIMin = "100.0", UIMax = "2000.0", Units = "cm"))
	float MaximumHoldError = 700.0f;

	/**
	 * 对应 C++ 属性 ThrowSpeed，由 ThrowHeldObject 作为基础目标速度，单位 cm/s。
	 * 初始值：2500；编辑范围：100~8000。调高投掷更快更远，调低更有重量感；仍会乘以单个物体 ThrowSpeedMultiplier。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|投掷", meta = (ClampMin = "100.0", ClampMax = "8000.0", UIMin = "100.0", UIMax = "8000.0", Units = "cm/s"))
	float ThrowSpeed = 2500.0f;

	/**
	 * 对应 C++ 属性 ThrownWeaponActiveDuration，由 ThrowHeldObject 决定投掷物保持"攻击性"标记的时长，单位 s。
	 * 初始值：2.5；编辑范围：0.1~10。此时长内撞到追猎者算受击，超时后物体回归普通物体不再触发受击。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|投掷", meta = (ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.5", UIMax = "5.0", Units = "s"))
	float ThrownWeaponActiveDuration = 2.5f;

	/** 光球系统接入前可直接测试的爆裂投掷次数；只在爆裂物成功建立正式投掷事务后扣除。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "0", ClampMax = "999", UIMin = "0", UIMax = "50"))
	int32 InitialExplosionCharges = 10;

	/** 激活爆裂状态后允许按 E 取消的最短时间；不限制激活后立即投掷。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "2.0", Units = "s"))
	float ExplosionModeCancelLockSeconds = 1.0f;

	/** 首次合格阻挡命中产生的 Pawn 球形查询半径。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "50.0", ClampMax = "2000.0", UIMin = "100.0", UIMax = "1000.0", Units = "cm"))
	float ExplosionRadius = 350.0f;

	/** 爆心处沿水平径向施加给 Heavy 身体的速度改变量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "0.0", ClampMax = "5000.0", UIMin = "0.0", UIMax = "2500.0", Units = "cm/s"))
	float ExplosionHorizontalVelocityChange = 900.0f;

	/** 爆心处向上施加给 Heavy 身体的速度改变量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "0.0", ClampMax = "3000.0", UIMin = "0.0", UIMax = "1500.0", Units = "cm/s"))
	float ExplosionUpwardVelocityChange = 450.0f;

	/** 半径边缘相对爆心仍保留的冲量与伤害比例。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float ExplosionEdgeEffectScale = 0.6f;

	/** 每个角色的水平径向随机偏转上限，正负范围对称。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "0.0", ClampMax = "45.0", UIMin = "0.0", UIMax = "30.0", Units = "deg"))
	float ExplosionDirectionJitterDegrees = 10.0f;

	/** 每个角色强度的对称随机比例；0.1 表示在基础结果上乘以 0.9~1.1。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "0.0", ClampMax = "0.5", UIMin = "0.0", UIMax = "0.3"))
	float ExplosionStrengthJitterRatio = 0.1f;

	/** 爆心伤害；半径边缘与 Heavy 速度使用相同距离衰减。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "0.0", ClampMax = "1000.0", UIMin = "0.0", UIMax = "200.0"))
	float ExplosionDamage = 30.0f;

	/** 只放大本次爆裂破碎的现有碎片分离速度；普通破碎始终使用 1。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|爆裂投掷", meta = (ClampMin = "1.0", ClampMax = "10.0", UIMin = "1.0", UIMax = "5.0"))
	float ExplosionFragmentSeparationMultiplier = 2.5f;

	/**
	 * 对应 C++ 属性 AimTraceDistance，由 CalculateAimPoint 决定准星射线最大距离，单位 cm。
	 * 初始值：10000；编辑范围：1000~50000。调高支持远距离瞄准，调低会让无命中投掷更早收敛。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|投掷", meta = (ClampMin = "1000.0", ClampMax = "50000.0", UIMin = "1000.0", UIMax = "50000.0", Units = "cm"))
	float AimTraceDistance = 10000.0f;

	/**
	 * 对应 C++ 属性 HandleLinearStiffness，由 Configure 写入 UPhysicsHandleComponent 线性刚度。
	 * 初始值：850；编辑范围：1~10000。调高跟随更紧但可能抖动，调低吸取更软、更慢；不允许 0 以免完全失去拉力。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|Physics Handle", meta = (ClampMin = "1.0", ClampMax = "10000.0", UIMin = "1.0", UIMax = "10000.0"))
	float HandleLinearStiffness = 850.0f;

	/**
	 * 对应 C++ 属性 HandleLinearDamping，由 Configure 写入 UPhysicsHandleComponent 线性阻尼。
	 * 初始值：120；编辑范围：0~5000。调高减少超调但响应更钝，调低更活跃但可能产生往复摆动。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|Physics Handle", meta = (ClampMin = "0.0", ClampMax = "5000.0", UIMin = "0.0", UIMax = "5000.0"))
	float HandleLinearDamping = 120.0f;

	/**
	 * 对应 C++ 属性 HandleInterpolationSpeed，由 Configure 写入 Physics Handle 目标插值速度。
	 * 初始值：50；编辑范围：0.1~200。调高目标更新更直接，调低更平滑但转身滞后更明显；0 会停止目标追随，因此不允许；改动后需重启 PIE。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "磁力手感|Physics Handle", meta = (ClampMin = "0.1", ClampMax = "200.0", UIMin = "0.1", UIMax = "200.0"))
	float HandleInterpolationSpeed = 50.0f;
};
