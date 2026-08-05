// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactReceiver.h
 * 职责：提供 PCG 可放置机关到玩家/AI 的最小重冲击准备接口。
 * 边界：机关不依赖具体角色类，接口也不允许直接施加冲量。
 */

#pragma once

#include "CoreMinimal.h"
#include "Physics/HeavyImpactTypes.h"
#include "UObject/Interface.h"

#include "HeavyImpactReceiver.generated.h"

/** 标记能够在真实重物接触前准备物理身体的对象。 */
UINTERFACE(BlueprintType)
class DEMO_API UHeavyImpactReceiver : public UInterface
{
	GENERATED_BODY()
};

/** 玩家和 AI 共用的重冲击准备契约。 */
class DEMO_API IHeavyImpactReceiver
{
	GENERATED_BODY()

public:
	/** 请求接收者为指定真实刚体的即将接触做好准备。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Heavy Impact")
	EHeavyImpactPrepareResult PrepareForHeavyImpact(const FHeavyImpactPreparationRequest& Request);
};
