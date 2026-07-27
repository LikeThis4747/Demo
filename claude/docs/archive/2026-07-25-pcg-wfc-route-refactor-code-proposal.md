# PCG 全图 WFC 路线重构：拟实现代码 V3（实施校准版）

> 状态：最新代码审计已通过且无阻断项；用户已明确授权并已完成源码、真实 Profile DataAsset、UE 5.8 构建、19 项 `Demo.PCG`、288 组 Seed Sweep 与 SelectedViewport PIE 技术烟测。本文已按真实实现回写；玩家主观观感、碰撞、导航和实际走通尚未验收。
>
> Owner：Codex / root
>
> 对应正式计划：`DOC/DailyPlan/2026-07-25-PCG-WFC路线重构实施方案.md`
>
> 代码基线：`b8a8c6e30b5f5e7c73c382e64c62c0e4d1706e78`，已推送内部工蜂。实施必须保留 `ZeroEscapeGenerationCore.cpp` 的 `FailCore` 修复，以及素材迁移写入的 `/Game/Assets/SciFiHydroLab` 路径。
>
> V2 审计依据：`claude/reviews/2026-07-25b-pcg-wfc-refactor-code-proposal-review.md`

## 0. V3 实施校准摘要

实现阶段用完整自动化暴露了一个重要差异：V2 曾把“候选数量最少”放在带权 Shannon 熵之前，这不是原版 WFC 的最低熵观察规则。当前默认权重下，未触及 Optional 的带权熵约 0.476，单开口前沿约 1.833；MRV 优先会反向追着前沿生长，使 `EmptyWeight` 失效。实际代码已经改为：

```cpp
const bool bStrictlyBetter = MinimumEntropyCell == INDEX_NONE
	|| Entropy < MinimumEntropy - UE_DOUBLE_SMALL_NUMBER;
const bool bEquivalentBest = !bStrictlyBetter
	&& FMath::Abs(Entropy - MinimumEntropy) <= UE_DOUBLE_SMALL_NUMBER;
```

完全同熵时继续使用 Seed 驱动的蓄水池抽样；候选顺序仍按权重无放回创建一次。修正后 36 组成功率从 14/36 提升到 32/36，证明观察顺序是主因。

剩余长尾采用成熟 WFC 常用的 bounded deterministic retries：请求 Seed 和 Signature 不变，只用 `AttemptIndex` 派生独立 WFC 子流；`MaxWfcCandidateAttempts=100000` 与 `MaxWfcBacktrackCount=25000` 是整局总预算，被稳定分给最多 `MaxWfcSolveAttempts=10` 棵搜索树，绝不按重试倍增。核心代码形态为：

```cpp
for (int32 AttemptIndex = 0; AttemptIndex < Settings.MaxWfcSolveAttempts; ++AttemptIndex)
{
	FRandomStream WfcRandom = FGenerationCore::MakeRandomStream(
		MasterSeed, Request.Signature.AlgorithmVersion,
		ERandomDomain::WfcLayout, AttemptIndex);
	WfcSettings.MaxCandidateAttempts = GetWfcAttemptBudget(
		Settings.MaxWfcCandidateAttempts, AttemptIndex, Settings.MaxWfcSolveAttempts);
	WfcSettings.MaxBacktrackCount = GetWfcAttemptBudget(
		Settings.MaxWfcBacktrackCount, AttemptIndex, Settings.MaxWfcSolveAttempts);
	// Solve 成功即原子提交；只对 BudgetExhausted 进入下一棵搜索树。
	// NoValid 表示带回溯搜索已经穷尽完整树，是无解证明，不能再换顺序重试。
}
```

Connected 也已从 V2 的 Cell BFS 增强为每格“中心 + N/E/S/W”五节点展开图，并使用迭代 Tarjan 强制连接 Relevant 所必需的中心/方向关节点。最终 288 组为 288/288：Solve Attempts P50/P95/Max=`1/3/7`，Candidate Attempts=`341/5954/17193`，Backtracks=`3/5005/15009`，Planning=`23.145/233.470/622.386 ms`，完整候选路线拒绝 Max=0。

## 1. 代码评审后补上的关键边界

### 1.1 完整候选必须能被拒绝并继续回溯

`Connected / Count / MaxConsecutive` 能保证连通、非空格数和连续贯通长度，但不能单独保证：

- `MaxRequiredRouteLengthTiles`；
- `MaxRequiredRouteExtraTiles`；
- K-of-N / CollectAll 的最短完整路线。

如果 `FWfcSolver::Solve` 提交第一个完整折叠结果后，Grid 才发现路线超限，那么当前 Seed 会直接失败，Solver 无法尝试同一决策栈中的其他候选。

因此本版只增加一个窄接口：

```text
完整 Domain -> 稠密 OpeningMask -> Grid 最终候选验收
                                      | Accept -> 原子提交
                                      | Reject -> 当前分支 contradiction，继续回溯
                                      | Fatal  -> 输入/代码不变量错误，立即终止
```

它不是通用约束插件框架：只有一个完成态验收点；WFC 不认识房间、Flow、K-of-N 或 `FZeroEscapeGeneratedLevelPlan`；Grid 不接触 Domain、Trail 或决策栈。

### 1.2 难度不能借权重明显扩大地图

完整 `FZeroEscapeWfcShapeWeights` 移入 Difficulty 后，必须校验三个难度：

1. `EmptyWeight` 相同；
2. 全部非空 Variant 的总权重相同：

```cpp
4 * DeadEnd + 2 * Straight + 4 * Corner + 4 * T + 1 * Cross
```

这样 Easy / Normal / Hard 只重新分配非空形态倾向，不主动改变 Empty 与 NonEmpty 的总体权重比例。最终非空格数仍由共享 Count 范围约束，并由 Seed Sweep 比较三个难度的分布。

### 1.3 V2 对回溯与校验分层的修订

本次审计不改变算法选型，只收紧四个实现细节：

1. `Solve` 每次只调用一次 CPP 内部 `BuildAndValidateDenseConstraintView`；它同时完成稠密映射、坐标唯一、Mask、边界和镜像校验，不能先校验时构建一次、初始化 Domain 时再构建一次。
2. Solver 不在完整候选导出后重复逐边扫描。局部边规则由传播保证并由 Automation 完整扫描；Count、MaxConsecutive、连通、路线和玩法不变量只在 Grid `ValidateFinalPlan` 做最终产品验收。
3. 外部坐标、Mask、配置和调用契约使用运行时失败报告；`NarrowDomain` 的内部 CellIndex 使用 `check`。审计建议把规范 Variant 校验降为 Debug，但当前 `Solve` 明确接收任意 `TArray<FTileVariant>`，所以 16 次顺序、正权重和总权重校验继续作为入口运行时校验；这点不机械采纳，除非未来先收窄接口。
4. 除“拒绝首个完整候选后可继续”外，必须独立覆盖：根固定点 Trail 清空后首决策矛盾，以及传播直接形成完整叶子后 Reject。它们分别防止恢复掉根 Ban、恢复过头或在无可退分支时死循环。

## 2. 文件增量与减量

### 修改

- `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h`
- `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationCore.h`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationCore.cpp`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.h`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.cpp`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcSolver.h`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcSolver.cpp`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp`

### 新增

- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcConstraints.h`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcConstraints.cpp`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/Tests/ZeroEscapeWfcSolverTests.cpp`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/Tests/ZeroEscapeGenerationPipelineTests.cpp`

### 删除或迁移净空

- 删除 `CarveOrthogonalRoute`、`CarveRequiredSkeleton`、`BuildOptionalEnvelope`、`PruneDisconnectedOptional`。
- 删除 `BackboneY`、`GateEdges`、`OptionalEnvelopeRadius`、`MaxOptionalSideBranches`、`MaxOptionalForwardLinks`、`ERandomDomain::OptionalLayout`。
- 删除对外的 `ValidateGuaranteedSolvableConstraints`；CPP 内部 `BuildAndValidateDenseConstraintView` 一次完成稠密视图和静态输入校验，不再重复建立坐标映射。
- 删除 WFC 成功导出后的重复逐边热路径复核；等价回归放进 Solver Automation，Grid 的独立最终验证继续保留。
- 保留规范 16 Variant 的运行时入口校验：当前私有 Solver API 仍允许测试或组合方传入任意数组，16 次循环成本可忽略，不能无证据地假定来源必为 `BuildCanonicalVariants`。
- 将旧 `ZeroEscapeGenerationTests.cpp` 当前内容迁入两个 `Tests/*.cpp` 后删除，不保留转发壳。

## 3. 公开失败与指标

### `ZeroEscapeGenerationTypes.h`

```cpp
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

	/** 输入合法，但全部 WFC 决策分支均已被证明不可满足。 */
	NoValidWfcSolution = 10,

	/** 搜索可能仍有解，但已达到候选尝试或回溯测量上限。 */
	SolverBudgetExhausted = 11
};
```

