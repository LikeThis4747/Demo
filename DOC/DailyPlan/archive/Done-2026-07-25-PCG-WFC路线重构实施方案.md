# 2026-07-25 PCG WFC 路线重构实施方案

> 状态：全图 WFC、全局约束与回溯已实现并归档；当前真实代码由下一轮完整审阅确认，AI 无需主动阅读本历史计划。

> 状态：两轮方案/代码审计已通过；用户已于 2026-07-25 明确授权实施。V4 源码、真实 Generation Profile、UE 5.8 构建、19 项 `Demo.PCG`、288 组 Seed Sweep 与 SelectedViewport PIE 技术烟测均已通过；仍待玩家主观观感、碰撞、导航和实际走通验收。
>
> Owner：Codex / root
>
> 审计依据：`claude/reviews/2026-07-25-pcg-route-generation-revision-review.md`、`claude/reviews/2026-07-25b-pcg-wfc-refactor-code-proposal-review.md`
>
> 实施前快照：`b8a8c6e30b5f5e7c73c382e64c62c0e4d1706e78`，已推送内部工蜂 `origin/main` 并核验一致。

## 1. 本轮要解决的问题

当前 V3.2 能稳定生成并通过自动化，但玩家视角已经否决其路线质量：

1. 固定中央主干使长直路成为必然结果，WFC 无权改变主要路线。
2. 旧 Optional Envelope 太小，最终非空区域过少。
3. 现有 WFC 遇到 contradiction 直接失败，没有为全局连通、数量和连续直线约束提供回溯。
4. 独立 PCG 验收使用了浮动 PIE 小窗口，不符合后续玩家验收习惯。
5. 自动化测试、独立 PIE Harness 与未来正式 GameFlow 的生命周期边界需要在继续加代码前冻结。

本轮目标是把路线拓扑改为：

**完整可玩 Grid 的 Simple-Tiled WFC + Connected / Count / MaxConsecutive 三项约束 + 有界 chronological backtracking + 总预算内的有限确定性重试 + 最终玩法验证。**

T 字、十字、转弯和环路都是合法结果，但不对单个 Seed 设置必须出现的配额。困难难度主要调整形态权重，不通过显著扩大地图或增加非空格数量来延长单局。

## 2. 审计意见的采纳结论

### 本轮采纳

- 删除固定中央主干、X/Y 雕路、Optional Envelope 和事后孤岛剪枝，不保留兼容壳。
- 删除 `ValidateGuaranteedSolvableConstraints` 中依赖“Optional 全空也必然可解”的构造性见证。
- 将该函数收敛为一次性的静态输入校验；保留 Mask、边界、坐标和必开边镜像检查。
- 删除 Grid 调用方对同一输入验证的重复调用，只由 WFC `Solve` 入口校验一次。
- 删除 WFC 导出后的重复逐边热路径复核；等价的局部邻接断言放到 WFC 自动化测试，产品层最终验证仍保留。
- Solver 预算不凭感觉冻结；先用 Seed Sweep 获取 P50、P95 和 Max，再确定正式值。
- 当前全局约束直接用三个私有纯值实现，不建设通用约束插件框架。
- `Solve` 通过 CPP 内部 `BuildAndValidateDenseConstraintView` 先且只构建一次稠密 `ConstraintsByIndex`，并在同一入口完成坐标、Mask、边界和镜像校验；不在同一次求解中重复做 O(N) 坐标映射。
- Solver 生产路径不在完整候选导出后重复逐边复核；Grid 的 `ValidateFinalPlan` 是 Count、MaxConsecutive、连通、路线与产品不变量的唯一最终验收入口，Solver Automation 单独扫描 16 个 OpeningMask 的邻接等价性。
- 外部配置、坐标、Mask 与调用契约继续用运行时失败报告；`NarrowDomain` 的 CellIndex 等纯内部不变量使用 `check`，不在热路径重复付费。`Solve` 当前仍接收任意 `TArray<FTileVariant>`，因此 16 个 Variant 的顺序、正权重与总权重校验继续保留为运行时入口校验；只有未来把接口收窄为 Solver 内部构造时才可降为 Debug。
- 回溯必须补两个独立边界夹具：根固定点保留后首个决策矛盾，以及传播直接形成完整叶子后候选验收 Reject。二者不得崩溃、死循环或恢复过头。

