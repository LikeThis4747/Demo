# PCG Grid-WFC V3.2 拟实现代码（评审稿）

> 状态：仅供用户与独立 AI 评审，尚未写入 `Source/Demo`，不代表实现授权。
>
> 本文件取代 V3.1 评审稿；V3.1 中的 WFC 缩区重试与骨架降级结论已撤回。
>
> 方案依据：600 cm WFC 逻辑 Tile；每个 Tile 固定展开为 2×2 个 300 cm SciFiHydroLab 表现单元；删除旧特殊 Socket/Portal/Cap 放置链。本版继续删除当前规则下没有实际职责的 A*、WFC 回溯/重试/降级和逻辑 Tile Catalog DataAsset。

## 1. V3.2 再复审后的关键修正

1. Straight、Corner、T、Cross 是 `OpeningMask` 的可选结果，不是每局配额。
2. 不增加 `MinTJunctionCount`、`MinCrossCount`、`bRequireCross` 等字段。
3. 删除当前精确的 `ShortLeafBranchCount` / `ForwardRejoinBranchCount`；Grid 难度只保留 `MaxOptionalSideBranches` / `MaxOptionalForwardLinks` 上限，玩法必需地标全部进入必达骨架。
4. Required 地标从预先验证容量的进度槽中选择；Seed 只在合法候选里改变 Y、转折顺序和可选连接，不使用随机摆放重试。
5. 首版无障碍、无不同代价，删除 A*；用必然成功的 X-first / Y-first 正交路径把相邻地标写成双向 `RequiredOpenMask`。
6. 逻辑 Variant 由代码完整生成 `OpeningMask 0..15`。在 RequiredOpen 对称、边界关闭、Required 非零且开闭不冲突时，CSP 有构造性解，因此删除 WFC Snapshot、回溯、重试和骨架降级。
7. WFC 仍完整实现最小熵选择、权重观察与邻接传播；“无回溯”不是取消 WFC，而是利用当前完整状态集收敛算法。
8. `OpeningMask == 0` 唯一表示 Empty；非零 Mask 都表示 Walkable，不再重复保存 `bWalkable`、StableTileId 或 StableVariantId。
9. 首版只有二元 Open/Closed，不实现尚无需求的宽度类型、门类型、HeightLayer 或通用 EdgeLabel。

## 2. 拟修改文件

| 文件 | 拟处理 |
|---|---|
| `Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h` | 用 Grid Cell/OpeningMask Plan 替换 Module/Portal/Closure 输出 |
| `Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h` | Generation Profile 内的网格尺寸/六类 WFC 权重、五类直接表现绑定；删除 Module Catalog、Socket/Actor 预留 |
| `Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp` | 删除 Portal/Cap/Bounds Overhang 校验，改为固定 16 Mask、形态权重和 600/300 跨资产尺度校验 |
| `Source/Demo/Private/PCG/ZeroEscapeGenerationCore.h/.cpp` | 保留 Flow/K-of-N，抽象图收敛为地标意图；删除 Socket/A* 随机域和 Portal Hash；分支精确数量改为上限 |
| `Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.h/.cpp` | 新增；负责进度带/Lane Region、正交路径雕刻、可选区和全局不变量验证 |
| `Source/Demo/Private/PCG/ZeroEscapeWfcSolver.h/.cpp` | 新增；实现固定 16 Mask 的最小熵、权重观察和邻接传播，不含回溯 |
| `Source/Demo/Private/PCG/ZeroEscapeLayoutSolver.h/.cpp` | 新求解器接管并验证后删除；不保留 façade 或兼容转发 |
| `Source/Demo/Public/PCG/ZeroEscapeRuntimeLevelGenerator.h` | 删除 `ModuleCatalog` 配置入口；保留 Profile、Presentation、生成查询和事务状态 |
| `Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp` | 从 Cells 派生 Floor/Ceiling/Wall/Trim/Pillar HISM，保留事务提交与回滚 |
| `Source/Demo/Private/PCG/ZeroEscapeGenerationTests.cpp` | 删除 Socket/Closure/回溯降级测试，增加 16 Mask 完整性、不变量、无需回溯和表面去重测试 |

继续使用现有 `Public/PCG`、`Private/PCG`，不新增碎片子目录。结构展开首版留在 Runtime Generator 内；只有该职责后续明显膨胀时才拆出 Assembler 文件。

## 3. `ZeroEscapeGenerationTypes.h` 拟实现

### 3.1 四向开口

```cpp
/**
 * WFC Tile 的四向逻辑开口。
 *
 * 这是纯整数邻接数据，不对应 UE Static Mesh Socket，也不携带世界坐标。
 * 位序固定为 N/E/S/W，进入生成 Hash 后不得随意调整。
 */
UENUM(BlueprintType, meta = (Bitflags))
enum class EZeroEscapeOpenEdge : uint8
{
	None  = 0,
	North = 1 << 0,
	East  = 1 << 1,
	South = 1 << 2,
	West  = 1 << 3
};
ENUM_CLASS_FLAGS(EZeroEscapeOpenEdge);

namespace ZeroEscape::Grid
{
	inline constexpr uint8 AllOpenEdges = 0x0F;

	/** 返回 Direction 的反向位；输入必须是 0..3 的单个方向索引。 */
	inline uint8 OppositeDirectionIndex(const uint8 Direction)
	{
		check(Direction < 4);
		return static_cast<uint8>((Direction + 2u) & 3u);
	}
}
```

