# PCG 多层数据合同与 Core 审查说明

## 审查对象

- 补丁：`01-data-contract-core.patch`
- 范围：多层纯数据类型、逻辑 DataAsset、集中配置校验、解析后的纯值输入、随机子流、规范 Hash，以及两项不依赖 World/WFC 的自动化。
- 本文件和补丁都只是审查稿。没有修改 `Source/`、`Content/`、`Config/` 或任何 UE 资产。
- 已在当前工作树执行：

```text
git apply --check -- claude/proposals/2026-08-03-PCG-MultiFloor-CodePreview/01-data-contract-core.patch
```

结果通过。它只证明 unified diff 能应用到当前源码，不是 C++ 构建或 UE 自动化证据。

## 已冻结的数据含义

1. `FIntVector.X/Y` 是层内逻辑格，`Z` 是从 `0` 开始的楼层序号。二维 WFC 仍使用 `FIntPoint`，本片没有把 WFC 改成三维。
2. `GridSize` 完全来自 `DA_LevelGenerationProfile`。C++ 默认 `18×12`、当前资产值或测试夹具尺寸都不是生成合同；校验只检查轴长、每层总格数和实际规则容量。
3. `LogicalTileSizeCm=600` 暂时仍是 HydroLab 已验证素材接口。`FloorHeightCm` 来自 DataAsset，当前首轮资产应填 `450cm`，求解器不写死 `450`。
4. Plan 自包含 `LogicalTileSizeCm`、`FloorHeightCm` 和 `AnchorHeightCm`。生成完成后的世界位置查询不得重新读取可能已经变化的 DataAsset。
5. 玩家与追猎者地址必须在一楼（`Z=0`），Exit 必须位于实际最高层（`Z=FloorCount-1`）。
6. `Plan.Structures` 保存全部已放置结构。`RequiredTwoFloorStairStableIdByLowerFloor` 的下标是较低楼层，值指向负责该相邻楼层对的必需双层楼梯。它必须恰好有 `FloorCount-1` 项，因此额外双层楼梯或三层楼梯间不能冒充最低配置。
7. `ThreeFloorStairwell` 只作为额外结构，最多一座。它不进入必需双层楼梯映射。
8. 高天花板房间的 `RequiredFloorCount=1`。其局部 `ClearanceCells` 可以位于 `Z=1`；只有它能开启 `bAllowClearanceAboveGeneratedTopFloor`。放在顶层时，Planner 只裁掉超出真实最高层的 Clearance，不得生成越界 Plan 地址；Walkable、Solid 和其他结构的任何越界仍失败。
9. `HighCeilingRooms.MaxCountPerFloor` 默认值为 `2`，只是可调初始值，不是硬上限。整栋最低数量、按楼层数配置的目标数量权重和每层上限共同决定实际数量；单层允许为零。
10. 每个难度与楼层数组合使用 `MinTotalWalkableCellCount/MaxTotalWalkableCellCount` 约束整栋普通格与结构自带 Walkable Cell 的总量；`MinOrdinaryWalkableCellCountPerFloor` 单独保证每层仍有普通内容。Profile 校验按 `GridSize × FloorCount` 检查整栋容量，并要求整栋下限不低于各层普通内容下限之和；逐层 WFC 必须根据已解楼层与剩余楼层下限动态分配边界，最终再精确验收整栋总量。
11. `FZeroEscapeJunctionMetrics` 没有删除。每层和整栋 Plan 都保存路口观测计数与 `CycleRank`；连通无向图使用 `E-V+1`。Plan 还保存最终 `PlayerToExitRouteLengthTiles` 和最短路线上的垂直换层次数，供 Seed 批测直接审计。

## 结构定义约束

