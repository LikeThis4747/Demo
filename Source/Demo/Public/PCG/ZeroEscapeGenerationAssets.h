// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationAssets.h
 * 职责：定义 PCG 难度/流程、逻辑模块目录和可替换表现绑定三类项目 DataAsset。
 * 边界：逻辑 Profile/Catalog 不引用 SFCorridors；只有 Presentation Profile 持有具体资源引用。
 * 状态 Owner：资产拥有作者配置；运行时生成器只在游戏线程读取并复制纯数据快照。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeGenerationAssets.generated.h"

class AActor;
class UStaticMesh;

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeSharedRouteConstraints
{
	GENERATED_BODY()

	/**
	 * 这组约束表达所有难度共同遵守的体验底线：关键路线长度、长距离回头路和地图边界
	 * 不因困难档而放宽。难度差异由 Difficulty Definition 增加局部分支、目标密度等内容，
	 * 从而提高决策复杂度但不明显延长单局时间。
	 */

	/**
	 * 三档难度共享的单层布局边界，单位 Cell；困难通过分支、目标和局部 WFC 复杂化，
	 * 不靠扩大地图边界来明显拉长单局时间。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FIntPoint GridExtentCells = FIntPoint(24, 24);

	/** 三档难度共用的关键路线节点数，防止困难难度靠拉长单局制造难度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "4"))
	int32 CriticalPathNodeCount = 12;

	/** 任何短叶支路允许的最大单向边数；所有难度都受同一折返上限约束。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0"))
	int32 MaxLeafOneWayEdgeCount = 2;

	/** 相对关键路线，完成必需目标路线允许增加的最大边数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0"))
	int32 MaxRequiredRouteExtraEdgeCount = 6;

	/** K-of-N 候选目标的共享硬上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MaxObjectiveCandidateCount = ZeroEscape::GenerationLimits::MaxObjectiveCandidates;

	/** 精确 Progression 状态搜索预算。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1"))
	int32 MaxProgressionSearchStates = 65536;

	/** A* 每个直行 Cell 的整数成本。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1"))
	int32 AStarStraightStepCost = 10;

	/** A* 改变方向时追加的软成本，不是硬转向次数限制。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0"))
	int32 AStarTurnPenalty = 3;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeDifficultyDefinition
{
	GENERATED_BODY()

	/**
	 * 一局开始时选定且整局不变的难度覆盖项。这里配置的是“生成多少结构/目标”，
	 * 敌人数、陷阱强度和奖励价值仍由玩法系统消费 Anchor 后决定。
	 */

	/** 本条配置对应的固定单局难度；Profile 中 Easy/Normal/Hard 必须各一条。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	/** 关键路线外、满足共同折返上限的短叶支路数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0"))
	int32 ShortLeafBranchCount = 2;

	/** 从较早主路节点分出并在更晚节点汇合的前向支路数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0"))
	int32 ForwardRejoinBranchCount = 1;

	/** Collect Flow 的候选目标数 N；增加时优先增加同路线目标密度，不延长关键路线。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "12"))
	int32 ObjectiveCandidateCount = 0;

	/** CollectKOfN 的 K；CollectAll 在解析时固定使用 K=N，EscapeOnly 忽略此值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "12"))
	int32 RequiredObjectiveCount = 0;

};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeFlowDefinition
{
	GENERATED_BODY()

	/**
	 * Flow 把可变化的通关规则数据化。新增流程应组合 CompletionRule 与允许的目标角色，
	 * 而不是在 WFC 或房间类型判断里硬编码“必须先去 A/B/C 再到 D”。
	 */

	/** 请求使用的稳定 Flow 身份；重命名会使旧请求失效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	FName StableFlowId = TEXT("EscapeOnly");

	/** Flow 语义发生变化时递增，并进入生成签名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "1"))
	int32 FlowVersion = 1;

	/** 逃离、全收集或 K-of-N 的完成判定；布局算法不写死具体任务房类型。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;

	/** 可承载目标的拓扑角色；Start/Exit 永远不能作为候选目标位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	TArray<EZeroEscapeTopologyRole> AllowedObjectiveRoles;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeSolverBudgets
{
	GENERATED_BODY()

	/**
	 * 所有字段都是失败可预期的实时预算，不是越大越好的质量参数。达到预算时生成器必须
	 * 返回结构化失败，不能在游戏线程继续无界搜索。Profile 上限还会被代码级护栏二次约束。
	 */

	/** 同一请求允许的完整布局重试次数；每次使用独立确定性子 Seed。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxLayoutAttempts = 3;

	/** 特殊 Socket 模块放置的最大回溯次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "0"))
	int32 MaxSocketBacktracks = 128;

	/** Socket 候选过滤与尝试的最大累计次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxSocketCandidateChecks = 4096;

	/** 单次 A* 路由最多展开的状态数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxAStarExpandedStates = 50000;

	/** 所有抽象边累计允许发起的 A* 路由尝试数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxAStarRouteAttempts = 128;

	/** WFC 决策栈允许恢复完整 Domain 快照的最大次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "0"))
	int32 MaxWfcBacktracks = 128;

	/** WFC 选择最小熵 Cell 并提交候选的最大次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxWfcObservationCount = 256;

	/** 删除事件传播中允许更新 Support Count 的最大次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxWfcSupportUpdates = 250000;

	/** 首个竖切的 Active WFC Cell 硬上限，不允许通过资产绕过。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxWfcActiveCells = ZeroEscape::GenerationLimits::FirstPassMaxWfcActiveCells;

	/** 旋转展开后的 WFC Variant 硬上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxWfcVariants = ZeroEscape::GenerationLimits::FirstPassMaxWfcVariants;

	/** 当前 Decision Stack 的嵌套 Domain 快照驻留内存上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxWfcSnapshotMemoryMB = ZeroEscape::GenerationLimits::FirstPassMaxWfcSnapshotMemoryMB;

	/** 单个 Layout Attempt 内累计复制 Domain 快照的硬上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxWfcCumulativeSnapshotCopyMB = ZeroEscape::GenerationLimits::FirstPassMaxWfcCumulativeSnapshotCopyMB;

	/** 一次完整布局（含全部重试）内由 Socket、A*、WFC 和全局验证共用；抽象阶段另以同值设独立上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxTotalWorkUnits = 750000;
};

UENUM(BlueprintType)
enum class EZeroEscapePresentationSpawnPolicy : uint8
{
	/** 当前首版支持路径：同 StableModuleId 的 Placement 合并为一个 HISM 组件。 */
	InstancedStaticMesh = 0,
	/** 未来受控项目包装类路径；当前实例化器明确 fail-closed。 */
	Actor = 1
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapePresentationBinding
{
	GENERATED_BODY()

	/**
	 * Binding 是逻辑模块与具体素材之间唯一允许的依赖方向。当前 SFCorridors 作为第一套
	 * Presentation Profile 接入；未来替换素材时，应重新测量 Mesh Bounds/Pivot 并更新这里，
	 * 而不是把资源路径、门名或 Mesh Socket 写进求解器。
	 */

	/** 对应 Catalog 的稳定 Module Id；一个模块最多一条 Binding。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	int32 StableModuleId = INDEX_NONE;

	/** 批量 HISM 或项目包装 Actor；不改变结构算法和 Stable Id。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	EZeroEscapePresentationSpawnPolicy SpawnPolicy = EZeroEscapePresentationSpawnPolicy::InstancedStaticMesh;

	/** InstancedStaticMesh 策略的素材引用；Actor 策略必须为空。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	/**
	 * Actor 策略的项目包装类；HISM 策略必须为空。
	 * 只允许经审查、无外部 Construction/BeginPlay 副作用的纯表现包装类；首个 SFCorridors 竖切不用此路径。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSubclassOf<AActor> ActorClass;

	/** Asset Local -> Logical Module Local；只允许平移/旋转与 Unit Scale，不改变逻辑 Portal 或占格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FTransform PivotCorrection = FTransform::Identity;

	/**
	 * Actor 策略必须填写的类本地作者声明 Bounds；验证只读此契约，不为测量而 Spawn Actor。
	 * InstancedStaticMesh 策略忽略此字段并读取 StaticMesh 自身 Bounds。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FBox ActorAssetLocalBounds = FBox(EForceInit::ForceInit);

	/**
	 * 具体表现 Bounds 相对逻辑 Module LocalBounds 的单轴最大外探量，单位 cm。
	 *
	 * 例如首套 SFCorridors 房间的开口中心遵循约 660 cm 步距，但墙体、管线或装饰会比
	 * 660 cm 格子多出几十厘米。这个字段把该差异留在可替换的 Presentation 层：Catalog
	 * 仍按真实开口步距定义 Portal 和占格，换素材时只需重新测量并更新 Binding。
	 *
	 * 该余量只放宽资产 Bounds 契约校验，不参与 WFC/Socket 占格，也不会自动证明相邻
	 * Mesh 没有碰撞；因此必须保持最小实测值，并继续通过 PIE 检查接缝、碰撞和可走净空。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	double BoundsOverhangAllowanceCm = 0.0;

	/** 仅 InstancedStaticMesh 策略读取的碰撞 Profile；必须能由运行时配置解析。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FName CollisionProfileName = TEXT("BlockAll");

	/** 仅 InstancedStaticMesh 策略读取；决定该 HISM 分组是否影响导航。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	bool bCanEverAffectNavigation = true;
};

UCLASS(BlueprintType)
class DEMO_API UZeroEscapeLevelGenerationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

	/**
	 * 玩法与性能策略资产：决定路线约束、难度、Flow 和算法预算。
	 * 它可以在不更换场景素材的情况下独立迭代。
	 */

public:
	/** Profile 任意生成语义变化时递增，并进入生成签名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
	int32 ProfileVersion = 1;

	/** 三档共用且不允许困难放宽的路线、折返、网格和状态预算。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FZeroEscapeSharedRouteConstraints SharedRouteConstraints;

	/** Easy、Normal、Hard 各一条；数组顺序不参与确定性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	TArray<FZeroEscapeDifficultyDefinition> Difficulties;

	/** 可被 Request 选择的 Flow；必须包含默认 EscapeOnly。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	TArray<FZeroEscapeFlowDefinition> Flows;

	/** 抽象阶段与完整布局阶段分别应用的首版确定性硬预算；布局内部所有 Attempt 共享同一计数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver")
	FZeroEscapeSolverBudgets SolverBudgets;

	/** 只供灰盒/算法验收 Profile 使用；生产 Seed 可以合法退化为唯一解。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Validation")
	bool bRequireEffectiveWfcChoice = false;

	/** 校验全部跨字段约束；失败时返回可定位的属性名，不静默修正。 */
	bool IsConfigured(FString& OutError) const;
};

UCLASS(BlueprintType)
class DEMO_API UZeroEscapeModuleCatalog final : public UPrimaryDataAsset
{
	GENERATED_BODY()

	/**
	 * 项目拥有的结构语言资产：定义占格、逻辑开口、角色和玩法锚点。
	 * 测量结果被翻译成 Catalog 契约；经用户授权对素材做纯表现配置修正时，也不能反向改变
	 * 占格、Portal、WFC 或玩法语义。
	 */

public:
	/** 逻辑模块、Portal 或 Anchor 语义变化时递增，并进入生成签名和 Variant 身份。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
	int32 CatalogVersion = 1;

	/** 由首批实际模块测量得到；不提供猜测性的 C++ 默认格尺寸。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FVector CellSize = FVector::ZeroVector;

	/** 项目自有逻辑模块；构建快照时按 StableModuleId 排序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modules")
	TArray<FZeroEscapeModuleDefinition> Modules;

	/** 校验 Stable Id、占格、Portal Frame、Closure、Anchor 与首版 Variant 硬上限。 */
	bool IsConfigured(FString& OutError) const;
};

UCLASS(BlueprintType)
class DEMO_API UZeroEscapePresentationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

	/**
	 * 可替换的表现适配层。它必须完整覆盖所有结构模块，但不会改变抽象图、WFC 规则身份
	 * 或布局 Hash；因此同一逻辑 Catalog 可以拥有灰盒、SFCorridors 和未来正式美术多套绑定。
	 */

public:
	/** 具体素材绑定或 Pivot 发生变化时递增；只进入完整运行签名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
	int32 PresentationVersion = 1;

	/** 当前素材集到 Catalog Module 的绑定；数组顺序不参与布局确定性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	TArray<FZeroEscapePresentationBinding> Bindings;

	/** 校验结构模块一一绑定、Spawn Policy、Pivot 和 StaticMesh Bounds。 */
	bool IsConfigured(const UZeroEscapeModuleCatalog& Catalog, FString& OutError) const;
};

/**
 * 在生成请求进入纯算法前，联合校验 Profile、Catalog 与 Presentation 的跨资产约束。
 * 只读且无世界副作用；失败时返回首个稳定、可定位的配置错误。
 */
DEMO_API bool ValidateZeroEscapeGenerationAssetSet(
	const UZeroEscapeLevelGenerationProfile& Profile,
	const UZeroEscapeModuleCatalog& Catalog,
	const UZeroEscapePresentationProfile& Presentation,
	FString& OutError);
