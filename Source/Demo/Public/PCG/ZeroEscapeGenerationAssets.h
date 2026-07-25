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

	/** 最终非空逻辑格数量下限；首轮数值用于灰盒 Seed Sweep，后续应以实测分布校准。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MinWalkableCellCount = 48;

	/** 最终非空逻辑格数量上限；所有难度共享，避免困难难度通过扩大地图拖长单局。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MaxWalkableCellCount = 72;

	/** 同一轴上同时拥有两侧开口的连续格上限；Straight、T 和 Cross 都计入。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MaxConsecutiveStraightTiles = 4;

	/** 起点到终点或满足通关条件后的必需路线最大步数，所有难度共用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1"))
	int32 MaxRequiredRouteLengthTiles = 40;

	/** 相对直接起终点最短路，通关路线最多允许增加的格步数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0"))
	int32 MaxRequiredRouteExtraTiles = 14;

	/** 一次 WFC 求解允许的 singleton 候选赋值次数；达到上限时报告预算耗尽而非无解。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxWfcCandidateAttempts = 100000;

	/** 一次 WFC 求解允许恢复决策帧的次数；首轮宽上限必须在 Seed Sweep 后重新评估。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxWfcBacktrackCount = 25000;

	/**
	 * 同一请求最多建立多少棵确定性的 WFC 搜索树。
	 *
	 * 每次尝试使用由请求 Seed 派生的独立 WFC 子流；不会修改玩家选择的 Seed，也不会改变
	 * Generation Signature。Candidate/Backtrack 是整局总预算，会平均分摊到这些尝试中，
	 * 因而增加尝试次数不会放大最坏运行成本。默认 10 是首轮 288 Seed Sweep 对少数困难 Collect
	 * 长尾做出的“更多浅搜索树、少深挖单树”调整；正式值仍以完整 P95/Max 数据为准。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxWfcSolveAttempts = 10;

	/** Player、Exit 与 Objective Anchor 相对地板顶面的默认高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gameplay", meta = (ClampMin = "0.0"))
	double GameplayAnchorHeightCm = 100.0;
};

/** WFC 对 0..15 开口形态的权重策略；路口是可能结果，不是硬性配额。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeWfcShapeWeights
{
	GENERATED_BODY()

	/**
	 * Empty 的抽样权重。它不能直接换算成最终空格比例：任何非空 OpeningMask 都可能通过出口
	 * 继续强制邻格非空。默认 12000 是 24x16、Count [48,72] 首轮多 Seed 求解校准值；Count 仍是硬约束，
	 * Seed Sweep 会继续测量最终格数和回溯分布，不能把该权重误当成固定地图密度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 EmptyWeight = 12000;

	/**
	 * 只保留来向出口、终止当前分支的权重。默认 100 让前沿格的平均后继出口数略低于 1，
	 * 避免旧值 15 造成路径持续膨胀并频繁撞上 Count 上限；它仍只是形态倾向，不是死路配额。
	 */
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

	/** 返回一个合法 4-bit mask 的权重；非法高位返回 0 以触发配置失败。 */
	int32 GetWeightForMask(uint8 OpeningMask) const;

	/** 返回全部 15 个非空 OpeningMask 的权重和；按形态旋转数量精确展开。 */
	int64 GetTotalNonEmptyVariantWeight() const;

	/** 六类权重必须为正，且 16 个 Variant 的总权重必须能被 int32 加权抽样安全表示。 */
	bool IsConfigured(FString& OutError) const;
};

/** 一局难度只控制目标数量和非空形态比例，不改变共同格数与路线长度上限。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeDifficultyDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	/** Collect 流程生成的候选目标总数 N。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "12"))
	int32 ObjectiveCandidateCount = 3;

	/** CollectKOfN 流程所需目标数 K；CollectAll 会忽略该值并使用 K=N。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "12"))
	int32 RequiredObjectiveCount = 2;

	/** 当前难度的非空形态偏好；不同难度必须保持 Empty 与非空总权重一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|WFC")
	FZeroEscapeWfcShapeWeights WfcShapeWeights;
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
