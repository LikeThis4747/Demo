// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file CharacterImpactTuningData.h
 * 职责：保存单类角色执行轻受击所需的三方向动画与时间上限。
 * 边界：不决定来源结果、不保存接收者类别，也不创建新的 AnimBP 或 Slot。
 */

#pragma once

#include "Animation/AnimSequenceBase.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"

#include "CharacterImpactTuningData.generated.h"

class USkeletalMeshComponent;

UCLASS(BlueprintType)
class DEMO_API UCharacterImpactTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Animation")
	TObjectPtr<UAnimSequenceBase> FrontReaction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Animation")
	TObjectPtr<UAnimSequenceBase> LeftReaction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Animation")
	TObjectPtr<UAnimSequenceBase> RightReaction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Animation",
		meta = (ClampMin = "0.0", ClampMax = "0.5", Units = "s"))
	float MontageBlendInSeconds = 0.08f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Animation",
		meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float MontageBlendOutSeconds = 0.12f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Animation",
		meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Timing",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumStrengthDurationScale = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Timing",
		meta = (ClampMin = "0.1", ClampMax = "5.0", Units = "s"))
	float MaxContinuousLightSeconds = 1.5f;

	/** 为当前角色启用由 UPhysicalAnimationComponent 驱动的短暂上半身物理表现。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation")
	bool bEnablePhysicalReaction = false;

	/** 只模拟该 Physics Asset Body 及其子 Body；pelvis、腿和脚不得包含在内。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation")
	FName UpperBodyRootBone = NAME_None;

	/** 胸腹、头部及不明确的上半身命中统一在该 Body 施加表现冲量。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation")
	FName TorsoImpulseBone = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation")
	FName LeftArmImpulseBone = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation")
	FName RightArmImpulseBone = NAME_None;

	/**
	 * UE 官方 Physical Animation 的局部角向驱动参数。
	 * Chaos 将 MaxAngularForce=0 解释为不限制；本项目要求有限正值，避免马达把上半身焊成刚体墙。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation")
	FPhysicalAnimationData PhysicalAnimationSettings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation",
		meta = (ClampMin = "0.0"))
	float PhysicalImpulseAtFullStrength = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation",
		meta = (ClampMin = "0.0", ClampMax = "0.25", Units = "s"))
	float PhysicalHoldSeconds = 0.08f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation",
		meta = (ClampMin = "0.05", ClampMax = "0.5", Units = "s"))
	float PhysicalBlendOutSeconds = 0.18f;

	/** 不同 ImpactId 也不能无限刷新同一次局部模拟会话。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Standing Impact|Physical Presentation",
		meta = (ClampMin = "0.1", ClampMax = "2.0", Units = "s"))
	float MaxContinuousPhysicalSeconds = 0.75f;

	bool IsConfigured(const USkeletalMeshComponent* Mesh, FString& OutError) const;
	bool IsPhysicalReactionConfigured(const USkeletalMeshComponent* Mesh, FString& OutError) const;
};