### 本轮不做

- 不实现 backjumping、修改请求 Seed、隐藏重试或固定路线降级。实际加入的有限重试是公开 Profile 参数：请求 Seed/Signature 不变，仅使用稳定派生的 WFC 子流，并共享同一候选/回溯总预算。
- 不实现 `LoopConstraint`；只有批量与玩家结果持续出现严重树状折返时才另行评估。
- 不实现房间图、Delaunay、MST 或 A* 第二套生成器。
- 不拆分 GridLayoutSolver 的房间与结构展开职责；先观察删除旧骨架后的实际规模。
- 不修改第三方 HydroLab 素材，也不在拓扑代码中硬编码素材目录。

## 3. 生成职责与数据流

### 3.1 Grid 初始化

1. 全部可玩 Cell 都进入 WFC；普通 Cell 允许 Empty 和合法 OpeningMask。
2. 外边界只禁止朝地图外开放，不预刻内部道路。
3. Start、Exit 和 Objective 占格必须非空。
4. Objective 继续使用确定性的 2×2 房间占格；房内共享边必开，房间外周不再预设 Gate，也不固定连接中央主干。
5. Progression 继续决定 Start / Exit / Objective 的相对位置意图、N/K 和进度带，但不负责画路。

### 3.2 一次 WFC 求解

1. 初始化每格 16-bit Domain，并执行局部 OpeningMask 邻接传播。
2. 局部传播稳定后，按 `Count → MaxConsecutive → Connected` 运行全局约束；约束产生的 Ban 重新进入局部传播，直到稳定。
3. 未全部折叠时，按原版 WFC 的最低带权 Shannon 熵选 Cell，并按权重一次性建立候选尝试顺序；不得把候选数量放在熵之前，否则会沿非空前沿持续生长并让 Empty 权重失效。
4. 保存决策帧后尝试候选；任意 Domain 归零或全局约束证明当前分支不可能时，恢复 Trail 并尝试下一候选。
5. 当前决策候选耗尽才退到上一决策；全部决策耗尽报告无解，预算耗尽报告预算失败。
6. 全部折叠后先把稠密 OpeningMask 交给 Grid 的单一完成态验收；路线总长或额外折返超限时把该完整候选作为当前分支 contradiction，继续同一决策栈回溯。
7. 只有完成态验收返回 Accept 才原子提交 Plan；非对称边、Required 丢失、Anchor 数量等输入/代码不变量返回 Fatal 并立即终止。
8. 仅当单棵搜索树达到候选或回溯分片预算时，才进入下一棵确定性搜索树；`NoValidWfcSolution` 表示完整回溯已经穷尽分支，必须立即停止。最多 `MaxWfcSolveAttempts` 次，Candidate/Backtrack 总预算按尝试数稳定分片，全部尝试合计不得超过 Profile 总上限。

候选顺序在创建决策帧时一次性确定，回溯不重新抽随机数。同一输入、Seed、算法版本和配置必须得到相同结果、候选尝试数与回溯数。完成态验收只是一个窄的调用方边界，不建设通用约束回调框架：WFC 不认识 K-of-N/房间/Plan，Grid 不接触 Domain/Trail/决策栈。

### 3.3 三项约束

#### Count

- 统计已经被迫非空的 Cell 数，以及仍可能非空的 Cell 数。
- 已确定非空数超过上限，或最大可能非空数低于下限，产生 contradiction。
- 达到上限时从其他 Domain 删除非空候选；只剩下限所需数量时，从这些 Domain 删除 Empty。

#### MaxConsecutive

- 水平方向统计同时具有 East/West 开口的状态；垂直方向统计同时具有 North/South 开口的状态。
- Straight、T 和 Cross 只要沿该轴贯通都计数，不能用路口绕过长直视线限制。
- 对长度 `MaxConsecutiveStraightTiles + 1` 的滑动窗口执行 Ban 或 contradiction。

#### Connected

