// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationAssets.h
 * 职责：定义纯 Grid/WFC 的策划参数与可替换结构表现绑定。
 * 边界：Profile 不保存求解状态；Presentation 只把五类规范结构映射到素材，不参与连通性决策。
 * 替换素材：只要新素材满足 300 cm 结构单元和 PivotCorrection 契约，就不需要修改 WFC 或流程代码。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeGenerationAssets.generated.h"

class UStaticMesh;

/** 所有难度共同遵守的空间和回头路约束。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeSharedRouteConstraints
{
	GENERATED_BODY()

	/** 二维逻辑网格尺寸；首版不生成多层垂直地图。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint GridSize = FIntPoint(18, 12);

	/** WFC 的一个逻辑格边长；HydroLab 首版固定为 600 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0"))
	double LogicalTileSizeCm = 600.0;

	/** Objective 房间的正方形边长（逻辑格）；首版 2 表示 2x2 房间。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1", ClampMax = "4"))
	int32 RoomSizeTiles = 2;

	/** 把起终点之间分成多少个可放目标的进度带；每带提供上下两个候选房间槽。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression", meta = (ClampMin = "1", ClampMax = "6"))
	int32 ObjectiveProgressBandCount = 3;

	/** Required 路线外允许 WFC 产生局部支路的包络半径；0 表示只保留必需结构。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "0", ClampMax = "3"))
	int32 OptionalEnvelopeRadius = 1;

	/** 起点到终点或满足通关条件后的必需路线最大步数，所有难度共用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1"))
	int32 MaxRequiredRouteLengthTiles = 40;

	/** 相对直接起终点最短路，通关路线最多允许增加的格步数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0"))
	int32 MaxRequiredRouteExtraTiles = 14;

	/** Player、Exit 与 Objective Anchor 相对地板顶面的默认高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gameplay", meta = (ClampMin = "0.0"))
	double GameplayAnchorHeightCm = 100.0;
};

/** 一局难度只控制局部可选复杂度和目标数量，不改变共同路线长度上限。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeDifficultyDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	/** 最多保留的短侧支数量；这是上限，不要求每局必须达到。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxOptionalSideBranches = 2;

	/** 最多保留的前向重连数量；它增加路线选择，但不强制玩家回到早期区域。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxOptionalForwardLinks = 1;

	/** Collect 流程生成的候选目标总数 N。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "12"))
	int32 ObjectiveCandidateCount = 3;

	/** CollectKOfN 流程所需目标数 K；CollectAll 会忽略该值并使用 K=N。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "12"))
	int32 RequiredObjectiveCount = 2;
};

/** 可被 Request 选择的流程语义；具体目标类型留给玩法层解释。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeFlowDefinition
{
	GENERATED_BODY()

	/** 跨 DataAsset 数组重排保持稳定的唯一 Id。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	FName StableFlowId = TEXT("EscapeOnly");

	/** 修改该 Flow 的通关语义时递增并进入生成签名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "1"))
	int32 FlowVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
};

/** WFC 对 0..15 开口形态的权重策略；路口是可能结果，不是硬性配额。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeWfcShapeWeights
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 EmptyWeight = 150;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 DeadEndWeight = 15;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 StraightWeight = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 CornerWeight = 80;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 TJunctionWeight = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 CrossWeight = 5;

	/** 返回一个合法 4-bit mask 的权重；非法高位返回 0 以触发配置失败。 */
	int32 GetWeightForMask(uint8 OpeningMask) const;

	/** 所有形态权重必须为正，保证加权选择有定义。 */
	bool IsConfigured(FString& OutError) const;
};

/** 难度、流程和空间规则的唯一权威 DataAsset。 */
UCLASS(BlueprintType)
class DEMO_API UZeroEscapeLevelGenerationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 任何会改变纯逻辑结果的配置修改都必须递增。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
	int32 ProfileVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FZeroEscapeSharedRouteConstraints SharedRouteConstraints;

	/** 必须恰好包含 Easy、Normal、Hard 各一条；数组编辑顺序不参与确定性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	TArray<FZeroEscapeDifficultyDefinition> Difficulties;

	/** StableFlowId 必须唯一，并至少包含 EscapeOnly。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	TArray<FZeroEscapeFlowDefinition> Flows;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	FZeroEscapeWfcShapeWeights WfcShapeWeights;

	/** 只读校验所有单资产和跨字段不变量，不进行自动修正。 */
	bool IsConfigured(FString& OutError) const;
};

/** 一类规范结构到一个 StaticMesh 的直接绑定；没有 Catalog 或按名称查找的中间层。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeStructureMeshBinding
{
	GENERATED_BODY()

	/** 结构类型所使用的网格；Trim/Pillar 可为空，Floor/Wall/Ceiling 必须配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	/** 先应用素材 Pivot 修正，再应用规范结构 Transform；必须有限且 Unit Scale。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FTransform PivotCorrection = FTransform::Identity;

	/** 应用于生成 HISM 的碰撞配置名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FName CollisionProfileName = TEXT("BlockAll");

	/** 决定该类实例是否进入未来导航脏区；首版地面和墙通常为 true。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	bool bCanEverAffectNavigation = true;
};

/** 五类规范结构的可替换表现配置；WFC 只产生开口，不知道任何具体 Mesh。 */
UCLASS(BlueprintType)
class DEMO_API UZeroEscapePresentationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 修改 Mesh、Pivot 或结构高度时递增，只影响完整运行签名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
	int32 PresentationVersion = 1;

	/** 一个逻辑格沿每轴展开为两个 300 cm 构件。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure", meta = (ClampMin = "1.0"))
	double StructureUnitSizeCm = 300.0;

	/** 地板规范顶面高度；Floor PivotCorrection 负责适配素材实际枢轴。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	double FloorTopZCm = 0.0;

	/** 墙规范底部高度；HydroLab 当前测量值约为 5 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	double WallBaseZCm = 5.0;

	/** 天花板规范 Pivot 高度；HydroLab 当前闭合组合约为 305 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	double CeilingPivotZCm = 305.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding Floor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding Ceiling;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding Wall;

	/** 可选；为空时结构仍然可玩，只是墙顶接缝缺少装饰遮盖。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding WallTopTrim;

	/** 可选；为空时不生成边界交点柱。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	FZeroEscapeStructureMeshBinding Pillar;

	/** 校验 2:1 尺寸关系、必需网格、Transform 和碰撞配置。 */
	bool IsConfigured(double LogicalTileSizeCm, FString& OutError) const;
};

/** 在消耗随机数或创建组件前完成两类 DataAsset 的联合校验。 */
DEMO_API bool ValidateZeroEscapeGenerationAssetSet(
	const UZeroEscapeLevelGenerationProfile& Profile,
	const UZeroEscapePresentationProfile& Presentation,
	FString& OutError);
