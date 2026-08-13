// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file CharacterImpactSourceProfile.h
 * 职责：由策划为单一轻受击来源声明玩家/追猎者结果和该物理来源自己的冲量范围。
 * 边界：不保存角色动画、Heavy 门槛或运行时状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Physics/CharacterImpactTypes.h"

#include "CharacterImpactSourceProfile.generated.h"

UCLASS(BlueprintType)
class DEMO_API UCharacterImpactSourceProfile final : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Mapping")
	FStandingImpactReactionSpec PlayerReaction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Mapping")
	FStandingImpactReactionSpec PursuerReaction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Source",
		meta = (ClampMin = "0.0"))
	float MinimumPhysicalImpulse = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Source",
		meta = (ClampMin = "0.0"))
	float FullStrengthPhysicalImpulse = 10000.0f;

	/**
	 * 达到 MinimumPhysicalImpulse 的有效攻击至少输出该归一化强度。
	 * 这是项目的策划表现保底，不代表 Chaos 的真实动量比例；零值保持旧线性映射。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Mapping",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumResponseStrength = 0.0f;

	const FStandingImpactReactionSpec& GetReaction(EImpactReceiverCategory Category) const;
	float NormalizePhysicalImpulse(float ImpulseMagnitude) const;
	bool IsConfigured(FString& OutError) const;
};
