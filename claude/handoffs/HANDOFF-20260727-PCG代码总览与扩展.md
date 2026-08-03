# HANDOFF — PCG 当前代码总览与扩展讨论

- From：Codex / root
- To：下一次 PCG 审阅对话
- Time：2026-07-27
- Status：当前运行时 PCG、Population 与最小 RoundFlow 已可运行；下一步先完整审阅真实代码，再讨论扩展

## 目标与当前完成度

下一对话不要直接增加功能。先以当前源码和资产为唯一事实来源，完整梳理 PCG 的数据、WFC、全局约束、布局、运行时表现、玩法对象放置和 GameFlow 边界，再提出可分阶段扩展方向。

当前已存在：实时非工具性 Grid-WFC、Count/Connected/MaxConsecutive、时间序回溯、Start/Exit/中立房间、HydroLab HISM 表现、约每两个逻辑格一盏顶灯、独立 Population 地刺放置、玩家/追猎者/Exit 最小局流程。

## 已完成与修改文件

- `Source/Demo/Public/GameFlow/ZeroEscapePrototypeRoundFlow.h`
- `Source/Demo/Private/GameFlow/ZeroEscapePrototypeRoundFlow.cpp`
  - 当前语义：玩家位于 PCG Start；追猎者位于玩家身后至少 1200 cm；双方初始朝向一致；Exit 只打印一次成功日志。
- `/Game/ZeroEscape/GameFlow/BP_ZeroEscapePrototypeRoundFlow`
- `/Game/Levels/L_PCG_RuntimeTest`
- 旧 `ZeroEscapeRuntimeGenerationTestHarness.*` 已删除，旧 Blueprint Redirector 已确认无引用后删除。

当前 `Source/Demo/Public|Private/PCG` 共 19 个 C++ 文件、7567 行，其中测试 1632 行；GameFlow RoundFlow 383 行（均含注释和空行）。

## 关键决定

- WFC/Generator 只拥有空间拓扑、Start/Exit/房间、结构表现和只读空间查询。
- Population 只消费空间结果，负责可重复陷阱/奖励等批量对象。
- GameFlow 负责唯一玩家、唯一追猎者、Exit 和单局状态；不把玩法状态写回 WFC。
- 当前不恢复旧 Socket、A*、Progression Graph、K-of-N Objective 或测试 Harness。
- 扩展必须继续遵循最小实现：先指出真实需求和当前阻塞，再决定是否增加抽象。

## 验证及结果

- UE 5.8 `DemoEditor Win64 Development` 完整构建：`Succeeded`。
- SelectedViewport PIE：`ZE_PCG_RESULT success=1`、`ZE_ROUND_SETUP result=Success`，玩家/追猎者二维距离 1200 cm。
- 用户已完成主视口验收；位置语义最终确认为玩家在 Start、追猎者在身后。
- 本轮未运行自动化测试。

## 未解决问题与风险

- 工作区存在并行对话留下的未提交 Generator、Population、追猎者资产和关卡修改；接手前必须先看 `git status`/逐文件 diff，不得假定归属或回退。
- 当前追猎者生成和出生位置已验收，但动态 NavMesh 与多 Seed 追逐质量仍需单独证据。
- 当前“追猎者在身后”按双方初始朝向与二维距离定义，不代表沿 Start→Exit 路径的图距离；只有玩法确实需要路线语义时才扩展，不能预先增加寻路生成逻辑。
- 小地图、正式胜利 UI、收集目标、多敌人、奖励和难度驱动玩法尚未进入实现。
- PCG 7567 行是否仍有冗余，需要下一对话按职责和调用链审阅后再判断，不能只按行数删代码。

## 精确下一步

1. 只读执行 `git status --short --branch`，区分已提交基线和并行未提交改动。
2. 按顺序阅读当前真实代码：GenerationTypes/Assets → WfcConstraints/WfcSolver → GridLayoutSolver/GenerationCore → RuntimeLevelGenerator → Population → RoundFlow → Tests。
3. 输出当前调用链、状态 Owner、可调整 DataAsset 参数、代码行数分布和实际复杂度来源。
4. 列出扩展候选并分级：直接数据配置、现有层的小增量、需要新职责的功能；先讨论，不落盘。

## 接手前最少阅读

- `AGENTS.md`
- `.ai-context/current-task.md`
- `memory-bank/activeContext.md`
- 本交接
- `DOC/AI_WORK_GUIDELINES/PROJECT_ARCHITECTURE_RULES.md`
- `DOC/DailyPlan/archive/Done-2026-07-27-PCG玩法对象放置层最小实现.md`
- 当前 `Source/Demo/Public/PCG`、`Source/Demo/Private/PCG` 与 `Source/Demo/*/GameFlow/ZeroEscapePrototypeRoundFlow.*`

默认不要读取 `claude/tasks/archive`、`claude/docs/archive`、`claude/handoffs/archive` 或 `claude/reviews/archive`；只有追溯历史决定时再按关键词定位单个文件。
