// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationTypes.h
 * 职责：定义运行时整关生成的稳定请求、布局结果、玩法锚点、指标与失败报告契约。
 * 边界：不引用具体素材资源，不执行随机、布局、实例化或玩法逻辑。
 * 状态 Owner：运行时 Generator 持有这些值；纯算法只构造或校验它们。
 */

#pragma once

#include "CoreMinimal.h"

#include "ZeroEscapeGenerationTypes.generated.h"

namespace ZeroEscape::GenerationLimits
{
	/**
	 * 这些值是首个可交付版本的代码级安全护栏，而不是策划调参项。
	 * DataAsset 可以在护栏以内收紧预算，但不能把它们放大；否则一份误配资产就可能让
	 * 实时生成在游戏线程上消耗不可控的时间或内存。需要扩大上限时，应当连同性能测试、
	 * 算法版本和失败报告一起审查，而不是只改资产。
	 */
	inline constexpr int32 MaxObjectiveCandidates = 12;
	inline constexpr int32 FirstPassMaxCriticalPathNodes = 64;
	inline constexpr int32 FirstPassMaxProgressionSearchStates = 1048576;
	inline constexpr int32 FirstPassMaxLayoutAttempts = 8;
	inline constexpr int32 FirstPassMaxSocketBacktracks = 4096;
	inline constexpr int32 FirstPassMaxSocketCandidateChecks = 1000000;
	inline constexpr int32 FirstPassMaxAStarExpandedStates = 500000;
	inline constexpr int32 FirstPassMaxAStarRouteAttempts = 1024;
	inline constexpr int32 FirstPassMaxWfcBacktracks = 4096;
	inline constexpr int32 FirstPassMaxWfcObservationCount = 4096;
	inline constexpr int32 FirstPassMaxWfcSupportUpdates = 10000000;
	inline constexpr int32 FirstPassMaxWfcActiveCells = 256;
	inline constexpr int32 FirstPassMaxWfcVariants = 64;
	inline constexpr int32 FirstPassMaxWfcSnapshotMemoryMB = 16;
	inline constexpr int32 FirstPassMaxWfcCumulativeSnapshotCopyMB = 64;
	inline constexpr int32 FirstPassMaxTotalWorkUnits = 50000000;
	inline constexpr int32 FirstPassMaxModuleFootprintAxis = 16;
	inline constexpr double FirstPassMaxCellSizeCm = 100000.0;
	/**
	 * 具体素材允许越出逻辑 Cell envelope 的最大作者声明余量。
	 * 该值只解决 SFCorridors 这类“开口按 660 cm 对齐、装饰外壳略微探出格子”的表现问题；
	 * 它不扩大结构占格，也不能替代 PIE 中的真实碰撞、接缝与净空验证。
	 */
	inline constexpr double FirstPassMaxPresentationBoundsOverhangCm = 100.0;
}

UENUM(BlueprintType)
enum class EZeroEscapeDifficulty : uint8
{
	/** 较少局部分支和目标；仍遵守与其他难度相同的关键路线与折返上限。 */
	Easy = 0,
	/** 项目默认的一局生成密度。 */
	Normal = 1,
	/** 增加分支、候选目标等局部复杂度，但不扩大共同关键路线长度。 */
	Hard = 2
};

/**
 * 一局的通关语义。这里只描述“什么条件算完成”，不描述目标物的具体类型、外观或交互。
 * 因此未来把逃离改成收集钥匙、能源或其他资源时，可以修改 Flow/DataAsset 与玩法层，
 * 而不必让空间算法依赖某个具体任务 Actor。
 */
UENUM(BlueprintType)
enum class EZeroEscapeCompletionRule : uint8
{
	/** Start 到 Exit 可达即可，不生成必需收集目标。 */
	EscapeOnly = 0,
	/** N 个候选目标全部是通关必需目标，即 K=N。 */
	CollectAll = 1,
	/** 地图生成 N 个候选目标，玩家收集其中任意 K 个即可通关，且 0<K<=N。 */
	CollectKOfN = 2
};

/**
 * 抽象图节点在玩家路线中的职责。它是拓扑层与模块目录之间的稳定接口：
 * Core 只决定需要什么角色，Catalog 决定哪些逻辑模块可以承载该角色。
 */
UENUM(BlueprintType)
enum class EZeroEscapeTopologyRole : uint8
{
	/** 从 Start 到 Exit 的关键路线内部节点。 */
	MainPath = 0,
	/** 从主路分出后原路返回的短支路，受共享单向边数上限约束。 */
	ShortLeaf = 1,
	/** 从较早主路节点分出、在更晚节点重新汇合的前向替代路线。 */
	ForwardRejoin = 2,
	/** 唯一开局节点，只能由带 PlayerSpawn Anchor 的专用模块承载。 */
	Start = 3,
	/** 唯一终点节点，只能由带 Exit Anchor 的专用模块承载。 */
	Exit = 4
};