- 每格展开为“中心 + N/E/S/W 四个方向节点”的 5 节点可能图，表达 Cell 非空与公共边可能开放两层状态。
- Start、Exit、Objective 占格和所有已经被迫非空/必开方向的节点都是 Relevant。
- 多个 Relevant 分量立即产生 contradiction；不含 Relevant 的可能分量被永久删除。
- 对唯一 Relevant 分量运行迭代 Tarjan：连接 Relevant 所必经的中心关节点 Ban Empty，必经方向关节点 Ban 关闭该方向的 Variant。
- 最终全部折叠后，这项约束等价于所有非空 Cell 属于同一连通分量。

每次传播稳定后重算约 24×16×5 节点的可能图，不提前实现增量 Tracker。最终 288 组 Sweep 的规划耗时 P50=23.145 ms、P95=233.470 ms、Max=622.386 ms；若玩家设备验收认为长尾不可接受，再单独评审增量 Tracker。

## 4. WFC 内部状态和失败边界

只增加求解需要的私有纯值状态：

- `FDomainChange`：记录 `CellIndex` 与修改前 Domain，供逆序恢复。
- `FWfcDecision`：记录 Cell、一次性候选顺序、下一个候选和 Trail 起点。
- `FZeroEscapeWfcSolveSettings`：保存 Start、数量范围、连续直线限制和求解预算；不保存 UObject 或玩法对象。
- 私有 contradiction reason：区分 Domain Empty、Connected、Count 和 MaxConsecutive，供统计和诊断。
- `FWfcCollapsedCandidateEvaluation`：只区分 Accept、可回溯 Reject 与不可回溯 Fatal；不演化为通用约束接口。

失败必须分为三类：

1. **输入或代码不变量错误**：非法 Mask、非稠密 Grid、坐标重复、镜像必开边冲突等；不允许回溯掩盖。
2. **当前搜索分支 contradiction**：Domain Empty、Connected、Count、MaxConsecutive 或完整候选路线超限均可恢复，正常进入回溯，不再误报为 invariant violation。
3. **最终失败**：所有候选耗尽为 `NoValidWfcSolution`；工作预算耗尽为 `SolverBudgetExhausted`。

## 5. 可调参数和位置

日常调参继续放在 `/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile`，不在 Solver CPP 中写魔法数字。

### 删除

- `OptionalEnvelopeRadius`
- `MaxOptionalSideBranches`
- `MaxOptionalForwardLinks`
- `ERandomDomain::OptionalLayout`

`EGridCellDomain::Optional` 仍保留，表示“该格可以为空”，它不再表示中央主干周围的小范围 Envelope。

### 新增

| DataAsset 属性路径 | 当前值 | 说明 |
|---|---:|---|
| `SharedRouteConstraints.MinWalkableCellCount` | 48 | 非空 Cell 下限 |
| `SharedRouteConstraints.MaxWalkableCellCount` | 72 | 非空 Cell 上限 |
| `SharedRouteConstraints.MaxConsecutiveStraightTiles` | 4 | 任一轴连续贯通格上限；T/Cross 也计入 |
| `SharedRouteConstraints.MaxWfcCandidateAttempts` | 100000 | 全部 WFC 尝试合计的 singleton 候选总预算 |
| `SharedRouteConstraints.MaxWfcBacktrackCount` | 25000 | 全部 WFC 尝试合计的决策帧恢复总预算 |
| `SharedRouteConstraints.MaxWfcSolveAttempts` | 10 | 最多建立 10 棵确定性搜索树；不增加上述总预算 |
| `SharedRouteConstraints.ObjectiveProgressBandCount` | 3 | 当前产品允许 1–6；六是“最多 12 个目标、每带双 Lane”的首版产品上限，不是 WFC 算法上限；实际可用数量仍按 Grid/房间间距动态校验 |
| `Difficulties[i].WfcShapeWeights.EmptyWeight` | 12000 | 倾向未使用格为空；不是最终空格比例 |
| `Difficulties[i].WfcShapeWeights.DeadEndWeight` | 100 | 终止当前分支的相对权重 |
| `Difficulties[i].WfcShapeWeights.StraightWeight` | 100 | 直线相对权重 |
| `Difficulties[i].WfcShapeWeights.CornerWeight` | 80 | 转弯相对权重 |
| `Difficulties[i].WfcShapeWeights.TJunctionWeight` | 25 | T 字相对权重 |
| `Difficulties[i].WfcShapeWeights.CrossWeight` | 5 | 十字相对权重 |