新值只追加到末尾，不改变现有枚举数值。

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationMetrics
{
	GENERATED_BODY()

	/** 成功布局中 OpeningMask != 0 的逻辑格数量。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WalkableCellCount = 0;

	/** 新建最小熵决策帧的数量；同一帧换候选不重复计数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcObservationCount = 0;

	/** 实际把一个决策 Cell 收窄为 singleton 候选的总次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcCandidateAttemptCount = 0;

	/** 局部传播或全局约束使 Domain 实际缩小的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcPropagationCount = 0;

	/** 当前搜索分支发生可恢复 contradiction 的总次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcContradictionCount = 0;

	/** 为尝试替代候选而恢复一个决策帧的次数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcBacktrackCount = 0;

	/** 完整折叠后因通关总长或额外折返超限而被拒绝的候选数。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcCollapsedCandidateRejectionCount = 0;

	/** 只统计非法输入或代码不变量；成功运行必须为 0。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 WfcInvariantFailureCount = 0;

	// InstancedMeshCount、HismComponentCount 与三项耗时字段原样保留。
};
```

删除 `RequiredCellCount / OptionalCellCount / PrunedOptionalCellCount`。

## 4. DataAsset 配置

### `ZeroEscapeGenerationAssets.h`

`FZeroEscapeWfcShapeWeights` 必须移动到 `FZeroEscapeDifficultyDefinition` 前定义，因为值成员需要完整类型。

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeWfcShapeWeights
{
	GENERATED_BODY()

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
	bool IsConfigured(FString& OutError) const;

	/** 计算 15 个非空 OpeningMask 的总权重，用于约束难度只改形态比例。 */
	int64 GetTotalNonEmptyVariantWeight() const
	{
		return 4LL * DeadEndWeight
			+ 2LL * StraightWeight
			+ 4LL * CornerWeight
			+ 4LL * TJunctionWeight
			+ CrossWeight;
	}
};
```

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeSharedRouteConstraints
{
	GENERATED_BODY()

	// GridSize、LogicalTileSizeCm、RoomSizeTiles、ObjectiveProgressBandCount 保留。

	/** 最终非空逻辑格数量下限。首轮 48 只是灰盒候选值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MinWalkableCellCount = 48;

	/** 最终非空逻辑格数量上限。首轮 72 只是灰盒候选值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MaxWalkableCellCount = 72;

	/** 同一轴上同时拥有两侧开口的连续格上限；T/Cross 也计入。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Topology", meta = (ClampMin = "1"))
	int32 MaxConsecutiveStraightTiles = 4;

	// MaxRequiredRouteLengthTiles、MaxRequiredRouteExtraTiles 保留。

	/** Seed Sweep 测量阶段的候选赋值安全上限，不是已冻结产品值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxWfcCandidateAttempts = 100000;

	/** Seed Sweep 测量阶段的决策帧恢复安全上限，不是已冻结产品值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
	int32 MaxWfcBacktrackCount = 25000;

	/** 同一请求最多建立的确定性搜索树数量；两个总预算平均分摊，不按次数倍增。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxWfcSolveAttempts = 10;

	// GameplayAnchorHeightCm 保留。
};
```

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeDifficultyDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	int32 ObjectiveCandidateCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	int32 RequiredObjectiveCount = 2;

	/** 只调整非空形态比例；共享格数和路线长度上限不随难度改变。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|WFC")
	FZeroEscapeWfcShapeWeights WfcShapeWeights;
};
```

删除 Profile 顶层 `WfcShapeWeights`。

### `ZeroEscapeGenerationAssets.cpp` 的关键校验

```cpp
const int32 GridCellCount = GridSize.X * GridSize.Y;
const FZeroEscapeSharedRouteConstraints& Route = SharedRouteConstraints;

if (Route.MinWalkableCellCount <= 0
	|| Route.MaxWalkableCellCount < Route.MinWalkableCellCount
	|| Route.MaxWalkableCellCount > GridCellCount
	|| Route.MaxConsecutiveStraightTiles <= 0
	|| Route.MaxConsecutiveStraightTiles > FMath::Max(GridSize.X, GridSize.Y)
	|| Route.MaxWfcCandidateAttempts <= 0
	|| Route.MaxWfcBacktrackCount <= 0
	|| Route.MaxWfcSolveAttempts <= 0
	|| Route.MaxWfcSolveAttempts > 16
	|| Route.MaxWfcCandidateAttempts < Route.MaxWfcSolveAttempts
	|| Route.MaxWfcBacktrackCount < Route.MaxWfcSolveAttempts)
{
	OutError = TEXT("全图 WFC 的格数、连续贯通或搜索预算配置非法。");
	return false;
}

int32 SharedEmptyWeight = INDEX_NONE;
int64 SharedNonEmptyWeight = INDEX_NONE;

for (const FZeroEscapeDifficultyDefinition& Definition : Difficulties)
{
	FString WeightError;
	if (!Definition.WfcShapeWeights.IsConfigured(WeightError))
	{
		OutError = FString::Printf(
			TEXT("难度 %d 的 WFC 权重非法：%s"),
			static_cast<int32>(Definition.Difficulty),
			*WeightError);
		return false;
	}

	const int64 NonEmptyWeight =
		Definition.WfcShapeWeights.GetTotalNonEmptyVariantWeight();
	if (NonEmptyWeight <= 0
		|| NonEmptyWeight + Definition.WfcShapeWeights.EmptyWeight > MAX_int32)
	{
		OutError = TEXT("WFC 16 个 Variant 的总权重超过 int32 加权抽样上限。");
		return false;
	}
	if (SharedEmptyWeight == INDEX_NONE)
	{
		SharedEmptyWeight = Definition.WfcShapeWeights.EmptyWeight;
		SharedNonEmptyWeight = NonEmptyWeight;
	}
	else if (SharedEmptyWeight != Definition.WfcShapeWeights.EmptyWeight
		|| SharedNonEmptyWeight != NonEmptyWeight)
	{
		OutError = TEXT(
			"三个难度必须保持相同 EmptyWeight 与非空总权重；"
			"难度只重新分配非空形态比例，不能靠填满更多格子延长单局。");
		return false;
	}

	const int32 FixedNonEmptyCellCount =
		2 + Definition.ObjectiveCandidateCount
			* Route.RoomSizeTiles * Route.RoomSizeTiles;
	if (FixedNonEmptyCellCount > Route.MaxWalkableCellCount)
	{
		OutError = TEXT("MaxWalkableCellCount 无法容纳 Start、Exit 和全部 Objective 房。");
		return false;
	}
}
```

旧“双门房替代主干边、每目标固定多 2 格”的构造路线证明随固定主干一起删除。

现有 `DA_LevelGenerationProfile` 需要一次原子迁移：把权重写入 Easy/Normal/Hard 三个条目，写入新 Count/Max/预算/尝试字段，并把 `ProfileVersion` 递增到 4。首轮三个难度使用相同权重；本次不修改 Presentation DataAsset。

## 5. Core 纯值接线

### `ZeroEscapeGenerationCore.h`

```cpp
inline constexpr int32 GAlgorithmVersion = 4;

enum class ERandomDomain : uint32
{
	Landmark = 0x20B8A51Du,
	WfcLayout = 0x95E27B43u,
	Presentation = 0xE13A5C89u
};

struct FGenerationProfileSnapshot
{
	int32 ProfileVersion = 0;
	FZeroEscapeSharedRouteConstraints SharedRouteConstraints;
	TArray<FZeroEscapeDifficultyDefinition> Difficulties;
	TArray<FZeroEscapeFlowDefinition> Flows;
};

struct FResolvedProgressionSettings
{
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;
	FName StableFlowId = NAME_None;
	int32 FlowVersion = 0;
	EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
	int32 ObjectiveCandidateCount = 0;
	int32 RequiredObjectiveCount = 0;

	/** 当前难度实际使用的 WFC 权重快照。 */
	FZeroEscapeWfcShapeWeights WfcShapeWeights;
};
```

`BuildGenerationSnapshot` 删除顶层权重复制；`ResolveProgressionSettings` 增加：

```cpp
Candidate.WfcShapeWeights = DifficultyDefinition->WfcShapeWeights;
```

并删除两个 Optional 字段。`ComputeCanonical...Hash` 必须把新共享参数和当前难度权重按固定字段顺序写入；数组编辑顺序仍不得进入 Hash。

## 6. 三项具体约束

### `ZeroEscapeWfcConstraints.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

namespace ZeroEscape::LevelGeneration
{
	using FWfcDomain = uint16;

	struct FZeroEscapeWfcSolveSettings
	{
		FIntPoint StartCoordinate = FIntPoint::ZeroValue;
		int32 MinWalkableCellCount = 1;
		int32 MaxWalkableCellCount = 1;
		int32 MaxConsecutiveStraightTiles = 1;
		int32 MaxCandidateAttempts = 1;
		int32 MaxBacktrackCount = 1;
	};