- `DefinitionId` 是项目数据键，不是 Actor、组件或关卡名称。它在整个 Generation Profile 中必须唯一。
- 同一个 `EZeroEscapeStructureKind` 可以有多条定义。Planner 必须先按 Kind 筛选，再用 UE `FName::LexicalLess` 按 `DefinitionId` 稳定排序后枚举候选；该比较忽略大小写，与 `FName` 的同值语义一致，不得用 DataAsset 作者数组顺序参与确定性。
- 双层楼梯始终至少需要一条定义。
- 只有任一难度的三层楼梯间概率大于零时，才强制至少存在一条三层楼梯间定义。
- 只有任一带正权重的高厅目标数量大于零，或整栋最低数量大于零时，才强制至少存在一条高厅定义。
- Walkable、Solid、Clearance 三组局部地址必须无重复、互不重叠。
- 结构局部 `X/Y` 坐标限制在 `[-(MaxGridAxis-1), MaxGridAxis-1]`。超出该范围的局部偏移不可能放入任何受支持的 Grid，配置阶段直接失败；所有坐标差与距离再使用 `int64` 计算，避免损坏资产用极值触发有符号溢出。
- 所有 Walkable Cell 必须通过 `InternalConnections` 构成一个连通图；同层连接必须是四邻域，跨层连接只能跨相邻楼层。同一无向边按端点顺序规范化后必须唯一，正向重复或反向重复都直接判配置无效，避免污染 Hash 与 `E-V+1` / `CycleRank`。
- 双层和三层楼梯必须逐层存在内部跨层连接，并且每个真实楼层各有一个唯一 Landing。
- 高厅首版严格是本层相邻的 `1×2` Walkable，并在局部上一层有对应 `1×2` Clearance；高厅不伪造楼梯 Landing。
- 每个 Opening 必须位于 Walkable Cell、只有一个水平朝向。从该格沿朝向迈出的局部邻格不得仍落在本结构的 Walkable/Solid/Clearance 中。
- Opening Set 通过稳定 `SetId` 和 `OpeningId` 关联；最终结构保存 `ActiveOpeningSetId`，不保存易受数组重排影响的下标。每个楼梯落脚层实际开放 `1～3` 个口。

## 难度与共享预算

- 每档难度必须包含 `2/3/4` 层三个互不重复选项；`SelectionWeight` 允许为零，总和必须大于零。
- 每对相邻楼层使用 `Zero/One/TwoAdditionalWeight` 抽取额外双层楼梯目标；整栋再受 `MaxAdditionalTwoFloorStairCount` 限制。该字段只计算额外楼梯，不包括 `FloorCount-1` 座必需楼梯。
- 四层初始最坏导航点计算仍为：`3` 座必需双层楼梯 + `4` 座额外双层楼梯 = `7` 座；`7×2` 个 Landing + 三层楼梯间 `3` 个 Landing + 玩家/追猎者/Exit `3` 点 = `20`。
- `MaxWholeLayoutAttempts`、`MaxStructureCandidateEvaluations`、逐层 WFC 三项预算、导航等待和验证点数只在共享配置中存在，不能按 Hard 难度放宽。
- `MaxStructureCandidateEvaluations=250000` 是首轮共享硬上限。所有完整布局尝试共用剩余计数，重试不得刷新。
- 逐层 WFC 的整栋候选、回溯和 Solve 上限由“每层上限 × 实际楼层数”计算一次，所有楼层和完整布局重试共享，不再乘完整重试次数。
- 所有加权选择在配置校验阶段都使用 `int64` 累加，并要求总和落在 `1..MAX_int32`；高天花板房间的“每层上限 × 楼层数”容量也先提升到 `int64`。这避免后续随机抽取或容量判断把溢出的负数/回绕值当成合法结果。

## Hash 决策

- 算法版本由 `5` 升为 `6`，旧单层 Hash 不作为新版回归标准。
- `PresentationVersion` 继续不进入纯布局 Hash。
- Seed、Difficulty、AlgorithmVersion、GenerationProfileVersion、实际楼层数、实际 GridSize、三项尺寸、三个关键地址、普通格、全部完整结构、`DefinitionId`、`ActiveOpeningSetId`、开放口、Landing、必需楼梯映射、每层验收结果和整栋验收结果进入 Hash。
- `FName` 使用 UTF-8 字符串内容写入 Hash，绝不使用进程内 `FName` 索引。
- `double` 使用 IEEE 754 bit 原样写入固定字节顺序的整数 Hash，不四舍五入到厘米或 `0.01`，避免两个不同配置得到同一 Hash。
- 普通格必须按 `Z/Y/X` 严格排序；结构必须按 `StableStructureId` 严格递增。每个已生成结构的 Walkable/Solid/Clearance 必须按 `Z/Y/X` 严格排序，内部连接的两个端点先规范化并按端点对严格排序，Opening 和 Landing 分别用 `FName::LexicalLess` 按 ID 严格排序。Hash 只验收这种规范 Plan；重排、重复或反向内部边直接返回 `0`，不会在 Hash 内静默重排并掩盖 Planner 的非确定性。

