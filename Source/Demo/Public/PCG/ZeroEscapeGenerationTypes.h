// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationTypes.h
 * 职责：定义运行时空间 PCG 的请求、逻辑网格、房间锚点、结果与失败报告。
 * 边界：这里只保存纯值；不引用具体素材，不执行 WFC，也不表达尚未确定的玩法目标。
 */

#pragma once

#include "CoreMinimal.h"

#include "ZeroEscapeGenerationTypes.generated.h"

/** 运行时安全上限用于拒绝误配置，不是策划调参项。 */
namespace ZeroEscape::GenerationLimits
{
	inline constexpr int32 MaxRoomCount = 6;
	inline constexpr int32 MaxGridCells = 1024;
	inline constexpr int32 MinGridAxis = 6;
	inline constexpr int32 MaxGridAxis = 64;
}

/** 一局开始时固定的难度；当前只选择 WFC 形态权重。 */
UENUM(BlueprintType)
enum class EZeroEscapeDifficulty : uint8
{
	Easy = 0,
	Normal = 1,
	Hard = 2
};

/** 四方向开口的稳定 bit 契约；bit 顺序属于算法版本。 */
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

/** 一个有效逻辑格在最终空间布局中的职责。 */
UENUM(BlueprintType)
enum class EZeroEscapeGridRegionKind : uint8
{
	Corridor = 0,
	Room = 1,
	Start = 2,
	Exit = 3
};

/** 失败发生的生成阶段；自动化应断言枚举而不是解析日志文本。 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationStage : uint8
{
	None = 0,
	Configuration = 1,
	GridLayout = 2,
	WfcLayout = 3,
	GlobalValidation = 4,
	Instantiation = 5
};

/** 空间生成公开入口的结构化失败原因。 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationFailure : uint8
{
	None = 0,
	InvalidConfiguration = 1,
	CapacityInsufficient = 2,
	SolverInvariantViolation = 3,
	RequiredRouteTooLong = 4,
	InstantiationFailed = 5,
	NoValidWfcSolution = 6,
	SolverBudgetExhausted = 7
};

/** 四方向帮助函数的唯一实现，WFC、Grid、结构展开和测试必须复用。 */
namespace ZeroEscape::Grid
{
	inline constexpr uint8 DirectionCount = 4;
	inline constexpr uint8 AllOpenEdges = 0x0F;

	FORCEINLINE constexpr uint8 DirectionBit(const uint8 DirectionIndex)
	{
		return DirectionIndex < DirectionCount ? static_cast<uint8>(1u << DirectionIndex) : 0u;
	}

	FORCEINLINE constexpr uint8 OppositeDirectionIndex(const uint8 DirectionIndex)
	{
		return DirectionIndex < DirectionCount
			? static_cast<uint8>((DirectionIndex + 2u) & 3u)
			: DirectionIndex;
	}

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

	FORCEINLINE constexpr bool IsInside(
		const FIntPoint Coordinate,
		const FIntPoint GridSize)
	{
		return Coordinate.X >= 0 && Coordinate.Y >= 0
			&& Coordinate.X < GridSize.X && Coordinate.Y < GridSize.Y;
	}

	FORCEINLINE constexpr int32 ToIndex(
		const FIntPoint Coordinate,
		const FIntPoint GridSize)
	{
		return Coordinate.Y * GridSize.X + Coordinate.X;
	}
}

/** 一次同步生成请求；UI 与测试都通过同一个值类型传入 Seed 和难度。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationRequest
{
	GENERATED_BODY()

	/** 同一算法/Profile 版本下，相同 Seed 必须得到相同逻辑布局。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	int32 Seed = 12345;

	/** 一局一个难度；不通过增加房间或格数故意延长困难局。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;
};

/** 记录一次结果由哪些稳定输入产生；表现版本不进入纯布局随机消费。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationSignature
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 Seed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 AlgorithmVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 GenerationProfileVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 PresentationVersion = 0;

	bool operator==(const FZeroEscapeGenerationSignature& Other) const
	{
		return Seed == Other.Seed
			&& Difficulty == Other.Difficulty
			&& AlgorithmVersion == Other.AlgorithmVersion
			&& GenerationProfileVersion == Other.GenerationProfileVersion
			&& PresentationVersion == Other.PresentationVersion;
	}
};

/** WFC 坍缩后的单格结果；Cells 按完整 Grid 稠密下标顺序导出非空格。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeCollapsedTile
{
	GENERATED_BODY()

	/** 仅对当前 Plan 稳定，等于该格在 Cells 数组中的下标。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint GridCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated",
		meta = (Bitmask, BitmaskEnum = "/Script/Demo.EZeroEscapeOpenEdge"))
	uint8 OpeningMask = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 RegionId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	EZeroEscapeGridRegionKind RegionKind = EZeroEscapeGridRegionKind::Corridor;
};

/** 玩法层以后可选择的中立房间；PCG 不决定房间里放什么。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedRoom
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 RegionId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint AnchorCoordinate = FIntPoint::ZeroValue;

	/** 相对 Generator.GeneratedRoot 的有限 Unit Scale Transform。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FTransform LocalTransform = FTransform::Identity;
};

/** 最终有效格中各种路口形态的观测计数，不是硬性配额。 */
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

/** 一次成功求解的纯空间结果，不包含 UObject、组件或第三方资源路径。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedLevelPlan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FZeroEscapeGenerationSignature Signature;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int64 CanonicalLayoutHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint GridSize = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	double LogicalTileSizeCm = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeCollapsedTile> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint StartCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint ExitCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FTransform PlayerStartLocalTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FTransform ExitLocalTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedRoom> Rooms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FZeroEscapeJunctionMetrics JunctionMetrics;
};

/** 搜索、实例化和耗时指标；回溯恢复 Domain 时不得回滚这些累计值。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationMetrics
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WalkableCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcObservationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcSolveAttemptCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcCandidateAttemptCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcPropagationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcContradictionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcLocalAdjacencyContradictionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcCountContradictionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcMaxConsecutiveContradictionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcConnectedContradictionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcGlobalBanContradictionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcBacktrackCount = 0;

	/** 完整叶子因 Start→Exit 路线过长而被拒绝并继续回溯的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcCollapsedCandidateRejectionCount = 0;

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

/** 公开生成入口的报告；失败必须保留首个不可恢复检查点。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationStage Stage = EZeroEscapeGenerationStage::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationFailure Failure = EZeroEscapeGenerationFailure::None;

	/** 与错误相关的房间、格子或结构类别 Id；无明确对象时为 INDEX_NONE。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 RelatedStableId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 ActualValue = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 LimitValue = 0;

	/** 面向开发者的说明；游戏逻辑不得解析此文本。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FString Message;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FZeroEscapeGenerationMetrics Metrics;
};
