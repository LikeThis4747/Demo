// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationAssets.h
 * 职责：定义多层 Grid/WFC、完整结构、难度调参与可替换表现绑定。
 * 边界：Generation Profile 不引用表现素材；Presentation 不参与结构放置或拓扑决策。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeGenerationAssets.generated.h"

class AActor;
class UStaticMesh;

/** 所有难度共同遵守的网格、层高和层内直线约束。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeSharedRouteConstraints
{
	GENERATED_BODY()

	/** 每层尺寸完全来自当前 DataAsset，不在代码或测试中固定某个长宽。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint GridSize = FIntPoint(18, 12);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0", Units = "cm"))
	double LogicalTileSizeCm = 600.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0", Units = "cm"))
	double FloorHeightCm = 450.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MaxConsecutiveStraightTiles = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchors", meta = (ClampMin = "0.0", Units = "cm"))
	double AnchorHeightCm = 100.0;
};

/** 结构定义中的一条局部无向连接。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeLocalCellConnection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FIntVector FirstCell = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FIntVector SecondCell = FIntVector::ZeroValue;
};

/** 一个可由合法组合选择开放或封闭的结构连接口。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeStructureOpeningDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FName OpeningId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FIntVector LocalWalkableCell = FIntVector::ZeroValue;

	/** 必须恰好是 North、East、South、West 中的一个。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	EZeroEscapeOpenEdge OutwardEdge = EZeroEscapeOpenEdge::North;
};

/** 楼梯每层落脚平台用于距离、表现和导航检查的代表格。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeStructureLandingDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FName LandingId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FIntVector LocalCoordinate = FIntVector::ZeroValue;
};

/** 一种结构实际允许开放的连接口组合。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeStructureOpeningSetDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FName SetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	TArray<FName> OpenOpeningIds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure", meta = (ClampMin = "1"))
	int32 SelectionWeight = 1;
};

/** 一座楼梯或高天花板房间的纯逻辑配方，不引用任何表现资源。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeStructureDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FName DefinitionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	EZeroEscapeStructureKind Kind = EZeroEscapeStructureKind::TwoFloorStair;

	/** 放置该结构必须真实存在的楼层数：双层楼梯 2、三层楼梯间 3、高厅 1。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure", meta = (ClampMin = "1", ClampMax = "3"))
	int32 RequiredFloorCount = 2;

	/** 只允许高厅开启；顶层放置时可裁掉真实顶层以上的 Clearance。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	bool bAllowClearanceAboveGeneratedTopFloor = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	TArray<FIntVector> WalkableCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	TArray<FIntVector> SolidCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	TArray<FIntVector> ClearanceCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	TArray<FZeroEscapeLocalCellConnection> InternalConnections;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	TArray<FZeroEscapeStructureOpeningDefinition> Openings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	TArray<FZeroEscapeStructureLandingDefinition> Landings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	TArray<FZeroEscapeStructureOpeningSetDefinition> AllowedOpeningSets;
};

/** 所有难度共享且只能在 C++ 硬上限内调低的生成与导航预算。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeSharedGenerationBudget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MaxWholeLayoutAttempts = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "250000"))
	int32 MaxStructureCandidateEvaluations = 250000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "100000"))
	int32 MaxWfcCandidateAttemptsPerFloor = 100000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "25000"))
	int32 MaxWfcBacktrackCountPerFloor = 25000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxWfcSolveAttemptsPerFloor = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation", meta = (ClampMin = "0.1", ClampMax = "10.0", Units = "s"))
	double NavigationBuildTimeoutSeconds = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation", meta = (ClampMin = "3", ClampMax = "20"))
	int32 MaxNavigationValidationPoints = 20;
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

/** 一个非负整数目标及其抽取权重。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeWeightedCount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Count", meta = (ClampMin = "0"))
	int32 Count = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Count", meta = (ClampMin = "0"))
	int32 Weight = 0;
};

/** 每对相邻楼层额外生成 0、1、2 座双层楼梯的权重。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeAdditionalTwoFloorStairWeights
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stairs", meta = (ClampMin = "0"))
	int32 ZeroAdditionalWeight = 50;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stairs", meta = (ClampMin = "0"))
	int32 OneAdditionalWeight = 40;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stairs", meta = (ClampMin = "0"))
	int32 TwoAdditionalWeight = 10;
};

/** 高天花板房间对整栋和单层的数量、间距约束。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeHighCeilingRoomSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "High Ceiling Rooms", meta = (ClampMin = "0"))
	int32 MinimumTotalCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "High Ceiling Rooms", meta = (ClampMin = "0"))
	int32 MaxCountPerFloor = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "High Ceiling Rooms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinimumSeparationRatio = 0.0;
};

/** 某一实际楼层数下的规模、路线和额外结构上限。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeFloorCountOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floors", meta = (ClampMin = "2", ClampMax = "4"))
	int32 FloorCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floors", meta = (ClampMin = "0"))
	int32 SelectionWeight = 1;

	/** 每层总可走格下限，包含普通格和结构自带 Walkable Cell。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", meta = (ClampMin = "1"))
	int32 MinTotalWalkableCellCountPerFloor = 1;

	/** 每层总可走格上限，包含普通格和结构自带 Walkable Cell。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", meta = (ClampMin = "1"))
	int32 MaxTotalWalkableCellCountPerFloor = 1;

	/** 每层由普通二维 WFC 提供的最低内容量，不被楼梯或高厅替代。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", meta = (ClampMin = "1"))
	int32 MinOrdinaryWalkableCellCountPerFloor = 1;

	/** 玩家出生点到顶层 Exit 的整栋逻辑最短路上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1"))
	int32 MaxPlayerToExitRouteLengthTiles = 1;

	/** 整栋额外双层楼梯上限，不包含每对相邻楼层必需的一座。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stairs", meta = (ClampMin = "0"))
	int32 MaxAdditionalTwoFloorStairCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "High Ceiling Rooms")
	TArray<FZeroEscapeWeightedCount> HighCeilingRoomTargetCounts;
};

/** 一档难度的楼层、规模、额外楼梯和高厅概率；安全预算不在这里。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeDifficultyDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|WFC")
	FZeroEscapeWfcShapeWeights WfcShapeWeights;

	/** 必须包含互不重复的 2、3、4 层选项；权重允许为 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|Floors")
	TArray<FZeroEscapeFloorCountOption> FloorCountOptions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|Stairs")
	FZeroEscapeAdditionalTwoFloorStairWeights AdditionalTwoFloorStairsPerFloorPair;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|Stairs", meta = (ClampMin = "0", ClampMax = "100", Units = "Percent"))
	int32 ThreeFloorStairwellChancePercent = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|Route", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinRequiredEndpointSpatialSeparationRatio = 0.65;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|Route", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinRequiredRouteCoverageRatio = 0.75;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|Stairs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinAdditionalStairSeparationRatio = 0.25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|Spawn", meta = (ClampMin = "0.0", Units = "cm"))
	double MinPlayerPursuerRouteDistanceCm = 1200.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|High Ceiling Rooms")
	FZeroEscapeHighCeilingRoomSettings HighCeilingRooms;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	FZeroEscapeSharedGenerationBudget SharedBudget;

	/** DefinitionId 全局唯一；同一种 Kind 可以配置多个候选定义。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structures")
	TArray<FZeroEscapeStructureDefinition> StructureDefinitions;

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

	/**
	 * 是否为本套表现生成室内顶灯。关闭后灯类与修正 Transform 不参与校验，
	 * Generator 也不会创建灯 Actor，便于不改拓扑地临时回退照明表现。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
	bool bSpawnCeilingLights = true;

	/**
	 * 每个选中逻辑格生成的原始灯具 Actor 类。当前绑定 HydroLab LampA；
	 * 灯的强度、颜色、半径和材质仍由该 Blueprint 自己拥有，PCG 不复制这些参数。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
	TSubclassOf<AActor> CeilingLightActorClass;

	/**
	 * 先相对“逻辑格中心 + CeilingPivotZCm”应用的素材 Pivot 修正。
	 * 必须为有限 Unit Scale Transform；当前 LampA 使用零平移与 Roll=180° 朝向室内。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
	FTransform CeilingLightCellTransform = FTransform::Identity;

	/** 校验结构绑定，以及启用时的顶灯 Actor 类和 Pivot 修正。 */
	bool IsConfigured(double LogicalTileSizeCm, FString& OutError) const;
};
