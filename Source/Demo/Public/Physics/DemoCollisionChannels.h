// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file DemoCollisionChannels.h
 * 职责：集中声明本项目在 DefaultEngine.ini 中注册的自定义碰撞通道。
 * 边界：这里只提供 C++ 常量映射，不创建 Profile，也不在运行时猜测通道用途。
 */

#pragma once

#include "Engine/EngineTypes.h"

namespace Demo::CollisionChannels
{
	/** 磁力物正式投掷期间使用的临时 Object Channel。 */
	inline constexpr ECollisionChannel AttackProjectileBody = ECC_GameTraceChannel1;
}
