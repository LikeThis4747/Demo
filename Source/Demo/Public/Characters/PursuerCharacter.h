// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerCharacter.h
 * 职责：装配追猎者角色本体（移动、胶囊、AI 控制器绑定），持有行为参数并对外提供攻击表现接口。
 * 边界：不做 AI 决策（由 APursuerAIController 负责），不实现物理受击/失衡/伤害（第二步），不硬编码资源路径。
 * 状态 Owner：角色只拥有自身组件与攻击蒙太奇的播放表现；行为参数来源为 UPursuerConfig。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "PursuerCharacter.generated.h"

class UPursuerConfig;

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
	/**
	 * 追猎者行为参数唯一来源；对应 UPursuerConfig，由本类与 AI 控制器读取。
	 * 初始值：空，必须在 BP_Pursuer 指定；缺失或非法时记录错误并使用引擎默认移动值，不使用路径兜底。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "追猎者", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPursuerConfig> Config;
};