## Room 与 RegionKind 的删除边界

本片有意删除：

- `MaxRoomCount`
- `RoomSizeTiles`
- `RoomCount`
- `FZeroEscapeGeneratedRoom`
- `Plan.Rooms`
- `EZeroEscapeGridRegionKind`
- 旧 Cell 的 `RegionId/RegionKind/StableCellId`
- 旧二维 `StartCoordinate/ExitCoordinate`
- 可由三维地址和 Plan 尺寸推导的 `PlayerStartLocalTransform/ExitLocalTransform`
- `ERandomDomain::RoomPlacement`

不提供兼容包装。旧 Room 不是当前高天花板房间；高厅由完整结构定义和放置结果重新表达。

由于当前生产消费者仍引用这些字段，`01` 不能被当作可单独构建的提交。它必须作为同一代码预览系列与后续片一起审查和落盘：

- `02`：替换旧 `FGridLayoutSolver` 上层、删除 Room/Region 写入，完成结构放置、逐层二维 WFC、最终三维通行图和 Plan 填充。
- `03`：迁移 Runtime Generator、GameMode、Population 和表现构建，不再调用旧 Room/Region 查询。

不允许为了让 `01` 单独编译而把旧字段临时加回来。

## 跨片接口

`FResolvedGenerationInput` 的最终字段为：

```cpp
FZeroEscapeGenerationSignature Signature;
FZeroEscapeSharedRouteConstraints SharedRules;
FZeroEscapeSharedGenerationBudget Budget;
FZeroEscapeDifficultyDefinition Difficulty;
TArray<FZeroEscapeStructureDefinition> StructureDefinitions;
FZeroEscapeWfcShapeWeights WfcShapeWeights;
```

`ResolveGenerationInput` 同时负责把 DataAsset 中语义上无序的数组规范化，后续随机选择和布局代码只允许读取这份解析结果：

- 每个楼层数选项的高天花板房间目标数量按 `Count` 排序。
- 每个结构的 Walkable/Solid/Clearance 格按 `Z/Y/X` 排序。
- 每条内部连接先把两个端点按 `Z/Y/X` 规范化，再按第一、第二端点排序。
- DefinitionId、Openings、Landings、AllowedOpeningSets 和每个 OpeningSet 内的 `OpenOpeningIds` 统一使用 `FName::LexicalLess` 排序。不混用默认区分大小写的 `FString::Compare`，否则合法的混合大小写 ID 会让 Planner 与 Hash 对“规范顺序”得出不同结论。

因此仅调整 DataAsset 数组显示顺序不会改变加权随机映射、结构 Plan 或最终布局 Hash；若以后新增语义无序数组，也必须在解析边界完成相同处理。

随机子流枚举为：

```cpp
FloorCount
RequiredTwoFloorStairPlacement
AdditionalTwoFloorStairCount
AdditionalTwoFloorStairPlacement
ThreeFloorStairwellPlacement
HighCeilingRoomCount
HighCeilingRoomPlacement
PlayerPursuerSpawn
WfcLayout
```

Runtime 查询应迁移为含义明确的四组接口，具体声明由运行流程片所有：

- Player Spawn
- Pursuer Spawn
- Exit
- Ordinary Gameplay Cells

普通玩法对象候选来自 `Plan.OrdinaryCells`，再排除玩家/追猎者/Exit 保护范围和调用者要求的非直走格；不重新引入 `RegionKind`。

坐标转 Generator 局部位置统一读取 Plan：

```text
LocalX = X * LogicalTileSizeCm
LocalY = Y * LogicalTileSizeCm
LocalZ = Z * FloorHeightCm + AnchorHeightCm
```

表现构件再按自身职责叠加 Presentation 的 `FloorTopZCm` 等偏移。

