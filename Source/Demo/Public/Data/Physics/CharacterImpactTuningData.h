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

	bool IsConfigured(const USkeletalMeshComponent* Mesh, FString& OutError) const;
};