以上日常参数都在 `/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile` 的 Details 面板调整；Solver CPP 不保存同名魔法数字。当前 288 组结果为 288/288，WFC 搜索树次数 P50=1/P95=3/Max=7，Candidate Attempts P50=341/P95=5954/Max=17193，Backtracks P50=3/P95=5005/Max=15009。

### 保留并调整职责

- `MaxRequiredRouteLengthTiles`：验证当前 Flow 的最短完整通关路线。
- `MaxRequiredRouteExtraTiles`：限制 K-of-N / CollectAll 相对直达 Exit 的额外成本。
- N/K、进度带和 Objective 数量继续位于各 Difficulty / Flow 配置。
- `FZeroEscapeWfcShapeWeights` 移入每个 Difficulty 条目；三个难度必须保持相同 `EmptyWeight` 与非空 Variant 总权重，只重新分配 DeadEnd/Straight/Corner/T/Cross 的比例。Hard 可提高 Corner/T/Cross、降低 Straight，但不能靠提高总体非空倾向明显扩大地图。

## 6. 文件级代码改动

### 修改 Runtime 契约和配置

#### `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h`

- 删除报告中的 Required/Optional/Pruned 路线计数。
- 增加 `WalkableCellCount`、`WfcSolveAttemptCount`、`WfcCandidateAttemptCount`、`WfcBacktrackCount`、五类 contradiction 计数与 `WfcCollapsedCandidateRejectionCount`。
- 增加 `NoValidWfcSolution` 与 `SolverBudgetExhausted`，保留真正的 invariant failure。

#### `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h`

- 删除三项旧 Optional 参数。
- 增加 Count、MaxConsecutive、两个总预算字段和 `MaxWfcSolveAttempts`。
- 将 WFC 形态权重移入 Difficulty 配置。

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp`

- 校验 `0 < MinWalkable <= MaxWalkable <= GridCells`。
- 校验最大数量至少能容纳 Start、Exit 和当前 Flow 的 Objective 占格。
- 校验 MaxConsecutive 与两个测量预算为正。
- 校验三个难度具有相同 Empty 权重与非空 Variant 总权重；难度只能重新分配非空形态比例。
- 在 Profile 阶段校验 Empty + 全部非空 Variant 总权重不超过 `MAX_int32`，避免把可诊断资产错误推迟到 Solver。
- 删除固定中央主干容量、Objective Gate 和旧构造性可解证明。

现有 `DA_LevelGenerationProfile` 在资产检查点原子迁移：顶层权重复制进三个 Difficulty、新字段写入上表值、`ProfileVersion` 递增到 4；首轮三个难度先保持相同权重，玩家路线验收后再调形态比例。

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationCore.h`

- 删除 `OptionalLayout` 随机域与两个 Optional 分支解析字段。
- Resolved Difficulty 携带该难度的 WFC 形态权重。

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationCore.cpp`

- 更新 Snapshot、Hash 和配置解析。
- 递增算法版本。
- 必须保留当前并行修改中的 `FailCore` Unity Build 修复，不覆盖素材迁移对话的改动。

### 修改 Grid 与 WFC

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.h`

- 删除旧 Optional 请求字段。
- 传入全图 WFC 约束设置和预算。

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.cpp`

- 用固定地标约束初始化替换固定骨架雕刻。
- 删除 `BackboneY`、`GateEdges`、`CarveOrthogonalRoute`、固定主干职责的 `CarveRequiredSkeleton`、`BuildOptionalEnvelope` 和 `PruneDisconnectedOptional`。
- `AddRequiredClosedEdge` 若删除旧逻辑后无调用，一并删除。
- 删除对 WFC 输入校验的重复调用。
- 保留 Progression 地标、2×2 房内开口、Region/Anchor、K-of-N bitmask DP、最终 BFS、路线长度、Junction 指标和 300/600 cm 结构展开。
- 最终独立验证增加非空 Cell 数量和水平/垂直轴向贯通上限复核；违反表示 Solver 接受了非法结果，按 invariant/Fatal 处理。
- 更新旧“骨架/剪枝”错误文本，删除算法后日志不得继续声称“剪枝丢失”或“必达骨架”。
- 不再通过事后剪掉孤岛修补结果；孤岛必须在 Connected 约束或最终验证中失败。

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcSolver.h`