### 3.2 Region 与最终 Tile

```cpp
/** Cell 的流程语义只负责约束布局和派生玩法 Anchor，不绑定任何具体 Mesh。 */
UENUM(BlueprintType)
enum class EZeroEscapeGridRegionKind : uint8
{
	Corridor = 0,
	Room = 1,
	Start = 2,
	Exit = 3,
	Objective = 4
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeCollapsedTile
{
	GENERATED_BODY()

	/** 最终 Plan 内按 (Y, X) 排序后分配的稳定实例 Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 StableTileInstanceId = INDEX_NONE;

	/** 600 cm WFC 逻辑格坐标；首版固定单层，因此不保存 Z。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint Coordinate = FIntPoint::ZeroValue;

	/**
	 * N/E/S/W 四位，同时也是逻辑 Variant 的稳定身份。
	 * 0 唯一表示 Empty，1..15 表示代码生成的完整 Walkable 状态集。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid",
		meta = (Bitmask, BitmaskEnum = "/Script/Demo.EZeroEscapeOpenEdge"))
	uint8 OpeningMask = 0;

	/** 连续走廊段或房间的稳定 Region Id；Empty 为 INDEX_NONE。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 RegionId = INDEX_NONE;

	/** 只影响玩法绑定和房间外墙约束，不影响具体素材选择。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	EZeroEscapeGridRegionKind RegionKind = EZeroEscapeGridRegionKind::Corridor;
};
```

### 3.3 Plan 删除旧 Portal 输出

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeLandmarkGridBinding
{
	GENERATED_BODY()

	/** Progression Intent 中地标的稳定 Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 StableLandmarkId = INDEX_NONE;

	/** Node 对应 Region 的中心 Tile；多格房间不伪装成单个 Module Placement。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint AnchorCoordinate = FIntPoint::ZeroValue;

	/** 承载该地标的稳定 Region Id。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 RegionId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedLevelPlan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FZeroEscapeGenerationSignature Signature;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int64 CanonicalProgressionHash = 0;

	/** Hash 只覆盖逻辑 Cells、地标和 Anchor，不包含 HydroLab Mesh 或 Pivot。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	int64 CanonicalLayoutHash = 0;

	/** 包含候选区内的 Empty 与 Walkable Tile，按 (Y, X) 稳定排序。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeCollapsedTile> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeLandmarkGridBinding> LandmarkBindings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeGeneratedAnchor> GameplayAnchors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	TArray<FZeroEscapeObjectiveBinding> ObjectiveBindings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint StartCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
	FIntPoint ExitCoordinate = FIntPoint::ZeroValue;
};
```

`FZeroEscapeGeneratedAnchor` 和 `FZeroEscapeObjectiveBinding` 同步删除 `StablePlacementId` / `StableModuleAnchorId`，改为 `GridCoordinate`、`RegionId` 与已解析的 Generator Local Transform。

### 3.4 Metrics 只观察，不强制路口

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeJunctionMetrics
{
	GENERATED_BODY()

	/** 以下数量只用于 Seed 分布和权重调参；任何一项为零都不代表生成失败。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 DeadEndCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 StraightCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 CornerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 TJunctionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 CrossCount = 0;

	/** WFC 后被移除的、与 Start 不连通的纯可选 Tile 数量。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 PrunedOptionalTileCount = 0;
};
```

## 4. `ZeroEscapeGenerationAssets.h` 拟实现

### 4.1 代码生成 16 个 Mask，DataAsset 只保留形态权重

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeWfcShapeWeights
{
	GENERATED_BODY()

	/** 权重必须为正；0..15 的任何 Mask 都必须保持可选，才能维持必然有解契约。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 Empty = 150;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 DeadEnd = 15;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 Straight = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 Corner = 80;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 TJunction = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC", meta = (ClampMin = "1"))
	int32 Cross = 5;

	/** 根据 Mask 的位数与相对方向返回形态权重；不改变 Mask 是否存在。 */
	int32 GetWeightForMask(uint8 OpeningMask) const;
};
```

在现有 `UZeroEscapeGenerationProfile` 中新增：

```cpp
/** 逻辑 Tile 的边长；V3.2 首版必须为 600 cm。 */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid",
	meta = (ClampMin = "600", ClampMax = "600", Units = "cm"))
int32 LogicalTileSizeCm = 600;