/**
 * 模块由哪一个空间阶段消费。WFC 只处理规则统一的单格填充，SocketModule 保留给
 * 起点、终点、房间和多出口节点等强语义结构，避免为了增加门、灯或柱子而膨胀 WFC 状态。
 */
UENUM(BlueprintType)
enum class EZeroEscapeLayoutPolicy : uint8
{
	/** 规则统一的 1x1x1 路由填充模块，由 Simple-Tiled WFC 选择旋转 Variant。 */
	WfcSingleCell = 0,
	/** 房间、起终点或多出口节点等强结构模块，由 Portal/Socket 阶段放置。 */
	SocketModule = 1,
	/** 封闭未连接 Sealable Portal 的单开口结构模块。 */
	Cap = 2,
	/** 不参加连通性与首版结构实例化的纯表现内容。 */
	DecorationOnly = 3
};

UENUM(BlueprintType)
enum class EZeroEscapeCardinalDirection : uint8
{
	/** 模块未旋转时的 +Y。 */
	North = 0,
	/** 模块未旋转时的 +X。 */
	East = 1,
	/** 模块未旋转时的 -Y。 */
	South = 2,
	/** 模块未旋转时的 -X。 */
	West = 3,
	Up = 4,
	Down = 5
};

UENUM(BlueprintType)
enum class EZeroEscapeSocketPolicy : uint8
{
	/** Finalize 时必须连接另一个兼容 Portal。 */
	Required = 0,
	/** 为未来真正可空置的 Portal 状态预留；首个单层版本校验时拒绝。 */
	Optional = 1,
	/** 可连接；若未连接则必须引用兼容 Cap 完整封口。 */
	Sealable = 2
};

/**
 * 空间层输出给玩法层的语义位置。生成器只保证位置、类型和稳定 Id，
 * 不负责在这里直接生成追猎者、陷阱、奖励或任务物品。
 */
UENUM(BlueprintType)
enum class EZeroEscapeGameplayAnchorType : uint8
{
	PlayerSpawn = 0,
	Exit = 1,
	Objective = 2,
	Reward = 3,
	Trap = 4,
	EnemySpawn = 5
};