- 删除“完整状态集不需要回溯”的旧契约和注释。
- 删除对外的 `ValidateGuaranteedSolvableConstraints`；初始输入校验收敛为 CPP 内部 `BuildAndValidateDenseConstraintView`，一次完成稠密视图与静态约束校验。
- 扩展 `Solve` 的约束设置与结果统计；Trail 和决策栈不进入 Public API。
- 在建立 Domain/BFS/随机观察前一次性执行输入、Variant、Solve Settings 和 Start=Required 校验；不得把 Start 越界或调用方契约错误误报为普通无解。

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcSolver.cpp`

- 保留 16 OpeningMask、Shannon entropy、加权观察和局部邻接传播。
- 删除旧构造性见证、`Required` 必须预开一条边的前提和无回溯主循环。
- 所有 Domain 修改统一经过可记录 Trail 的入口。
- 实现有界 chronological backtracking。
- 完整折叠后调用一次 Grid 提供的窄候选验收；Reject 进入同一回溯路径，Fatal 立即报告不变量错误。
- 稠密约束视图每次 Solve 只构建一次；静态约束校验、Domain 初始化和 Start 契约都复用它。
- `NarrowDomain` 的内部索引用 `check`；由于当前 `Solve` 接口允许任意 Variant 数组，16 Variant 顺序/权重仍做一次运行时入口校验，避免静默改变 Domain bit 语义。
- 删除导出后的重复局部逐边热路径复核；等价回归由 WFC 单测负责，产品最终复核只在 Grid `ValidateFinalPlan`。

### 新增一对私有约束文件

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcConstraints.h`

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcConstraints.cpp`

- 只承载 Connected、Count、MaxConsecutive 三项具体纯值约束及最小结果枚举。
- 不建立 `IConstraint`、注册表、反射对象或每约束一个文件。
- Solver 负责观察、传播、Trail 和回溯；约束文件只读取/缩小 Domain 并报告 contradiction。

### 修改 Runtime 实例化与日志

#### `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp`

- 从 Snapshot 传递五项新参数与当前 Difficulty 权重。
- 更新 `ZE_PCG_RESULT schema=4`，记录 Walkable、SolveAttempts、CandidateAttempts、Backtracks、五类 Contradictions 和 CollapsedCandidateRejections。
- 不加入测试传送、窗口模式或 HydroLab 硬编码路径。

### 测试文件组织

当前测试文件在新增三约束和回溯用例后会接近 1800 行，因此实施时只做一次有实际收益的拆分：

- `D:/UE5projects/Demo/Source/Demo/Private/PCG/Tests/ZeroEscapeWfcSolverTests.cpp`
  - OpeningMask、输入校验、三项约束、真实回溯、无解、预算和确定性。
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/Tests/ZeroEscapeGenerationPipelineTests.cpp`
  - Core、Profile、Progression、Grid、结构展开、Seed Sweep 和项目资产烟测。

原 `ZeroEscapeGenerationTests.cpp` 的测试迁入这两个文件后删除；不保留转发壳。除非实现时确实出现跨文件重复，首版不新增 Fixture 头文件。该目录有两个 CPP，不会形成“一层文件夹只装一个 CPP”的琐碎结构。

素材迁移对话已经修改旧测试文件中的 HydroLab 路径；必须等迁移交接后再串行拆分，并保留 `/Game/Assets/SciFiHydroLab` 的实际新路径。

## 7. 测试代码的长期边界

### Automation 测试：长期保留

纯算法自动化测试受 `WITH_DEV_AUTOMATION_TESTS` 保护，不属于 GameFlow，也不应因为功能完成就全部删除。它们负责防止确定性、连通、回溯、预算、结构展开和资产契约回归。

本轮删除或改写的只是旧假设：

- `ConstructiveConstraintContracts` → 初始输入约束测试。
- `WeightedCollapseNeedsNoBacktracking` → 真实回溯、候选恢复和预算测试。
- `RouteRoomConnectivityAndPrune` → 全图 Connected、数量、最终路线和无事后剪枝测试。

确定性、随机域隔离、K-of-N、Seed Sweep、结构展开和项目资产烟测继续保留。