Generation Stage 新值：

```text
None, Configuration, StructurePlacement, WfcLayout, GlobalValidation,
Instantiation, NavigationBuild, NavigationValidation
```

Generation Failure 新值：

```text
None, InvalidConfiguration, CapacityInsufficient, StructurePlacementFailed,
SolverInvariantViolation, RequiredRouteTooLong, RequiredRouteTooShort,
NoValidWfcSolution, SolverBudgetExhausted, GlobalConnectivityFailed,
InstantiationFailed, NavigationBuildTimeout, NavigationValidationFailed
```

`FZeroEscapeGenerationReport.OperationId` 只由 Runtime Generator 拥有：纯 `Resolve/Solve` 阶段可重置 Report 并保持 `0`，Runtime 只在唯一的 `FinishGeneration` 终态出口写入当前编号后再广播/记录。`FZeroEscapeGenerationMetrics.FloorWfcMetrics` 按楼层号保存每层二维 WFC 的观察、求解树、候选、传播、矛盾和回溯统计，整栋累计字段继续保留；这些搜索诊断只属于 Report，不进入 Plan 或规范 Hash。导航 Metrics 包含投射数、路径存在性检查数、`TestPathSync` 累计访问节点数和导航验证耗时。

## 测试与编译风险

补丁新增：

- `Demo.PCG.Unit.MultiFloor.ProfileContract`
- `Demo.PCG.Unit.MultiFloor.ResolvedInputNormalization`
- `Demo.PCG.Unit.MultiFloor.CanonicalHash`

它们覆盖：可调 GridSize、楼层数极值在下标计算前拒绝、整栋总可走量与每层普通内容下限的不同语义、每层 WFC 报告默认值与楼层号、同 Kind 多定义、条件式结构定义要求、顶层高厅净空开关、导航点预算、内部无向边去重、各类权重总和溢出、高厅数量容量乘法溢出、结构局部坐标极值拒绝、DataAsset 内外层数组重排规范化、混合大小写 `FName` 排序一致性、FName 字符串 Hash、精确 double Hash、出生楼层，以及普通格和结构内部数组的规范顺序/重复/反向边拒绝。

现有测试仍需由后续片机械迁移：

- `ZeroEscapeGenerationContractTests.cpp` 当前夹具仍构造 `RoomCount/RoomSizeTiles/Plan.Cells/RegionKind/Transform`。
- `ZeroEscapeWfcLayoutTests.cpp` 的 WFC 内核测试可保留；旧 `Grid.RoomAndConnectivity`、布局确定性和 `ValidProfile288` 必须改用新版 Planner 与多层 Plan。
- 固定 `24×16` 的真实资产断言必须删除，不能替换成 `20×12`。
- 旧 Room Hash 和 HISM 数量会有意变化，不能再作为删除 Room 后的通过标准。

主要编译/集成风险：

1. UHT 字段删除后，正式 DataAsset、蓝图和 Population 资产必须在同一迁移阶段更新并保存，否则旧序列化字段会保留为无消费者数据或配置直接失败。
2. `ZeroEscapeGenerationAssets.cpp` 已在本片同步替换，避免头文件删除字段后实现仍引用旧 Room/预算；但 Grid Solver、Generator 和消费者仍依赖后续片。
3. `DefinitionId` 与 Presentation Recipe 必须一一覆盖；缺绑定应在生成前失败，不能按资产路径或 Actor 名猜测。
4. 本片未证明 Level0 几何、隐藏坡面或运行时导航，只定义其逻辑输入和报告字段。
5. `NavigationVisitedNodeCount` 依赖 UE5.8 `TestPathSync` 的访问节点输出参数；运行流程片必须使用实际 API 填写，不能估算。
6. 完整补丁系列合并后必须执行 UHT/C++ 构建、全部 `Demo.PCG` 自动化、正式 DataAsset 校验、主菜单进入 `L_Game`、代表 Seed PIE 和玩家/追猎者实际上下楼验收。

交叉编译补充：两个仅在“已有前项”分支读取的 `FIntVector` 前值现已显式初始化为
`FIntVector::ZeroValue`，避免 UE5.8 C4701 告警；这不改变规范顺序验证或 Hash 结果。
