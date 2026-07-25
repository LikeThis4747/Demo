// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationTypes.h
 * 职责：定义运行时整关 PCG 在请求、逻辑网格、玩法锚点、结果与错误报告之间共享的稳定值类型。
 * 边界：本文件不引用具体素材，不执行 WFC、不创建组件，也不保存任何跨局求解状态。
 * 设计：逻辑层只认识 600 cm 格子和四方向开口；300 cm 美术构件由表现层在实例化阶段展开。
 */

#pragma once

#include "CoreMinimal.h"

#include "ZeroEscapeGenerationTypes.generated.h"

/** 运行时生成的代码级安全上限；它们用于拒绝误配置，不是策划调参项。 */
namespace ZeroEscape::GenerationLimits
{
	/** 首版 K-of-N 目标使用 32 位掩码验证；12 个目标也足以覆盖当前单局规模。 */
	inline constexpr int32 MaxObjectiveCandidates = 12;
	/** 首版仅提供六个双 Lane 进度带；同时作为纯值入口的硬校验，不能只依赖编辑器 Clamp。 */
	inline constexpr int32 MaxObjectiveProgressBands = 6;
	/** 防止 DataAsset 误配导致同步生成在游戏线程申请过大的工作集。 */
	inline constexpr int32 MaxGridCells = 1024;
	/** 单轴至少能容纳起点、终点、目标房间和边界。 */
	inline constexpr int32 MinGridAxis = 6;
	/** 单轴硬上限与总格数共同约束首版二维生成规模。 */
	inline constexpr int32 MaxGridAxis = 64;
}

/** 一局开始时固定的难度；难度只改变局部复杂度，不放宽共同路线长度约束。 */
UENUM(BlueprintType)
enum class EZeroEscapeDifficulty : uint8
{
	Easy = 0,
	Normal = 1,
	Hard = 2
};

/** 通关目标的抽象语义；空间算法不依赖具体任务 Actor 或物品类型。 */
UENUM(BlueprintType)
enum class EZeroEscapeCompletionRule : uint8
{
	/** 起点到终点可达即可。 */
	EscapeOnly = 0,
	/** N 个候选目标全部必需，即 K=N。 */
	CollectAll = 1,
	/** N 个候选目标中收集任意 K 个即可，即 0<K<=N。 */
	CollectKOfN = 2
};

/** 对玩法系统开放的语义位置；生成器只提供位置，不生成对应玩法对象。 */
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

/** 四方向开口的稳定 bit 契约；bit 顺序属于算法版本，不得按枚举显示顺序改写。 */
UENUM(BlueprintType, meta = (Bitflags))
enum class EZeroEscapeOpenEdge : uint8
{
	None = 0,
	North = 1 << 0,
	East = 1 << 1,
	South = 1 << 2,
	West = 1 << 3
};
ENUM_CLASS_FLAGS(EZeroEscapeOpenEdge);

/** 一个有效逻辑格在最终布局中的空间职责。 */
UENUM(BlueprintType)
enum class EZeroEscapeGridRegionKind : uint8
{
	Corridor = 0,
	Room = 1,
	Start = 2,
	Exit = 3,
	Objective = 4
};

/** 失败发生的管线阶段；日志文本只是补充，自动化判断应使用此枚举。 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationStage : uint8
{
	None = 0,
	Configuration = 1,
	Progression = 2,
	GridLayout = 3,
	WfcLayout = 4,
	GlobalValidation = 5,
	Instantiation = 6
};

/** 精简后的结构化失败原因；已删除旧 Graph、Socket、A-star 与外层换 Seed 重试专用状态。 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationFailure : uint8
{
	None = 0,
	InvalidConfiguration = 1,
	InvalidKOfN = 2,
	ObjectiveLimitExceeded = 3,
	CapacityInsufficient = 4,
	SolverInvariantViolation = 5,
	RequiredRouteTooLong = 6,
	LongRetraceLimitExceeded = 7,
	PresentationMissing = 8,
	InstantiationFailed = 9,

	/** 输入与配置均合法，但有界回溯已经证明当前 WFC 搜索空间不存在满足全部约束的结果。 */
	NoValidWfcSolution = 10,

	/** 搜索空间尚未证明无解，但候选尝试或回溯次数已经达到本局配置的确定性上限。 */
	SolverBudgetExhausted = 11
};

