# HANDOFF — TASK-20260802-001 PCG 多层 WFC 新方案讨论

- From：Codex / 当前会话
- To：下一次 PCG 方案会话
- Time：2026-08-02 23:56 +08:00
- Status：方案 A+ 已获得方向性认可；正式 Plan v0.1 已形成，仍有少量策划参数待冻结；尚未进入代码预览、落盘或资产修改

## 目标与当前完成度

目标不是生成尽可能多的楼层，而是在 2～4 层的首版难度范围内，生成“有一条隐含主路线，旁边有支路和循环”、复杂度足够但不会让玩家疲劳的迷宫。新增结构范围是双层楼梯、可选三层楼梯塔和高天花板房间；摆锤与其他机关继续由后续 Population 负责，不能和空间求解硬耦合。

当前已经完成现有源码、真实 DataAsset、蓝图引用、Level0 三层 V2 样板、Git 历史和外部一手资料的只读审计，并与用户完成主要架构讨论。用户认可继续使用稳定二维 WFC 内核，在其外增加多层规划；不采用统一细格三维 WFC。任务卡中已有正式实施 Plan 草案 v0.1，但还不是获准实施的最终方案。

建议下一会话不要重新做泛泛调研，也不要直接写代码；先把下方未决项冻结，整理成 Plan v0.2 给用户确认，再展示拟实现的类型、函数与删除清单。

## 已完成与修改文件

- 新建并持续维护：`claude/tasks/active/TASK-20260802-001-PCG多层WFC新方案讨论.md`
  - 记录源码/资产审计、外部参考、双方讨论结论、已排除方案、性能基线、旧实现减法候选和正式 Plan 草案 v0.1。
- 新建：`claude/handoffs/HANDOFF-20260802-PCG多层WFC方案讨论.md`
- 本轮没有修改 PCG/GameFlow C++、DataAsset、Blueprint、Level、配置或第三方资产，也没有把任何 UE 资产弄脏。
- 接手前仍须重新核对工作树并保护非本任务改动；不得擅自回退或混入实现。

## 关键决定

### 1. 总体架构

采用本项目讨论中称为“方案 A+”的有限多层编排：

```text
Difficulty/Profile
  → 生成必要换层主干及少量可选楼梯/高厅
  → 检查整栋三维占用、支撑与净空冲突
  → 将完整模块转换成各楼层的 Required / Outside / 固定开闭边
  → 逐层调用现有二维 16-state WFC
  → 合并同层边与楼梯内部跨层边
  → 在整栋通行图上验收主路线、支路、循环、规模与楼层可达
  → 独立 Structure Builder 展开墙、地板、栏杆、楼梯和高厅
  → Population 后续消费空间结果
```

- 普通 WFC Tile 继续只有 N/E/S/W 四位开口，共 16 种状态；不增加 Up/Down 位，也不把主路线、支路或楼层身份塞进 Tile 状态。
- 多层逻辑地址使用 UE 标准 `FIntVector(X,Y,Z)`；单层 Solver 内部继续使用 `FIntPoint`。
- 双层楼梯、三层塔和高厅先作为不可拆的完整模块被选择和放置，再一次性转换成各层约束。转换后的单格不是可独立随机坍缩的模块碎片。
- 模块实例是结构的唯一事实源，记录种类、基准地址、旋转、实际开放口、占用/净空及内部跨层通行边；垂直边不进入四位 OpeningMask。
- 高厅上层净空与楼梯实体/支撑必须是不同占用类别。二者虽都让二维 WFC 不能使用该格，但表现结果分别需要临空栏杆和实体收边。
- Runtime Generator 保持同步、无 Tick、蓝图只装配；不增加异步、兼容层、固定兜底关卡或写死资产路径。

### 2. 路线与楼梯约束

