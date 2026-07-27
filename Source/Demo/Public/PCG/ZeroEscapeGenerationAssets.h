// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationAssets.h
 * 职责：定义空间 Grid/WFC 参数与可替换结构表现绑定。
 * 边界：Generation Profile 不保存玩法目标；Presentation 只映射 Mesh，不参与拓扑决策。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeGenerationAssets.generated.h"

class UStaticMesh;

/** 所有难度共同遵守的地图规模、房间、路线和求解预算。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeSharedRouteConstraints
{
	GENERATED_BODY()

	/** 二维逻辑网格尺寸；当前不生成多层垂直地图。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint GridSize = FIntPoint(18, 12);

	/** WFC 一个逻辑格的边长；HydroLab 当前结构契约为 600 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0"))
	double LogicalTileSizeCm = 600.0;

	/** 中立正方形房间的边长；当前 2 表示 2x2 逻辑格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rooms", meta = (ClampMin = "1", ClampMax = "4"))
	int32 RoomSizeTiles = 2;

	/** 所有难度共享的中立房间数量；房间内容由未来玩法层决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rooms", meta = (ClampMin = "0", ClampMax = "6"))
	int32 RoomCount = 3;

	/** 最终非空逻辑格数量下限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MinWalkableCellCount = 48;

	/** 最终非空逻辑格数量上限；所有难度共享，避免困难局显著变长。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MaxWalkableCellCount = 72;

	/** 同一轴连续贯通格上限；Straight、T 和 Cross 都计入。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MaxConsecutiveStraightTiles = 4;

	/** Start 到 Exit 的逻辑最短距离上限；不等同于 NavMesh 或实际玩家路线。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1"))
	int32 MaxRequiredRouteLengthTiles = 40;

	/** 全部确定性 WFC 尝试合计允许的 singleton 候选次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxWfcCandidateAttempts = 100000;

	/** 全部尝试合计允许恢复决策帧的次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxWfcBacktrackCount = 25000;

	/** 最多建立的确定性搜索树数；总候选/回溯预算不会随此值放大。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxWfcSolveAttempts = 10;

	/** Start、Exit 和房间 Anchor 相对地板顶面的高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchors", meta = (ClampMin = "0.0"))
	double AnchorHeightCm = 100.0;
};

/** WFC 对 0..15 OpeningMask 的形态权重；路口是可能结果，不是硬配额。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeWfcShapeWeights
{
	GENERATED_BODY()

	/** Empty 权重不能直接换算为空格比例；Count 仍是最终数量硬约束。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 EmptyWeight = 12000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 DeadEndWeight = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 StraightWeight = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 CornerWeight = 80;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 TJunctionWeight = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 CrossWeight = 5;

	int32 GetWeightForMask(uint8 OpeningMask) const;
	int64 GetTotalNonEmptyVariantWeight() const;
	bool IsConfigured(FString& OutError) const;
};

/** 难度当前只重分配非空形态比例，不改变地图规模和房间数量。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeDifficultyDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|WFC")
	FZeroEscapeWfcShapeWeights WfcShapeWeights;
};

/** 空间规则和难度权重的唯一权威 DataAsset。 */
UCLASS(BlueprintType)
class DEMO_API UZeroEscapeLevelGenerationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 任何改变纯逻辑结果的配置修改都必须递增。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
	int32 ProfileVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	FZeroEscapeSharedRouteConstraints SharedRouteConstraints;

	/** 必须恰好包含 Easy、Normal、Hard 各一条；数组顺序不参与确定性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	TArray<FZeroEscapeDifficultyDefinition> Difficulties;

	/** 只读校验所有单资产和跨字段不变量，不自动修正配置。 */
	bool IsConfigured(FString& OutError) const;
};

/** 一类规范结构到一个 StaticMesh 的直接绑定。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeStructureMeshBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	/** 先应用 Pivot 修正，再应用规范结构 Transform；必须有限且 Unit Scale。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FTransform PivotCorrection = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FName CollisionProfileName = TEXT("BlockAll");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	bool bCanEverAffectNavigation = true;
};

/** 五类规范结构的可替换表现配置；WFC 不知道任何具体 Mesh。 */
UCLASS(BlueprintType)
class DEMO_API UZeroEscapePresentationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
	int32 PresentationVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure", meta = (ClampMin = "1.0"))
	double StructureUnitSizeCm = 300.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	double FloorTopZCm = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	double WallBaseZCm = 5.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	double CeilingPivotZCm = 305.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding Floor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding Ceiling;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding Wall;

	/** 可选；为空时结构仍可玩，只缺少墙顶接缝装饰。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding WallTopTrim;

	/** 可选；为空时不生成边界交点柱。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding Pillar;

	bool IsConfigured(double LogicalTileSizeCm, FString& OutError) const;
};