/**
 * 网格方向帮助函数的唯一实现。
 * WFC、Grid、结构展开和测试都必须复用这里的 bit/步进定义，避免各自维护一套方向约定。
 */
namespace ZeroEscape::Grid
{
	inline constexpr uint8 DirectionCount = 4;
	inline constexpr uint8 AllOpenEdges = 0x0F;

	/** 把稳定方向索引 0=N、1=E、2=S、3=W 转成开口 bit。 */
	FORCEINLINE constexpr uint8 DirectionBit(const uint8 DirectionIndex)
	{
		return DirectionIndex < DirectionCount ? static_cast<uint8>(1u << DirectionIndex) : 0u;
	}

	/** 返回相反方向的稳定索引；非法输入原样返回，便于上层 fail-closed。 */
	FORCEINLINE constexpr uint8 OppositeDirectionIndex(const uint8 DirectionIndex)
	{
		return DirectionIndex < DirectionCount ? static_cast<uint8>((DirectionIndex + 2u) & 3u) : DirectionIndex;
	}

	/** 沿稳定方向移动一个逻辑格。 */
	FORCEINLINE FIntPoint Step(const FIntPoint Coordinate, const uint8 DirectionIndex)
	{
		switch (DirectionIndex)
		{
		case 0: return Coordinate + FIntPoint(0, 1);
		case 1: return Coordinate + FIntPoint(1, 0);
		case 2: return Coordinate + FIntPoint(0, -1);
		case 3: return Coordinate + FIntPoint(-1, 0);
		default: return Coordinate;
		}
	}

	/** 判断坐标是否落在左下角为 (0,0) 的二维网格内。 */
	FORCEINLINE constexpr bool IsInside(const FIntPoint Coordinate, const FIntPoint GridSize)
	{
		return Coordinate.X >= 0 && Coordinate.Y >= 0
			&& Coordinate.X < GridSize.X && Coordinate.Y < GridSize.Y;
	}

	/** 把坐标映射为按 Y 行、X 列的稳定稠密下标；调用前必须先检查 IsInside。 */
	FORCEINLINE constexpr int32 ToIndex(const FIntPoint Coordinate, const FIntPoint GridSize)
	{
		return Coordinate.Y * GridSize.X + Coordinate.X;
	}
}