- 每对相邻楼层至少有一条物理换层连接。中间层至少有一个来自下层的必需楼梯口和一个通往上层的必需楼梯口，并在本层平面网络中可达。
- 玩家上楼后不能立刻进入下一座上楼楼梯；必需上下楼梯口之间需要可调的本层最短路线下限。
- 各层所有楼梯口以及该层存在的 Start/Exit 作为必达端点，只需验证它们位于同一可达分量，不为每一对端点重复寻路。
- 少量额外楼梯只承担可选复杂度、跨层循环或捷径；失败时可降低该次复杂度，不能破坏必要通路或每层内容下限。
- 同一楼梯平台可支持最多三个平面出口，实际开放组合随 Seed 变化，但必须在放置/求解前冻结，并受主路线、可达性、封口能力和出口短距离重汇合约束，不能独立随机开关。
- 三层楼梯塔不是必需结构，首版倾向作为稀有可选捷径或地标。
- “连接口”是当前统一中文说法；不要把 `Socket` 冒充 WFC/UE 官方术语或未经确认的项目正式类型名。

### 3. 难度与规模

- 用户暂定 Easy/Normal/Hard 对应 2/3/4 层，必须保存在可调 Profile 中，不能写死在算法。
- 同时控制每层普通 WFC 可走格范围和整栋总可走规模；每层有效内容下限优先于总量上限，不能把某层压成只有楼梯的过场层。
- 总量包含楼梯/高厅模块自带的可走格；每层普通格下限不包含模块格。
- 冲突优先级：必要楼梯与每层普通内容下限 > 整栋总量范围 > 可选楼梯/三层塔/高厅。
- 首轮 Seed Sweep 调参起点，不是最终策划值：
  - Easy：2 层；普通格每层 54～68；总可走 120～144；额外楼梯最多 1。
  - Normal：3 层；普通格每层 44～56；总可走 144～174；额外楼梯最多 1。
  - Hard：4 层；普通格每层 36～48；总可走 164～200；额外楼梯最多 2。

### 4. 保留与减法方向

- 保留稳定内核：二维 16-state WFC、Count、MaxConsecutive、Connected、有界回溯、纯值 Plan、确定性 Hash、事务式 HISM 提交和 Plan/Population 分离。
- 高优先级删除候选：`RoomSizeTiles/RoomCount/MaxRoomCount`、`FZeroEscapeGeneratedRoom`、`Plan.Rooms`、`GetGeneratedRoomWorldTransforms`、`ERandomDomain::RoomPlacement`、只服务旧房间流程的 `RegionId/RegionKind::Room` 及对应校验、Hash 和测试。
- 继续复核后再决定：其余 `RegionKind`、仅等于数组下标的 `StableCellId`、双份 Start/Exit Transform、仅测试消费的 JunctionMetrics、三档完全相同的 WFC 权重。
- Profile、Grid Solver 和 WFC Settings 的重复参数校验应收敛，但 fail-closed 行为和有价值的失败诊断必须保留。
- `MaxWfcSolveAttempts` 外层确定性重启是否保留，要用对照 Seed Sweep 决定，不能凭行数删除。
- 上一版曾有 Flow/Objective/Anchor/Progression 等更重抽象，后来删除约 2600 行；本次不得换名重新引入。

### 5. GameFlow 和 Population 边界

- `BP_ZeroEscapeGameMode` 虽然 EventGraph 为空，仍为正式 `L_Game` 装配玩家与追猎者；`BP_MainMenuGameMode` 仍装配主菜单，不能因事件图为空删除。
- `BP_ZeroEscapePrototypeGameMode` 目前只被 `Level0` 引用，`BP_ZeroEscapePrototypeRoundFlow` 只被 `L_PCG_RuntimeTest` 引用。它们是后续邻接清理候选，必须先确认测试场是否保留并迁移地图引用，不能混进多层 Solver。
- 高厅未来主要承载摆锤，但摆锤尚未实现。本次只生成高厅、净空与可查询模块实例，不预埋陷阱锚点、扫掠体积或装饰标签。

## 验证及结果

- 官方 UE MCP 实读当前 Generation Profile：`20x12`、逻辑格 `600 cm`、单层可走格 `48～72`、最大连续直行 `4`、路线长度上限 `64`；三档难度的局部 WFC 权重当前完全相同。
- Presentation Profile 结构高度为 `300 cm`；Level0 V2 样板走行面位于 `Z=0/450/900`，说明正式表现数值还需按真实楼梯资产测量，不能直接把样板高度写死。
- 未发现已封装的正式楼梯/高厅模块蓝图；V2 主要由 HydroLab 构件手工拼装。实现前需要用真实构件验证旋转、左右手性、开墙、连接地板、栏杆与支撑边界。
- 当前 18 项 `Demo.PCG` 自动化为 16 通过、2 失败：
  - 真实资产烟测仍断言旧的 `24x16`，实际 Profile 已为 `20x12`。
  - Presentation 瞬态夹具未满足后来增加的顶灯配置要求。
  两项是旧测试契约漂移，第一实施步骤应先恢复全绿基线。