	enum class EWfcConstraintContradiction : uint8
	{
		None = 0,
		CountTooMany,
		CountTooFew,
		MaxConsecutiveHorizontal,
		MaxConsecutiveVertical,
		Disconnected
	};

	struct FWfcConstraintFailure
	{
		EWfcConstraintContradiction Kind = EWfcConstraintContradiction::None;
		int32 CellIndex = INDEX_NONE;
		int32 ObservedCount = 0;
		int32 Limit = 0;
		FString Message;
	};

	/** 约束只输出要删除的 Variant bit；Solver 负责真正修改并写 Trail。 */
	struct FWfcConstraintWorkspace
	{
		TArray<FWfcDomain> BanMaskByCell;
		TArray<uint8> Visited;
		TArray<int32> Queue;
		int32 BanCellCount = 0;

		void PrepareForPass(int32 CellCount)
		{
			BanMaskByCell.Init(0, CellCount);
			Visited.Init(0, CellCount);
			Queue.Reset(CellCount);
			BanCellCount = 0;
		}
	};

	class FWfcConstraints final
	{
	public:
		static bool ValidateSettings(
			FIntPoint GridSize,
			const FZeroEscapeWfcSolveSettings& Settings,
			int32 DomainCount,
			FString& OutError);

		/**
		 * 返回 false 表示当前搜索分支矛盾，可以回溯。
		 * 返回 true 时可能没有 Ban，也可能在 Workspace 中产生 Count/Max Ban。
		 */
		static bool Evaluate(
			FIntPoint GridSize,
			const FZeroEscapeWfcSolveSettings& Settings,
			TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure);
	};
}
```

### `ZeroEscapeWfcConstraints.cpp` 的核心逻辑

```cpp
namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		constexpr FWfcDomain EmptyBit = static_cast<FWfcDomain>(1u << 0u);
		constexpr FWfcDomain NonEmptyBits = static_cast<FWfcDomain>(MAX_uint16 & ~EmptyBit);

		constexpr FWfcDomain BuildVariantsContaining(const uint8 RequiredEdges)
		{
			FWfcDomain Result = 0;
			for (uint8 OpeningMask = 0; OpeningMask < 16; ++OpeningMask)
			{
				if ((OpeningMask & RequiredEdges) == RequiredEdges)
				{
					Result |= static_cast<FWfcDomain>(1u << OpeningMask);
				}
			}
			return Result;
		}

		constexpr FWfcDomain HorizontalThrough = BuildVariantsContaining(0x0A); // E|W
		constexpr FWfcDomain VerticalThrough = BuildVariantsContaining(0x05);   // N|S
		constexpr FWfcDomain OpenByDirection[4] =
		{
			BuildVariantsContaining(0x01),
			BuildVariantsContaining(0x02),
			BuildVariantsContaining(0x04),
			BuildVariantsContaining(0x08)
		};

		bool CanBeWalkable(const FWfcDomain Domain)
		{
			return (Domain & NonEmptyBits) != 0;
		}

		bool MustBeWalkable(const FWfcDomain Domain)
		{
			return CanBeWalkable(Domain) && (Domain & EmptyBit) == 0;
		}

		void AddBan(
			const int32 CellIndex,
			const FWfcDomain RequestedBan,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace)
		{
			const FWfcDomain EffectiveBan = RequestedBan & Domains[CellIndex];
			if (EffectiveBan == 0)
			{
				return;
			}

			FWfcDomain& ExistingBan = Workspace.BanMaskByCell[CellIndex];
			if (ExistingBan == 0)
			{
				++Workspace.BanCellCount;
			}
			ExistingBan |= EffectiveBan;
		}

		bool EvaluateCount(
			const FZeroEscapeWfcSolveSettings& Settings,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure)
		{
			int32 Forced = 0;
			int32 Possible = 0;
			for (const FWfcDomain Domain : Domains)
			{
				check(Domain != 0);
				if (CanBeWalkable(Domain))
				{
					++Possible;
					Forced += MustBeWalkable(Domain) ? 1 : 0;
				}
			}

			if (Forced > Settings.MaxWalkableCellCount)
			{
				OutFailure.Kind = EWfcConstraintContradiction::CountTooMany;
				OutFailure.ObservedCount = Forced;
				OutFailure.Limit = Settings.MaxWalkableCellCount;
				OutFailure.Message = TEXT("已被迫非空的 Cell 数超过上限。");
				return false;
			}
			if (Possible < Settings.MinWalkableCellCount)
			{
				OutFailure.Kind = EWfcConstraintContradiction::CountTooFew;
				OutFailure.ObservedCount = Possible;
				OutFailure.Limit = Settings.MinWalkableCellCount;
				OutFailure.Message = TEXT("仍可能非空的 Cell 数低于下限。");
				return false;
			}

			for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
			{
				const FWfcDomain Domain = Domains[CellIndex];
				const bool bFlexible = (Domain & EmptyBit) != 0 && (Domain & NonEmptyBits) != 0;
				if (!bFlexible)
				{
					continue;
				}

				if (Forced == Settings.MaxWalkableCellCount)
				{
					AddBan(CellIndex, NonEmptyBits, Domains, Workspace);
				}
				if (Possible == Settings.MinWalkableCellCount)
				{
					AddBan(CellIndex, EmptyBit, Domains, Workspace);
				}
			}
			return true;
		}

		bool EvaluateConsecutiveLine(
			const int32 FirstCell,
			const int32 Stride,
			const int32 LineLength,
			const FWfcDomain ThroughBits,
			const bool bHorizontal,
			const FZeroEscapeWfcSolveSettings& Settings,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure)
		{
			const int32 WindowLength = Settings.MaxConsecutiveStraightTiles + 1;
			if (WindowLength > LineLength)
			{
				return true;
			}

			for (int32 WindowStart = 0;
				WindowStart + WindowLength <= LineLength;
				++WindowStart)
			{
				int32 ForcedThrough = 0;
				int32 FlexibleCell = INDEX_NONE;
				int32 FlexibleCount = 0;

				for (int32 Offset = 0; Offset < WindowLength; ++Offset)
				{
					const int32 CellIndex = FirstCell + (WindowStart + Offset) * Stride;
					const FWfcDomain Domain = Domains[CellIndex];
					const bool bCanThrough = (Domain & ThroughBits) != 0;
					const bool bMustThrough = bCanThrough
						&& (Domain & static_cast<FWfcDomain>(~ThroughBits)) == 0;
					ForcedThrough += bMustThrough ? 1 : 0;
					if (bCanThrough && !bMustThrough)
					{
						FlexibleCell = CellIndex;
						++FlexibleCount;
					}
				}

				if (ForcedThrough == WindowLength)
				{
					OutFailure.Kind = bHorizontal
						? EWfcConstraintContradiction::MaxConsecutiveHorizontal
						: EWfcConstraintContradiction::MaxConsecutiveVertical;
					OutFailure.CellIndex = FirstCell + WindowStart * Stride;
					OutFailure.ObservedCount = WindowLength;
					OutFailure.Limit = Settings.MaxConsecutiveStraightTiles;
					OutFailure.Message = TEXT("连续轴向贯通格超过上限。");
					return false;
				}

				if (ForcedThrough == WindowLength - 1 && FlexibleCount == 1)
				{
					AddBan(FlexibleCell, ThroughBits, Domains, Workspace);
				}
			}
			return true;
		}

		bool EvaluateConnected(
			const FIntPoint GridSize,
			const FZeroEscapeWfcSolveSettings& Settings,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure)
		{
			const int32 StartIndex = ZeroEscape::Grid::ToIndex(Settings.StartCoordinate, GridSize);
			if (!MustBeWalkable(Domains[StartIndex]))
			{
				OutFailure.Kind = EWfcConstraintContradiction::Disconnected;
				OutFailure.CellIndex = StartIndex;
				OutFailure.Message = TEXT("Start 必须是非空 Required Cell。");
				return false;
			}

			Workspace.Visited[StartIndex] = 1;
			Workspace.Queue.Add(StartIndex);
			for (int32 Head = 0; Head < Workspace.Queue.Num(); ++Head)
			{
				const int32 CellIndex = Workspace.Queue[Head];
				const FIntPoint Coordinate(CellIndex % GridSize.X, CellIndex / GridSize.X);
				for (uint8 Direction = 0; Direction < ZeroEscape::Grid::DirectionCount; ++Direction)
				{
					if ((Domains[CellIndex] & OpenByDirection[Direction]) == 0)
					{
						continue;
					}
					const FIntPoint Neighbor = ZeroEscape::Grid::Step(Coordinate, Direction);
					if (!ZeroEscape::Grid::IsInside(Neighbor, GridSize))
					{
						continue;
					}
					const int32 NeighborIndex = ZeroEscape::Grid::ToIndex(Neighbor, GridSize);
					const uint8 Opposite = ZeroEscape::Grid::OppositeDirectionIndex(Direction);
					if (Workspace.Visited[NeighborIndex] == 0
						&& (Domains[NeighborIndex] & OpenByDirection[Opposite]) != 0)
					{
						Workspace.Visited[NeighborIndex] = 1;
						Workspace.Queue.Add(NeighborIndex);
					}
				}
			}

			for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
			{
				if (MustBeWalkable(Domains[CellIndex]) && Workspace.Visited[CellIndex] == 0)
				{
					OutFailure.Kind = EWfcConstraintContradiction::Disconnected;
					OutFailure.CellIndex = CellIndex;
					OutFailure.Message = TEXT("被迫非空的 Cell 已不可能从 Start 到达。");
					return false;
				}
			}
			return true;
		}
	}

	bool FWfcConstraints::Evaluate(
		const FIntPoint GridSize,
		const FZeroEscapeWfcSolveSettings& Settings,
		const TConstArrayView<FWfcDomain> Domains,
		FWfcConstraintWorkspace& Workspace,
		FWfcConstraintFailure& OutFailure)
	{
		Workspace.PrepareForPass(Domains.Num());
		OutFailure = {};

		if (!EvaluateCount(Settings, Domains, Workspace, OutFailure))
		{
			return false;
		}

		for (int32 Y = 0; Y < GridSize.Y; ++Y)
		{
			if (!EvaluateConsecutiveLine(
					Y * GridSize.X, 1, GridSize.X, HorizontalThrough, true,
					Settings, Domains, Workspace, OutFailure))
			{
				return false;
			}
		}
		for (int32 X = 0; X < GridSize.X; ++X)
		{
			if (!EvaluateConsecutiveLine(
					X, GridSize.X, GridSize.Y, VerticalThrough, false,
					Settings, Domains, Workspace, OutFailure))
			{
				return false;
			}
		}

		return EvaluateConnected(GridSize, Settings, Domains, Workspace, OutFailure);
	}
}
```

这里故意没有实现通用接口、增量 Tracker 或 Loop 配额；实际实现已加入五节点展开图与迭代 Tarjan 关节点传播。

## 7. WFC 接口与完成态验收

### `ZeroEscapeWfcSolver.h`

```cpp
#include "Containers/ArrayView.h"
#include "Templates/Function.h"
#include "PCG/ZeroEscapeWfcConstraints.h"

