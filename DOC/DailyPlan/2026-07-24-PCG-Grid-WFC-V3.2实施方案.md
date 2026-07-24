# 2026-07-24 PCG Grid-WFC V3.2 实施方案

> 状态：方案已获用户允许写入 DailyPlan；尚未获得本轮源码/UE 资产落盘授权，下一次对话再开始实施。
>
> 评审依据：`claude/reviews/2026-07-24-pcg-grid-wfc-v3-draft-review.md`，结论为“方案通过，建议实施”。
>
> 代码预览：`claude/docs/2026-07-24-pcg-grid-wfc-v3_2-code-proposal.md`。

## 1. 目标

在 UE 5.8 中实现项目自有的 Runtime、非工具型 PCG：按 Seed 在 PIE/游戏运行时生成 600 cm 宽的单层走廊、小房间和可选路口，使用 SciFiHydroLab 的 300 cm 分离式结构件展开表现；结果可复现、全部必要地标可达，并能在 `L_PCG_RuntimeTest` 中实际走通。

## 2. 已冻结的最小方案

| 项目 | 本轮决定 |
|---|---|
| 逻辑尺度 | 600×600 cm WFC Tile；每 Tile 固定展开为 2×2 个 300 cm 表现单元 |
| WFC | 代码固定生成 `OpeningMask 0..15`；实现最小熵、权重观察和开闭边传播 |
| 可满足性 | RequiredOpen 双向、非零、不越界且不与 RequiredClosed 冲突；正常 Seed 不允许无解 |
| 回溯 | 不实现 Snapshot、Decision Trail、Backtrack、Retry、换 Seed或 SkeletonFallback |
| 全局路径 | 不使用 A*；无障碍矩形网格用 X-first/Y-first 正交雕刻 |
| 流程层 | 只输出 CompletionRule、K/N、Start/Objective/Exit Landmark 与 ProgressBandIndex |
| 空间层 | 先构造 Start→Exit 主骨架，再把全部 Objective 接入已连通骨架；有合法空间时可向后续推进带重连 |
| 路口 | Corner/T/Cross 允许自然出现，不设每局配额；数量为零不失败 |
| 难度 | 固定地图范围和折返上限；Hard 可增加目标密度、可选侧支与前向连接，不显著延长关键路线 |
| 表现 | Floor/Wall/Ceiling/WallTopTrim/Pillar 由闭边、面和顶点公式生成；表现不参与 WFC Hash |

陷阱、敌人、门、多层、不规则大房间、风格 Palette 和目标交互不在本轮实现。它们未来通过独立玩法/表现阶段接入，不提前污染首版 WFC。

## 3. 审计意见采纳

1. 旧 `ZeroEscapeLayoutSolver.h/.cpp` 在新链路切换并通过构建后直接删除；完成态不得保留 façade、转发壳、注释代码或 `if(false)` 分支。
2. Socket/Portal/Closure/Cap/A*/Catalog/回溯相关测试与实现同步删除或替换。
3. 删除 `AStarStraightStepCost`、`AStarTurnPenalty`、`ERandomDomain::SocketLayout`、精确 `ShortLeafBranchCount/ForwardRejoinBranchCount`、搜索/快照/重试预算及对应硬上限。
4. `FResolvedGenerationBudget` 改为轻量 `FResolvedProgressionSettings`，只保留实际消费者：Difficulty、Flow Id/Version、CompletionRule、N、K。
5. `GenerationSignature` 删除 `CatalogVersion`；保留 Seed、Difficulty、Flow/Algorithm/GenerationProfile/Presentation 版本。逻辑结果继续使用 Progression Hash 与 Layout Hash。
6. Grid 与 WFC 保持独立文件。`ZeroEscapeGridLayoutSolver.cpp` 接近 1500 行时暂停并按“地标/雕刻/验证”职责复审是否拆分，不机械继续堆积。
7. 首版 PCG 源码连同测试以约 3000–5000 行为合理自查区间；若完成态仍超过 8000 行，不直接宣布完成，先重新审计是否再次过度设计。

本次审计继续留在 `claude/reviews/` 根目录，直到上述代码项真正落实；由实施代码的 AI 标注完成并移入 `archive/`，本轮只写计划不提前归档。

## 4. 源码与文档范围

### 4.1 新增

| 完整路径 | 职责 |
|---|---|
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.h` | 内部 Grid 约束、进度带/Lane 地标、正交雕刻与最终 Grid 验证声明 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.cpp` | 无重试的地标嵌入、必要路线、Optional Envelope、孤岛清理和最终验证 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcSolver.h` | 固定 16 Mask 的纯值 WFC 接口 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeWfcSolver.cpp` | Domain 初始化、最小熵、权重选择与传播；不含回溯 |

### 4.2 修改

