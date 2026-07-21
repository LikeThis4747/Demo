// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticObjectComponent.h
 * 职责：声明 Actor 是否是可被磁力系统识别的物理道具，并保存少量单物体差异。
 * 边界：不保存玩家全局抓取手感、不控制 Chaos 刚体，也不持有抓取状态。
 * 状态 Owner：本组件只拥有道具身份配置；全局角阻尼等持有策略属于 MagneticGrabTuningData。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "MagneticObjectComponent.generated.h"

class UPrimitiveComponent;

/** 给 Actor 添加可磁吸标记，并提供选取优先级与单物体投掷倍率。 */
UCLASS(ClassGroup = (Magnetism), meta = (BlueprintSpawnableComponent))
class DEMO_API UMagneticObjectComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的纯配置组件；磁性资格只在玩家输入触发的选取中读取。 */
	UMagneticObjectComponent();

	/** 检查磁性标记、刚体模拟状态和全局质量上限，决定候选组件是否可抓取。 */
	bool CanGrab(const UPrimitiveComponent* CandidateComponent, float MaxAllowedMass) const;

	/**
	 * 对应 C++ 属性 bMagnetizable；初始值：true。
	 * 关闭后该 Actor 不进入磁力候选，但不会修改碰撞或 Chaos 模拟状态。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体")
	bool bMagnetizable = true;

	/**
	 * 对应 C++ 属性 SelectionPriority，由 FindBestCandidate 处理重叠候选，无单位。
	 * 初始值：1；编辑范围：0~10。调高更容易在重叠轮廓中胜出，调低则让其他道具优先。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0"))
	float SelectionPriority = 1.0f;

	/**
	 * 对应 C++ 属性 ThrowSpeedMultiplier，由 ThrowHeldObject 乘到全局 ThrowSpeed，无单位。
	 * 初始值：1；编辑范围：0.1~3。调高让特殊轻型道具飞得更快，调低可表现重型或吸能道具。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体", meta = (ClampMin = "0.1", ClampMax = "3.0", UIMin = "0.1", UIMax = "3.0"))
	float ThrowSpeedMultiplier = 1.0f;
};
