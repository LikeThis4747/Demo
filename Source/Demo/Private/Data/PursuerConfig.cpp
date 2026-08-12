// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerConfig.cpp
 * 职责：实现追猎者感知、近战与预判跑跳攻击参数的无副作用自校验。
 * 边界：只检查数值、距离层级和资源引用，不加载资源、不接触运行时对象。
 */

#include "Data/PursuerConfig.h"

#include "Data/Physics/CharacterImpactSourceProfile.h"

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

	if (JumpAttackMinRange < AttackRange || JumpAttackMaxRange <= JumpAttackMinRange)
	{
		OutError = TEXT("距离层级必须满足 AttackRange ≤ JumpAttackMinRange < JumpAttackMaxRange。");
		return false;
	}

	if (JumpAttackMaxRange >= LoseSightRadius)
	{
		OutError = TEXT("JumpAttackMaxRange 必须 < LoseSightRadius，避免失去目标后仍发起跑跳攻击。");
		return false;
	}

	const bool bInvalidTiming = CloseAttackPlayRate <= 0.0f
		|| CloseAttackHitDelay <= 0.0f
		|| CloseAttackRecoverySeconds <= 0.0f
		|| JumpAttackPlayRate <= 0.0f
		|| JumpAttackLaunchDelay <= 0.0f
		|| JumpAttackFlightSeconds <= 0.0f
		|| JumpAttackRecoverySeconds <= 0.0f
		|| AttackCooldown <= 0.0f;
	if (bInvalidTiming)
	{
		OutError = TEXT("攻击播放倍率、起手、飞行、恢复与冷却参数必须为正数。");
		return false;
	}

	if (CloseAttackReach <= 0.0f
		|| CloseAttackSweepRadius <= 0.0f
		|| JumpAttackImpactRadius <= 0.0f
		|| JumpAttackLeadSeconds < 0.0f
		|| CloseAttackDamage < 0.0f
		|| JumpAttackDamage < 0.0f
		|| !FMath::IsFinite(AttackImpactStrength)
		|| AttackImpactStrength < 0.0f
		|| AttackImpactStrength > 1.0f)
	{
		OutError = TEXT("攻击距离、命中范围、预判、伤害或 AttackImpactStrength 含非法值。");
		return false;
	}

	if (AttackMontage.IsNull())
	{
		OutError = TEXT("AttackMontage 未指定；请在 DA_Pursuer 指定近战斧击 Montage。");
		return false;
	}

	if (JumpAttackMontage.IsNull())
	{
		OutError = TEXT("JumpAttackMontage 未指定；请先将跑跳攻击重定向到追猎者骨骼并创建 Montage。");
		return false;
	}

	if (!IsValid(AttackImpactSourceProfile))
	{
		OutError = TEXT("AttackImpactSourceProfile 未指定；攻击不得用 Heavy Impact 代替站立命中反馈。");
		return false;
	}

	OutError.Reset();
	return true;
}
