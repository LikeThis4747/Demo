# PCG 空间核心精简：删改方案与拟实现代码

> 状态：讨论稿，尚未写入 `Source/`、`Content/` 或正式 `DOC/DailyPlan/`。
> 已确认决定：空间 PCG 不实现 K-of-N、CollectAll 或具体玩法目标；只生成中立房间与空间结果，未来玩法层再向房间分配材料、奖励、陷阱和敌人。

## 1. 精简后的唯一职责

空间 PCG 保留：

- Seed、Difficulty 与确定性复现。
- 16 OpeningMask WFC、邻接传播、Count、MaxConsecutive、Connected 和有限时间序回溯。
- Start、Exit、固定数量的中立 2x2 房间约束。
- 最终逻辑格数量、路口统计和 Start -> Exit 逻辑最短距离上限。
- 纯逻辑 Plan 到五类结构 HISM 的事务式实例化。

空间 PCG 删除：

- `EscapeOnly / CollectAll / CollectKOfN` 多完成规则。
- `FlowProfileId`、Flow DataAsset 数组和 FlowVersion。
- Objective N/K、Objective Progression Intent、Objective Binding。
- K-of-N bitmask DP 和目标绕行验收。
- Landmark/GameplayAnchor/ObjectiveBinding 三套重复映射。
- Progression Hash、与旧玩法链绑定的错误码、指标和测试。

## 2. 逐文件删改范围

| 文件/资产 | 动作 | 主要变化 |
|---|---|---|
| `Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h` | 修改 | 删除 CompletionRule、玩法 Anchor 枚举、K/N 错误和三套 Binding；新增中立 `GeneratedRoom`；Request 只保留 Seed/Difficulty 并允许 Blueprint 写入；精简 Plan/Signature/Metrics |
| `Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h` | 修改 | `ObjectiveProgressBandCount` 改为共享 `RoomCount`；删除路线 Extra、难度 K/N 和 Flow 数组；保留三档形态权重 |
| `Source/Demo/Public/PCG/ZeroEscapeRuntimeLevelGenerator.h` | 修改 | DefaultRequest 直接展开；Objective 查询改为 Room 查询；删除通用 GameplayAnchor 私有查询；暂不添加小地图接口 |
| `Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp` | 修改 | 删除 K/N、Flow、Objective 容量校验；改为中立房间容量校验；保留尺度、密度、预算、难度权重和表现绑定校验 |
| `Source/Demo/Private/PCG/ZeroEscapeGenerationCore.h/.cpp` | 大幅精简 | 删除 Snapshot + Progression 两段链，收敛为一次 `ResolveGenerationInput`；保留随机域、单一 Layout Hash 和确定性签名 |
| `Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.h/.cpp` | 大幅精简 | 删除 GridLayoutRequest/重复 Settings、Progression Intent、K-of-N DP、目标绕行和重复叶节点校验；直接从纯值 Rules 放置 Start/Exit/中立房间并调用 WFC |
| `Source/Demo/Private/PCG/ZeroEscapeWfcSolver.h/.cpp` | 小改 | Variant 参数改用只读 ArrayView，去掉临时复制；Metrics 只保留调优真正使用的汇总值；WFC/回溯算法不变 |
| `Source/Demo/Private/PCG/ZeroEscapeWfcConstraints.h/.cpp` | 原则上不改 | 三项标准全局约束保留；只有编译所需的字段名同步才修改 |
| `Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp` | 精简 | 生成管线改为 ResolveInput -> Grid/WFC -> 单 Hash -> HISM；日志删除 Flow/Progression/细碎矛盾字段；Room Transform 代替 Objective Transform |
| `Source/Demo/Public/PCG/ZeroEscapeRuntimeGenerationTestHarness.h` | 注释小改 | 删除旧 Flow/Socket 历史叙述；接口仍用显式 Request，确保重生成前先把玩家送回 Staging |
| `Source/Demo/Private/PCG/ZeroEscapeRuntimeGenerationTestHarness.cpp` | 小改 | 日志只输出 Seed/Difficulty；安全传送和重生成流程不变 |
| `Source/Demo/Private/PCG/Tests/ZeroEscapeGenerationContractTests.cpp` | 精简 | 删除 Flow/K-of-N/Progression 测试；保留 Profile、素材、结构展开、难度数组顺序和随机域契约 |
| `Source/Demo/Private/PCG/Tests/ZeroEscapeWfcLayoutTests.cpp` | 精简 | Objective 房测试改为中立房间；Seed Sweep 改为 3 难度 x 96 Seed；回溯、约束和确定性测试保留 |
| `/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile` | 迁移 | ProfileVersion 4 -> 5；RoomCount=3；删除旧 Flow/K/N 序列化字段；权重和 WFC 预算暂不调参 |
| `/Game/ZeroEscape/Generation/BP_ZeroEscapeRuntimeLevelGenerator` | 编译/保存 | 父类 USTRUCT 迁移；保留 Generation/Presentation 引用；没有图逻辑需要重写 |
| `/Game/ZeroEscape/Generation/Debug/BP_RuntimeGenerationTestHarness` | 编译/保存 | Request Pin 迁移；没有图逻辑需要重写 |
| `/Game/Levels/L_PCG_RuntimeTest` | 条件保存 | 只在旧 Request 结构导致关卡实例变脏时保存，不主动改变关卡摆放 |