namespace ZeroEscape::LevelGeneration
{
	enum class EWfcCollapsedCandidateVerdict : uint8
	{
		Accept,
		RejectBranch,
		FatalError
	};

	struct FWfcCollapsedCandidateEvaluation
	{
		EWfcCollapsedCandidateVerdict Verdict = EWfcCollapsedCandidateVerdict::Accept;
		FString Message;
		int32 ActualValue = 0;
		int32 LimitValue = 0;
		int32 RelatedStableId = INDEX_NONE;

		static FWfcCollapsedCandidateEvaluation Accept()
		{
			return {};
		}

		static FWfcCollapsedCandidateEvaluation Reject(
			FString InMessage,
			const int32 InActualValue,
			const int32 InLimitValue)
		{
			FWfcCollapsedCandidateEvaluation Result;
			Result.Verdict = EWfcCollapsedCandidateVerdict::RejectBranch;
			Result.Message = MoveTemp(InMessage);
			Result.ActualValue = InActualValue;
			Result.LimitValue = InLimitValue;
			return Result;
		}

		static FWfcCollapsedCandidateEvaluation Fatal(
			FString InMessage,
			const int32 InRelatedStableId = INDEX_NONE)
		{
			FWfcCollapsedCandidateEvaluation Result;
			Result.Verdict = EWfcCollapsedCandidateVerdict::FatalError;
			Result.Message = MoveTemp(InMessage);
			Result.RelatedStableId = InRelatedStableId;
			return Result;
		}
	};

	using FWfcCollapsedCandidateValidator =
		TFunctionRef<FWfcCollapsedCandidateEvaluation(TConstArrayView<uint8>)>;

	class FWfcSolver final
	{
	public:
		static void BuildCanonicalVariants(
			const FZeroEscapeWfcShapeWeights& Weights,
			TStaticArray<FTileVariant, 16>& OutVariants);

		static bool Solve(
			FIntPoint GridSize,
			const TArray<FGridCellConstraint>& Constraints,
			const FZeroEscapeWfcSolveSettings& SolveSettings,
			const TArray<FTileVariant>& Variants,
			FRandomStream& Random,
			FWfcCollapsedCandidateValidator ValidateCollapsedCandidate,
			TArray<uint8>& OutOpeningMaskByCell,
			FZeroEscapeGenerationReport& OutReport);
	};
}
```

CPP 内部的 `BuildAndValidateDenseConstraintView` 保留稠密 Grid、坐标唯一、空指针、4-bit Mask、Open/Closed 冲突、边界和必开边镜像，并在一次入口调用内完成。删除 `Required` 必须预开一条边和 Optional 全空的构造性见证，不再暴露一个可被调用方重复执行的公开验证入口。

`Solve` 必须在建立任何 Domain、执行 BFS 或消耗随机数前，按固定顺序完成一次入口校验：

```cpp
OutOpeningMaskByCell.Reset();
ResetWfcMetrics(OutReport.Metrics);

FString ValidationError;
if (!FWfcConstraints::ValidateSettings(
		GridSize,
		SolveSettings,
		Constraints.Num(),
		ValidationError))
{
	return ReportConfigurationFailure(ValidationError, OutReport);
}

TArray<const FGridCellConstraint*> ConstraintsByIndex;
if (!BuildAndValidateDenseConstraintView(
		GridSize,
		Constraints,
		ConstraintsByIndex,
		ValidationError))
{
	return ReportInvariantFailure(ValidationError, OutReport);
}

if (!ValidateCanonicalVariants(Variants, ValidationError))
{
	return ReportInvariantFailure(ValidationError, OutReport);
}

const int32 StartIndex =
	ZeroEscape::Grid::ToIndex(SolveSettings.StartCoordinate, GridSize);
if (ConstraintsByIndex[StartIndex]->Domain != EGridCellDomain::Required)
{
	return ReportInvariantFailure(
		TEXT("WFC Connected 的 Start 必须对应 Required Cell。"),
		OutReport);
}
```

`ValidateSettings` 校验 Start 在界内、`0 < Min <= Max <= CellCount`、连续贯通上限和两个搜索上限为正；非法策划值报告 `InvalidConfiguration`，Start 不是 Required 等调用方契约错误报告 `SolverInvariantViolation`。上述顺序没有建立 Domain、没有执行 BFS、没有消耗随机数，并且只构建、校验一次稠密视图。Variant 校验仍保留，因为当前接口接受任意数组；若未来 `Solve` 直接接收权重并在内部构造 Variant，再单独评审是否降为 Debug。

## 8. Trail、传播和 chronological backtracking

### `ZeroEscapeWfcSolver.cpp` 私有状态

```cpp
namespace
{
	constexpr int32 CanonicalVariantCount = 16;

	struct FDomainChange
	{
		int32 CellIndex = INDEX_NONE;
		uint16 PreviousDomain = 0;
	};

	struct FWfcDecision
	{
		int32 CellIndex = INDEX_NONE;
		TArray<uint8, TInlineAllocator<CanonicalVariantCount>> CandidateOrder;
		int32 NextCandidateIndex = 0;
		int32 TrailStart = 0;
	};

	enum class EWfcBranchStatus : uint8
	{
		Stable,
		Contradiction,
		InvariantFailure
	};

	struct FWfcBranchResult
	{
		EWfcBranchStatus Status = EWfcBranchStatus::Stable;
		int32 RelatedCellIndex = INDEX_NONE;
		FString Message;
	};

	/** 所有候选赋值、局部传播和全局 Ban 的唯一 Domain 修改入口。 */
	bool NarrowDomain(
		const int32 CellIndex,
		const uint16 AllowedVariants,
		TArray<uint16>& InOutDomains,
		TArray<FDomainChange>& InOutTrail,
		TArray<int32>& OutChangedCells,
		FWfcBranchResult& OutResult)
	{
		// CellIndex 只来自最小熵选择、合法邻格或稠密约束 Ban；属于内部不变量。
		check(InOutDomains.IsValidIndex(CellIndex));

		const uint16 Previous = InOutDomains[CellIndex];
		const uint16 Next = Previous & AllowedVariants;
		if (Next == Previous)
		{
			return true;
		}

		InOutTrail.Add({CellIndex, Previous});
		InOutDomains[CellIndex] = Next;
		OutChangedCells.Add(CellIndex);

		if (Next == 0)
		{
			OutResult.Status = EWfcBranchStatus::Contradiction;
			OutResult.RelatedCellIndex = CellIndex;
			return false;
		}
		return true;
	}

