// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlacementTypes.h
 * 职责：定义生成后玩法对象放置层的纯数据规则；一条规则=一类要撒进关卡的 Actor 及其放置约束。
 * 边界：只保存纯值，不引用具体资产实例、不执行放置、不依赖生成器内部结构。
 */

#pragma once

#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapePlacementTypes.generated.h"

/** 单条放置规则：把某类 Actor 按区域语义与稀疏度撒入生成结果。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapePlacementRule
{
	GENERATED_BODY()

	/** 要放置的 Actor 类（软引用，避免硬加载；地刺、铁板、奖励等任意 Actor）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	TSoftClassPtr<AActor> ActorClass;

	/** 只放在此区域语义的格子上（走廊/房间/…），复用生成器空间标签。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	EZeroEscapeGridRegionKind TargetRegionKind = EZeroEscapeGridRegionKind::Corridor;

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

	/** 避开 Start/Exit 及其相邻格，避免一出生就命中或堵住出口。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bAvoidStartExitNeighbors = true;

	/** 仅放在直走的走廊格（排除拐角/T型/十字/死胡同），避免横向并排挡不住路。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bStraightCorridorOnly = true;

	/** 生成时在候选点世界 Z 上叠加的高度偏移（cm）；物理物体可抬高一点自然落下，贴地物填 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0"))
	float SpawnZOffsetCm = 0.0f;
};
