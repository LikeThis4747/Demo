// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationTypes.h
 * 职责：定义运行时空间 PCG 的请求、三维逻辑地址、完整结构、最终 Plan 与失败报告。
 * 边界：这里只保存纯值；不引用具体素材，不执行 WFC，也不访问 World 或导航系统。
 */

#pragma once

#include "CoreMinimal.h"

#include "ZeroEscapeGenerationTypes.generated.h"

/** 运行时安全上限用于拒绝误配置，不是策划调参项。 */
namespace ZeroEscape::GenerationLimits
{
	inline constexpr int32 MaxGridCells = 1024;
	inline constexpr int32 MinGridAxis = 6;
	inline constexpr int32 MaxGridAxis = 64;
	inline constexpr int32 MinFloorCount = 2;
	inline constexpr int32 MaxFloorCount = 4;
	inline constexpr int32 MaxWholeLayoutAttempts = 4;
	inline constexpr int32 MaxStructureCandidateEvaluations = 250000;
	inline constexpr int32 MaxWfcCandidateAttemptsPerFloor = 100000;
	inline constexpr int32 MaxWfcBacktrackCountPerFloor = 25000;
	inline constexpr int32 MaxWfcSolveAttemptsPerFloor = 10;
	inline constexpr int32 MaxNavigationValidationPoints = 20;
	inline constexpr double MaxNavigationBuildTimeoutSeconds = 10.0;
}

/** 一局开始时固定的难度；决定楼层、额外楼梯、高天花板房间和路线目标等可调配置。 */
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

/** 失败发生的生成阶段；自动化应断言枚举而不是解析日志文本。 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationStage : uint8
{
	None = 0,
	Configuration = 1,
	StructurePlacement = 2,
	WfcLayout = 3,
	GlobalValidation = 4,
	Instantiation = 5,
	NavigationBuild = 6,
	NavigationValidation = 7
};

/** 空间生成公开入口的结构化失败原因。 */
UENUM(BlueprintType)
enum class EZeroEscapeGenerationFailure : uint8
{
	None = 0,
	InvalidConfiguration = 1,
	CapacityInsufficient = 2,
	StructurePlacementFailed = 3,
	SolverInvariantViolation = 4,
	RequiredRouteTooLong = 5,
	RequiredRouteTooShort = 6,
	NoValidWfcSolution = 7,
	SolverBudgetExhausted = 8,
	GlobalConnectivityFailed = 9,
	InstantiationFailed = 10,
	NavigationBuildTimeout = 11,
	NavigationValidationFailed = 12
};