	void RestoreDomains(
		const int32 TrailStart,
		TArray<uint16>& InOutDomains,
		TArray<FDomainChange>& InOutTrail)
	{
		check(TrailStart >= 0 && TrailStart <= InOutTrail.Num());
		for (int32 Index = InOutTrail.Num() - 1; Index >= TrailStart; --Index)
		{
			const FDomainChange& Change = InOutTrail[Index];
			InOutDomains[Change.CellIndex] = Change.PreviousDomain;
		}
		InOutTrail.SetNum(TrailStart, EAllowShrinking::No);
	}

	/**
	 * 调用方先保证当前帧仍有候选；false 只表示候选尝试预算已耗尽。
	 * 候选来自创建帧时的 Domain，恢复到 TrailStart 后必须仍然存在。
	 */
	bool TryNextCandidate(
		FWfcDecision& Decision,
		const FZeroEscapeWfcSolveSettings& Settings,
		TArray<uint16>& InOutDomains,
		TArray<FDomainChange>& InOutTrail,
		FZeroEscapeGenerationReport& InOutReport)
	{
		check(Decision.NextCandidateIndex < Decision.CandidateOrder.Num());
		if (InOutReport.Metrics.WfcCandidateAttemptCount
			>= Settings.MaxCandidateAttempts)
		{
			return false;
		}

		const uint8 VariantIndex =
			Decision.CandidateOrder[Decision.NextCandidateIndex++];
		const uint16 CandidateBit = static_cast<uint16>(1u << VariantIndex);
		check((InOutDomains[Decision.CellIndex] & CandidateBit) != 0);

		TArray<int32> ChangedCells;
		FWfcBranchResult Result;
		const bool bAssigned = NarrowDomain(
			Decision.CellIndex,
			CandidateBit,
			InOutDomains,
			InOutTrail,
			ChangedCells,
			Result);

		// 从帧创建时的合法候选中选择 singleton，不应在赋值瞬间制造空 Domain。
		check(bAssigned && Result.Status == EWfcBranchStatus::Stable);
		++InOutReport.Metrics.WfcCandidateAttemptCount;
		return true;
	}
}
```

候选顺序在决策帧创建时按权重无放回生成一次；回溯只推进 `NextCandidateIndex`，不重新消耗 `Random`。

### Required 初始化不能再依赖 RequiredOpen

```cpp
switch (Constraint.Domain)
{
case EGridCellDomain::Outside:
	bAllowed = OpeningMask == 0;
	break;

case EGridCellDomain::Required:
	// Start、Exit 和 Objective 房格必须非空，即使外部连接尚未预刻。
	bAllowed = OpeningMask != 0
		&& (OpeningMask & Constraint.RequiredOpenMask) == Constraint.RequiredOpenMask
		&& (OpeningMask & Constraint.RequiredClosedMask) == 0;
	break;

case EGridCellDomain::Optional:
	bAllowed = (OpeningMask & Constraint.RequiredOpenMask) == Constraint.RequiredOpenMask
		&& (OpeningMask & Constraint.RequiredClosedMask) == 0;
	break;

default:
	return ReportInvariantFailure(TEXT("未知 EGridCellDomain。"), OutReport);
}
```

### 固定点循环

```cpp
FWfcBranchResult StabilizeDomains(
	const FIntPoint GridSize,
	const TArray<const FGridCellConstraint*>& ConstraintsByIndex,
	const FZeroEscapeWfcSolveSettings& Settings,
	const TArray<FTileVariant>& Variants,
	TArray<int32> PropagationSources,
	TArray<uint16>& InOutDomains,
	TArray<FDomainChange>& InOutTrail,
	FWfcConstraintWorkspace& ConstraintWorkspace,
	FZeroEscapeGenerationReport& InOutReport)
{
	for (;;)
	{
		FWfcBranchResult Result = PropagateDomainsWithTrail(
			GridSize,
			ConstraintsByIndex,
			Variants,
			MoveTemp(PropagationSources),
			InOutDomains,
			InOutTrail,
			InOutReport);
		if (Result.Status != EWfcBranchStatus::Stable)
		{
			return Result;
		}

		FWfcConstraintFailure ConstraintFailure;
		if (!FWfcConstraints::Evaluate(
				GridSize,
				Settings,
				InOutDomains,
				ConstraintWorkspace,
				ConstraintFailure))
		{
			return {
				EWfcBranchStatus::Contradiction,
				ConstraintFailure.CellIndex,
				ConstraintFailure.Message};
		}

		if (ConstraintWorkspace.BanCellCount == 0)
		{
			return {};
		}

		PropagationSources.Reset();
		for (int32 CellIndex = 0; CellIndex < InOutDomains.Num(); ++CellIndex)
		{
			const uint16 Ban = ConstraintWorkspace.BanMaskByCell[CellIndex];
			if (Ban == 0)
			{
				continue;
			}

			if (!NarrowDomain(
					CellIndex,
					static_cast<uint16>(~Ban),
					InOutDomains,
					InOutTrail,
					PropagationSources,
					Result))
			{
				return Result;
			}
			++InOutReport.Metrics.WfcPropagationCount;
		}
	}
}
```

### 主搜索循环

```cpp
TArray<FDomainChange> Trail;
TArray<FWfcDecision> Decisions;
FWfcConstraintWorkspace ConstraintWorkspace;
TArray<int32> PendingSources = MakeAllCellIndices(Domains.Num());
bool bRootState = true;

for (;;)
{
	// 所有候选赋值与回溯替代候选都从同一个稳定入口继续，避免漏处理矛盾。
	FWfcBranchResult BranchResult = StabilizeDomains(
		GridSize,
		ConstraintsByIndex,
		SolveSettings,
		Variants,
		MoveTemp(PendingSources),
		Domains,
		Trail,
		ConstraintWorkspace,
		OutReport);

	if (BranchResult.Status == EWfcBranchStatus::InvariantFailure)
	{
		return ReportInvariantFailure(BranchResult.Message, OutReport);
	}

	if (bRootState && BranchResult.Status == EWfcBranchStatus::Stable)
	{
		// 根固定点由所有分支共享，首个决策不得把这些合法 Ban 撤销。
		Trail.Reset();
		bRootState = false;
	}

	bool bNeedBacktrack = BranchResult.Status == EWfcBranchStatus::Contradiction;
	if (!bNeedBacktrack)
	{
		const int32 CellIndex = FindMinimumEntropyCell(Domains, Variants);
		if (CellIndex != INDEX_NONE)
		{
			FWfcDecision Decision;
			Decision.CellIndex = CellIndex;
			Decision.TrailStart = Trail.Num();
			if (!BuildWeightedCandidateOrder(
					Domains[CellIndex], Variants, Random, Decision.CandidateOrder))
			{
				return ReportInvariantFailure(TEXT("无法建立候选顺序。"), OutReport);
			}

			Decisions.Add(MoveTemp(Decision));
			++OutReport.Metrics.WfcObservationCount;

			if (!TryNextCandidate(
					Decisions.Last(), SolveSettings, Domains, Trail, OutReport))
			{
				return ReportBudgetFailure(
					TEXT("WFC 达到候选尝试上限。"),
					OutReport.Metrics.WfcCandidateAttemptCount,
					SolveSettings.MaxCandidateAttempts,
					OutReport);
			}

			PendingSources = {CellIndex};
			continue;
		}

		// 没有可观察 Cell 表示所有 Domain 都是 singleton；此时仍需调用 Grid 验收。
		TArray<uint8> CollapsedMasks;
		FString ExportError;
		if (!BuildCollapsedOpeningMasks(Domains, Variants, CollapsedMasks, ExportError))
		{
			return ReportInvariantFailure(ExportError, OutReport);
		}
		const FWfcCollapsedCandidateEvaluation Evaluation =
			ValidateCollapsedCandidate(CollapsedMasks);
		if (Evaluation.Verdict == EWfcCollapsedCandidateVerdict::Accept)
		{
			OutOpeningMaskByCell = MoveTemp(CollapsedMasks);
			return true;
		}
		if (Evaluation.Verdict == EWfcCollapsedCandidateVerdict::FatalError)
		{
			OutReport.RelatedStableId = Evaluation.RelatedStableId;
			return ReportInvariantFailure(Evaluation.Message, OutReport);
		}

		++OutReport.Metrics.WfcCollapsedCandidateRejectionCount;
		BranchResult = {
			EWfcBranchStatus::Contradiction,
			INDEX_NONE,
			Evaluation.Message};
		bNeedBacktrack = true;
	}

	check(bNeedBacktrack);
	++OutReport.Metrics.WfcContradictionCount;
	bool bFoundAlternative = false;
	while (!Decisions.IsEmpty())
	{
		if (OutReport.Metrics.WfcBacktrackCount >= SolveSettings.MaxBacktrackCount)
		{
			return ReportBudgetFailure(
				TEXT("WFC 达到回溯上限。"),
				OutReport.Metrics.WfcBacktrackCount,
				SolveSettings.MaxBacktrackCount,
				OutReport);
		}

		FWfcDecision& Frame = Decisions.Last();
		RestoreDomains(Frame.TrailStart, Domains, Trail);
		++OutReport.Metrics.WfcBacktrackCount;

		if (Frame.NextCandidateIndex >= Frame.CandidateOrder.Num())
		{
			Decisions.Pop(EAllowShrinking::No);
			continue;
		}

		if (!TryNextCandidate(Frame, SolveSettings, Domains, Trail, OutReport))
		{
			return ReportBudgetFailure(
				TEXT("WFC 达到候选尝试上限。"),
				OutReport.Metrics.WfcCandidateAttemptCount,
				SolveSettings.MaxCandidateAttempts,
				OutReport);
		}

		PendingSources = {Frame.CellIndex};
		bFoundAlternative = true;
		break;
	}

	if (!bFoundAlternative)
	{
		return ReportNoValidSolution(BranchResult, OutReport);
	}
}
```

上述控制流保证新决策、祖先替代候选和根状态都从同一个 `StabilizeDomains` 入口继续；任何替代候选产生的 contradiction 都会在下一轮被处理，不会越过回溯状态机。`Trail.Reset()` 只把根固定点变为不可回滚基线，不修改 `Domains`；叶子 Reject 只恢复最近决策帧的 `TrailStart`，若根传播已经直接形成完整叶子且 `Decisions` 为空，则稳定返回 `NoValidWfcSolution`。

预算定义冻结为：

- `WfcSolveAttemptCount`：每启动一棵确定性 WFC 搜索树加一；
- `WfcCandidateAttemptCount`：每次把决策 Cell 收窄到一个候选 singleton 时加一；
- `WfcBacktrackCount`：每次恢复一个决策帧时加一；
- `WfcLocalAdjacency/Count/MaxConsecutive/Connected/GlobalBanContradictionCount`：按来源拆分可恢复矛盾；
- 初始化、传播、BFS、完整候选验收不消耗 Candidate Attempt，但各自仍有 Grid 硬上限和耗时统计；
- 指标统计失败分支实际成本，回溯时不回滚。

## 9. Grid：不再画路线，只固定语义占格

### `ZeroEscapeGridLayoutSolver.h`

```cpp
struct FGridLayoutSettings
{
	FIntPoint GridSize = FIntPoint(18, 12);
	int32 LogicalTileSizeCm = 600;
	int32 RoomSizeTiles = 2;
	int32 ObjectiveProgressBandCount = 3;