- 现有 `ValidProfile288` Seed Sweep 为 288/288 成功，约 86 秒；`attempts_p95=3`、`attempts_max=4`、`planning_ms_p50=97.376`、`p95=992.100`、`max=1526.537`。它使用 `24x16` 测试夹具，只能作为旧实现对照，不能直接预测多层性能。
- 官方 MCP 的资产读取、蓝图/引用审计和自动化均可用；Semantic Search 因本机缺少 embedding key 返回 401，不是项目代码故障。

## 未解决问题与风险

1. **Start/Exit 楼层规则尚未最终冻结。** 首版建议默认 Start 在最低层、Exit 在最高层，以自然保证主路线穿过各层；若要允许 Profile 自由指定，需要明确“所有楼层通常经过”到底是软评分还是硬验收。
2. **楼梯距离仍缺数值。** 需要确定或至少给出首轮测试区间：中间层必需上下口最短路径、同平台多出口允许重汇合的最短距离、额外楼梯配额与失败降级方式。
3. **高厅出现频率尚未冻结。** 需要决定每档是 `0..N` 可选、每层上限，还是整栋至少一个；摆锤未实现前不应为了假想玩法强制高厅泛滥。
4. **规模数字只是调参起点。** 必须结合真实模块占格、每档至少 300 Seed 的 P95/P99、PIE 实走和 10～20 局疲劳度再定标。
5. **正式资产绑定未建立。** 不能在代码里猜楼梯尺寸、层高、左右手性或出口构件；表现阶段必须以真实资产测量和 V2 样板逐项核对。
6. **旧代码减法需要用户看过代码预览后授权。** 删除候选有较强证据，但仍要展示生产调用、测试迁移和完整文件清单，不能边加新层边留下兼容包装。
7. **真实三层运行尚未验证。** 还没有玩家胶囊连续实走、Recast 多层覆盖、追猎者上下楼、栏杆/天花板碰撞和 Population 保留区验证。

## 精确下一步

1. 先向用户提交一页 Plan v0.2 决策表，只冻结四件事：Start/Exit 楼层规则、楼梯最短路线测试区间、额外楼梯/三层塔降级规则、高厅数量规则。不要重新解释已经认可的 A+ 架构。
2. 将冻结结果更新到当前 active 任务卡；把“调参起点”和“最终策划值”明确分开。
3. 在对话中展示第一实施批次的完整代码预览与删除清单，至少包括：修复两项基线测试、旧房间链条删除/消费者迁移、Structure Builder 职责拆分；列出每个新增/修改文件的绝对路径。
4. 只有用户明确允许后才落盘。第一批只恢复全绿基线并做旧实现减法，不同时实现多层生成；完成后构建、运行 18 项测试和现有 Seed Sweep 对照。
5. 第二批再加入纯值三维 Plan、模块占用与逐层约束转换测试；确认无资产实例化时的数据合同稳定后，才接多层编排和结构表现。

## 接手前最少阅读

按以下顺序读取，避免历史资料污染当前结论：

1. `AGENTS.md`
2. `DOC/AI_WORK_GUIDELINES/PROJECT_ARCHITECTURE_RULES.md`
3. `DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md`
4. `claude/tasks/active/TASK-20260802-001-PCG多层WFC新方案讨论.md`（本次完整事实与 Plan v0.1）
5. 本交接文档
6. `Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h`
7. `Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h`
8. `Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.*`
9. `Source/Demo/Private/PCG/ZeroEscapeGenerationCore.*`
10. `Source/Demo/Private/PCG/ZeroEscapeWfcSolver.*` 与 `ZeroEscapeWfcConstraints.*`
11. `Source/Demo/Private/PCG/Tests/*`

外部依据的直接链接已集中记录在 active 任务卡；下一会话只在出现新的不确定点时补充一手资料。不要默认读取 `claude/handoffs/archive`、`claude/tasks/archive` 或 `claude/reviews/archive`。
