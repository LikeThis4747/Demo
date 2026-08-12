// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerConfig.h
 * 职责：集中保存单一追猎者的移动、感知、近战与预判跑跳攻击参数，供角色、攻击组件与 AI 只读消费。
 * 边界：不保存运行时状态，不引用具体关卡或玩家，不实现命中查询、受击响应或伤害结算。
 * 状态 Owner：本资产是追猎者行为调参的唯一来源；运行时对象只读取并执行。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PursuerConfig.generated.h"

class UAnimMontage;
class UCharacterImpactSourceProfile;

/** 单一追猎者原型的行为调参资产；所有属性初值与编辑范围均可在创建资产后直接查看。 */
UCLASS(BlueprintType)
class DEMO_API UPursuerConfig final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 校验数值、距离层级、攻击时序和必填资产；失败时返回具体属性名与原因。 */
	bool IsConfigured(FString& OutError) const;

	/**
	 * 移动组件的最大行走速度，由 APursuerCharacter 写入，单位 cm/s。
	 * 初始值：450；范围：100~1200。应接近或快于玩家(500)以形成持续压力。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|移动", meta = (ClampMin = "100.0", ClampMax = "1200.0", Units = "cm/s"))
	float MaxWalkSpeed = 450.0f;

	/**
	 * 开始追击的察觉半径，单位 cm。玩家进入此半径（且视线通过）后转入追击。
	 * 初始值：1500；范围：300~5000。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|感知", meta = (ClampMin = "300.0", ClampMax = "5000.0", Units = "cm"))
	float SenseRadius = 1500.0f;

	/**
	 * 放弃追击的丢失半径，单位 cm。已追击时玩家超过此半径才脱离，需≥SenseRadius 以避免边界抖动。
	 * 初始值：2000；范围：300~6000。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|感知", meta = (ClampMin = "300.0", ClampMax = "6000.0", Units = "cm"))
	float LoseSightRadius = 2000.0f;

	/**
	 * 是否在开始追击前做一次可见性射线检测。
	 * 初始值：true。关闭后仅按距离追击（隔墙也追），用于纯移动闭环调试。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|感知")
	bool bUseLineOfSight = true;

	/**
	 * 近距离斧击的最大发起距离，单位 cm；AI 在此距离内优先近战，需 < JumpAttackMinRange。
	 * 初始值：220；范围：80~500。调高更容易贴身命中，但会压缩跑跳攻击区间。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击", meta = (ClampMin = "80.0", ClampMax = "500.0", Units = "cm"))
	float AttackRange = 220.0f;

	/**
	 * 两次攻击起手之间的最短间隔，单位 s；组件空闲但仍在冷却时 AI 可以继续追击。
	 * 初始值：1.2；范围：0.3~8.0。恢复窗口和冷却互相独立。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击", meta = (ClampMin = "0.3", ClampMax = "8.0", Units = "s"))
	float AttackCooldown = 1.2f;

	/**
	 * 近距离斧击全身 Montage，由 UPursuerAttackComponent 同步加载；必须在 DA_Pursuer 指定。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击")
	TSoftObjectPtr<UAnimMontage> AttackMontage;

	/** 近距离 Montage 播放倍率；初始值 1.2，调高会同步提前视觉动作但命中时刻仍由 CloseAttackHitDelay 独立决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|近战", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float CloseAttackPlayRate = 1.2f;

	/** 近战起手到 Sweep 的秒数；初始值 0.45，应对齐斧刃通过玩家身体的画面帧。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|近战", meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
	float CloseAttackHitDelay = 0.45f;

	/** 从角色胶囊前方扫掠的长度，单位 cm；初始值 220，不依赖 AxeMesh 组件名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|近战", meta = (ClampMin = "50.0", ClampMax = "500.0", Units = "cm"))
	float CloseAttackReach = 220.0f;

	/** 近战球形 Sweep 半径，单位 cm；初始值 70，调高增加侧向容错。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|近战", meta = (ClampMin = "10.0", ClampMax = "200.0", Units = "cm"))
	float CloseAttackSweepRadius = 70.0f;

	/** 近战命中伤害；初始值 18，由 HealthComponent 通过 ApplyDamage 接收。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|近战", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float CloseAttackDamage = 18.0f;

	/** 近战命中或落空后的不可行动恢复，单位 s；初始值 0.6。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|近战", meta = (ClampMin = "0.05", ClampMax = "3.0", Units = "s"))
	float CloseAttackRecoverySeconds = 0.6f;

	/** 跑跳下砸全身 Montage；使用追猎者骨骼的重定向版本，缺失时该攻击不会启动。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳")
	TSoftObjectPtr<UAnimMontage> JumpAttackMontage;

	/** 跑跳攻击的最小发起距离，单位 cm；初始值 220，必须 ≥ AttackRange。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "100.0", ClampMax = "1000.0", Units = "cm"))
	float JumpAttackMinRange = 220.0f;

	/** 跑跳攻击的最大发起与落点锁定距离，单位 cm；初始值 650，必须 > JumpAttackMinRange。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "200.0", ClampMax = "1500.0", Units = "cm"))
	float JumpAttackMaxRange = 650.0f;

	/** 玩家水平速度的预判时间，单位 s；初始值 0.35，仅在离地时计算一次，空中不追踪。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float JumpAttackLeadSeconds = 0.35f;

	/** Montage 起手到真正 Launch 的秒数；初始值 0.65，应对齐脚离地画面。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
	float JumpAttackLaunchDelay = 0.65f;

	/** 抛物线从离地到预测落点的固定飞行时间，单位 s；初始值 0.65。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "0.2", ClampMax = "1.5", Units = "s"))
	float JumpAttackFlightSeconds = 0.65f;

	/** 落地范围查询半径，单位 cm；初始值 160，给持续横跑玩家可读但可躲的容错。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "50.0", ClampMax = "400.0", Units = "cm"))
	float JumpAttackImpactRadius = 160.0f;

	/** 跑跳下砸命中伤害；初始值 30，高于近战以补偿明显起手和落空风险。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float JumpAttackDamage = 30.0f;

	/** 下砸落地后不可行动恢复，单位 s；初始值 0.75，玩家躲开后可借此拉开距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "0.05", ClampMax = "3.0", Units = "s"))
	float JumpAttackRecoverySeconds = 0.75f;

	/** 跑跳 Montage 播放倍率；初始值 1.3，使约 0.85s 离地/1.70s 落地对齐 0.65s+0.65s 物理时序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|跑跳", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float JumpAttackPlayRate = 1.3f;

	/** 攻击命中后提交给玩家现有 StandingImpact 的来源 Profile；不属于 Heavy Impact。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|反馈")
	TObjectPtr<UCharacterImpactSourceProfile> AttackImpactSourceProfile = nullptr;

	/** StandingImpact 的归一化表现强度；初始值 1，范围 0~1。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击|反馈", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackImpactStrength = 1.0f;

	/**
	 * 无攻击可启动时寻路贴近的目标距离，单位 cm；必须 < AttackRange。
	 * 初始值 100；冷却期间也继续使用此半径追击，不再停在攻击边界等待。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击", meta = (ClampMin = "50.0", Units = "cm"))
	float AttackApproachRadius = 100.0f;

	/**
	 * 受击反应蒙太奇（按命中方向）；允许为空，缺失时只做物理反应不播受击动画。背后命中复用正面。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|受击")
	TSoftObjectPtr<UAnimMontage> HitReactFromFront;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|受击")
	TSoftObjectPtr<UAnimMontage> HitReactFromLeft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|受击")
	TSoftObjectPtr<UAnimMontage> HitReactFromRight;

	/**
	 * AI 状态机 Timer 的思考周期，单位 s；替代常驻 Tick。调小反应更灵敏但更耗，调大更省但迟钝。
	 * 初始值：0.2；范围：0.05~1.0。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|AI", meta = (ClampMin = "0.05", ClampMax = "1.0", Units = "s"))
	float ThinkInterval = 0.2f;
};