	int32 MinWalkableCellCount = 48;
	int32 MaxWalkableCellCount = 72;
	int32 MaxConsecutiveStraightTiles = 4;
	int32 MaxRequiredRouteLengthTiles = 40;
	int32 MaxRequiredRouteExtraTiles = 14;
	int32 MaxWfcCandidateAttempts = 100000;
	int32 MaxWfcBacktrackCount = 25000;
	int32 MaxWfcSolveAttempts = 10;

	double GameplayAnchorHeightCm = 100.0;
};

struct FGridLayoutRequest
{
	FZeroEscapeGenerationSignature Signature;
	FProgressionIntent Progression;
};
```

### 初始化整个 Grid 为 WFC 候选区

```cpp
void InitializeConstraintGrid(
	const FIntPoint GridSize,
	FGridWorkingState& OutState)
{
	OutState = {};
	OutState.GridSize = GridSize;
	OutState.Constraints.SetNum(GridSize.X * GridSize.Y);

	for (int32 Y = 0; Y < GridSize.Y; ++Y)
	{
		for (int32 X = 0; X < GridSize.X; ++X)
		{
			FGridCellConstraint& Cell = OutState.Constraints[Y * GridSize.X + X];
			Cell = {};
			Cell.Coordinate = FIntPoint(X, Y);
			Cell.Domain = EGridCellDomain::Optional;
			Cell.RegionId = 0;
			Cell.RegionKind = EZeroEscapeGridRegionKind::Corridor;

			if (Y == GridSize.Y - 1) Cell.RequiredClosedMask |= DirectionBit(0);
			if (X == GridSize.X - 1) Cell.RequiredClosedMask |= DirectionBit(1);
			if (Y == 0) Cell.RequiredClosedMask |= DirectionBit(2);
			if (X == 0) Cell.RequiredClosedMask |= DirectionBit(3);
		}
	}
}
```

### 只固定 Start / Exit / Objective 房内约束

```cpp
bool ApplyLandmarkConstraints(
	const FGridLayoutSettings& Settings,
	FGridWorkingState& State,
	FZeroEscapeGenerationReport& OutReport)
{
	MarkRequired(State, State.StartCoordinate, 1, EZeroEscapeGridRegionKind::Start);
	MarkRequired(State, State.ExitCoordinate, 2, EZeroEscapeGridRegionKind::Exit);

	for (const FObjectiveRoomPlacement& Room : State.ObjectiveRooms)
	{
		for (int32 LocalY = 0; LocalY < Settings.RoomSizeTiles; ++LocalY)
		{
			for (int32 LocalX = 0; LocalX < Settings.RoomSizeTiles; ++LocalX)
			{
				const FIntPoint Cell = Room.MinCoordinate + FIntPoint(LocalX, LocalY);
				MarkRequired(
					State,
					Cell,
					Room.RegionId,
					EZeroEscapeGridRegionKind::Objective);

				if (LocalX + 1 < Settings.RoomSizeTiles
					&& !AddRequiredOpening(State, Cell, Cell + FIntPoint(1, 0), OutReport))
				{
					return false;
				}
				if (LocalY + 1 < Settings.RoomSizeTiles
					&& !AddRequiredOpening(State, Cell, Cell + FIntPoint(0, 1), OutReport))
				{
					return false;
				}
			}
		}
	}
	return true;
}
```

房间外周不预设 Gate，也不预先封闭；WFC 决定从哪一侧接入、是否有第二出口。

### `ValidateFinalPlan` 增加独立 Count / Max 复核

Solver 约束不能作为自身正确性的唯一证据。Grid 现有最终验证在建立 `CellByCoordinate` 后增加以下检查；失败说明 Solver 接受了违反产品不变量的结果，因此按 Fatal / `SolverInvariantViolation` 处理，而不是再次当作普通路线 Reject：

```cpp
if (Plan.Cells.Num() < Settings.MinWalkableCellCount
	|| Plan.Cells.Num() > Settings.MaxWalkableCellCount)
{
	return Fail(
		OutReport,
		EZeroEscapeGenerationStage::GlobalValidation,
		EZeroEscapeGenerationFailure::SolverInvariantViolation,
		TEXT("最终非空 Cell 数量不满足 WFC Count 范围。"),
		Plan.Cells.Num(),
		Plan.Cells.Num() < Settings.MinWalkableCellCount
			? Settings.MinWalkableCellCount
			: Settings.MaxWalkableCellCount);
}

auto HasAxisThrough =
	[&](const FIntPoint Coordinate, const uint8 RequiredEdges)
	{
		const int32* CellIndex = CellByCoordinate.Find(Coordinate);
		return CellIndex != nullptr
			&& (Plan.Cells[*CellIndex].OpeningMask & RequiredEdges) == RequiredEdges;
	};

int32 MaxObservedRun = 0;
for (int32 Y = 0; Y < Plan.GridSize.Y; ++Y)
{
	int32 Run = 0;
	for (int32 X = 0; X < Plan.GridSize.X; ++X)
	{
		Run = HasAxisThrough(FIntPoint(X, Y), 0x0A) ? Run + 1 : 0; // E|W
		MaxObservedRun = FMath::Max(MaxObservedRun, Run);
	}
}
for (int32 X = 0; X < Plan.GridSize.X; ++X)
{
	int32 Run = 0;
	for (int32 Y = 0; Y < Plan.GridSize.Y; ++Y)
	{
		Run = HasAxisThrough(FIntPoint(X, Y), 0x05) ? Run + 1 : 0; // N|S
		MaxObservedRun = FMath::Max(MaxObservedRun, Run);
	}
}