不修改 `DA_Presentation_SciFiHydroLab`、第三方 HydroLab 素材、Level0、追猎者、Physics 或 `Demo.Build.cs`。

## 3. 拟实现公共类型

以下是目标接口，省略未变化的四方向帮助函数和简单 UPROPERTY 注释。

```cpp
namespace ZeroEscape::GenerationLimits
{
    inline constexpr int32 MaxRoomCount = 6;
    inline constexpr int32 MaxGridCells = 1024;
    inline constexpr int32 MinGridAxis = 6;
    inline constexpr int32 MaxGridAxis = 64;
}

UENUM(BlueprintType)
enum class EZeroEscapeDifficulty : uint8
{
    Easy,
    Normal,
    Hard
};

UENUM(BlueprintType)
enum class EZeroEscapeGridRegionKind : uint8
{
    Corridor,
    Room,
    Start,
    Exit
};

UENUM(BlueprintType)
enum class EZeroEscapeGenerationStage : uint8
{
    None,
    Configuration,
    GridLayout,
    WfcLayout,
    GlobalValidation,
    Instantiation
};

UENUM(BlueprintType)
enum class EZeroEscapeGenerationFailure : uint8
{
    None,
    InvalidConfiguration,
    CapacityInsufficient,
    SolverInvariantViolation,
    RequiredRouteTooLong,
    InstantiationFailed,
    NoValidWfcSolution,
    SolverBudgetExhausted
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationRequest
{
    GENERATED_BODY()

    /** 同一算法和 Profile 版本下，相同 Seed 必须得到相同逻辑布局。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
    int32 Seed = 12345;

    /** 一局固定一个难度；当前只选择 WFC 形态权重。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
    EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;
};

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

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeCollapsedTile
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
    FIntPoint GridCoordinate = FIntPoint::ZeroValue;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated",
        meta = (Bitmask, BitmaskEnum = "/Script/Demo.EZeroEscapeOpenEdge"))
    uint8 OpeningMask = 0;

    /** Room 使用非负 RegionId；其他类型通常为 INDEX_NONE。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
    int32 RegionId = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
    EZeroEscapeGridRegionKind RegionKind = EZeroEscapeGridRegionKind::Corridor;
};

/** 玩法层以后可以选择的中立房间位置；PCG 不决定房间里放什么。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGeneratedRoom
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
    int32 RegionId = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
    FIntPoint AnchorCoordinate = FIntPoint::ZeroValue;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated")
    FTransform LocalTransform = FTransform::Identity;
};

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
```

