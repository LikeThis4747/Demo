// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerCharacter.h
 * 职责：装配追猎者移动、攻击、轻/重冲击组件并持有行为参数，作为各独立职责的组合入口。
 * 边界：不做 AI 决策，不直接拥有攻击、物理模拟、失衡或伤害结算状态，不硬编码资源路径和骨骼映射。
 * 状态 Owner：攻击、轻受击和重冲击分别由专用 Component 独占；角色只负责装配与接口转发。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CharacterImpactReceiver.h"
#include "Interfaces/HeavyImpactReceiver.h"

#include "PursuerCharacter.generated.h"

class UAnimMontage;
class UCharacterImpactResponseComponent;
class UCharacterImpactTuningData;
class UHeavyImpactResponseComponent;
class UHeavyImpactTuningData;
class UHealthComponent;
class UPursuerConfig;
class UPhysicalAnimationComponent;
class UPhysicsControlComponent;
class UPhysicsControlHitResponseComponent;
class UPhysicsControlHitTuningData;
class UPursuerAttackComponent;
class USphereComponent;

/** 物理命中在角色本地空间的简化方向（由 PhysicsControlHitResponseComponent 广播）。 */
enum class EPhysicsHitDirection : uint8;

/** 单一追猎者角色：只负责组件装配、移动参数应用与攻击蒙太奇播放，决策交给 AI 控制器。 */
UCLASS()
class DEMO_API APursuerCharacter final
	: public ACharacter
	, public IHeavyImpactReceiver
	, public ICharacterImpactReceiver
{
	GENERATED_BODY()

public:
	/** 创建无常驻 Tick 的追猎者，并默认绑定 APursuerAIController 与放置即占有策略。 */
	APursuerCharacter();

	/** 重冲击预测与真实接触统一使用角色 Skeletal Mesh，而不是外层移动 Capsule。 */
	virtual UPrimitiveComponent* GetHeavyImpactPredictionPrimitive_Implementation() const override;

	/** 把机关的重冲击准备请求转发给唯一共享响应组件。 */
	virtual EHeavyImpactPrepareResult PrepareForHeavyImpact_Implementation(
		const FHeavyImpactPreparationRequest& Request) override;

	virtual EStandingImpactSubmitResult SubmitStandingImpact_Implementation(
		const FStandingImpactRequest& Request) override;

	/** 返回行为参数资产；可能为空，调用方需自行判空。 */
	const UPursuerConfig* GetConfig() const { return Config; }

	/** 返回追猎者攻击事务的唯一 Owner；构造后始终存在，配置无效时组件自行拒绝攻击。 */
	UPursuerAttackComponent* GetAttackComponent() const { return PursuerAttack; }

	/** 返回攻击组件短时驱动的隐藏真实刚体；平时无碰撞，不承担命中查询。 */
	USphereComponent* GetAttackImpactBody() const { return AttackImpactBody; }

	/** Heavy 或 Light Stop 是否阻断追击移动。 */
	bool IsImpactMovementBlocked() const;

	/** Heavy 或任意活动 Light 是否禁止发起新攻击。 */
	bool IsImpactAttackSuppressed() const;

	/** 转发给攻击组件取消当前事务，只停止攻击组件明确记录的 Montage。 */
	void InterruptActiveAttackMontage();

	/** Toggle only the upper-body charge presentation; the future charge executor owns gameplay state. */
	UFUNCTION(BlueprintCallable, Category = "Pursuer|Charge")
	void SetChargeAnimationActive(bool bActive);

	/** 是否正在播放受击反应；AI 在受击期间应停止追击与攻击。 */
	bool IsReacting() const;

	/** 物理命中方向回调；按方向从 Config 选受击蒙太奇播放。 */
	UFUNCTION()
	void HandleHitReact(EPhysicsHitDirection HitDirection);

	/** 受击蒙太奇结束（正常或被打断）时清除受击状态，允许 AI 恢复追击。 */
	void OnHitReactMontageEnded(UAnimMontage* Montage, bool bInterrupted);

protected:
	/** 组件初始化后按 Config 应用移动速度；Config 无效时保留引擎默认并记录错误。 */
	virtual void PostInitializeComponents() override;

private:
	/** 近战与预判跑跳攻击阶段、计时器、命中和恢复的唯一运行时 Owner。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|攻击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPursuerAttackComponent> PursuerAttack;

	/**
	 * 只在既有近战 Sweep 或落地范围先选中玩家后短时启用的真实 Chaos 球体。
	 * 它比可见斧刃更宽容，但仍须由同一刚体真实接触才能提交 Heavy Impact。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|攻击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> AttackImpactBody;

	/** 官方 Physics Control 求解组件；当前只由重冲击响应组件创建和驱动运行时记录。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|物理受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsControlComponent> PhysicsControl;

	/** 追猎者重冲击准备、真实接触、飞行和倒地状态的唯一运行时 Owner。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|重冲击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHeavyImpactResponseComponent> HeavyImpactResponse;

	/** 追猎者站立轻受击、速度恢复与 Heavy 抢占的运行时 Owner。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|轻受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCharacterImpactResponseComponent> CharacterImpactResponse;

	/** 复用现有生命组件接收爆炸 ApplyDamage；归零后的死亡后果不属于本轮。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|属性", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHealthComponent> HealthComponent;

	/** UE 官方局部动画驱动物理组件；只由 CharacterImpactResponse 配置和调节强度。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|轻受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicalAnimationComponent> LightPhysicalAnimation;

	/** 追猎者独立的重冲击 PCA 与判稳参数；必须在 BP_Pursuer 类默认值中指定。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "追猎者|重冲击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHeavyImpactTuningData> HeavyImpactTuningData;

	/** 追猎者轻受击三方向动画和时序参数；必须在 BP_Pursuer 类默认值中指定。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "追猎者|轻受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCharacterImpactTuningData> CharacterImpactTuningData;

	/** 保留用于旧局部受击回退；重冲击原型期间不创建、不配置也不解引用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|物理受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsControlHitResponseComponent> PhysicsControlHitResponse;

	/** 项目自有的局部受击骨骼映射与调参；必须在 BP_Pursuer 的类默认值中指定。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "追猎者|物理受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsControlHitTuningData> PhysicsControlHitTuning;

	/**
	 * 追猎者行为参数唯一来源；对应 UPursuerConfig，由本类与 AI 控制器读取。
	 * 初始值：空，必须在 BP_Pursuer 指定；缺失或非法时记录错误并使用引擎默认移动值，不使用路径兜底。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "追猎者", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPursuerConfig> Config;

	/** 受击反应播放期间为 true；由 HandleHitReact 置位、受击蒙太奇结束回调清除，供 AI 查询以暂停行动。 */
	bool bIsReacting = false;

};