/** 首版能够整体放置的三种项目结构；不是 UE 或 WFC 官方类型。 */
UENUM(BlueprintType)
enum class EZeroEscapeStructureKind : uint8
{
	TwoFloorStair = 0,
	ThreeFloorStairwell = 1,
	HighCeilingRoom = 2
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

/** 普通二维 WFC 生成的可走格；结构自带的格子保存在结构记录中。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedOrdinaryCell
{
	GENERATED_BODY()

	/** X/Y 是层内坐标，Z 是从 0 开始的楼层序号。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector Coordinate = FIntVector::ZeroValue;

	/** 只表示同层 North/East/South/West；跨层连接单独保存。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated",
		meta = (Bitmask, BitmaskEnum = "/Script/Demo.EZeroEscapeOpenEdge"))
	uint8 OpeningMask = 0;
};

/** 楼梯或房间内部的一条无向通行连接。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedCellConnection
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector FirstCoordinate = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector SecondCoordinate = FIntVector::ZeroValue;
};

/** 一座已放置结构实际开放的连接口。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedStructureOpening
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FName OpeningId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector StructureCoordinate = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector ConnectedOrdinaryCoordinate = FIntVector::ZeroValue;
};

/** 每个楼梯落脚平台用于距离、表现和导航检查的唯一代表点。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedStructureLanding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FName LandingId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector Coordinate = FIntVector::ZeroValue;
};

/** 一座不可拆开的双层楼梯、三层楼梯间或高天花板房间。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedStructure
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 StableStructureId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FName DefinitionId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	EZeroEscapeStructureKind Kind = EZeroEscapeStructureKind::TwoFloorStair;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector BaseCoordinate = FIntVector::ZeroValue;

	/** 绕 Z 轴顺时针旋转 0、1、2、3 个 90 度。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	uint8 QuarterTurnCount = 0;

	/** 使用稳定 FName，而不是依赖 AllowedOpeningSets 的数组下标。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FName ActiveOpeningSetId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FIntVector> WalkableCells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FIntVector> SolidCells;

	/** 只包含本局真实存在楼层内的净空地址，不保存顶层以上的虚构地址。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FIntVector> ClearanceCells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedCellConnection> InternalConnections;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedStructureOpening> Openings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedStructureLanding> Landings;
};

/** 可走图中各类平面开口形态的观测计数，不是硬配额。 */
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

/** 每层保证通关的进入点、离开点和验收结果。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedFloorSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 FloorIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector RequiredEnterCoordinate = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector RequiredLeaveCoordinate = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 OrdinaryWalkableCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 TotalWalkableCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 RequiredRouteLengthTiles = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 FarthestRouteLengthTiles = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	double SpatialSeparationRatio = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	double RouteCoverageRatio = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FZeroEscapeJunctionMetrics JunctionMetrics;

	/** 连通无向图使用 E-V+1；0 表示无循环。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 CycleRank = 0;
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
	int32 FloorCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint GridSize = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	double LogicalTileSizeCm = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	double FloorHeightCm = 0.0;

	/** 地址转局部位置所需高度；生成后不得重新回读可变 DataAsset。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	double AnchorHeightCm = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedOrdinaryCell> OrdinaryCells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedStructure> Structures;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector PlayerSpawnCoordinate = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector PursuerSpawnCoordinate = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntVector ExitCoordinate = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedFloorSummary> Floors;

	/** 下标是较低楼层，值是该相邻楼层对的必需双层楼梯 StableStructureId。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<int32> RequiredTwoFloorStairStableIdByLowerFloor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 PlayerToExitRouteLengthTiles = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 VerticalTransitionCountOnShortestRoute = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FZeroEscapeJunctionMetrics JunctionMetrics;

	/** 整栋三维通行图的 E-V+1；成功 Plan 必须是单一连通分量。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int32 CycleRank = 0;
};

/** 单层二维 WFC 的搜索统计；只进入生成报告，不属于 Plan 或规范 Hash。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeFloorWfcMetrics
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 FloorIndex = INDEX_NONE;

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
	int32 WfcBacktrackCount = 0;
};

/** 搜索、实例化和耗时指标；回溯恢复 Domain 时不得回滚这些累计值。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationMetrics
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WholeLayoutAttemptCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 StructureCandidateEvaluationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 GeneratedFloorCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 RequiredTwoFloorStairCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 AdditionalTwoFloorStairCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 ThreeFloorStairwellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 HighCeilingRoomCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WalkableCellCount = 0;

	/** 按 FloorIndex 升序保存；整栋累计字段仍保留，方便运行时快速读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	TArray<FZeroEscapeFloorWfcMetrics> FloorWfcMetrics;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 NavigationProjectionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 NavigationPathTestCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 NavigationVisitedNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	double NavigationValidationMilliseconds = 0.0;
};

/** 公开生成入口的报告；失败必须保留首个不可恢复检查点。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationReport
{
	GENERATED_BODY()

	/** 只由 Runtime Generator 在最终终态写入；纯 Resolve/Solve 阶段保持为 0。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int64 OperationId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationStage Stage = EZeroEscapeGenerationStage::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	EZeroEscapeGenerationFailure Failure = EZeroEscapeGenerationFailure::None;

	/** 与错误相关的已生成结构稳定 Id；无明确结构时为 INDEX_NONE。 */
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