/** 只调整合法 Mask 间的概率，不删除 Variant，也不是路口配额。 */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
FZeroEscapeWfcShapeWeights WfcShapeWeights;
```

第一轮权重是“同类每个具体 Mask”的相对权重，只是调参起点，不是验收值：

| 形态 | 建议初值 | 说明 |
|---|---:|---|
| Empty | 150 | 让骨架外围候选区保持稀疏 |
| DeadEnd | 15 | 允许短支路，但避免大量无意义死路 |
| Straight | 100 | 常见基础形态 |
| Corner | 80 | 允许自然转向 |
| T | 25 | 低频分岔或汇合 |
| Cross | 5 | 稀有但合法 |

例如四种朝向的 Corner 各自取得 `Corner` 权重，而不是四者共享一次权重；这条规则简单、确定，实际分布由批量 Seed 指标再调。权重只在当前 Domain 的合法候选间生效。`FWfcSolver::BuildCanonicalVariants` 固定遍历 `Mask=0..15`，直接生成 16 个 Variant；`OpeningMask` 同时是稳定 Variant 身份，不再需要 Tile Id、旋转展开、Catalog 快照或 Catalog DataAsset。这样可满足性来自代码不变量，而不是依赖人工是否漏配某个 Tile。

### 4.2 直接表现绑定

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeStructureMeshBinding
{
	GENERATED_BODY()

	/** 当前结构部件使用的 Static Mesh；Trim/Pillar 可为空，Floor/Wall/Ceiling 必填。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	/**
	 * Asset Local -> 规范 300 cm 结构单元的固定校正。
	 * 只允许有限平移/旋转与 Unit Scale，不参与 WFC 或逻辑 Hash。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FTransform PivotCorrection = FTransform::Identity;

	/** HISM 碰撞配置；为空或无法解析时配置校验失败。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FName CollisionProfileName = TEXT("BlockAll");

	/** Floor/Wall 默认影响导航；Ceiling/Trim/Pillar 可按实际碰撞配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	bool bCanEverAffectNavigation = true;
};

UCLASS(BlueprintType)
class DEMO_API UZeroEscapePresentationProfile final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** HydroLab 基础结构单元，首版必须为 300 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1", Units = "cm"))
	int32 StructureUnitSizeCm = 300;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FZeroEscapeStructureMeshBinding Floor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FZeroEscapeStructureMeshBinding Ceiling;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FZeroEscapeStructureMeshBinding Wall;

	/** 可为空；当前 HydroLab 使用 WallTrimG 防止墙顶漏光。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FZeroEscapeStructureMeshBinding WallTopTrim;

	/** 可为空；当前 HydroLab 使用 PillarC 覆盖墙角和端头。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure")
	FZeroEscapeStructureMeshBinding Pillar;

	/** 校验必填 Mesh、Pivot、碰撞与 LogicalTileSize/300 的固定 2:1 契约。 */
	bool IsConfigured(int32 LogicalTileSizeCm, FString& OutError) const;
};
```

首版删除当前未启用的 `Actor SpawnPolicy`、`ActorClass`、`ActorAssetLocalBounds` 和 Bounds Overhang 兼容字段。

### 4.3 难度中的分支不再是精确配额

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeDifficultyDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	/**
	 * 允许生成的可选短支路上限。实际数量由 Seed 从预验证的合法槽中决定；
	 * 0..Max 都是合法结果，不借此强制 T 字路口。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0"))
	int32 MaxOptionalSideBranches = 2;

	/** 可选前向汇合上限；实际数量可以为零，不等于 Cross/T 配额。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0"))
	int32 MaxOptionalForwardLinks = 1;

	/** K-of-N 的候选目标 N；全部生成且可达，K 只决定完成条件。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "12"))
	int32 ObjectiveCandidateCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "12"))
	int32 RequiredObjectiveCount = 0;
};
```

`BuildProgressionIntent` 只按 Flow/Difficulty 解析 CompletionRule、Start/Exit 与 Objective Candidate。`MaxOptionalSideBranches` / `MaxOptionalForwardLinks` 由 Grid 层使用独立确定性随机域从 `0..Max` 选择本局空间丰富度；Hard 配置更高上限会提高期望复杂度，但不会把某种路口写成固定数量，折返硬上限仍由三档共享约束控制。

### 4.4 Progression Intent 只保留流程真正需要的数据

```cpp
struct FResolvedProgressionSettings
{
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;
	FName StableFlowId = NAME_None;
	int32 FlowVersion = 0;
	EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
	int32 ObjectiveCandidateCount = 0;
	int32 RequiredObjectiveCount = 0;
};

enum class EProgressionLandmarkKind : uint8
{
	Start,
	Objective,
	Exit
};

struct FProgressionLandmark
{
	int32 StableLandmarkId = INDEX_NONE;
	EProgressionLandmarkKind Kind = EProgressionLandmarkKind::Objective;

	/** 推进带索引；多个目标可共享同一带的不同 Lane，不保存世界坐标。 */
	int32 ProgressBandIndex = 0;
};

struct FProgressionIntent
{
	EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;

	/** N 直接由 Objective Landmark 数量得到；这里只保存通关需要的 K。 */
	int32 RequiredObjectiveCount = 0;

	TArray<FProgressionLandmark> Landmarks;
};
```

这里不再输出抽象 Link/Edge。EscapeOnly 只需 Start/Exit；Collect/K-of-N 增加 Objective Candidate。Grid 层先雕刻 Start→Exit 主骨架，再把每个 Objective 接到附近已连通骨架；有合法空间时可再接到更晚的骨架位置，没有就保持受长度限制的短支路。因此“死路房还是前向重连房”仍会变化，但它是空间生成结果，不被 Flow 写死。

确定性随机域最终只保留 `Landmark`、`OptionalLayout`、`WfcLayout` 与 `Presentation`；删除旧 `Topology`、`ObjectivePlacement`、`SocketLayout` 和 Attempt 重试语义。各域仍用显式常量并进入算法版本契约。

## 5. `ZeroEscapeWfcSolver.h` 拟实现

```cpp
namespace ZeroEscape::LevelGeneration
{
	enum class EGridCellDomain : uint8
	{
		/** 求解区域之外，只允许 Empty。 */
		Outside,
		/** 必要路线、房间或玩法 Anchor；RequiredOpenMask 必须非零。 */
		Required,
		/** 骨架附近有限候选区，允许 Empty 或非空 Tile。 */
		Optional
	};