| 完整路径 | 职责 |
|---|---|
| `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h` | 用 Grid Cell/Landmark/OpeningMask Plan 与新 Report 替换 Module/Portal/Closure 输出 |
| `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h` | Generation Profile 的 Grid/形态权重/难度配置和 HydroLab 直接表现绑定 |
| `D:/UE5projects/Demo/Source/Demo/Public/PCG/ZeroEscapeRuntimeLevelGenerator.h` | 删除 ModuleCatalog 入口；保留 Profile、Presentation、查询和事务状态 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp` | 新 Profile/Presentation 校验；删除 Catalog、Portal、Cap、Overhang 兼容校验 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationCore.h` | 轻量 Snapshot、ProgressionSettings、ProgressionIntent、随机域和 Hash 声明 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationCore.cpp` | Flow/K-of-N 解析与 Landmark Intent；删除抽象空间图和目标回退搜索 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp` | 新纯值管线、结构实例展开、HISM 事务提交和 Schema 2 日志 |
| `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeGenerationTests.cpp` | 按第 7 节迁移测试，不机械保留旧测试 |
| `D:/UE5projects/Demo/DOC/Design/PCG/SCIFI_HYDROLAB_MODULE_TABLE.md` | 明确 300 cm 是表现单元、600 cm 是逻辑 Tile，并同步 2×2 Tile 房间 Fixture |

`ZeroEscapeRuntimeGenerationTestHarness.h/.cpp` 默认不改；只有新 Plan 查询接口导致编译或注释失真时才做最小适配，不扩展职责。

### 4.3 完成态删除

- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeLayoutSolver.h`
- `D:/UE5projects/Demo/Source/Demo/Private/PCG/ZeroEscapeLayoutSolver.cpp`

删除前先确认新 Runtime 已不再 include 或调用旧 Solver。删除完成后用全文搜索验证 Socket/Portal/Closure/Cap/A*/WFC Backtrack/Retry/Fallback/Catalog 旧链路在 PCG 源码中零残留；Gameplay Anchor 这一仍有消费者的概念不在删除范围。

### 4.4 不修改

- `D:/UE5projects/Demo/Source/Demo/Demo.Build.cs`：不新增 Editor-only 或第三方依赖。
- `D:/UE5projects/Demo/Demo.uproject`、`Config/`、UEEditorMCP 插件和无关 UE 5.8 升级文件。
- `/Game/SciFiHydroLab` 与 `/Game/Assets/SFCorridors` 第三方素材内容；若 HISM Usage 等素材设置阻塞，先说明直接修改项并征求用户许可，不创建映射层绕过。

## 5. UE 资产范围

### 创建

- `/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SciFiHydroLab`

第一套绑定只使用已验证规格：

- `/Game/SciFiHydroLab/Meshes/Floors/SM_HydroLab_LargeFloorB`
- `/Game/SciFiHydroLab/Meshes/Walls/SM_HydroLab_WallH`
- `/Game/SciFiHydroLab/Meshes/Ceiling/SM_HydroLab_CeilingC`
- `/Game/SciFiHydroLab/Meshes/Trims/SM_HydroLab_WallTrimG`
- `/Game/SciFiHydroLab/Meshes/Pillars/SM_HydroLab_PillarC`

Floor/Ceiling 水平 Trim、门框和表现变体本轮关闭。

### 原位更新

- `/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile`
- `/Game/ZeroEscape/Generation/BP_ZeroEscapeRuntimeLevelGenerator`
- `/Game/Levels/L_PCG_RuntimeTest`

`/Game/ZeroEscape/Generation/Debug/BP_RuntimeGenerationTestHarness` 只编译与回读，接口仍兼容时不修改。

### 暂不删除

- `/Game/ZeroEscape/Generation/Data/DA_LevelModuleCatalog`
- `/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SFCorridors`
- SFCorridors 第三方素材包

只有新链路通过 Fixture、构建、自动化、正常视口 PIE 和玩家走通后，才另行列出引用和删除候选并征求用户许可。

## 6. 实施检查点

### 检查点 0：UE 5.8 基线

1. 记录当前 Git 状态，保护已有 UE 5.8 升级和用户改动；不 reset、不清理、不把无关文件纳入本任务。
2. UE 关闭时完整构建 `DemoEditor Win64 Development`，再运行当前 `Demo.PCG`。
3. 若基线失败，先报告升级兼容问题，不把既有错误混入 PCG 重构。

回退：无文件修改。

### 检查点 1：新纯值 Grid/WFC 契约

1. 新增 Grid/WFC 文件，在不切换 Runtime 的前提下实现 16 Mask、约束校验、正交路径和无回溯传播。
2. 新类型可与旧链路短暂并存以保持可编译，但不建立 façade、数据转换层或双运行时路径。
3. 先跑纯值单元测试：Mask 完整性、RequiredOpen 不变量、构造性解、Optional 响应、正交路径和同 Seed 复现。

回退：删除本检查点新增文件和未被旧 Runtime 消费的新字段；旧运行时仍可构建。

### 检查点 2：Runtime 原子切换与旧链路清理

1. Core 收敛为 Progression Intent；Runtime 切换到 Grid Plan 和直接结构装配。
2. 删除 ModuleCatalog 配置入口、旧抽象图、Socket/A*/Closure/回溯字段、日志和测试。
3. 删除旧 LayoutSolver 两个文件，不保留兼容壳。
4. 完整构建并执行新 `Demo.PCG`；全文搜索旧符号零残留。

回退：只回退本检查点 PCG 源文件；不触碰 UE 5.8 升级文件和第三方资产。

### 检查点 3：固定结构 Fixture

1. 创建 HydroLab Presentation，Optional Envelope 暂设为 0，生成固定 Seed 的 600 cm L 形走廊与 2×2 逻辑 Tile（1200×1200 cm）房间。
2. 每个非 Empty Tile 恰好展开 4 Floor + 4 Ceiling；闭边展开两段 Wall，WallTopTrim 与 Pillar 使用规范 Key 去重。
3. 在普通 UE 视口检查无共面重复、Z-fighting、明显接缝、错误墙面、碰撞阻塞或第三人称净空问题。
4. 用户确认 Fixture 观感与通行后才进入 Optional WFC。

回退：恢复三份项目自有资产的旧引用；不修改或删除 HydroLab/SFC 第三方素材。

### 检查点 4：Optional Envelope 与真实 WFC

1. 开启有限 Optional Envelope，由最小熵和形态权重生成可选连接。
2. 批量 Seed 验证不出现空 Domain、回溯、布局重试或降级；若出现，按不变量 Bug 处理。
3. T/Cross 为零的结果仍通过；孤立纯 Optional 分量可确定性移除。
4. 同输入的 Progression/Layout Hash 必须一致，失败调用不得泄漏旧 Plan。

回退：将 Optional Envelope 设回 0 可恢复已验证的必要骨架；不引入备用算法。

### 检查点 5：Flow 与 K-of-N 数据契约

1. EscapeOnly：`K=N=0`。
2. CollectAll：`K=N`。
3. CollectKOfN：`1 <= K <= N`。
4. 全部 N 个 Objective Candidate 均生成且位于 Start 连通分量；K 只改变完成条件，不参与连通补救。
5. 本轮只生成 Landmark/Anchor 数据，不实现收集交互或胜负流程。

回退：保留 EscapeOnly 最小路径；Flow 失败必须原子返回空 Plan，不发布半成品。

### 检查点 6：最终运行验收

1. UE 5.8 完整 C++ 构建通过；新 `Demo.PCG` 全部发现项通过。
2. 编译并保存 Generator/Harness Blueprint 与项目自有 DataAsset。
3. 在 `/Game/Levels/L_PCG_RuntimeTest` 使用正常渲染的 NewWindow PIE，确认 Runtime 实时生成，不依赖 Editor-only WFC/工具。
4. `ZE_PCG_RESULT Schema=2` 报告 Ready、Cell/路口/HISM/Hash 指标；Harness 正常传送。
5. 用户实际从 Start 走到 Exit，检查碰撞、缝隙、重复墙、天花板/Trim、净空与基础性能。

仅构建或自动化通过不等于完成；最终状态保持“待用户验收”。

## 7. 测试迁移

### 删除

- `Demo.PCG.Unit.Assets.CatalogContracts`
- `Demo.PCG.Unit.Core.ObjectiveRouteFallback`
- `Demo.PCG.Unit.Layout.StrongSocketBranchRegression`

### 替换

- `GridRotationContract` → `OpeningMaskDirectionContract`
- `SFCorridorsPipelineSmoke` → `ProjectHydroLabPipelineSmoke`
- `AbstractDeterminism` → `ProgressionIntentDeterminism`
- `Layout.DeterminismAndStateIsolation` → 新 Grid/WFC 的确定性与失败原子性

### 保留有效部分并删除旧半边

- `TransformComposition`：只保留 `PivotCorrection → Canonical Structure Transform → Generator Root`。
- `SnapshotStableOrdering`：只验证仍存在的 Difficulty/Flow 稳定排序。
- `RandomDomainIsolation`：改为 Landmark、OptionalLayout、WfcLayout、Presentation。
- `ProgressionRules`：改为轻量 Landmark Intent 的 EscapeOnly/CollectAll/K-of-N 契约。
- Profile/Presentation 合同、Runtime HISM 事务回滚按新结构适配。

### 新增核心覆盖

- 16 Mask 完整且唯一。
- RequiredOpen 双向、非零、不越界、不与 RequiredClosed 冲突。
- 构造性最小赋值始终满足约束。
- Optional 正确响应 Required 邻格开口。
- 任意加权观察无需回溯。
- 合法 Gate 的 X-first/Y-first 正交路径必连通。
- 2×2 房间内部开放、外围只保留 Gate。
- Floor/Ceiling 展开、共享闭边、直墙/转角 Pillar 去重。
- Start/Exit/全部 Objective Candidate 可达，K-of-N 最短合法路线在最终 Tile 图上验证。

## 8. 下一次对话的授权边界

下一次用户明确说“开始实现”后，即视为授权按本计划完整实施，无需在每个检查点重复请求落盘许可；实施 AI 在检查点完成后报告实际差异与验证结果。只有方案范围发生变化、需要修改第三方素材、需要额外删除未列明文件或与 UE 5.8 升级改动发生冲突时，才暂停并说明情况、直接方案、影响与替代方案，再请求用户决定。不得把追猎者、陷阱和目标交互夹带进本任务。
