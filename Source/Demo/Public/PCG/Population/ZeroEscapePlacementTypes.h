// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlacementTypes.h
 * 职责：定义普通可走格上的玩法对象放置规则；一条规则=一类 Actor 及其数量/间距约束。
 * 边界：只保存纯值，不引用具体资产实例、不执行放置、不依赖生成器内部结构。
 */

#pragma once

#include "CoreMinimal.h"

#include "ZeroEscapePlacementTypes.generated.h"

class AActor;

/** 单条放置规则：只消费生成器明确返回的普通玩法候选格。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapePlacementRule
{
	GENERATED_BODY()

	/** 要放置的 Actor 类（软引用，避免硬加载；地刺、铁板、奖励等任意 Actor）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	TSoftClassPtr<AActor> ActorClass;

	/** 稀疏度：约每这么多个候选格放一个；越大越稀疏。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "1"))
	int32 OneEveryNCells = 4;

	/** 本条规则单局最多放置几处（每处含 LateralCount 个并排实例）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "1"))
	int32 MaxCount = 8;

	/** 同一格沿走廊横向并排的实例数；默认 2，组成一道横挡走廊的刺墙。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "1"))
	int32 LateralCount = 2;

	/** 横向相邻实例中心间距（cm）；略大于缩放后单个宽度，两侧留缝防穿模。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0"))
	float LateralSpacing = 300.0f;

	/** 避开玩家、追猎者、Exit 及其同层相邻格，避免出生命中或堵住出口。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bAvoidStartExitNeighbors = true;

	/** 仅放在直走普通格（排除拐角/T 型/十字/死胡同）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bStraightCorridorOnly = true;

	/** 生成时在候选点世界 Z 上叠加的高度偏移（cm）；物理物体可抬高一点自然落下，贴地物填 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0"))
	float SpawnZOffsetCm = 0.0f;
};