	struct FGridCellConstraint
	{
		FIntPoint Coordinate = FIntPoint::ZeroValue;
		EGridCellDomain Domain = EGridCellDomain::Outside;

		/** Variant 必须包含的开口；正交路线和房间内部只写这一项。 */
		uint8 RequiredOpenMask = 0;

		/** Variant 禁止包含的开口；只用于地图边界、房间外墙和明确禁区。 */
		uint8 RequiredClosedMask = 0;

		int32 RegionId = INDEX_NONE;
		EZeroEscapeGridRegionKind RegionKind = EZeroEscapeGridRegionKind::Corridor;
	};

	struct FTileVariant
	{
		/** 0..15，同时是稳定 Variant 身份。 */
		uint8 OpeningMask = 0;
		int32 Weight = 1;
	};

	/** 固定 16 Mask、最小熵观察、权重选择与开闭边传播的纯值 WFC 入口。 */
	class FWfcSolver final
	{
	public:
		/** 无资产输入，始终按 0..15 顺序构建完整 Variant 集。 */
		static void BuildCanonicalVariants(
			const FZeroEscapeWfcShapeWeights& Weights,
			TStaticArray<FTileVariant, 16>& OutVariants);

		static bool Solve(
			const TArray<FGridCellConstraint>& Constraints,
			const TStaticArray<FTileVariant, 16>& Variants,
			FRandomStream& Random,
			TArray<uint8>& OutOpeningMaskByCell,
			FZeroEscapeGenerationReport& OutReport);
	};
}
```

### 5.1 Domain 过滤

```cpp
/** 判断 Variant 是否满足单格硬约束；函数不查看邻格，邻接由传播表负责。 */
bool ConstraintAllowsVariant(
	const FGridCellConstraint& Constraint,
	const FTileVariant& Variant)
{
	const uint8 Mask = Variant.OpeningMask;
	const bool bIsEmpty = Mask == 0;

	if (Constraint.Domain == EGridCellDomain::Outside)
	{
		return bIsEmpty;
	}
	if (Constraint.Domain == EGridCellDomain::Required && bIsEmpty)
	{
		return false;
	}
	if ((Mask & Constraint.RequiredOpenMask) != Constraint.RequiredOpenMask)
	{
		return false;
	}
	if ((Mask & Constraint.RequiredClosedMask) != 0)
	{
		return false;
	}
	return true;
}
```

### 5.2 邻接兼容

```cpp
/**
 * 相邻 Tile 只有一种兼容规则：两侧对应开口同时开放或同时关闭。
 * 首版不引入 Type、Width、Height 或浮点容差。
 */
bool AreVariantsCompatible(
	const FTileVariant& A,
	const uint8 DirectionFromAToB,
	const FTileVariant& B)
{
	const uint8 ABit = static_cast<uint8>(1u << DirectionFromAToB);
	const uint8 Opposite = ZeroEscape::Grid::OppositeDirectionIndex(DirectionFromAToB);
	const uint8 BBit = static_cast<uint8>(1u << Opposite);
	return (A.OpeningMask & ABit) != 0
		? (B.OpeningMask & BBit) != 0
		: (B.OpeningMask & BBit) == 0;
}
```

### 5.3 可满足性不变量

```cpp
/**
 * WFC 开始前验证能证明本轮必然有解的契约。
 * 返回 false 表示 Profile、地标/路线构造或代码错误；不是可重试的 Seed 失败。
 */
