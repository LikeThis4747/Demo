# PCG 路线生成修正讨论稿

> 状态：2026-07-25 PIE 视觉验收后形成的讨论稿；最新独立审计已判定方案可行并允许进入 DailyPlan，但尚未获得新一轮源码和 UE 资产修改确认。
>
> 本稿只修正路线生成、WFC 失败处理、可调参数和室内测试照明；不实现追猎者、陷阱、收集交互或胜负流程。
>
> `SciFiHydroLab` 的目录移动由另一个对话独立处理，不属于本稿范围。本稿不检查、不移动素材，也不冻结旧或新包路径；后续实施前只以 Presentation DataAsset 的实际引用回读结果为准，拓扑求解代码不得依赖第三方素材路径。

## 1. 本次验收推翻的结论

当前 V3.2 虽然通过了构建、自动化和 Seed Sweep，但玩家视角暴露出两个不能接受的问题：

1. Start 与 Exit 之间先被代码雕刻为贯穿地图中央的固定横线，WFC 无权改变主路线，所以长直路是必然结果。
2. Normal 难度只开放 7 个 Optional 候选格给 WFC，且 Empty/Straight 权重过高，最终只在 24 个必经格之外增加约 3 个非空格，路线明显过少。

因此，以下旧结论不再作为后续实现依据：

- 不再保留固定中央主路线。
- 不再把 WFC 限制在主路线附近的小范围 Optional Envelope。
- 不再用项目内部的“先走 X、再走 Y”辅助函数代替路线生成。
- 旧函数名 `CarveOrthogonalRoute` 只代表一个简单实现细节，不是公开算法名，也不得在方案或答辩中包装成算法。

## 2. 名称和引用规则

方案只把下列有明确来源的内容称为算法或约束：

- Wave Function Collapse（WFC）及最小熵观察、约束传播；
- Constraint Satisfaction Problem（CSP）中的 chronological backtracking（按决策顺序回溯）；
- DeBroglie 的 `ConnectedConstraint`、`LoopConstraint`、`MaxConsecutiveConstraint`、`CountConstraint`；
- Breadth-First Search（BFS）最终连通验证；
- 备选房间图方案中的 Delaunay triangulation、Minimum Spanning Tree（MST）和额外边；
- 只在“连接已经选定的两个位置”时使用 A*，不把 A* 说成地图生成算法。

下面这些属于本项目的配置或玩法规则，不称为新算法：

- Start/Exit 的边界位置范围；
- 最长连续直线格数；
- 非空格数量范围；
- 不同难度的形态权重；
- Objective 的 N/K 和进度分布。

## 3. 采用的成熟参考

### 3.1 原始 WFC

Maxim Gumin 的原始实现说明：传播可能使某个 Cell 的全部候选归零，此时发生 contradiction，算法不能继续。Karth 与 Smith 对 WFC 的分析也明确把原始 WFC 描述为 non-backtracking greedy search。

