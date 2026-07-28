// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerConfig.cpp
 * 职责：实现追猎者参数资产的自校验。
 * 边界：只做数值范围与参数间一致性检查，不加载资源、不接触运行时对象。
 */

#include "Data/PursuerConfig.h"

bool UPursuerConfig::IsConfigured(FString& OutError) const
{
	if (LoseSightRadius < SenseRadius)
	{
		OutError = TEXT("LoseSightRadius 必须 ≥ SenseRadius，否则追击会在边界抖动。");
		return false;
	}

	if (AttackRange >= SenseRadius)
	{
		OutError = TEXT("AttackRange 必须 < SenseRadius，否则追猎者未察觉就已在攻击距离内。");
		return false;
	}

	if (AttackApproachRadius >= AttackRange)
	{
		OutError = TEXT("AttackApproachRadius 必须小于 AttackRange。");
		return false;
	}

	// [临时-A] 项目暂无攻击动画，第一步先让 AttackMontage 可选以验证追击闭环；有了攻击动画后恢复以下必填校验。
	// if (AttackMontage.IsNull())
	// {
	// 	OutError = TEXT("AttackMontage 未指定；请在 DA_Pursuer 指定一个攻击蒙太奇。");
	// 	return false;
	// }

	return true;
}
