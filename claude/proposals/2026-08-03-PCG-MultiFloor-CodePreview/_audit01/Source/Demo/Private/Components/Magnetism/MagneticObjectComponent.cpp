// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticObjectComponent.cpp
 * 职责：实现磁性道具的资格判断，不复制 Chaos 模拟、碰撞或玩家磁力手感。
 * 边界：只读取自身标记、候选刚体状态和调用方传入的全局质量上限。
 * 状态 Owner：本组件不写入刚体，也没有运行时持有状态。
 */

#include "Components/Magnetism/MagneticObjectComponent.h"

#include "Components/PrimitiveComponent.h"

/** 初始化为纯事件驱动的道具配置组件，禁止无意义的常驻 Tick。 */
UMagneticObjectComponent::UMagneticObjectComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/** 只有启用磁性、正在模拟物理且质量不超过玩家能力上限的 PrimitiveComponent 才可抓取。 */
bool UMagneticObjectComponent::CanGrab(const UPrimitiveComponent* CandidateComponent, const float MaxAllowedMass) const
{
	return bMagnetizable
		&& IsValid(CandidateComponent)
		&& CandidateComponent->IsSimulatingPhysics()
		&& CandidateComponent->GetMass() <= MaxAllowedMass;
}
