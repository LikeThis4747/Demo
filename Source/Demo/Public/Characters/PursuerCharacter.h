// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerCharacter.h
 * 职责：装配追猎者角色本体（移动、胶囊、AI、Physics Control 局部受击组件），持有行为参数并提供攻击表现接口。
 * 边界：不做 AI 决策，不拥有物理受击运行态、失衡或伤害结算，不硬编码资源路径和骨骼映射。
 * 状态 Owner：角色只拥有组件装配与攻击播放；AI 和局部物理受击状态分别由专用 Controller/Component 独占。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "PursuerCharacter.generated.h"

class UPursuerConfig;
class UPhysicsControlComponent;
class UPhysicsControlHitResponseComponent;
class UPhysicsControlHitTuningData;

/** 单一追猎者角色：只负责组件装配、移动参数应用与攻击蒙太奇播放，决策交给 AI 控制器。 */
UCLASS()
class DEMO_API APursuerCharacter final : public ACharacter
{
	GENERATED_BODY()

public:
	/** 创建无常驻 Tick 的追猎者，并默认绑定 APursuerAIController 与放置即占有策略。 */
	APursuerCharacter();

	/** 返回行为参数资产；可能为空，调用方需自行判空。 */
	const UPursuerConfig* GetConfig() const { return Config; }

	/** 播放攻击蒙太奇；Config 或蒙太奇缺失时记录错误并安全返回。 */
	void PlayAttackMontage();

protected:
	/** 组件初始化后按 Config 应用移动速度；Config 无效时保留引擎默认并记录错误。 */
	virtual void PostInitializeComponents() override;

private:
	/** 官方 Physics Control 求解组件；只由 PhysicsControlHitResponse 驱动。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "追猎者|物理受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsControlComponent> PhysicsControl;

	/** 局部受击唯一状态 Owner；自身无 Tick，配置由角色显式注入。 */
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
};