if (MaxObservedRun > Settings.MaxConsecutiveStraightTiles)
{
	return Fail(
		OutReport,
		EZeroEscapeGenerationStage::GlobalValidation,
		EZeroEscapeGenerationFailure::SolverInvariantViolation,
		TEXT("最终布局超过连续轴向贯通格上限。"),
		MaxObservedRun,
		Settings.MaxConsecutiveStraightTiles);
}
```

同时把旧错误文本中的“必达骨架 Cell”“WFC 或剪枝丢失”“剪枝后仍有孤岛”改为“Required Cell”“WFC 丢失”“最终布局仍有孤岛”，避免删除旧算法后日志继续误导。

### Grid 使用完成态验收接入 WFC

私有 `ExportCandidatePlan` 的 OpeningMask 参数会从 `const TArray<uint8>&` 收敛为 `TConstArrayView<uint8>`，这样每次完整候选验收无需为了适配旧签名复制数组；该改动不进入 Public API。

以下保留“单棵搜索树如何调用完成态验收”的主体；实际外层由第 0 节的有限尝试循环包裹，并为每次写入分片后的预算、独立随机子流和临时 Report，最后累加搜索指标。

```cpp
FZeroEscapeWfcSolveSettings WfcSettings;
WfcSettings.StartCoordinate = State.StartCoordinate;
WfcSettings.MinWalkableCellCount = Settings.MinWalkableCellCount;
WfcSettings.MaxWalkableCellCount = Settings.MaxWalkableCellCount;
WfcSettings.MaxConsecutiveStraightTiles = Settings.MaxConsecutiveStraightTiles;
// MaxCandidateAttempts / MaxBacktrackCount 由外层 Attempt 循环写入当次预算分片。

FZeroEscapeGeneratedLevelPlan AcceptedCandidate;
const auto ValidateCollapsedCandidate =
	[&](const TConstArrayView<uint8> OpeningMasks)
		-> FWfcCollapsedCandidateEvaluation
	{
		FZeroEscapeGeneratedLevelPlan Candidate;
		FZeroEscapeGenerationReport ScratchReport;

		if (!ExportCandidatePlan(
				Request,
				Settings,
				State,
				OpeningMasks,
				Candidate,
				ScratchReport))
		{
			return FWfcCollapsedCandidateEvaluation::Fatal(
				ScratchReport.Message,
				ScratchReport.RelatedStableId);
		}

		if (!ValidateFinalPlan(Request, Settings, State, Candidate, ScratchReport))
		{
			switch (ScratchReport.Failure)
			{
			case EZeroEscapeGenerationFailure::RequiredRouteTooLong:
			case EZeroEscapeGenerationFailure::LongRetraceLimitExceeded:
				if (ScratchReport.ActualValue != INDEX_NONE)
				{
					return FWfcCollapsedCandidateEvaluation::Reject(
						ScratchReport.Message,
						ScratchReport.ActualValue,
						ScratchReport.LimitValue);
				}
				break;
			default:
				break;
			}

			// 非对称边、Required 丢失、Anchor 数量等都属于代码不变量错误。
			return FWfcCollapsedCandidateEvaluation::Fatal(
				ScratchReport.Message,
				ScratchReport.RelatedStableId);
		}

		AcceptedCandidate = MoveTemp(Candidate);
		return FWfcCollapsedCandidateEvaluation::Accept();
	};

TArray<uint8> AcceptedOpeningMasks;
if (!FWfcSolver::Solve(
		Settings.GridSize,
		State.Constraints,
		WfcSettings,
		Variants,
		WfcRandom,
		ValidateCollapsedCandidate,
		AcceptedOpeningMasks,
		OutReport))
{
	return false;
}

OutReport.Metrics.WalkableCellCount = AcceptedCandidate.Cells.Num();
BuildJunctionMetrics(AcceptedCandidate);
AcceptedCandidate.CanonicalLayoutHash =
	FGenerationCore::ComputeCanonicalLayoutHash(AcceptedCandidate);

if (AcceptedCandidate.CanonicalProgressionHash == 0
	|| AcceptedCandidate.CanonicalLayoutHash == 0)
{
	return Fail(
		OutReport,
		EZeroEscapeGenerationStage::GlobalValidation,
		EZeroEscapeGenerationFailure::SolverInvariantViolation,
		TEXT("规范 Progression/Layout Hash 不能为 0。"));
}

OutReport.Stage = EZeroEscapeGenerationStage::None;
OutReport.Failure = EZeroEscapeGenerationFailure::None;
OutReport.RelatedStableId = INDEX_NONE;
OutReport.ActualValue = 0;
OutReport.LimitValue = 0;
OutReport.Message.Reset();
OutReport.Metrics.PlanningMilliseconds =
	(FPlatformTime::Seconds() - StartSeconds) * 1000.0;

// 只有全部求解、玩法验收、指标和 Hash 成功后才一次性提交最终 Plan。
OutPlan = MoveTemp(AcceptedCandidate);
return true;
```

`ValidateFinalPlan` 仍是 Grid 的独立产品不变量入口；只是它的路线超限结果现在能让 WFC 回溯，而不是在第一个完整候选后直接终止整个 Seed。

## 10. Runtime Generator 接线与日志

```cpp
FGridLayoutRequest LayoutRequest;
LayoutRequest.Signature = Signature;
LayoutRequest.Progression = Progression;

FGridLayoutSettings LayoutSettings;
const FZeroEscapeSharedRouteConstraints& Route = Snapshot.SharedRouteConstraints;
LayoutSettings.GridSize = Route.GridSize;
LayoutSettings.LogicalTileSizeCm = FMath::RoundToInt(Route.LogicalTileSizeCm);
LayoutSettings.RoomSizeTiles = Route.RoomSizeTiles;
LayoutSettings.ObjectiveProgressBandCount = Route.ObjectiveProgressBandCount;
LayoutSettings.MinWalkableCellCount = Route.MinWalkableCellCount;
LayoutSettings.MaxWalkableCellCount = Route.MaxWalkableCellCount;
LayoutSettings.MaxConsecutiveStraightTiles = Route.MaxConsecutiveStraightTiles;
LayoutSettings.MaxRequiredRouteLengthTiles = Route.MaxRequiredRouteLengthTiles;
LayoutSettings.MaxRequiredRouteExtraTiles = Route.MaxRequiredRouteExtraTiles;
LayoutSettings.MaxWfcCandidateAttempts = Route.MaxWfcCandidateAttempts;
LayoutSettings.MaxWfcBacktrackCount = Route.MaxWfcBacktrackCount;
LayoutSettings.MaxWfcSolveAttempts = Route.MaxWfcSolveAttempts;
LayoutSettings.GameplayAnchorHeightCm = Route.GameplayAnchorHeightCm;

if (!FGridLayoutSolver::Solve(
		LayoutRequest,
		LayoutSettings,
		ProgressionSettings.WfcShapeWeights,
		Request.Seed,
		CandidatePlan,
		Report))
{
	return false;
}
```

`ZE_PCG_RESULT` 升级为 schema 4，至少记录：

```cpp
TEXT(
	"ZE_PCG_RESULT schema=4 success=%d seed=%d difficulty=%s flow=%s "
	"stage=%s failure=%s walkable=%d solve_attempts=%d observations=%d "
	"candidate_attempts=%d propagations=%d contradictions=%d "
	"contradiction_local=%d contradiction_count=%d contradiction_max_straight=%d "
	"contradiction_connected=%d contradiction_global_ban=%d "
	"backtracks=%d leaf_rejections=%d "
	"instances=%d hism=%d progression_hash=%lld layout_hash=%lld "
	"planning_ms=%.3f total_ms=%.3f message=\"%s\"")
```

Runtime Generator 不增加 PIE 窗口、测试传送、素材路径或灯光逻辑。

## 11. Automation 拟实现

### Solver / Constraint 测试

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FZeroEscapeWfcBacktrackingTest,
	"Demo.PCG.WFC.BacktrackingRestoresAllDomains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FZeroEscapeWfcBacktrackingTest::RunTest(const FString& Parameters)
{
	const FIntPoint GridSize(4, 3);
	TArray<FGridCellConstraint> Constraints =
		MakeRequiredEndpointsWithOptionalInterior(GridSize);

	FZeroEscapeWfcSolveSettings Settings;
	Settings.StartCoordinate = FIntPoint(0, 1);
	Settings.MinWalkableCellCount = 5;
	Settings.MaxWalkableCellCount = 7;
	Settings.MaxConsecutiveStraightTiles = 2;
	Settings.MaxCandidateAttempts = 1000;
	Settings.MaxBacktrackCount = 1000;

	TArray<FTileVariant> Variants = MakeCanonicalVariantArray();
	FRandomStream Random(/* 实施时用测试跑出的固定回溯 Seed */);

	int32 VisitedLeafCount = 0;
	const auto RejectFirstCompleteCandidate =
		[&](const TConstArrayView<uint8>)
		{
			++VisitedLeafCount;
			return VisitedLeafCount == 1
				? FWfcCollapsedCandidateEvaluation::Reject(
					TEXT("测试要求拒绝首个完整候选。"), 1, 0)
				: FWfcCollapsedCandidateEvaluation::Accept();
		};

	TArray<uint8> Output;
	FZeroEscapeGenerationReport Report;
	const bool bSolved = FWfcSolver::Solve(
		GridSize,
		Constraints,
		Settings,
		Variants,
		Random,
		RejectFirstCompleteCandidate,
		Output,
		Report);

	TestTrue(TEXT("拒绝首个完整候选后应继续求解"), bSolved);
	TestTrue(TEXT("夹具必须至少创建一个真实决策帧"),
		Report.Metrics.WfcObservationCount > 0);
	TestTrue(TEXT("必须实际发生回溯"), Report.Metrics.WfcBacktrackCount > 0);
	TestEqual(TEXT("只拒绝一个完整候选"),
		Report.Metrics.WfcCollapsedCandidateRejectionCount, 1);
	TestEqual(TEXT("必须访问被拒绝与被接受两个完整叶子"), VisitedLeafCount, 2);
	return true;
}
```