/** 失败发生的管线阶段；与 Failure 组合后可区分同一种症状来自配置、求解还是实例化。 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationStage : uint8
{
	None = 0,
	Configuration = 1,
	Progression = 2,
	Topology = 3,
	SocketLayout = 4,
	WfcLayout = 5,
	GlobalValidation = 6,
	Instantiation = 7
};

/**
 * 对外稳定的失败分类。不要用日志文本替代这些枚举：自动化测试、PIE 调试和将来的
 * 失败统计需要基于结构化原因判断是否应换 Seed、修资产或直接阻止开局。
 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationFailure : uint8
{
	None = 0,
	InvalidConfiguration = 1,
	InvalidKOfN = 2,
	InvalidGraph = 3,
	ObjectiveLimitExceeded = 4,
	TopologyCapacityInsufficient = 5,
	ProgressionNoSolution = 6,
	SearchBudgetExceeded = 7,
	SolverInvariantViolation = 8,
	MissingModuleRole = 9,
	InvalidModulePortal = 10,
	SocketPlacementNoSolution = 11,
	WfcNoSolution = 12,
	WfcNoEffectiveChoice = 13,
	WfcBudgetExceeded = 14,
	GeometryOverlap = 15,
	PortalMismatch = 16,
	RequiredRouteTooLong = 17,
	LongRetraceLimitExceeded = 18,
	LayoutAttemptsExhausted = 19,
	PresentationMissing = 20,
	InstantiationFailed = 21
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationRequest
{
	GENERATED_BODY()

	/** 决定同一算法/资产版本下的确定性布局；同请求必须可复现。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 Seed = 12345;

	/** 开局固定的一局难度；生成完成后本局不再变化。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	/** 在 Generation Profile 中唯一解析的 Flow Stable Id。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	FName FlowProfileId = TEXT("EscapeOnly");
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationSignature
{
	GENERATED_BODY()

	/**
	 * Signature 记录一次结果由哪些版本化输入产生。复现问题时必须同时保留 Seed、难度、
	 * Flow 和四类版本；只有 Seed 相同并不代表布局应当相同。表现版本不进入纯布局 Hash，
	 * 所以替换 SFCorridors 的网格或 Pivot 不会伪装成拓扑算法变化。
	 */

	/** 本次请求的 Master Seed。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 Seed = 0;

	/** 本次请求解析后的单局难度。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	/** 本次请求解析后的稳定 Flow Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FName FlowProfileId;

	/** 改变确定性算法结果时递增；0 表示签名尚未构建。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 AlgorithmVersion = 0;

	/** 参与本次生成的 Profile 版本；0 表示无有效签名。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 GenerationProfileVersion = 0;

	/** 参与本次生成的具体 Flow 版本；0 表示无有效签名。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 FlowVersion = 0;

	/** 参与本次生成的逻辑 Catalog 版本；0 表示无有效签名。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 CatalogVersion = 0;

	/** 只进入完整运行签名，不进入 Abstract/Layout Hash。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 PresentationVersion = 0;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeModulePortal
{
	GENERATED_BODY()

	/**
	 * Portal 是项目自己的逻辑连接契约，不要求第三方 StaticMesh 自带 UE Socket。
	 * 素材测量只负责把开口中心和朝向录入 LocalTransform；求解器始终使用稳定整数签名
	 * 判断兼容性，从而允许未来用另一套美术资产替换当前 SFCorridors。
	 */

	/** 当前 Module 内唯一且跨数组重排稳定的 Socket Id。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	int32 StableSocketId = INDEX_NONE;

	/** 未旋转模块中、相对 Footprint 最小角的非负 Cell 坐标；旋转必须走统一 QuarterTurn helper。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	FIntVector CellOffset = FIntVector::ZeroValue;

	/** Portal Frame 位于逻辑模块局部空间，单位 cm；+X 向模块外，+Z 向上，且必须 Unit Scale。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	FTransform LocalTransform = FTransform::Identity;

	/** 未旋转模块的离散朝外方向；首个单层版本只接受 N/E/S/W。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	EZeroEscapeCardinalDirection Direction = EZeroEscapeCardinalDirection::North;

	/** 整数连接类型；只有 Type、Width、Height 都兼容时才能连接。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal", meta = (ClampMin = "0"))
	int32 ConnectorTypeId = 0;

	/** 仅供作者阅读的连接类型名，不参与确定性或兼容判断。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	FName DisplayType = TEXT("Walk");

	/** 离散开口宽度等级，不使用浮点 Mesh 尺寸作为连接身份。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal", meta = (ClampMin = "0"))
	int32 WidthClass = 1;

	/** 离散高度层；首个单层竖切固定为 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	int32 HeightLayer = 0;

	/** Required 必须连接；Sealable 可连接或由 ClosureModule 封口；首版拒绝 Optional。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	EZeroEscapeSocketPolicy Policy = EZeroEscapeSocketPolicy::Required;

	/** Sealable Portal 的 Cap StableModuleId；其他 Policy 必须为 INDEX_NONE。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	int32 ClosureModuleId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeModuleAnchor
{
	GENERATED_BODY()

	/** 当前 Module 内唯一、进入最终 Anchor 实例映射的稳定 Id。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor")
	int32 StableAnchorId = INDEX_NONE;

	/** 玩法语义类型；空间算法只携带，不创建具体玩法 Actor。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor")
	EZeroEscapeGameplayAnchorType Type = EZeroEscapeGameplayAnchorType::Objective;

	/** 逻辑 Module 局部空间中的有限 Unit Scale Transform，单位 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor")
	FTransform LocalTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeModuleDefinition
{
	GENERATED_BODY()

	/**
	 * Definition 描述“一个可连接、可占格的逻辑模块”，不描述它由哪张 Mesh 表现。
	 * LocalBounds、Portal 与 Anchor 是必须长期稳定的结构契约；具体 StaticMesh 和 Pivot
	 * 放在 Presentation Profile。更换素材时只要新表现仍满足该 envelope，就无需重做拓扑。
	 */

	/** Catalog 全局唯一且跨数组重排稳定的 Module Id。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	int32 StableModuleId = INDEX_NONE;

	/** 仅供作者与错误报告阅读，不参与 Hash 或随机选择。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	FName DisplayName;

	/** 决定由单格 WFC、特殊 Socket、封口或纯装饰阶段消费。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	EZeroEscapeLayoutPolicy LayoutPolicy = EZeroEscapeLayoutPolicy::WfcSingleCell;

	/** 该结构模块可承载的抽象拓扑角色；Cap/Decoration 必须为空。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	TArray<EZeroEscapeTopologyRole> AllowedRoles;

	/** 逻辑模块占用的未旋转 Cell 尺寸；不是具体 Mesh Bounds。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	FIntVector Footprint = FIntVector(1, 1, 1);

	/** Bit 0/1/2/3 = 绕模块局部 +Z 的 +Yaw 0/90/180/270 度；枚举值与数组顺序无关。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module", meta = (Bitmask))
	int32 AllowedQuarterTurnsMask = 0xF;

	/** 稳定整数选择权重；只在已稳定排序的合法候选集合内使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module", meta = (ClampMin = "1"))
	int32 Weight = 100;

	/**
	 * 逻辑规范包围盒，单位 cm；模块原点位于 Footprint 的 XY 中心和 Z 底面。
	 * 替换素材经 PivotCorrection 后仍必须落入此 envelope，且 envelope 不得越过 Footprint 占格。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	/** 逻辑 Portal；构建快照时按 StableSocketId 排序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	TArray<FZeroEscapeModulePortal> Portals;

	/** 玩法只依赖项目逻辑锚点，不查找 Mesh Socket 或 Actor 名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	TArray<FZeroEscapeModuleAnchor> GameplayAnchors;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapePlacedModule
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StablePlacementId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableModuleId = INDEX_NONE;

	/** 仅在当前 CatalogVersion 内稳定；由排序后的 (StableModuleId, QuarterTurns) 唯一派生，不是数组下标。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableVariantId = INDEX_NONE;

	/** 绕局部 +Z 的 +Yaw 四分之一圈数，合法值 0..3。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	uint8 QuarterTurns = 0;

	/** 特殊/节点模块记录绑定的抽象节点；路由填充模块为 INDEX_NONE。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 AbstractNodeId = INDEX_NONE;

	/** 旋转后占地的最小世界格坐标；所有占格通过 Stable Id 映射，绝不按此值索引数组。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector GridOrigin = FIntVector::ZeroValue;

	/** Generator Actor 局部空间中的逻辑模块 Transform，单位 cm，必须 Unit Scale。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FTransform LocalTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeNodePlacementBinding
{
	GENERATED_BODY()

	/** Core 抽象图中的稳定 Node Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 AbstractNodeId = INDEX_NONE;

	/** Finalize 后承载该抽象节点的稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StablePlacementId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeEdgeRouteBinding
{
	GENERATED_BODY()

	/** Core 抽象图中的稳定 Edge Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 AbstractEdgeId = INDEX_NONE;

	/** 路线有向导出的起始抽象 Node Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 FromNodeId = INDEX_NONE;

	/** 路线有向导出的结束抽象 Node Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 ToNodeId = INDEX_NONE;

	/** 从 FromNodeId 到 ToNodeId 的顺序，包含两端 Placement；相邻 Id 必须由 PortalConnection 直接连接。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<int32> OrderedStablePlacementIds;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapePortalConnection
{
	GENERATED_BODY()

	/** 最终 Plan 内唯一且稳定排序的 Connection Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableConnectionId = INDEX_NONE;

	/** 可选环路为 INDEX_NONE；抽象必需连接保存原 Edge Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 AbstractEdgeId = INDEX_NONE;

	/** 连接 A 端的稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StablePlacementAId = INDEX_NONE;

	/** 连接 A 端在其 Module Definition 内的 StableSocketId。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableSocketAId = INDEX_NONE;

	/** 连接 B 端的稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StablePlacementBId = INDEX_NONE;

	/** 连接 B 端在其 Module Definition 内的 StableSocketId。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableSocketBId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeClosedPortal
{
	GENERATED_BODY()

	/** 被封口结构模块的稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StablePlacementId = INDEX_NONE;

	/** 被封口 Portal 在原 Module 内的 StableSocketId。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableSocketId = INDEX_NONE;

	/** 实际用于封口的 Cap 稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableClosurePlacementId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedAnchor
{
	GENERATED_BODY()

	/** 最终 Plan 内唯一的 Anchor 实例 Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableAnchorInstanceId = INDEX_NONE;

	/** 玩法语义类型；玩法层据此创建或查询内容。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	EZeroEscapeGameplayAnchorType Type = EZeroEscapeGameplayAnchorType::Objective;

	/** Anchor 所属的稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StablePlacementId = INDEX_NONE;

	/** 源 Module Definition 内的 StableAnchorId。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableModuleAnchorId = INDEX_NONE;

	/** 已解析为 Generator Actor 局部空间，不再相对单个模块。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FTransform LocalTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeObjectiveBinding
{
	GENERATED_BODY()

	/** 抽象 Plan 内的稳定 Objective Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableObjectiveId = INDEX_NONE;

	/** 该目标意图绑定的抽象 Node Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 AbstractNodeId = INDEX_NONE;

	/** 最终承载目标的稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StablePlacementId = INDEX_NONE;

	/** 目标绑定的最终 Objective Anchor 实例 Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableAnchorInstanceId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedLevelPlan
{
	GENERATED_BODY()

	/**
	 * 完整、纯数据且可验证的实例化清单。求解阶段必须先把所有连接、封口、锚点和 Hash
	 * 一次性定稿，运行时生成器随后只按清单提交场景，不在 Spawn 过程中继续做随机选择。
	 * 这条边界使失败可以整批回滚，也让同一 Plan 能用于测试、存档签名和后续玩法装配。
	 */

	/** 请求及算法/资产版本的完整复现身份。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FZeroEscapeGenerationSignature Signature;

	/** 只描述抽象拓扑与目标绑定；同输入的规范序列必须得到相同非零值。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int64 CanonicalAbstractHash = 0;

	/** 描述最终模块、连接、封口和 Anchor；实例化前会重新计算并逐值核对。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int64 CanonicalLayoutHash = 0;

	/** 以下数组在最终 Plan 中按各自主 Stable Id 排序；调用者必须建 Dense Index 映射，禁止把 Stable Id 当下标。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapePlacedModule> Modules;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeNodePlacementBinding> NodeBindings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeEdgeRouteBinding> EdgeRoutes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapePortalConnection> PortalConnections;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeClosedPortal> ClosedPortals;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedAnchor> GameplayAnchors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeObjectiveBinding> ObjectiveBindings;

	/** Start 抽象节点对应的最终稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StartPlacementId = INDEX_NONE;

	/** Exit 抽象节点对应的最终稳定 Placement Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 ExitPlacementId = INDEX_NONE;

	/** 唯一 PlayerSpawn Anchor 的最终实例 Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 PlayerSpawnAnchorInstanceId = INDEX_NONE;

	/** 唯一 Exit Anchor 的最终实例 Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 ExitAnchorInstanceId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationMetrics
{
	GENERATED_BODY()

	/**
	 * 指标只观察求解行为，不参与随机选择或 Hash。它们用于证明“实时、非工具”生成没有
	 * 悄悄退化成无界搜索，并帮助区分素材规则过紧、预算过低和算法回归。
	 */

	/** 实际进入 WFC 的空间规模和旋转展开后的规则规模。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcActiveCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcVariantCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcInitialChoiceCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcInitialAlternativeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcInitialMaxDomainSize = 0;

	/** 决策、矛盾和回溯计数；用于识别“看似成功但主要靠暴力回溯”的规则集。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcDecisionFrameCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcObservationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcContradictionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcBacktrackCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcPeakDecisionDepth = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcVariantRemovalCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 WfcSupportUpdateCount = 0;

	/** 完整 Domain 快照的峰值驻留量和累计复制量，分别约束内存峰值与复制工作量。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int64 WfcPeakSnapshotBytes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	int64 WfcCumulativeSnapshotCopyBytes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	bool bHadEffectiveWfcChoice = false;

	/** 各管线阶段的墙钟时间，仅用于诊断；不能写入确定性结果。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timing")
	double AbstractMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timing")
	double SocketMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timing")
	double AStarMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timing")
	double WfcMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timing")
	double ValidationMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timing")
	double InstantiationMilliseconds = 0.0;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeFailureCount
{
	GENERATED_BODY()

	/**
	 * 多次 Layout Attempt 的聚合项。保留首次/末次 Attempt，既避免日志刷屏，
	 * 又能判断失败是偶发 Seed 分支还是每次都在同一阶段发生。
	 */

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationStage Stage = EZeroEscapeGenerationStage::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationFailure Failure = EZeroEscapeGenerationFailure::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 Count = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 FirstAttemptIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 LastAttemptIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationReport
{
	GENERATED_BODY()

	/**
	 * 单次公开 Generate 调用的最终诊断结果。成功报告同样保留 Metrics；失败报告中的
	 * Stage、Failure 和数值字段供程序判断，Message 只负责给人阅读，不应被解析成逻辑。
	 */

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationStage Stage = EZeroEscapeGenerationStage::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationFailure Failure = EZeroEscapeGenerationFailure::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 AttemptIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 AttemptsExecuted = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationFailure LastAttemptFailure = EZeroEscapeGenerationFailure::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 RelatedStableId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 ActualValue = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 LimitValue = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FString Message;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FZeroEscapeGenerationMetrics Metrics;

	/** 按 (Stage, Failure) 稳定排序；不得用 TMap 遍历顺序输出。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	TArray<FZeroEscapeFailureCount> AttemptFailureCounts;
};