- [Maxim Gumin / WaveFunctionCollapse](https://github.com/mxgmn/WaveFunctionCollapse)
- [Karth & Smith: WaveFunctionCollapse is Constraint Solving in the Wild](https://escholarship.org/uc/item/1f29235t)

结论：无回溯是原始 WFC 的一种实现，不代表加入复杂全局约束后仍应坚持无回溯。

### 3.2 DeBroglie / Tessera

DeBroglie 是公开的 WFC 库，具有完整回溯和非局部约束支持；其路径约束文档明确提供：

- `ConnectedConstraint`：保证相关 Cell 之间存在合法路径；
- `LoopConstraint`：保证至少两条独立路径；
- `MaxConsecutiveConstraint`：限制某类 Tile 沿坐标轴连续出现的数量；
- `CountConstraint`：限制某类 Tile 的最少/最多数量；
- Path Constraint 通常需要回溯才能得到较好的结果。

参考：

- [DeBroglie GitHub](https://github.com/BorisTheBrave/DeBroglie)
- [DeBroglie Constraints](https://boristhebrave.github.io/DeBroglie/articles/constraints.html)
- [DeBroglie Path Constraints](https://boristhebrave.github.io/DeBroglie/articles/path_constraints.html)
- [DeBroglie Backtracking](https://boristhebrave.github.io/DeBroglie/articles/developing.html#backtracking)
- [DeBroglie `TilePropagatorOptions.cs`](https://github.com/BorisTheBrave/DeBroglie/blob/master/DeBroglie/TilePropagatorOptions.cs)
- [Tessera Quality, Backtracking and Path Constraints](https://www.boristhebrave.com/docs/tessera/6/articles/quality.html)

本项目是 UE C++ Runtime，不直接引入 C# 库；只参考其已经公开验证的约束职责和回溯方法，再用现有 16 个 OpeningMask 实现等价能力。

### 3.3 房间图备选方案

成熟的房间型地牢方案常先生成房间位置，用 Delaunay triangulation 建候选连接，再由 MST 保证全图连通，并从候选边中加回少量额外边形成环路。Jim Whitehead 的 PCG 2020 论文明确采用了这一流程。

- [Spatial Layout of Procedural Dungeons Using Linear Constraints and SMT Solvers](https://pcgworkshop.com/archive/whitehead2020spatial.pdf)

该方案适合未来“完整房间是主要单位”的版本。当前素材和逻辑状态已经是墙/地板/开口格，首选仍是 WFC 加全局约束，不立即切换到房间图，也不同时实现两套生成器。

## 4. 推荐修正方案

### 4.1 WFC 的生成范围

1. 删除固定中央主路线及其 X/Y 两段路径函数在主路线中的职责。
2. 除边界禁用区、固定房间占格和明确保留区外，让整个可玩 Grid 进入 WFC；Empty 仍是合法状态。
3. Start 与 Exit 使用固定 Cell 约束放在地图相对两侧，但不预先连接它们。
4. Objective 房间以后作为固定的相关 Cell/区域加入约束；其位置可由流程层决定，但不预先雕刻通路。

### 4.2 连通约束

参考 DeBroglie `ConnectedConstraint`：

1. OpeningMask 的 N/E/S/W 开口就是 `EdgedPathSpec` 所需的有向边信息。
2. 每次 WFC 传播后，用当前 Domain 构建“仍可能连通”的保守视图：相邻两个 Cell 的 Domain 都还包含朝向彼此的开口候选时，该边才算可能开放。
3. Relevant Cell 包括 Start、Exit、Objective，以及 Domain 已经不再包含 Empty 的所有 Cell。用 BFS 检查它们是否仍处于同一个可能连通分量。
4. 若某次选择已经使任意必要点不可能连通，则判定 contradiction，交给 WFC 回溯，而不是另画一条路补救。
5. 最终结果再次用确定状态的开口边执行 BFS；Start、Exit、Objective 和全部非空路线 Cell 必须处于同一连通分量。

这里采用的是安全但较轻的约束传播：可能图不连通时一定无解；可能图仍连通只表示“尚未证明无解”，不保证当前分支最终可解，剩余冲突由后续传播和回溯处理。首版不复制 DeBroglie 更复杂的内部 tracker/AC4 实现。

首版不启用 `LoopConstraint`。T 字、十字和环路先通过合法状态、数量范围和权重自然产生；只有 Seed Sweep 和玩家样本持续显示树状长回头路时，才单独评估是否增加 Loop Constraint。

### 4.3 限制长直路

参考 DeBroglie `MaxConsecutiveConstraint`：

- 对 X 轴统计所有同时具有 East/West 开口的状态；
- 对 Y 轴统计所有同时具有 North/South 开口的状态；
- 连续数量不得超过 `MaxConsecutiveStraightTiles`；
- Straight、T 和 Cross 只要在该轴保持贯通，都计入连续长度，避免用一个路口状态绕过限制。
- 实现时对每行/列检查长度为 `MaxConsecutiveStraightTiles + 1` 的滑动窗口：窗口内全部 Cell 都已被迫沿该轴贯通时产生 contradiction；只差一个 Cell 时，从该 Cell 的 Domain 中删除会继续贯通该轴的候选。

这是一项硬约束，不是提高 Corner 权重的概率性补丁。首轮候选值为 4 格，但必须通过 PIE 尺度检查后确认；600 cm Tile 下 4 格已经是 24 米。

### 4.4 控制路线数量

参考 DeBroglie `CountConstraint`，对非 Empty 状态设置最少/最多数量：

- `MinWalkableCellCount` 防止再次生成“一条主线加三格支路”；
- `MaxWalkableCellCount` 防止整张地图被走廊填满并显著延长单局时间；
- 每次检查同时统计“已经确定为非 Empty”的数量和“仍可能为非 Empty”的数量：前者超过上限或后者低于下限都立即产生 contradiction；达到上限时在其余 Cell 禁止非 Empty，恰好只剩下限数量可选时在这些 Cell 禁止 Empty；
- 首轮 24×16 Grid 的候选范围为 48～72 个非空格，只作为第一轮灰盒测试值，不在代码中写死；
- Easy/Normal/Hard 首版共用接近的数量范围，难度主要通过 Corner/T/Cross/DeadEnd 权重改变复杂度，避免 Hard 单纯扩大地图或延长路线。

不设置“每局必须有一个 T 或一个 Cross”的配额。具体路口允许为零，但批量结果不能长期退化为只有直线。

### 4.5 WFC 回溯

当前 `ZeroEscapeWfcSolver` 没有任何回溯：选择一个 Variant 后立即传播，出现空 Domain 就直接失败。旧方案能够这样做，是因为完整 16 Mask 加固定主路线使局部约束几乎总有构造性解；新方案加入连通、数量和连续长度这些全局约束后，这个前提不再成立。

因此，本次修正应加入有界 chronological backtracking：

1. 每个决策保存 Cell、按当前权重一次性生成的候选尝试顺序、下一个候选位置和 Trail 起点；回溯不重新抽随机数。
2. 每次 Domain 删除写入 change trail；回溯时按逆序恢复到决策前状态。
3. 当前 Variant 导致局部传播或全局约束 contradiction 时，恢复并尝试同一决策的下一个 Variant。
4. 当前决策候选耗尽才退到上一层。
5. 使用总求解步数和回溯步数硬预算；预算耗尽返回明确失败，不偷偷换算法、补路或降级成固定主线。

这与 DeBroglie 记录 Wave/Domain 变化并逆序撤销的做法一致。不会恢复旧 Socket、Portal、A* 或 5000 行通用 Solver，只在现有纯值 WFC 中实现本次需要的回溯职责。

首版明确采用 DeBroglie `BacktrackType.Backtrack` 对应的普通按决策回溯，不实现 `Backjump`。当前 24×16、每格最多 16 个候选的规模没有证据需要更复杂的冲突归因；若以后普通回溯的 P95/Max 明显超预算，再单独评估 backjumping。首版也不自动全局重启；若 Seed Sweep 证明预算耗尽不是个例，再参考 Tessera 的 Step Limit + fresh generation 做确定性重试方案评审，而不是提前加入隐藏降级。

回溯预算的初值不凭感觉冻结。先用 24×16 Grid 跑至少当前的 288 组 Difficulty/Flow/Seed，并记录 Backtrack P50/P95/Max、总步数、耗时和失败原因，再把能覆盖正常样本且仍满足实时生成的值写入正式计划。

### 4.6 与 DeBroglie/Tessera 的实现对应关系

本项目不是移植 C# 库，也不声称复刻 DeBroglie 的全部求解能力；只把成熟实现中与本项目直接对应的职责落到现有 UE C++ 纯值求解器中：

| 成熟实现中的概念 | 当前项目已有基础 | 计划中的最小对应实现 |
|---|---|---|
| `Wave` / 每格 Domain | `TArray<uint16> Domains`，16 bit 对应 OpeningMask 0..15 | 保留表示，不改成对象化 Tile Catalog |
| `Ban` / `Select` | 邻接传播会缩小 Domain，观察会选定单个 Variant | 所有 Domain 修改统一走一个可记录 Trail 的入口 |
| Wave change log | 当前没有 | 私有 `FDomainChange { CellIndex, PreviousDomain }`，逆序恢复 |
| Backtrack decision | 当前没有 | 私有 `FWfcDecision { CellIndex, CandidateOrder, NextCandidateIndex, TrailStart }` |
| `BacktrackType.Backtrack` | 当前 contradiction 直接失败 | 普通 chronological backtracking；不做 `Backjump` |
| `ConnectedConstraint` + `EdgedPathSpec` | OpeningMask 已包含四向开口 | 对 Domain 的可能开口图做 BFS 可行性检查 |
| `CountConstraint` | 只有最终 Optional/Required 计数 | 对非 Empty 的已确定数/可能数做上下界传播 |
| `MaxConsecutiveConstraint` | 只有最终 Junction 指标 | 对 X/Y 贯通候选做滑动窗口 Ban/contradiction |
| Step/Backtrack limit | 当前只有外层生成耗时记录 | DataAsset 中的确定性工作量预算和结构化失败原因 |

三个全局约束不会被包装成通用 `ITileConstraint` 插件系统。首版只有这三个稳定需求，直接使用私有纯值函数和一个私有约束设置结构即可；如果以后确实出现第四、第五类可插拔约束且重复代码明显，再讨论抽象接口。

### 4.7 一次求解循环

1. Grid 层创建全图初始 Domain：边界禁止朝地图外开口；Start/Exit/Objective 房间禁止 Empty；房间内部共享边要求开放；其余可玩 Cell 允许 Empty 和合法 OpeningMask。
2. WFC 执行初始邻接传播，再依次运行 Connected、Count、MaxConsecutive 检查；约束产生的 Ban 继续进入同一传播队列，直到稳定。
3. 若稳定且未全部坍缩，按最小熵选择 Cell，保存决策帧，按权重候选顺序尝试一个 Variant。
4. 任意 Domain 变空、全局约束证明不可能或最终检查失败时，回滚到最近仍有候选的决策并继续。
5. 全部 Cell 坍缩后执行确定开口 BFS、非空数量、最长贯通段和当前 Flow 的最短完成路线验证；通过后才导出 Plan。

Start/Exit/Objective 只固定“必须存在且必须连通”的语义，不预刻它们之间的道路。Objective 房间保留 2×2 内部结构，但房间外周不再固定连接中央主干；至少一个对外连接由连通约束自然产生。

## 5. 当前已有可调参数和调节位置

日常调参只修改 UE DataAsset 或关卡 Actor，不修改 C++ 默认值。C++ 文件只是属性声明和校验位置。

### 5.1 单局请求

调节位置：打开 `/Game/Levels/L_PCG_RuntimeTest`，在 World Outliner 选择 `BP_ZeroEscapeRuntimeLevelGenerator` 实例，展开 `Details > PCG > Default Request`。

| 参数 | 当前值 | 作用 |
|---|---:|---|
| `Seed` | 12345 | 同配置下决定本局随机结果 |
| `Difficulty` | Normal | 选择 Easy/Normal/Hard 参数组 |
| `FlowProfileId` | EscapeOnly | 选择 EscapeOnly/CollectAll/CollectKOfN |

C++ 声明：`Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h` 的 `FZeroEscapeGenerationRequest`。

### 5.2 Generation Profile

调节位置：Content Browser 打开 `/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile`。

| 分类 | 参数 | 当前值 | 调高/调低的主要影响 |
|---|---|---:|---|
| Grid | `GridSize` | 24×16 | 增加可用范围；旧方案会让横向固定路更长，新方案中只扩大搜索空间 |
| Grid | `LogicalTileSizeCm` | 600 cm | 改变世界尺度，不改变逻辑拓扑；当前素材契约不应随意改 |
| Grid | `RoomSizeTiles` | 2 | Objective 房间边长为 2×2 逻辑格 |
| Progression | `ObjectiveProgressBandCount` | 3 | Objective 在流程上的分段数量 |
| Grid | `OptionalEnvelopeRadius` | 1 | 旧方案 Optional 范围；新方案计划删除 |
| Route | `MaxRequiredRouteLengthTiles` | 64 | 旧必经路线长度上限；新方案改为最终最短可行路线验证 |
| Route | `MaxRequiredRouteExtraTiles` | 24 | Objective 相对直达路线允许增加的成本；是否保留待新流程验证 |
| Gameplay | `GameplayAnchorHeightCm` | 100 cm | Start/Exit/Objective Anchor 相对地板高度 |

难度数组当前值：

| 难度 | `MaxOptionalSideBranches` | `MaxOptionalForwardLinks` | N | K |
|---|---:|---:|---:|---:|
| Easy | 1 | 0 | 2 | 1 |
| Normal | 2 | 1 | 3 | 2 |
| Hard | 4 | 2 | 4 | 3 |

其中前两项只服务旧固定骨架和 Optional Envelope，新方案计划删除；N/K 保留。

WFC 形态权重当前值：

| 参数 | 当前值 |
|---|---:|
| `EmptyWeight` | 150 |
| `DeadEndWeight` | 15 |
| `StraightWeight` | 100 |
| `CornerWeight` | 80 |
| `TJunctionWeight` | 25 |
| `CrossWeight` | 5 |

C++ 声明和校验位置：

- `Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h`
- `Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp`

### 5.3 Presentation Profile

调节位置：Content Browser 打开 `/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SciFiHydroLab`。

当前可调的是 300 cm 结构单元、地板/墙/天花板高度、Mesh Binding、PivotCorrection、碰撞和导航设置；这些只影响表现，不改变路线。

当前 PCG 没有室内光源参数。`L_PCG_RuntimeTest` 中的 `PCG_TestSun`、`PCG_TestFillLight`、`PCG_TestSkyLight` 只是关卡测试灯，不会随生成路线分布。

## 6. 新方案需要新增或调整的公开参数

以下是计划中的 DataAsset 字段，不是已经存在的属性；用户确认代码预览后才落盘。

### 6.1 `/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile`

| 分类 | 参数 | 首轮候选值 | 作用 |
|---|---|---:|---|
| Route | `MinWalkableCellCount` | 48 | Count Constraint 的非空格下限 |
| Route | `MaxWalkableCellCount` | 72 | Count Constraint 的非空格上限 |
| Route | `MaxConsecutiveStraightTiles` | 4 | Max Consecutive Constraint；所有难度都应避免长直路 |
| Solver | `MaxSolveSteps` | Seed Sweep 后确定 | 整次 WFC 的硬工作预算 |
| Solver | `MaxBacktrackSteps` | Seed Sweep 后确定 | 回溯硬预算；不是游戏难度参数 |

调整方式：以后都在该 DataAsset 的 `Route`/`Solver` 分类中调，不在 `ZeroEscapeWfcSolver.cpp` 里改魔法数字。

现有 `MaxRequiredRouteLengthTiles` 和 `MaxRequiredRouteExtraTiles` 保留，不再另造只检查 Start→Exit 的重复参数。前者继续限制当前 Flow 的最短完整通关路线，后者继续限制 K-of-N/CollectAll 相对直达 Exit 的额外成本；现有 bitmask DP 可直接用于最终 WFC 结果。

WFC 形态权重计划从 Profile 顶层移动到各 Difficulty 条目中，使 Hard 可以提高 Corner/T/Cross、降低 Straight，而不用扩大 Grid 或提高总路线格数。具体默认值要通过第一轮可视 Seed 样本确定，不直接把当前 150/100/80/25/5 当成新方案默认值。

### 6.2 `/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SciFiHydroLab`

为了让运行时生成的封闭场景可见，计划增加最小室内灯配置：

| 分类 | 参数 | 初值决定方式 | 作用 |
|---|---|---|---|
| Lighting | `InteriorLightSpacingTiles` | 先在 PIE 实测 | 每隔多少非空格放置一个灯，不参与 WFC |
| Lighting | `InteriorLightHeightCm` | 按 305 cm 天花板实测 | 灯相对地板高度 |
| Lighting | `InteriorLightIntensityLumens` | PIE 曝光实测 | 室内灯亮度 |
| Lighting | `InteriorLightAttenuationRadiusCm` | PIE 覆盖实测 | 控制覆盖范围和重叠数量 |

灯光属于 Presentation/实例化阶段，不增加 OpeningMask 状态。首版只用一种项目自有室内灯规则，不建立灯具 Catalog 或风格系统。

## 7. 拟修改文件、代码增量与减量

以下数字是基于 2026-07-25 当前源码的设计估算，用于防止范围膨胀，不是假装已经产生的 diff。独立审计按其统计口径实测当前 PCG 为 6713 行；下表中的旧行数是讨论稿形成时的本地物理行统计，两种口径不用于判断功能完成。实际实现允许约 ±20% 偏差；若核心路线改动净增超过约 950 行，或需要额外通用框架，停止实现并重新评审。

### 7.1 核心路线求解改动

| 完整路径 | 当前行数 | 预计删除 | 预计新增 | 主要变化 |
|---|---:|---:|---:|---|
| `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h` | 441 | 15～25 | 30～45 | 删除 Optional/Pruned 指标；增加非空格、求解步数、回溯和最终失败诊断 |
| `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h` | 235 | 15～30 | 35～55 | 删除 Optional 骨架参数；增加 Count/MaxConsecutive/预算字段，并把形态权重放入 Difficulty |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp` | 318 | 35～60 | 55～80 | 删除固定骨架容量推导；增加新参数交叉校验和逐难度权重校验 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationCore.h` | 132 | 3～8 | 3～8 | 删除 `OptionalLayout` 随机域和两项 Optional 上限；解析逐难度权重 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationCore.cpp` | 440 | 10～25 | 25～45 | 快照、解析和 Hash 改用新参数；不增加第二套路由生成器 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.h` | 125 | 10～20 | 10～20 | 删除 Optional 请求字段；传入全图 WFC 约束设置 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.cpp` | 1484 | 300～380 | 150～220 | 删除固定中央主干、X/Y 雕路、Optional Envelope/剪枝；保留地标嵌入、2×2 房内约束、K-of-N DP、最终验证和结构展开 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcSolver.h` | 84 | 0～10 | 25～45 | 扩展 Solve 输入/结果；求解器内部 Trail 不暴露为公共 API |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcSolver.cpp` | 809 | 40～90 | 220～340 | 统一 Domain 修改入口，加入决策栈、Trail、普通回溯和预算；保留最小熵、加权选择、邻接传播 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcConstraints.h` | 新文件 | 0 | 90～140 | 私有纯值约束设置、结果和函数声明；不建新子目录、不建通用插件接口 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcConstraints.cpp` | 新文件 | 0 | 260～420 | Connected、Count、MaxConsecutive 三项检查和 Ban 计算 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp` | 562 | 15～30 | 20～45 | 删除 Optional 参数传递/日志，传递逐难度权重和新约束预算 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationTests.cpp` | 1201 | 220～340 | 420～620 | 删除固定主干/Optional/无回溯断言；新增三约束、真实回溯、预算、确定性和 Seed Sweep 诊断 |

审计指出旧“无回溯必然可解”的构造性见证和 WFC 导出后的重复逐边复核还可额外删除约 80～100 行，因此完成后的净增应低于原先 450～950 行估算的上沿。新增量主要来自约束实现和测试，不来自兼容层、Socket、A* 或第二套生成器。

新增一对私有 `ZeroEscapeWfcConstraints` 文件是有意的职责拆分：三类约束都要读取整张 Domain，和局部邻接传播不同；把它们塞进当前 809 行的 WFC CPP 会使求解循环再次难以审计。它们仍位于现有 `Private/PCG` 文件夹，不再为每个约束各建一个文件。

### 7.2 明确删除、保留与新增的符号

计划删除：

- `OptionalEnvelopeRadius`、`MaxOptionalSideBranches`、`MaxOptionalForwardLinks`；
- `ERandomDomain::OptionalLayout`、`BackboneY`；
- `CarveOrthogonalRoute`、固定中央主干部分的 `CarveRequiredSkeleton`、`BuildOptionalEnvelope`、`PruneDisconnectedOptional`；
- `ValidateGuaranteedSolvableConstraints` 中依赖固定 Required 主干的构造性见证；函数本身改名并收敛为一次静态输入校验；
- WFC 导出后的重复局部逐边复核；等价断言移到 Solver 自动化测试，Grid 层最终玩法验证保留；
- Grid 调用方对同一 WFC 输入校验的重复调用；只由 `Solve` 入口校验一次；
- `RequiredCellCount`、`OptionalCellCount`、`PrunedOptionalCellCount` 以及“WFC 不需要回溯”的旧测试；
- Objective 房间固定接入中央主干的双 Gate/第三 Gate 选择逻辑。房间内部 2×2 共享开口保留，外周连接交给 WFC。

计划保留：

- OpeningMask 0..15、最小熵、形态权重、局部邻接传播和相同 Seed 可复现；
- Start/Exit/Objective 的进度带与位置意图，但不保留它们之间的固定道路；
- `ComputeShortestCompletionRoute` 的 K-of-N bitmask DP；
- `MaxRequiredRouteLengthTiles`、`MaxRequiredRouteExtraTiles`、最终 BFS、Junction 指标；
- 300/600 cm 结构展开、Presentation Binding、HISM 事务式提交和 Harness。

计划新增的核心私有状态只有：

- `FZeroEscapeWfcConstraintSettings`：Grid、Relevant Cells、Count、MaxConsecutive 和预算；
- `FDomainChange` 与 `FWfcDecision`：Trail 和普通回溯；
- 私有 contradiction reason，用于区分 Domain Empty、Connectivity、Count、MaxConsecutive；
- 公共报告中的 `WalkableCellCount`、`WfcSolveStepCount`、`WfcBacktrackCount`、`WfcContradictionCount`，以及最终的 `NoValidWfcSolution`/`SolverBudgetExhausted` 失败类型。分支内可恢复 contradiction 不再误记为 invariant violation。

### 7.3 室内灯是独立增量

灯光不与求解器重构绑成一个不可拆检查点。后续若继续实施，预计额外修改：

| 完整路径 | 预计净增 | 主要变化 |
|---|---:|---|
| `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h` | 20～30 | Presentation 的四项最小灯光参数 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp` | 15～25 | 灯光配置校验 |
| `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeRuntimeLevelGenerator.h` | 10～20 | 生成灯组件的生命周期/回滚状态 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp` | 80～130 | 按最终非空 Cell 稀疏放灯并参与事务式清理 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationTests.cpp` | 30～60 | 无效配置与回滚测试 |

灯光预计净增约 155～265 行，不改变 WFC Domain、拓扑或生成 Hash。可以在新路线先通过纯值测试后作为下一检查点处理。

### 7.4 UE 资产变更边界

- `/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile`：删除旧 Optional 字段，填写路线约束、预算和逐难度权重。
- 当前实际的 HydroLab Presentation DataAsset：只在灯光检查点填写项目自有表现参数；具体资产路径以另一个对话完成移动后的回读为准。
- 不修改第三方 Mesh/材质，不恢复旧 Socket/Portal/Catalog/A* 链，不在 C++ 中硬编码 HydroLab 包路径。

## 8. 验证和停止条件

### 失败概率的当前判断

首版并不预期“很容易失败”：完整 OpeningMask 0..15、允许 Empty、48～72 的宽数量区间和不强制 T/Cross 配额都让解空间保持较大。加入回溯是为了让一次局部随机选择不再直接杀死整局，并为以后收紧房间/陷阱/目标约束保留正确基础，不是因为当前配置已经被证明经常无解。

仍需区分三种情况：

1. **可恢复 contradiction**：某个候选导致 Domain Empty 或全局约束不可能，普通回溯换候选，属于正常搜索过程。
2. **预算耗尽**：可能有解，但在 `MaxSolveSteps`/`MaxBacktrackSteps` 内未找到；必须单独报告，不能说成数学无解。
3. **已证明无解或配置非法**：所有决策候选耗尽，或参数本身矛盾，例如最小非空数大于最大值、固定相关 Cell 在可能图上先天不连通。

Connected 检查每次稳定点最多扫描约 384 个 Cell 和四邻边；这个规模下预计可接受，但只是设计判断，必须以 Runtime Seed Sweep 的 P50/P95/Max 数据确认。若它成为主要耗时，再参考 DeBroglie 的 tracker/增量更新；首版不提前实现缓存体系。

### 自动验证

1. 小 Grid 构造一个“第一次选择必然矛盾、第二个候选可解”的用例，证明至少发生一次真实回溯。
2. 回溯后 Domain、随机候选顺序和最终 Hash 可复现。
3. Start/Exit/Objective 最终 BFS 全部可达；所有非空路线属于同一分量。
4. 非空格数量始终在配置范围内。
5. 任意水平或垂直贯通段不超过 `MaxConsecutiveStraightTiles`。
6. 288 组 Difficulty/Flow/Seed Sweep 记录成功率、回溯数、总步数和 P50/P95/Max 耗时，不只记录 success。
7. 若正常配置仍出现预算失败，先分析约束或权重，不允许偷偷恢复固定主线。

### PIE 验收

所有人工与 MCP 玩家验收统一使用主编辑器视口的 `SelectedViewport`，不再使用 `NewWindow`。这属于编辑器测试模式，不在 Runtime Generator、Harness 或 GameFlow 中增加窗口处理代码。

1. 至少抽查 10 个固定 Seed 的鸟瞰图和玩家视角。
2. 不再出现贯穿地图的大段固定直线。
3. 结果中可以出现 Corner、T、Cross 和环路，但不要求每个 Seed 都具备全部形态。
4. 玩家从 Start 到 Exit 不需要长距离反复回头；Objective Flow 再检查 K-of-N 的最坏折返。
5. 生成区域有可用室内照明，且不再依赖单个关卡 Point Light 覆盖整图。

纯算法 Automation 继续受 `WITH_DEV_AUTOMATION_TESTS` 保护并长期保留，不合并进 GameFlow；未来替换的是旧算法假设测试。Runtime Harness 只保留到正式 GameFlow 接管生成请求、Ready 后开局和玩家出生，届时迁移产品职责并单独申请删除 Harness 类、Debug Blueprint 和测试装配，不把整套 Staging/手动重生成逻辑并入产品流程。

## 9. 需要用户确认后才进入代码预览的决定

推荐进入下一步的方案是：

**完整可玩 Grid 的 Simple-Tiled WFC + Connected/Count/MaxConsecutive 三类成熟约束 + 有界 chronological backtracking + 最终 BFS 验证。**

房间图的 Delaunay/MST 方案保留为未来房间占主导时的备选，不与本轮同时实现。Loop Constraint 也不在首版强制启用，只有实际结果仍有明显长回头路时再根据数据评估。