该夹具使用 2×2 固定绕行 + 一条可选捷径：首个决策关闭捷径后，传播直接把其余格折叠为完整叶子；验收拒绝该叶子，回溯后打开捷径并接受。最终固定断言 `CandidateAttempts==2`，从结果证明叶子是在原决策传播后形成，没有凭空创建第二个决策帧。固定 Seed 不在文档阶段伪造；实现时只搜索一次“关闭优先”的 Seed，随后固化并删除搜索逻辑。

审计要求的根 Trail 边界另用一个最小夹具，不与上面的成功回溯测试混在同一函数：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FZeroEscapeRootTrailSurvivesExhaustionTest,
	"Demo.PCG.WFC.Backtracking.RootTrailSurvivesFirstDecisionExhaustion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FZeroEscapeRootTrailSurvivesExhaustionTest::RunTest(const FString& Parameters)
{
	// 四个 Required 格形成两列；顶部共享边是首个决策。
	// 第五个 Optional 格在根 Count(forced==max) 阶段被强制为 Empty，
	// 这次 Ban 写入 Root Trail，随后 Trail.Reset() 只应丢历史、不能恢复 Domain。
	const FRootTrailFixture Fixture = MakeRootTrailFixture();
	int32 CompleteLeafCount = 0;
	const auto RejectOnlyConnectedLeaf =
		[&](const TConstArrayView<uint8> Masks)
		{
			++CompleteLeafCount;
			TestEqual(TEXT("根 Count Ban 必须跨所有分支保留"),
				Masks[Fixture.RootBannedOptionalIndex], uint8(0));
			return FWfcCollapsedCandidateEvaluation::Reject(
				TEXT("测试要求耗尽唯一连通完整叶子。"), 1, 0);
		};

	TArray<uint8> Output;
	FZeroEscapeGenerationReport Report;
	const bool bSolved = RunRootTrailFixture(
		Fixture, RejectOnlyConnectedLeaf, Output, Report);

	TestFalse(TEXT("全部首决策候选耗尽后应明确无解"), bSolved);
	TestEqual(TEXT("失败分类必须是 NoValidWfcSolution"),
		Report.Failure, EZeroEscapeGenerationFailure::NoValidWfcSolution);
	TestEqual(TEXT("失败不得泄漏半成品"), Output.Num(), 0);
	TestEqual(TEXT("只有打开桥的分支能到达完整验收"), CompleteLeafCount, 1);
	TestTrue(TEXT("必须真实恢复首个决策帧"),
		Report.Metrics.WfcBacktrackCount > 0);
	return true;
}
```

再保留一个三格或单格极小边界：若根固定点已经是唯一完整叶子、`Decisions` 从未创建且 validator 返回 Reject，正确结果是 `NoValidWfcSolution`、`WfcBacktrackCount==0`。它没有可替代分支，不应错误期待“无决策帧仍能找到第二个解”。

仅“人为拒绝首个叶节点”还不足以证明局部传播和全局 Ban 都被 Trail 恢复。还要增加一个固定夹具：被拒绝分支必须实际触发 Count 或 MaxConsecutive Ban；随后把该分支的首个决策候选在输入中预先排除，做一次不需要回溯的干净求解。两次最终输出必须完全一致：

```cpp
const FSolverRun BacktrackedRun = RunFixture(
	FixtureSeed,
	/* bRejectKnownFirstLeaf = */ true,
	/* bPreExcludeKnownFirstDecision = */ false);

const FSolverRun CleanRun = RunFixture(
	FixtureSeed,
	/* bRejectKnownFirstLeaf = */ false,
	/* bPreExcludeKnownFirstDecision = */ true);

TestTrue(TEXT("夹具的已知失败前缀必须让 Count 或 Max 输出 Ban"),
	FixtureRejectedBranchProducesGlobalReduction());
TestTrue(TEXT("回溯夹具必须恢复至少一个决策帧"),
	BacktrackedRun.Report.Metrics.WfcBacktrackCount > 0);
TestTrue(TEXT("回溯恢复后的输出必须等于排除失败分支后的干净求解"),
	BacktrackedRun.OpeningMasks == CleanRun.OpeningMasks);
```

`FSolverRun / RunFixture / FixtureRejectedBranchProducesGlobalReduction` 只属于测试 CPP；其中后者直接调用纯值约束检查已知 Domain 快照，不为它们增加 Runtime API。若无法用纯黑盒夹具稳定证明恢复结果，停止并评审最小的 `WITH_DEV_AUTOMATION_TESTS` 观测点，不能为了测试暴露生产可变状态。

还需独立覆盖：

- Count 到上下限产生 Ban；上下界不可能时 contradiction；
- MaxConsecutive 对 Straight、T、Cross 都计数；
- Connected 在可能图断开 Required/被迫非空 Cell 时 contradiction；
- 局部传播、Count Ban、Max Ban 都能随 Trail 完整恢复；
- 候选耗尽为 `NoValidWfcSolution`；两个预算耗尽均为 `SolverBudgetExhausted`；
- 完整候选路线过长或额外折返超限后能回溯成功；
- 同输入/Seed 的 Layout Hash、Attempts、Backtracks、Contradictions、Rejections 一致；
- 16 OpeningMask 的边镜像不变量在单测中完整扫描；
- 288 组 Difficulty × Flow × Seed Sweep 统计成功率、P50/P95/Max 耗时和搜索指标；
- 三个难度的 Walkable 分布不得因 Hard 系统性靠近上限而显著拉长单局；
- 项目资产烟测保留素材迁移后的 `/Game/Assets/SciFiHydroLab/...` 路径。

### Pipeline 测试保留范围

- Profile / K-of-N / Progression / Random Domain 隔离；
- Grid 地标、2×2 房内开口、最终连通、路线长度、结构展开；
- Canonical Hash / Signature；
- Runtime Generator Blueprint CDO 与真实 DataAsset 烟测。

Harness 不迁入上述纯算法测试，也不增加职责；它继续只服务 `L_PCG_RuntimeTest` 的生成、Staging 和玩家传送，等正式 GameFlow 接管后按计划退役。

## 12. 实施后的参数判断

1. `48..72` 与 `MaxConsecutiveStraightTiles=4` 是首轮灰盒候选，不是最终产品参数。
2. `MaxWfcCandidateAttempts=100000`、`MaxWfcBacktrackCount=25000` 是全部有限尝试共享的整局硬上限；288 组实测 Max 为 17193/15009。`MaxWfcSolveAttempts=10`，实测 Max=7。
3. `MaxConsecutiveStraightTiles` 统计的是“同时拥有轴两侧开口的内部贯通格”。一段视觉直线两端若是 Corner/DeadEnd，视觉长度可能比该值多最多两个格；PIE 若仍觉得过长，先调低参数，再决定是否要改为按共享开放边计数。
4. Connected 使用五节点展开可能图与迭代 Tarjan 关节点传播；仍未实现增量 Tracker。
5. 完成态路线验收可能造成晚期回溯，因此单独记录 `WfcCollapsedCandidateRejectionCount`。只有数据证明它是瓶颈，才评审部分状态路线下界；本轮不提前加。
6. 首轮三个难度可先复制同一套权重，先证明求解稳定；之后再在保持 Empty/非空总权重不变的前提下调 Corner/T/Cross 比例。

## 13. 实施顺序（取得明确授权后）

1. 等素材迁移对话交接，重新读取 Core、Tests、Presentation 当前状态。
2. 原子切换 Types / Assets / Core / Grid/WFC 函数签名并让工程重新编译。
3. 实现约束与 Trail，先跑小 Grid 单测。
4. 接入完成态候选验收，证明路线超限能回溯而不是直接失败。
5. 删除固定主干、Gate、Envelope、剪枝和旧构造性校验。
6. 拆分测试，运行完整构建、`Demo.PCG` 和 288 Seed Sweep。
7. 数据确认后才冻结预算并调整难度形态权重。
8. 最后单独做室内灯增量与 `SelectedViewport` 玩家验收。