bool ValidateGuaranteedSolvableConstraints(
	const FIntPoint GridSize,
	const TArray<FGridCellConstraint>& Constraints,
	FString& OutError)
{
	for (const FGridCellConstraint& Cell : Constraints)
	{
		if ((Cell.RequiredOpenMask & Cell.RequiredClosedMask) != 0)
		{
			return Fail(Cell, TEXT("同一方向同时 RequiredOpen 与 RequiredClosed"), OutError);
		}

		if (Cell.Domain == EGridCellDomain::Required && Cell.RequiredOpenMask == 0)
		{
			return Fail(Cell, TEXT("Required Cell 没有接入必要骨架"), OutError);
		}

		for (uint8 Direction = 0; Direction < 4; ++Direction)
		{
			const FIntPoint Neighbor = Step(Cell.Coordinate, Direction);
			const bool bOpen = HasBit(Cell.RequiredOpenMask, Direction);
			if (bOpen && !IsInsideGrid(Neighbor, GridSize))
			{
				return Fail(Cell, TEXT("RequiredOpen 指向网格外"), OutError);
			}
			if (bOpen && !NeighborRequiresOppositeOpen(Cell, Direction, Constraints))
			{
				return Fail(Cell, TEXT("RequiredOpen 未双向写入"), OutError);
			}
		}
	}
	return true;
}
```

可构造见证解：每条公共边只要任一侧 `RequiredOpen` 就开放，否则关闭；每个 Cell 取四条公共边拼出的 Mask。由于 0..15 全部存在，Optional 会自动响应 Required 邻格的反向开口，Required 又保证至少一个必开边，所以所有 Cell 都有合法 Variant。

### 5.4 无回溯 WFC 核心

```cpp
bool FWfcSolver::Solve(
	const TArray<FGridCellConstraint>& Constraints,
	const TStaticArray<FTileVariant, 16>& Variants,
	FRandomStream& Random,
	TArray<uint8>& OutOpeningMaskByCell,
	FZeroEscapeGenerationReport& OutReport)
{
	TArray<uint16> Domains;
	InitializeDomainsFromUnaryConstraints(Constraints, Variants, Domains);
	if (!PropagateOpenClosedEdges(Constraints, Domains))
	{
		return ReportInvariantFailure(TEXT("初始约束传播产生空 Domain"), OutReport);
	}

	for (;;)
	{
		const int32 CellIndex = ChooseMinimumEntropyCell(Domains, Variants);
		if (CellIndex == INDEX_NONE)
		{
			break;
		}

		const uint8 ChosenMask = ChooseWeightedMask(Domains[CellIndex], Variants, Random);
		Domains[CellIndex] = static_cast<uint16>(1u << ChosenMask);

		// 完整 0..15 状态集保证任一已传播为合法的观察都可继续扩展。
		if (!PropagateOpenClosedEdgesFrom(CellIndex, Constraints, Domains))
		{
			return ReportInvariantFailure(TEXT("观察后产生空 Domain"), OutReport);
		}
	}

	return ExportSingleMasks(Domains, OutOpeningMaskByCell, OutReport);
}
```

保留 `ChooseMinimumEntropyCell`、`ChooseWeightedMask`、Domain 删除和传播队列；删除 `ConnectorSignature`、Support Count 快照、Decision Frame、`RestoreDecisionAndRebuild`、Backtrack/Retry/Fallback 以及对应失败分类。首版最多 16 个 Variant，邻接只有单比特相等关系，直接用 `uint16 Domain` 比通用 Support-count + Snapshot 更清楚。

## 6. `ZeroEscapeGridLayoutSolver.h/.cpp` 拟实现

### 6.1 入口

```cpp
class FGridLayoutSolver final
{
public:
	/**
	 * 从已验证容量的进度槽构建必达骨架，再由无回溯 WFC 丰富局部路网。
	 * 正常 Seed 没有重试分支；false 只表示配置、素材或代码不变量错误。
	 */
	static bool Solve(
		const FGridLayoutRequest& Request,
		const FGridLayoutSettings& Settings,
		const FZeroEscapeWfcShapeWeights& Weights,
		int32 MasterSeed,
		FZeroEscapeGeneratedLevelPlan& OutPlan,
		FZeroEscapeGenerationReport& OutReport);

private:
	static bool EmbedRequiredLandmarksInProgressSlots(...);
	static bool CarveMainSpineAndAttachObjectives(...);
	static bool BuildOptionalWfcEnvelope(...);
	static bool PruneDisconnectedOptionalTiles(...);
	static bool ValidateGridPlan(...);
};
```

### 6.2 对称写入必要开口

```cpp
/**
 * 把两个四邻域 Cell 之间的边写成双向 RequiredOpen。
 * 不关闭其他方向，让 WFC 仍能在骨架上形成可选 T 或 Cross。
 * 调用前必须保证 A/B 在网格内；冲突时直接报告不变量错误，不换路线重试。
 */
bool AddRequiredOpening(
	const FIntPoint A,
	const FIntPoint B,
	TMap<FIntPoint, FGridCellConstraint>& InOutConstraints)
{
	const FIntPoint Delta = B - A;
	const int32 Direction = DirectionIndexFromDelta(Delta);
	if (Direction == INDEX_NONE)
	{
		return false;
	}

	FGridCellConstraint& CellA = InOutConstraints.FindChecked(A);
	FGridCellConstraint& CellB = InOutConstraints.FindChecked(B);
	const uint8 ABit = static_cast<uint8>(1u << Direction);
	const uint8 BBit = static_cast<uint8>(
		1u << ZeroEscape::Grid::OppositeDirectionIndex(Direction));
	if ((CellA.RequiredClosedMask & ABit) != 0
		|| (CellB.RequiredClosedMask & BBit) != 0)
	{
		return false;
	}
	CellA.Domain = EGridCellDomain::Required;
	CellB.Domain = EGridCellDomain::Required;
	CellA.RequiredOpenMask |= ABit;
	CellB.RequiredOpenMask |= BBit;
	return true;
}
```

### 6.3 确定性正交路径

```cpp
/**
 * 在无障碍矩形网格内连接两个合法 Gate。
 * Seed 只选择先走 X 还是先走 Y；两种顺序都必然到达，不需要 Open Set 或路线尝试预算。
 */
