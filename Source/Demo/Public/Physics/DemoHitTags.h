// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file DemoHitTags.h
 * 职责：定义跨系统的受击契约 Tag，作为"攻击性物体"的唯一标识来源。
 * 边界：只提供 Tag 名，不含逻辑；磁力投掷/陷阱等攻击源添加此 Tag，追猎者受击据此判定。
 */

#pragma once

#include "CoreMinimal.h"

namespace DemoHitTags
{
	/**
	 * 攻击性抛射物 Tag：被玩家投掷或被陷阱击飞的物体在有效期内携带此 Tag。
	 * 追猎者受击只认带此 Tag 的物体，从而区分"被当作武器打出的物体"与"路上撞到的普通物体"。
	 * 用函数内 static 局部变量避免全局 FName 静态初始化顺序问题。
	 */
	inline const FName& AttackProjectile()
	{
		static const FName Tag(TEXT("AttackProjectile"));
		return Tag;
	}
}
