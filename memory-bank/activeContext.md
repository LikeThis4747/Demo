# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

PCG 路线正式计划与拟实现代码评审稿已完成。当前唯一关键路径是审查并在明确授权后实施“完整可玩 Grid 的 WFC + Connected/Count/MaxConsecutive + 有界 chronological backtracking + 完成态路线验收回溯”；未取得新一轮源码/资产授权前不修改 PCG 实现，也不把追猎者并入 PCG 工作范围。

## 活跃任务

- `TASK-20260723-002`：唯一关键路径，正式计划与拟实现代码待用户/独立 AI 审查。
- `TASK-20260723-004`：旧素材预览筛选，可暂停/归档，不抢占关键路径。
- 素材迁移与追猎者任务有并行工作区改动；PCG 实施前必须先取得交接，禁止覆盖 Core、Tests、Presentation、Level0 或 Demo.Build.cs 的改动。

## 近期决定

- 正式计划：`DOC/DailyPlan/2026-07-25-PCG-WFC路线重构实施方案.md`。
- 拟实现代码：`claude/docs/2026-07-25-pcg-wfc-route-refactor-code-proposal.md`；当前只是评审稿，未落盘 Source。
- 删除固定中央主干、Optional Envelope、旧构造性可解见证和重复 WFC 热路径复核，不保留兼容壳。
- 保留 16 OpeningMask、Progression、2×2 Objective 房内约束、K-of-N DP、最终 BFS 和结构展开。
- WFC 增加 change trail、普通 chronological backtracking、Connected 可能图、Count 上下界传播和 MaxConsecutive 滑动窗口；不做 Backjump、LoopConstraint、通用框架或隐藏重启。
- 完整候选若路线总长或额外折返超限，必须作为当前分支 Reject 继续回溯；其他最终不变量为 Fatal。只提供一个窄的完成态候选验收边界。
- 三个难度保持相同 EmptyWeight 与非空 Variant 总权重，只重新分配非空形态比例，避免 Hard 靠扩大非空区域延长单局。
- Solver 预算精确定义为 Candidate Attempts 与 Decision Frame Restores；在新 Seed Sweep 得到失败率、耗时和 P50/P95/Max 后才冻结正式值。
- Automation 回归长期保留；Runtime Harness 只作临时 PIE 装配，正式 GameFlow 接管产品职责后再单独申请删除。
- 本机 PIE 默认已改为 `PlayMode_InViewPort`；以后人工/MCP 玩家验收显式使用 `SelectedViewport`，不再使用 `NewWindow`。