### Runtime Harness：临时保留，最终删除类而不是整类合并

`AZeroEscapeRuntimeGenerationTestHarness` 只负责独立测试关卡中的：

- 触发首局生成；
- 等待 Generator Ready；
- 从 Staging 安全传送至生成 Start；
- 手动重生成前回到 Staging；
- 输出测试诊断。

本轮不扩大 Harness 职责，也不把它塞进 Runtime Generator。未来正式 GameFlow 只接管产品真正需要的“提交生成请求、Ready 后开始单局、把玩家出生在 Start、必要的重生成过渡”；Staging、手动测试入口、Pawn 轮询和测试日志不迁入正式流程。

Harness 的退役门禁：

1. 主游戏关卡完成同等 Start→Exit、碰撞、净空和重生成边界验收。
2. 正式 GameFlow 已接管生成请求、Ready 事件和玩家出生。
3. UE 引用审计确认 Harness 只剩测试 Map / Debug Blueprint 引用。
4. 打包 Map 白名单排除 `L_PCG_RuntimeTest` 和 `/Game/ZeroEscape/Generation/Debug`。
5. 取得单独删除授权后，删除 Harness C++、Debug Blueprint 和不再需要的测试装配。

短期不为三周 Demo 新建 Developer/Test 模块。若正式流程迟迟未接管，打包前至少用 Map 白名单排除测试资产；Harness 原生类即使暂时编译进 Runtime，也必须保持无生产引用、无自动运行和无 Tick。

## 8. PIE 窗口模式修正

浮动小窗口来自本机保存的：

`LastExecutedPlayModeType=PlayMode_InEditorFloating`

本轮已将本机设置改为：

`LastExecutedPlayModeType=PlayMode_InViewPort`

后续所有由本地 MCP 发起的人工 PCG 验收都显式使用：

`editor.start_pie(mode = SelectedViewport)`

这项修正不修改 PCG、Harness 或 GameFlow 代码。Automation 测试继续在自动化环境运行，不以打开 PIE 窗口作为测试方式；玩家视觉、碰撞和走通验收才使用主编辑器视口。

## 9. 并发修改与资产边界

素材迁移与追猎者并行成果已经纳入实施前快照；本轮从该快照继续，严格限定 PCG 文件范围：

- `ZeroEscapeGenerationCore.cpp` 有并行 Unity Build 名称修复，必须保留。
- `ZeroEscapeGenerationTests.cpp` 有五项 HydroLab 新路径修改，必须保留。
- `DA_Presentation_SciFiHydroLab.uasset` 已随素材目录迁移更新引用；本轮核心 WFC 不碰它。
- `Level0.umap`、`Demo.Build.cs` 和追猎者文件不属于本轮 WFC 范围。
- 旧 `Content/SciFiHydroLab` 与新 `Content/Assets/SciFiHydroLab` 的移动由另一个对话完成，本任务不删除、不回滚、不二次移动。

实施前已重新暂存并核对上述并行改动，提交 `b8a8c6e` 后本地与远端一致。后续若这些共享文件再次出现任务外修改，停止覆盖并重新核对。

## 10. 实施检查点

### 检查点 0：冻结新基线

- [x] 核对并保留 Core、Tests、Presentation、追猎者与素材迁移并行改动。
- [x] 重新全量暂存，提交并推送 `b8a8c6e` 到内部工蜂。
- UE 关闭时做一次完整 `DemoEditor Win64 Development` 基线构建。
- 本检查点不接受“覆盖后再补回”的合并方式。

### 检查点 1：契约与配置原子切换

- 修改 Types、Assets、Core、Grid/WFC 调用签名和测试编译骨架。
- 删除旧 Optional 配置和固定骨架符号，不保留双轨逻辑。
- 完整构建通过后再进入求解器实现。

### 检查点 2：回溯与三约束