bool CarveOrthogonalRoute(
	const FIntPoint From,
	const FIntPoint To,
	const bool bHorizontalFirst,
	TMap<FIntPoint, FGridCellConstraint>& InOutConstraints)
{
	if (!IsInsideGrid(From) || !IsInsideGrid(To))
	{
		return false;
	}

	FIntPoint Current = From;
	auto StepAxis = [&](const bool bHorizontal)
	{
		while ((bHorizontal ? Current.X : Current.Y)
			!= (bHorizontal ? To.X : To.Y))
		{
			FIntPoint Next = Current;
			int32& Axis = bHorizontal ? Next.X : Next.Y;
			const int32 Target = bHorizontal ? To.X : To.Y;
			Axis += Target > Axis ? 1 : -1;
			if (!AddRequiredOpening(Current, Next, InOutConstraints))
			{
				return false;
			}
			Current = Next;
		}
		return true;
	};

	return bHorizontalFirst
		? StepAxis(true) && StepAxis(false)
		: StepAxis(false) && StepAxis(true);
}
```

地标放置不做 rejection sampling。Profile 保存固定 Grid 大小、房间尺寸、推进带/Lane 槽位和安全边距；`IsConfigured` 在生成前证明它能容纳 Start、Exit 与所有难度允许的全部 Objective Candidate。Start 固定在首带、Exit 固定在末带；多个 Objective 可以共享中间推进带的不同 Lane，因此 Hard 增加目标数量不必扩大地图或显著拉长关键路线。Seed 只在已经合法的空槽、Gate 与转折顺序中选择。房间 Gate 与路线先确定，随后才关闭其余房间外围边，避免人为制造 Open/Closed 冲突。

### 6.4 单次主流程

```cpp
bool FGridLayoutSolver::Solve(
	const FGridLayoutRequest& Request,
	const FGridLayoutSettings& Settings,
	const FZeroEscapeWfcShapeWeights& Weights,
	const int32 MasterSeed,
	FZeroEscapeGeneratedLevelPlan& OutPlan,
	FZeroEscapeGenerationReport& OutReport)
{
	OutPlan = {};

	FGridWorkingState State;
	if (!EmbedRequiredLandmarksInProgressSlots(Request, Settings, MasterSeed, State, OutReport)
		|| !CarveMainSpineAndAttachObjectives(Request, MasterSeed, State, OutReport)
		|| !BuildOptionalWfcEnvelope(Request, State, OutReport))
	{
		return false;
	}

	FString ConstraintError;
	if (!ValidateGuaranteedSolvableConstraints(
			Settings.GridSize, State.Constraints, ConstraintError))
	{
		return ReportInvariantFailure(ConstraintError, OutReport);
	}

	TStaticArray<FTileVariant, 16> Variants;
	FWfcSolver::BuildCanonicalVariants(Weights, Variants);
	FRandomStream WfcRandom = FGenerationCore::MakeRandomStream(
		MasterSeed, GAlgorithmVersion, ERandomDomain::WfcLayout, 0);

	TArray<uint8> OpeningMasks;
	if (!FWfcSolver::Solve(
			State.Constraints, Variants, WfcRandom, OpeningMasks, OutReport))
	{
		return false;
	}

	FZeroEscapeGeneratedLevelPlan Candidate;
	ExportPlan(State, OpeningMasks, Candidate);

	if (!PruneDisconnectedOptionalTiles(State, Candidate, OutReport)
		|| !ValidateGridPlan(Request, Candidate, OutReport))
	{
		return false;
	}

	Candidate.CanonicalLayoutHash = ComputeCanonicalLayoutHash(Candidate);
	if (Candidate.CanonicalLayoutHash == 0)
	{
		return false;
	}
	OutPlan = MoveTemp(Candidate);
	return true;
}
```

这里没有 `LayoutAttempt`、`MaxWfcAttempts` 或备用骨架。Grid 面积在 Profile 校验时受固定上限约束，16 个 Variant 的初始化、传播、导出与验证都有明确的 `O(GridArea)` 上界；达到不可能的空 Domain 或非法坐标时立即报错，不能换 Seed 掩盖。

### 6.5 路口分类只记 Metrics

```cpp
EJunctionShape ClassifyOpeningMask(const uint8 Mask)
{
	const int32 Count = FMath::CountBits(static_cast<uint64>(Mask & 0x0F));
	if (Count <= 0) return EJunctionShape::Empty;
	if (Count == 1) return EJunctionShape::DeadEnd;
	if (Count == 3) return EJunctionShape::T;
	if (Count == 4) return EJunctionShape::Cross;

	const bool bNorthSouth = (Mask & 0x05) == 0x05;
	const bool bEastWest = (Mask & 0x0A) == 0x0A;
	return bNorthSouth || bEastWest
		? EJunctionShape::Straight
		: EJunctionShape::Corner;
}
```

`ValidateGridPlan` 不检查上述数量下限。它只验证：

1. 所有相邻边开闭对称；
2. Start、Exit 和玩法必需 Objective 可达；
3. Prune 后所有非 Empty Tile 均从 Start 可达；
4. 关键路线和必要折返不超过 Profile 上限；
5. Start、Exit 与全部 Landmark Binding 均落在同一个 Required 连通分量；
6. 每个 Required Cell 的必开 Mask 非零、双向且不指向边界外；
7. Cell 数量和固定 Grid 尺寸不越过项目硬上限。

## 7. Runtime 表现展开拟实现

### 7.1 纯结构实例描述

```cpp
enum class EStructurePieceKind : uint8
{
	Floor,
	Ceiling,
	Wall,
	WallTopTrim,
	Pillar
};

struct FStructureInstance
{
	EStructurePieceKind Kind = EStructurePieceKind::Floor;
	FTransform CanonicalLocalTransform = FTransform::Identity;
};
```

### 7.2 600 Tile 展开为 300 单元

```cpp
/**
 * 从已经验证的逻辑 Cell 构建结构实例。
 * 同一 300 cm 墙段使用规范 Edge Key 去重；Pillar 只在墙终点、转角或多边交汇处产生。
 */