Metrics 只保留 Seed Sweep 和运行时验收真正使用的字段：

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeGenerationMetrics
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
    int32 WalkableCellCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
    int32 WfcSolveAttemptCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
    int32 WfcCandidateAttemptCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
    int32 WfcContradictionCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
    int32 WfcBacktrackCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
    int32 WfcCollapsedCandidateRejectionCount = 0;

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
```

## 4. 拟实现 Profile

```cpp
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeSharedRouteConstraints
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
    FIntPoint GridSize = FIntPoint(24, 16);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0"))
    double LogicalTileSizeCm = 600.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rooms", meta = (ClampMin = "1", ClampMax = "4"))
    int32 RoomSizeTiles = 2;

    /** 所有难度共享的中立房间数量；困难不通过增加房间故意延长一局。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rooms", meta = (ClampMin = "0", ClampMax = "6"))
    int32 RoomCount = 3;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Density", meta = (ClampMin = "1"))
    int32 MinWalkableCellCount = 48;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Density", meta = (ClampMin = "1"))
    int32 MaxWalkableCellCount = 72;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1"))
    int32 MaxConsecutiveStraightTiles = 4;

    /** Start -> Exit 的逻辑最短距离上限；不声称等于 NavMesh 或玩家实际路线。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1"))
    int32 MaxRequiredRouteLengthTiles = 64;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
    int32 MaxWfcCandidateAttempts = 100000;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1"))
    int32 MaxWfcBacktrackCount = 25000;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solver", meta = (ClampMin = "1", ClampMax = "16"))
    int32 MaxWfcSolveAttempts = 10;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchors", meta = (ClampMin = "0.0"))
    double AnchorHeightCm = 100.0;
};

USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeDifficultyDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
    EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty|WFC")
    FZeroEscapeWfcShapeWeights WfcShapeWeights;
};

UCLASS(BlueprintType)
class DEMO_API UZeroEscapeLevelGenerationProfile final : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
    int32 ProfileVersion = 5;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
    FZeroEscapeSharedRouteConstraints SharedRouteConstraints;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
    TArray<FZeroEscapeDifficultyDefinition> Difficulties;

    bool IsConfigured(FString& OutError) const;
};
```

Profile 校验只保留：网格/尺度、RoomCount 容量、Count、最大直线、路线长度、WFC 预算、三档唯一难度、权重合法性与跨难度总体权重一致。

## 5. 拟实现纯值解析核心

不再建立 Snapshot -> ProgressionSettings -> ProgressionIntent 三段对象，改为一次解析：

```cpp
namespace ZeroEscape::LevelGeneration
{
    inline constexpr int32 GAlgorithmVersion = 5;

    enum class ERandomDomain : uint32
    {
        RoomPlacement = 0x20B8A51Du,
        WfcLayout = 0x95E27B43u
    };

    struct FResolvedGenerationInput
    {
        FZeroEscapeGenerationSignature Signature;
        FZeroEscapeSharedRouteConstraints Rules;
        FZeroEscapeWfcShapeWeights WfcShapeWeights;
    };

    class FGenerationCore final
    {
    public:
        static bool ResolveGenerationInput(
            const UZeroEscapeLevelGenerationProfile& Profile,
            const FZeroEscapeGenerationRequest& Request,
            int32 PresentationVersion,
            FResolvedGenerationInput& OutInput,
            FZeroEscapeGenerationReport& OutReport);

        static FRandomStream MakeRandomStream(
            int32 MasterSeed,
            int32 AlgorithmVersion,
            ERandomDomain Domain,
            int32 Salt = 0);

        static int64 ComputeCanonicalLayoutHash(
            const FZeroEscapeGeneratedLevelPlan& Plan);
    };
}
```

```cpp
bool FGenerationCore::ResolveGenerationInput(
    const UZeroEscapeLevelGenerationProfile& Profile,
    const FZeroEscapeGenerationRequest& Request,
    const int32 PresentationVersion,
    FResolvedGenerationInput& OutInput,
    FZeroEscapeGenerationReport& OutReport)
{
    OutInput = {};

    FString Error;
    if (!Profile.IsConfigured(Error) || PresentationVersion <= 0)
    {
        return FailCore(
            OutReport,
            EZeroEscapeGenerationStage::Configuration,
            EZeroEscapeGenerationFailure::InvalidConfiguration,
            Error.IsEmpty() ? TEXT("PresentationVersion 必须大于 0。") : Error);
    }

    const FZeroEscapeDifficultyDefinition* Difficulty =
        Profile.Difficulties.FindByPredicate(
            [&Request](const FZeroEscapeDifficultyDefinition& Candidate)
            {
                return Candidate.Difficulty == Request.Difficulty;
            });
    if (Difficulty == nullptr)
    {
        return FailCore(
            OutReport,
            EZeroEscapeGenerationStage::Configuration,
            EZeroEscapeGenerationFailure::InvalidConfiguration,
            TEXT("Request.Difficulty 无法从 Generation Profile 解析。"));
    }

    OutInput.Rules = Profile.SharedRouteConstraints;
    OutInput.WfcShapeWeights = Difficulty->WfcShapeWeights;
    OutInput.Signature.Seed = Request.Seed;
    OutInput.Signature.Difficulty = Request.Difficulty;
    OutInput.Signature.AlgorithmVersion = GAlgorithmVersion;
    OutInput.Signature.GenerationProfileVersion = Profile.ProfileVersion;
    OutInput.Signature.PresentationVersion = PresentationVersion;
    return true;
}
```

Layout Hash 只混入：Seed、Difficulty、算法/Profile 版本、GridSize、TileSize、Start/Exit、稳定排序 Cells 和 Rooms；不再混入 Flow、Progression、Objective 或浮点 Transform。

## 6. 拟实现中立房间放置

Grid Solver 不再接收重复的 `FGridLayoutRequest` 和 `FGridLayoutSettings`：

```cpp
class FGridLayoutSolver final
{
public:
    static bool Solve(
        const FZeroEscapeGenerationSignature& Signature,
        const FZeroEscapeSharedRouteConstraints& Rules,
        const FZeroEscapeWfcShapeWeights& Weights,
        FZeroEscapeGeneratedLevelPlan& OutPlan,
        FZeroEscapeGenerationReport& OutReport);
};
```

```cpp
struct FRoomPlacement
{
    int32 RegionId = INDEX_NONE;
    FIntPoint MinCoordinate = FIntPoint::ZeroValue;
    FIntPoint AnchorCoordinate = FIntPoint::ZeroValue;
};

bool EmbedSpatialConstraints(
    const FZeroEscapeGenerationSignature& Signature,
    const FZeroEscapeSharedRouteConstraints& Rules,
    FGridWorkingState& OutState,
    FZeroEscapeGenerationReport& OutReport)
{
    InitializeConstraintGrid(Rules.GridSize, OutState);

    const int32 CenterY = Rules.GridSize.Y / 2;
    OutState.StartCoordinate = FIntPoint(1, FMath::Clamp(CenterY - 1, 2, Rules.GridSize.Y - 3));
    OutState.ExitCoordinate = FIntPoint(
        Rules.GridSize.X - 2,
        FMath::Clamp(CenterY + 1, 2, Rules.GridSize.Y - 3));

    const int32 MinRoomX = 4;
    const int32 MaxRoomX = Rules.GridSize.X - Rules.RoomSizeTiles - 5;
    const int32 LowerLaneY = CenterY - Rules.RoomSizeTiles;
    const int32 UpperLaneY = CenterY + 1;

    FRandomStream Random = FGenerationCore::MakeRandomStream(
        Signature.Seed,
        Signature.AlgorithmVersion,
        ERandomDomain::RoomPlacement);

    for (int32 RoomIndex = 0; RoomIndex < Rules.RoomCount; ++RoomIndex)
    {
        const int32 RoomX = Rules.RoomCount == 1
            ? (MinRoomX + MaxRoomX) / 2
            : MinRoomX + ((MaxRoomX - MinRoomX) * RoomIndex) / (Rules.RoomCount - 1);
        const int32 Lane = Random.RandRange(0, 1);

        FRoomPlacement& Room = OutState.Rooms.AddDefaulted_GetRef();
        Room.RegionId = RoomIndex;
        Room.MinCoordinate = FIntPoint(RoomX, Lane == 0 ? LowerLaneY : UpperLaneY);
        Room.AnchorCoordinate = Room.MinCoordinate + FIntPoint(
            0,
            Lane == 0 ? Rules.RoomSizeTiles - 1 : 0);
    }

    MarkRequired(OutState, OutState.StartCoordinate, INDEX_NONE, EZeroEscapeGridRegionKind::Start);
    MarkRequired(OutState, OutState.ExitCoordinate, INDEX_NONE, EZeroEscapeGridRegionKind::Exit);

    for (const FRoomPlacement& Room : OutState.Rooms)
    {
        for (int32 LocalY = 0; LocalY < Rules.RoomSizeTiles; ++LocalY)
        {
            for (int32 LocalX = 0; LocalX < Rules.RoomSizeTiles; ++LocalX)
            {
                const FIntPoint Cell = Room.MinCoordinate + FIntPoint(LocalX, LocalY);
                MarkRequired(OutState, Cell, Room.RegionId, EZeroEscapeGridRegionKind::Room);

                if (LocalX + 1 < Rules.RoomSizeTiles
                    && !AddRequiredOpening(OutState, Cell, Cell + FIntPoint(1, 0), OutReport))
                {
                    return false;
                }
                if (LocalY + 1 < Rules.RoomSizeTiles
                    && !AddRequiredOpening(OutState, Cell, Cell + FIntPoint(0, 1), OutReport))
                {
                    return false;
                }
            }
        }
    }
    return true;
}
```

这仍然只固定 Start/Exit 和房间内部，不预雕刻它们之间的路线；外部入口、转弯、T/Cross 和连接方式仍由 WFC 生成。

## 7. 拟实现 Plan 导出与最终路线验收

```cpp
bool ExportCandidatePlan(
    const FZeroEscapeGenerationSignature& Signature,
    const FZeroEscapeSharedRouteConstraints& Rules,
    const FGridWorkingState& State,
    const TConstArrayView<uint8> OpeningMasks,
    FZeroEscapeGeneratedLevelPlan& OutPlan,
    FZeroEscapeGenerationReport& OutReport)
{
    if (OpeningMasks.Num() != State.Constraints.Num())
    {
        return Fail(
            OutReport,
            EZeroEscapeGenerationStage::WfcLayout,
            EZeroEscapeGenerationFailure::SolverInvariantViolation,
            TEXT("WFC 输出数量与完整 Grid 数量不一致。"));
    }

    OutPlan = {};
    OutPlan.Signature = Signature;
    OutPlan.GridSize = Rules.GridSize;
    OutPlan.LogicalTileSizeCm = Rules.LogicalTileSizeCm;
    OutPlan.StartCoordinate = State.StartCoordinate;
    OutPlan.ExitCoordinate = State.ExitCoordinate;

    auto MakeAnchorTransform = [&Rules](const FIntPoint Coordinate)
    {
        return FTransform(FVector(
            Coordinate.X * Rules.LogicalTileSizeCm,
            Coordinate.Y * Rules.LogicalTileSizeCm,
            Rules.AnchorHeightCm));
    };
    OutPlan.PlayerStartLocalTransform = MakeAnchorTransform(State.StartCoordinate);
    OutPlan.ExitLocalTransform = MakeAnchorTransform(State.ExitCoordinate);

    for (int32 DenseIndex = 0; DenseIndex < OpeningMasks.Num(); ++DenseIndex)
    {
        if (OpeningMasks[DenseIndex] == 0)
        {
            continue;
        }

        const FGridCellConstraint& Source = State.Constraints[DenseIndex];
        FZeroEscapeCollapsedTile& Tile = OutPlan.Cells.AddDefaulted_GetRef();
        Tile.GridCoordinate = Source.Coordinate;
        Tile.OpeningMask = OpeningMasks[DenseIndex];
        Tile.RegionId = Source.RegionId;
        Tile.RegionKind = Source.RegionKind;
    }

    for (const FRoomPlacement& Source : State.Rooms)
    {
        FZeroEscapeGeneratedRoom& Room = OutPlan.Rooms.AddDefaulted_GetRef();
        Room.RegionId = Source.RegionId;
        Room.AnchorCoordinate = Source.AnchorCoordinate;
        Room.LocalTransform = MakeAnchorTransform(Source.AnchorCoordinate);
    }
    return true;
}
```

最终生产验收只保留已经产生实际产品价值的 BFS：

```cpp
bool ValidateFinalRoute(
    const FZeroEscapeSharedRouteConstraints& Rules,
    const FZeroEscapeGeneratedLevelPlan& Plan,
    FZeroEscapeGenerationReport& OutReport)
{
    TMap<FIntPoint, int32> CellByCoordinate;
    const TArray<int32> Distances = BuildDistances(
        Plan,
        Plan.StartCoordinate,
        CellByCoordinate);

    const int32* ExitIndex = CellByCoordinate.Find(Plan.ExitCoordinate);
    if (ExitIndex == nullptr || Distances[*ExitIndex] == INDEX_NONE)
    {
        return Fail(
            OutReport,
            EZeroEscapeGenerationStage::GlobalValidation,
            EZeroEscapeGenerationFailure::SolverInvariantViolation,
            TEXT("Connected WFC 结果中 Start 无法到达 Exit。"));
    }

    // 这次遍历复用路线 BFS 结果，不再重复执行 Count、MaxConsecutive 和第二套 Connected 算法。
    for (const int32 Distance : Distances)
    {
        if (Distance == INDEX_NONE)
        {
            return Fail(
                OutReport,
                EZeroEscapeGenerationStage::GlobalValidation,
                EZeroEscapeGenerationFailure::SolverInvariantViolation,
                TEXT("最终 Plan 含有不属于 Start 连通分量的非空 Cell。"));
        }
    }

    const int32 RouteLength = Distances[*ExitIndex];
    if (RouteLength > Rules.MaxRequiredRouteLengthTiles)
    {
        return Fail(
            OutReport,
            EZeroEscapeGenerationStage::GlobalValidation,
            EZeroEscapeGenerationFailure::RequiredRouteTooLong,
            TEXT("Start -> Exit 逻辑最短距离超过上限。"),
            RouteLength,
            Rules.MaxRequiredRouteLengthTiles);
    }

    if (Plan.Rooms.Num() != Rules.RoomCount)
    {
        return Fail(
            OutReport,
            EZeroEscapeGenerationStage::GlobalValidation,
            EZeroEscapeGenerationFailure::SolverInvariantViolation,
            TEXT("最终 Plan 的中立房间数量与 Profile 不一致。"),
            Plan.Rooms.Num(),
            Rules.RoomCount);
    }
    return true;
}
```

Count、MaxConsecutive、开口对称和 Connected 的独立重复生产复核删除；这些不变量由 WFC 约束和专项自动化测试负责。Route BFS 已经需要执行，因此顺带检查最终连通不会增加第二套遍历。

`FGridLayoutSolver::Solve` 只在候选通过上述验收后计算并写入唯一 Layout Hash；Hash 函数本身不读取 `CanonicalLayoutHash` 字段，避免自引用：

```cpp
if (!ValidateFinalRoute(Rules, AcceptedCandidate, OutReport))
{
    return false;
}

BuildJunctionMetrics(AcceptedCandidate);
AcceptedCandidate.CanonicalLayoutHash =
    FGenerationCore::ComputeCanonicalLayoutHash(AcceptedCandidate);
if (AcceptedCandidate.CanonicalLayoutHash == 0)
{
    return Fail(
        OutReport,
        EZeroEscapeGenerationStage::GlobalValidation,
        EZeroEscapeGenerationFailure::SolverInvariantViolation,
        TEXT("规范 Layout Hash 不能为 0。"));
}

OutPlan = MoveTemp(AcceptedCandidate);
return true;
```

## 8. 拟实现 Runtime 入口和 Blueprint Seed

```cpp
UPROPERTY(EditAnywhere, Category = "PCG", meta = (ShowOnlyInnerProperties))
FZeroEscapeGenerationRequest DefaultRequest;

UFUNCTION(BlueprintPure, Category = "PCG")
bool GetGeneratedRoomWorldTransforms(TArray<FTransform>& OutTransforms) const;
```

```cpp
FResolvedGenerationInput Input;
if (!FGenerationCore::ResolveGenerationInput(
        *GenerationProfile,
        Request,
        PresentationProfile->PresentationVersion,
        Input,
        Report))
{
    return FinishFailedGeneration(...);
}

FString PresentationError;
if (!PresentationProfile->IsConfigured(
        Input.Rules.LogicalTileSizeCm,
        PresentationError))
{
    Report.Stage = EZeroEscapeGenerationStage::Configuration;
    Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
    Report.Message = MoveTemp(PresentationError);
    return FinishFailedGeneration(...);
}

FZeroEscapeGeneratedLevelPlan CandidatePlan;
if (!FGridLayoutSolver::Solve(
        Input.Signature,
        Input.Rules,
        Input.WfcShapeWeights,
        CandidatePlan,
        Report))
{
    return FinishFailedGeneration(...);
}

const int64 RecomputedHash = FGenerationCore::ComputeCanonicalLayoutHash(CandidatePlan);
if (!(CandidatePlan.Signature == Input.Signature)
    || CandidatePlan.CanonicalLayoutHash == 0
    || CandidatePlan.CanonicalLayoutHash != RecomputedHash)
{
    // 结构化失败，不提交任何 HISM。
}
```

Room 查询直接转换 `Plan.Rooms` 的 LocalTransform，不再按 GameplayAnchorType 和 StableAnchorInstanceId 做二次查找。Start/Exit Getter 直接使用 Plan 中各自的 LocalTransform。

Blueprint 当前即可使用：

```text
Make ZeroEscapeGenerationRequest(Seed, Difficulty)
    -> TestHarness.RegenerateFromRequest(Request)   // PIE 安全重生成
```

正式菜单以后把同一 Request 交给 GameFlow；不新增 `SetSeed` 或第二个 Seed 状态。

## 9. 测试删改

保留：

- OpeningMask/Variant 契约。
- Count、MaxConsecutive、Connected 约束测试。
- Expanded Graph/Tarjan 连通传播测试。
- Root/Decision/Leaf 三类回溯测试。
- WFC 确定性重放。
- 中立 2x2 房间和全图连通测试。
- 多尝试预算失败原子性。
- Transform、结构展开、HydroLab 真实资产烟测。
- 3 难度 x 96 Seed = 288 组 Seed Sweep。

删除或替换：

- 删除 ProgressionIntent/K-of-N/CollectAll 合同测试。
- Snapshot 排序测试改为“Difficulty 数组重排不改变同一请求解析结果”。
- RandomDomain 测试只保留 RoomPlacement 与 WfcLayout 隔离。
- Layout Hash 测试删除 ProgressionHash/FlowVersion 断言。
- 项目资产烟测断言 `RoomCount=3`，不再断言三种 Flow、K/N 和 route extra。

## 10. 历史上下文整理

源码实施完成并通过验证后，同一任务收尾处理：

1. 源码顶部和函数注释改为正向描述当前职责，删除对旧 Graph/Socket/A-star/固定主干的历史叙述。
2. 已处理的 PCG Review 加 `Done-` 并移入 `claude/reviews/archive/`。
3. 旧 PCG 实现卡和素材预览卡移入 `claude/tasks/archive/`；本任务卡成为本次精简的唯一恢复入口，完成后同样归档。
4. `claude/docs` 五份旧 PCG 中间稿移入明确的历史子目录，不再位于默认讨论稿根目录。
5. 旧 V2、V3.2、V4 DailyPlan 移入 `DOC/DailyPlan/archive/PCG/`；不删除日报或 Ideas。
6. 在 `DOC/Design/PCG/` 建立一份简短的当前实现索引，并更新 `DOC/README.md`。

历史移动属于独立可回退检查点，不与源码编译失败修复混在同一个提交中。

## 11. 代码规模门禁

当前基线：17 个 PCG 文件、9,034 物理行，其中 Runtime 7,125、Tests 1,909。

预计完成后：

| 部分 | 当前 | 目标区间 |
|---|---:|---:|
| Runtime/非测试 | 7,125 | 6,150–6,450 |
| Tests | 1,909 | 1,400–1,600 |
| 总计 | 9,034 | 7,550–8,050 |

硬门禁：

- 如果实施后净减不足 1,000 行，停止并重新审计，不用新增抽象层掩盖减量失败。
- 不以删除 WFC 回溯、三项全局约束或关键回归测试换取行数。
- 每个检查点都报告修改前总行数、修改后总行数和净变化。
- 注释保留职责、假设、不变量和失败边界；删除历史流水和逐行复述。

## 12. 实施检查点与回退

1. **纯 C++ 契约切换**：Types/Assets/Core/Grid/Runtime/Tests 原子修改；未编译前不打开并保存相关 UE 资产。
2. **构建与自动化**：完整 DemoEditor Build；运行 `Demo.PCG`；失败只修本次 PCG 变化，不触碰 Pursuer/Physics。
3. **资产迁移**：编辑器加载新反射类型后，仅修改项目自有 Generation Profile，编译两份空图 Blueprint；关卡只在迁移确实使其变脏时保存。
4. **Seed Sweep**：288/288，记录 Attempts/Candidates/Backtracks/Planning P50/P95/Max。
5. **PIE 烟测**：正常主视口生成、Harness 传送、Seed/Difficulty Blueprint Request 可用。
6. **历史整理**：在代码和资产验证完成后归档旧上下文；单独检查链接和 `DOC/README`。

任一检查点失败都停在该检查点，不通过兼容壳、旧字段转发或备用生成算法绕过。