/** 一次同步运行时生成请求；Seed、难度和 Flow 在本局内保持不变。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationRequest
{
	GENERATED_BODY()

	/** 同一版本资产与算法下，相同 Seed 必须得到相同逻辑布局。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 Seed = 12345;

	/** 一局一个难度；困难增加局部复杂度，但不故意延长主路线。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	/** 在 Generation Profile 中按 StableFlowId 唯一解析的流程。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	FName FlowProfileId = TEXT("EscapeOnly");
};

/** 完整记录一次结果由哪些稳定输入产生；表现版本不进入纯布局 Hash。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationSignature
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 Seed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FName FlowProfileId = NAME_None;

	/** 改变确定性规则或随机消费顺序时递增。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 AlgorithmVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 GenerationProfileVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 FlowVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 PresentationVersion = 0;
};

/** WFC 坍缩后的单格逻辑结果；数组按 GridCoordinate 的稠密下标稳定排序。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeCollapsedTile
{
	GENERATED_BODY()

	/** 仅对当前 Plan 稳定；等于该格在 Cells 数组中的稠密下标。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint GridCoordinate = FIntPoint::ZeroValue;

	/** N/E/S/W 四方向开口掩码，合法范围为 0..15。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated", meta = (Bitmask, BitmaskEnum = "/Script/Demo.EZeroEscapeOpenEdge"))
	uint8 OpeningMask = 0;

	/** 同一房间或走廊语义区域共享的稳定编号。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 RegionId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	EZeroEscapeGridRegionKind RegionKind = EZeroEscapeGridRegionKind::Corridor;
};

/** 把流程层的稳定 Landmark 映射到最终网格区域，便于诊断而不暴露内部求解对象。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeLandmarkBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableLandmarkId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint GridCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 RegionId = INDEX_NONE;
};

/** 玩法层可查询的局部空间锚点；位置由网格语义产生，不依赖 StaticMesh Socket。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedAnchor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableAnchorInstanceId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	EZeroEscapeGameplayAnchorType Type = EZeroEscapeGameplayAnchorType::Objective;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint GridCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 RegionId = INDEX_NONE;

	/** 相对 Generator GeneratedRoot 的有限 Unit Scale Transform。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FTransform LocalTransform = FTransform::Identity;
};

/** 一个候选目标与其网格区域、玩法 Anchor 的稳定关联。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeObjectiveBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableObjectiveId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint GridCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 RegionId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableAnchorInstanceId = INDEX_NONE;
};

/** 最终有效格中各种路口形态的计数；它们是观测结果，不是硬性生成配额。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeJunctionMetrics
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 DeadEndCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StraightCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 CornerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 TJunctionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 CrossJunctionCount = 0;
};

/** 一次成功求解的纯逻辑结果；不包含 StaticMesh、组件指针或第三方资源路径。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedLevelPlan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FZeroEscapeGenerationSignature Signature;

	/** 流程意图和最终布局各自的规范 Hash，均排除表现资源。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int64 CanonicalProgressionHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int64 CanonicalLayoutHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint GridSize = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	double LogicalTileSizeCm = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 ObjectiveCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 RequiredObjectiveCount = 0;

	/** 仅保存最终保留的有效格，按 StableCellId 稳定排序；Outside 格不进入结果。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeCollapsedTile> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeLandmarkBinding> LandmarkBindings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedAnchor> GameplayAnchors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeObjectiveBinding> ObjectiveBindings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint StartCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint ExitCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 PlayerSpawnAnchorInstanceId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 ExitAnchorInstanceId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FZeroEscapeJunctionMetrics JunctionMetrics;
};

/**
 * 纯算法与实例化阶段共享的轻量指标。
 * 搜索指标只累计本次求解实际做过的工作，回溯恢复 Domain 时不得把计数回滚。
 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationMetrics
{
	GENERATED_BODY()

	/** 成功布局中 OpeningMask 非零的逻辑格数量；失败时允许保持为 0。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WalkableCellCount = 0;

	/** 新建一个最小熵决策帧的次数；同一帧回溯后改试其他候选不重复计数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcObservationCount = 0;

	/** 本局实际启动的 WFC 搜索树数量；成功通常小于配置的最大尝试数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcSolveAttemptCount = 0;

	/** 实际把决策 Cell 收窄到一个 singleton 候选的次数，包括最终失败分支。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcCandidateAttemptCount = 0;

	/** 局部邻接传播或全局约束使候选 Domain 实际缩小的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcPropagationCount = 0;

	/** 当前搜索分支发生可恢复 contradiction 的次数；它是正常回溯成本，不是不变量错误。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcContradictionCount = 0;

	/** 局部 OpeningMask 邻接传播把某个 Domain 缩为空的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcLocalAdjacencyContradictionCount = 0;

	/** Count 约束直接证明当前分支不满足非空 Cell 数量上下界的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcCountContradictionCount = 0;

	/** MaxConsecutive 约束直接证明当前分支含有过长连续贯通段的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcMaxConsecutiveContradictionCount = 0;

	/** Connected 约束直接证明 Required/被迫非空节点已无法连通的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcConnectedContradictionCount = 0;

	/** 合并后的全局约束 Ban 立即把某个 Domain 缩为空的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcGlobalBanContradictionCount = 0;

	/** 为尝试替代候选而恢复一个决策帧的次数；弹出已耗尽的帧同样计入。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcBacktrackCount = 0;

	/** 完整折叠后因路线总长或额外折返超限而被 Grid 完成态验收拒绝的候选数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcCollapsedCandidateRejectionCount = 0;

	/** 只统计非法内部状态或代码不变量；成功运行必须为 0。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcInvariantFailureCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 InstancedMeshCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 HismComponentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	double PlanningMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	double InstantiationMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	double TotalMilliseconds = 0.0;
};

/** 公开生成入口的结构化报告；失败必须写入首个不可恢复检查点。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationStage Stage = EZeroEscapeGenerationStage::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationFailure Failure = EZeroEscapeGenerationFailure::None;

	/** 与错误相关的 Landmark、格子或 Anchor Id；无明确对象时为 INDEX_NONE。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 RelatedStableId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 ActualValue = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 LimitValue = 0;

	/** 面向开发者的单次失败说明；游戏逻辑不得解析此文本。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FString Message;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FZeroEscapeGenerationMetrics Metrics;
};
