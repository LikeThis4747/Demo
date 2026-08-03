// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePopulationProfile.h
 * 职责：一局要往生成结果里撒哪些玩法对象的权威数据资产；一个条目一类对象。
 * 边界：只描述"放什么/放哪/放多少"，不含放置算法，也不引用生成器运行时状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCG/Population/ZeroEscapePlacementTypes.h"

#include "ZeroEscapePopulationProfile.generated.h"

/** 玩法对象放置规则表；新增一类对象=加一条规则，无需改代码。 */
UCLASS(BlueprintType)
class DEMO_API UZeroEscapePopulationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 按顺序执行的放置规则；每条独立消费一类区域格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population")
	TArray<FZeroEscapePlacementRule> Rules;
};
