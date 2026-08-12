// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePopulationProfile.h
 * 职责：保存一份共享机关/资源装配和三档纯数值 Population 配置。
 * 边界：不保存候选规则链、不执行放置，也不引用生成器运行时状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCG/Population/ZeroEscapePlacementTypes.h"

#include "ZeroEscapePopulationProfile.generated.h"

/** 一局玩法填充的唯一配置资产。 */
UCLASS(BlueprintType)
class DEMO_API UZeroEscapePopulationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population")
	FZeroEscapeHazardPopulationAssembly HazardAssembly;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population")
	FZeroEscapeResourcePopulationAssembly ResourceAssembly;

	/** 必须恰好包含 Easy、Normal、Hard 各一条；数组顺序不参与确定性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Difficulty")
	TArray<FZeroEscapePopulationDifficultySettings> Difficulties;
};
