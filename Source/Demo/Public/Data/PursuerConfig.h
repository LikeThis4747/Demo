// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerConfig.h
 * 职责：集中保存单一追猎者的移动、感知、攻击节奏与思考周期参数，供追猎者角色与其 AI 状态机只读消费。
 * 边界：不保存运行时状态，不引用具体关卡或玩家，不实现物理受击（第二步）与伤害结算。
 * 状态 Owner：本资产是该追猎者原型行为参数的唯一来源；APursuerCharacter 与 APursuerAIController 只读取并执行。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PursuerConfig.generated.h"

class UAnimMontage;

/** 单一追猎者原型的行为调参资产；所有属性初值与编辑范围均可在创建资产后直接查看。 */
UCLASS(BlueprintType)
class DEMO_API UPursuerConfig final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 校验数值范围及参数间约束（丢失半径≥察觉半径、攻击距离<察觉半径、攻击蒙太奇已指定）；失败时返回具体属性名与原因。 */
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
	 * 攻击距离，单位 cm。既作为发起攻击的判定距离，也作为寻路到达半径（移动止于此距离）。需 < SenseRadius。
	 * 初始值：180；范围：80~500。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击", meta = (ClampMin = "80.0", ClampMax = "500.0", Units = "cm"))
	float AttackRange = 180.0f;

	/**
	 * 两次攻击之间的最短间隔，单位 s。冷却期间保持面向玩家但不移动、不再次攻击。
	 * 初始值：2.0；范围：0.3~8.0。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击", meta = (ClampMin = "0.3", ClampMax = "8.0", Units = "s"))
	float AttackCooldown = 2.0f;

	/**
	 * 攻击蒙太奇，由 APursuerCharacter::PlayAttackMontage 同步加载并播放。
	 * 初始值：空，必须在 DA_Pursuer 指定；缺失时记录错误并跳过攻击，不使用路径兜底。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "追猎者|攻击")
	TSoftObjectPtr<UAnimMontage> AttackMontage;

	/**
	 * 攻击时寻路贴近的目标距离，单位 cm；必须 < AttackRange。
	 * 作用：追击到比攻击判定更近处，一旦进入攻击距离即主动停下攻击，避免停在 AttackRange 边界导致的攻击抖动。
	 * 初始值：100；范围：50~AttackRange。
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