bool BuildStructureInstances(
	const FZeroEscapeGeneratedLevelPlan& Plan,
	const int32 LogicalTileSizeCm,
	const int32 StructureUnitSizeCm,
	TArray<FStructureInstance>& OutInstances,
	FString& OutError)
{
	OutInstances.Reset();
	if (LogicalTileSizeCm != 600 || StructureUnitSizeCm != 300)
	{
		OutError = TEXT("V3.2 首版只接受 600 cm Tile 固定展开为 2x2 个 300 cm 单元。");
		return false;
	}

	TSet<FIntPoint> WalkableTiles;
	for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
	{
		if (Cell.OpeningMask != 0)
		{
			WalkableTiles.Add(Cell.Coordinate);
		}
	}

	TSet<FStructureEdgeKey> WallSegments;
	TSet<FIntPoint> FloorUnits;
	for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
	{
		if (Cell.OpeningMask == 0)
		{
			continue;
		}

		const FIntPoint UnitOrigin = Cell.Coordinate * 2;
		for (int32 LocalY = 0; LocalY < 2; ++LocalY)
		{
			for (int32 LocalX = 0; LocalX < 2; ++LocalX)
			{
				const FIntPoint Unit = UnitOrigin + FIntPoint(LocalX, LocalY);
				FloorUnits.Add(Unit);
				AddFloorAndCeiling(Unit, OutInstances);
			}
		}

		for (uint8 Direction = 0; Direction < 4; ++Direction)
		{
			const bool bOpen = (Cell.OpeningMask & (1u << Direction)) != 0;
			if (!bOpen)
			{
				AddTwoCanonicalWallSegmentKeys(
					Cell.Coordinate, Direction, WallSegments);
			}
		}
	}

	// TSet Key 使两个相邻闭合 Tile 共享的墙只生成一次，避免重复面和 Z-fighting。
	for (const FStructureEdgeKey& Edge : StableSortEdges(WallSegments))
	{
		AddWallAndOptionalTopTrim(Edge, OutInstances);
	}

	// 两条共线墙只是连续直墙，不放柱；端点、直角或三向以上交汇才放 Pillar。
	AddRequiredPillarsFromWallGraph(WallSegments, OutInstances);
	return true;
}
```

`InstantiateValidatedPlan` 随后：

1. 调用 `BuildStructureInstances` 得到完整临时清单；
2. 按五个直接 Binding 分组创建 HISM；
3. 对每个 Canonical Transform 组合 `PivotCorrection`；
4. 全部实例添加成功后才注册组件；
5. 任一步失败销毁 Staged Components，保持现有事务边界。

不再扫描 `Plan.Modules`，不再按 `StableModuleId` 建 HISM，也不保留未启用的 Actor SpawnPolicy。

## 8. Hash 与日志拟修改

`FZeroEscapeGenerationSignature` 删除 `CatalogVersion`，最终只记录 Seed、Difficulty、FlowProfileId、AlgorithmVersion、GenerationProfileVersion、FlowVersion 与 PresentationVersion。PresentationVersion 用于追踪完整运行输入，但不进入 Progression/Layout Hash。

`ComputeCanonicalLayoutHash` 改为依次 Hash：

- 按 (Y, X) 排序的 Cells；
- `OpeningMask / RegionId / RegionKind`；
- Landmark -> Grid Binding；
- Gameplay Anchor 与 Objective Binding。

Floor、Wall、Ceiling、Trim、Pillar、具体 Mesh、Pivot 与 `PresentationVersion` 不进入逻辑 Layout Hash。

`ZE_PCG_RESULT` 拟改为 Schema 2：

```text
ZE_PCG_RESULT Schema=2
Cells=...
Walkable=...
Empty=...
DeadEnds=...
Straights=...
Corners=...
TJunctions=...
Crosses=...
PrunedOptional=...
HISMComponents=...
HISMInstances=...
```

移除 `Modules`、`PortalConnections`、`SocketMs`、`AStarMs`、`WfcBacktracks` 和 `SkeletonFallback`。WFC 日志保留 `Observations`、`Propagations` 与 `InvariantFailures`，用于证明算法确实运行并快速暴露实现错误。

## 9. 明确删除的旧符号

### Types / Assets

- `EZeroEscapeSocketPolicy`
- `FZeroEscapeModulePortal`
- `FZeroEscapePortalConnection`
- `FZeroEscapeClosedPortal`
- `EZeroEscapeLayoutPolicy::SocketModule`
- `EZeroEscapeLayoutPolicy::Cap`
- `MaxSocketBacktracks`
- `MaxSocketCandidateChecks`
- `MaxLayoutAttempts`
- `MaxAStarExpandedStates`
- `MaxAStarRouteAttempts`
- `MaxWfcBacktracks`
- `MaxWfcSnapshotMemoryMB`
- `MaxWfcCumulativeSnapshotCopyMB`
- `MaxProgressionSearchStates`
- `SocketMilliseconds`
- `AStarMilliseconds`
- `ERandomDomain::Topology` / `ObjectivePlacement` / `SocketLayout`
- `WfcNoSolution` / `WfcBudgetExceeded` 作为正常 Seed 结果的分支
- `Actor SpawnPolicy` 及其预留字段
- `FZeroEscapeTileDefinition`
- `UZeroEscapeModuleCatalog` 与 Generator 上的 `ModuleCatalog` 属性

### Layout Solver

- `FConnectorSignature`
- Strong / Weak Anchor 分类
- `FSocketPlacementCandidate`
- `FRouteEndpointOption`
- `PlaceRequiredSocketModules`
- `BuildEndpointOptions`
- `RouteGraphEdgesWithAStar` 及其 Open Set / 转弯代价 / 路线重试
- `BuildConnectionsAndEdgeRoutes`
- `CloseUnusedSpecialPortals`
- `SolveModuleLocalTransform`
- Closure Placement 与 Portal Finalize
- WFC Domain Snapshot / Decision Frame / `RestoreDecisionAndRebuild`
- `BuildSkeletonFallback`

WFC 只迁移可直接复用的 Domain、最小熵选择、权重选择和传播思想；不为了复用而把旧 Socket Signature、通用 Support-count、Snapshot 或回溯框架搬进新文件。A* 暂不迁移；未来出现真实障碍或代价差异时，再从 Git 历史取回并按当时需求评审。

## 10. 拟测试清单

### 保留并适配

- Generation Profile / Flow / K-of-N 契约
- Progression Intent Determinism
- Random Domain Isolation
- Runtime HISM 事务回滚
- Project Asset Pipeline Smoke

### 新测试

1. `OpeningMaskDirectionContract`：N/E/S/W 位序、反向和步进坐标一致。
2. `CanonicalVariantSetCoversAll16Masks`：代码生成结果恰好覆盖 0..15，无缺失或重复。
3. `RequiredOpenIsSymmetricAndInsideGrid`：必开边双向、非零、不越界且不与必闭边冲突。
4. `ConstructiveMinimalAssignmentAlwaysSolves`：由公共边拼 Mask 的见证解通过全部约束。
5. `OptionalCellRespondsToRequiredNeighbor`：Optional 不会错误地用 Empty 封死 Required 开口。
6. `ArbitraryWeightedCollapseNeedsNoBacktracking`：批量 Seed 完成观察与传播，Domain 从不为空，代码中无重试计数。
7. `OrthogonalRouteAlwaysConnectsValidGates`：所有合法 Gate 组合、X-first/Y-first 均在边界内连通。
8. `ProgressSlotCapacityIsValidatedUpfront`：非法容量在 Profile 校验阶段失败；合法 Seed 不发生摆放重试。
9. `OptionalJunctionNoQuota`：无 T/Cross 的合法 Seed 仍成功。
10. `DisconnectedOptionalPrune`：只移除非必需孤立分量。
11. `RoomRegion2x2`：1200 cm 房间内部开放、外围只保留指定入口。
12. `RequiredGameplayGraphIsConnected`：Start、Exit 与全部 Objective Candidate 位于同一连通分量。
13. `StructureTileExpansion`：一个非 Empty Tile 恰好生成 4 Floor + 4 Ceiling。
14. `SharedClosedEdgeDedup`：两个相邻闭 Tile 只生成一组墙段。
15. `StraightWallNoPillarSpam`：连续共线墙缝不逐段生成 Pillar。
16. `CornerAndTerminationPillar`：转角和端点正确生成 Pillar。
17. `LayoutDeterminismAndStateIsolation`：同输入 Hash 一致，配置/不变量失败不污染复用输出。
18. `ProjectHydroLabPipelineSmoke`：从磁盘读取 Generation Profile、Presentation 和 Generator BP；Generator 不再依赖 Catalog。

批量 Seed 中 Straight/Corner/T/Cross 的分布只输出诊断报告，不作为容易波动的自动化通过门槛。

## 11. 资产迁移和实施门禁

拟创建：

- `/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SciFiHydroLab`

拟更新：

- `/Game/ZeroEscape/Generation/BP_ZeroEscapeRuntimeLevelGenerator`
- `/Game/Levels/L_PCG_RuntimeTest`

现有 `DA_LevelGenerationProfile` 原位迁移为 Grid 尺寸、进度槽与六类 WFC 权重的权威配置，不新增第二份 Generation Profile。旧 `DA_LevelModuleCatalog`、`DA_Presentation_SFCorridors` 和 SFCorridors 素材不在第一步删除；新方案完成固定 Fixture、构建、自动化、正常 PIE 与玩家走通后，再列出真实引用并单独请求删除许可。

实施门禁：

1. 用户和评审确认本代码文档；
2. 将确认范围写入新的 `DOC/DailyPlan`；
3. UE 5.8 工程能够完整编译；
4. 用户明确授权修改源码；
5. 先做固定 600 cm L 形 + 2×2 Tile 房间 Fixture；
6. Fixture 视觉/碰撞通过后再接无回溯 WFC。

## 12. 本轮明确不实现

- 多层/3D WFC；
- 楼梯、电梯和非 600 cm 特殊模块；
- 物理 Socket 或逻辑 Portal Transform；
- 门宽、连接类型、HeightLayer；
- 多 Palette、按房间属性换风格；
- WFC 回溯、重启和骨架降级；
- 强制全局 ConnectedConstraint；
- T/Cross 最低数量；
- 每个难度固定路口数量；
- 旧 Socket DataAsset 兼容层。