- 先以小 Grid 证明至少一次真实回溯后仍可解。
- 分别接入 Count、MaxConsecutive、Connected。
- 每项至少覆盖“产生 Ban 后成功”“当前分支 contradiction 后回溯成功”“明确无解”边界。
- 验证 Trail 全量恢复、失败候选不会再次尝试、同 Seed 指标可复现。
- 独立验证根传播产生的 singleton/Ban 在清空 Root Trail 后仍保留；首个决策矛盾应返回 `NoValidWfcSolution`，不得恢复根状态或死循环。
- 独立验证传播在没有新增决策帧时直接形成完整叶子；候选验收 Reject 后只能退到真实覆盖该叶子的最近决策，若根本没有可退决策则稳定报告 `NoValidWfcSolution`。
- 对已知会触发 Count/Max Ban 的失败分支，比较“回溯后的输出”与“输入预先排除该失败候选后的干净求解”，两者必须一致。
- 验证首个完整候选路线超限时会继续回溯并接受后续候选，而不是让合法 Seed 在 Solver 外直接失败。

### 检查点 3：删除固定路线并切换全图 WFC

- 原子删除固定主干、Gate、Envelope 和剪枝代码。
- 接入 Objective 2×2 房内约束和全图 WFC。
- 最终 BFS、Count、连续贯通、K-of-N 与路线长度验证全部通过。

### 检查点 4：自动化与预算测量

- [x] UE 5.8 `DemoEditor Win64 Development` 完整构建成功。
- [x] 全部 `Demo.PCG` 自动化测试通过。
- [x] 288 组 Difficulty × Flow × Seed Sweep 为 288/288；已记录生成耗时、Solve Attempts、Candidate Attempts、Backtracks、Contradictions、Walkable 与完整候选拒绝数的 P50/P95/Max。
- [x] 保留 `MaxWfcCandidateAttempts=100000`、`MaxWfcBacktrackCount=25000` 作为整局硬上限；实际 Max 分别为 17193/15009。有限尝试上限为 10，实际 Max 为 7。

若正常配置出现重复预算失败，先分析约束、参数或实现；不允许恢复固定路线、修改请求 Seed、增加未记录重试或隐藏降级。

### 检查点 5：室内灯独立增量

- 路线纯值测试稳定后，再在 Presentation / Runtime 实例化层增加稀疏室内灯。
- 灯光不进入 WFC Domain，也不改变拓扑 Hash。
- 等素材迁移完成并回读真实 Presentation 引用后才修改对应 DataAsset。

### 检查点 6：主视口玩家验收

- [x] MCP 显式使用 `SelectedViewport` 启动 `L_PCG_RuntimeTest`；PIE 状态为 Running、非 Simulate，`ZE_PCG_RESULT schema=4 success=1` 与 Harness 传送断言通过，随后已正常停止。
- 抽查至少 10 个固定 Seed 的鸟瞰与玩家视角。
- 检查长直路、路线丰富度、T/十字/转弯自然出现、折返、碰撞、净空、接缝、照明和 Start→Exit 实际走通。
- 自动化、日志或成功生成不能替代这一步。

## 11. 停止条件

出现任一情况，停止扩写并重新评审：

- 核心路线重构需要引入通用约束框架、第二套生成器或素材专用拓扑分支。
- 需要覆盖素材迁移或追猎者对话的未交接改动。
- 新 WFC 核心净增明显超过本轮约束、回溯和测试所需范围。
- Seed Sweep 的 P95/Max 明显不能满足实时生成，却只能靠放宽到失去约束意义解决。
- 为了通过测试需要修改请求 Seed、预刻路线、事后剪孤岛、突破总预算或加入未记录重试。

## 12. 本轮授权边界

用户已经明确授权：先保存并推送当前快照，再按最新代码审计修订方案/拟实现代码并开始落盘。本轮允许修改本计划列出的 PCG C++、项目自有 Profile DataAsset 与 PCG Automation；不据此扩大到第三方 HydroLab 素材、Level0、追猎者、正式 GameFlow 或灯光资产。

拟实现代码评审稿已写入：

`claude/docs/2026-07-25-pcg-wfc-route-refactor-code-proposal.md`

代码稿已补上最新审计要求的三个回溯边界、单次稠密视图、最终验收唯一入口、内部不变量分层、最低 Shannon 熵观察与总预算内有限重试。检查点 0 的 Git 快照、源码、真实 Profile 迁移、构建、19 项 Automation、288 Seed Sweep 和 SelectedViewport PIE 技术烟测均已完成；仍须玩家完成主观观感、碰撞、导航与实际走通验收后才能标记功能完成。
