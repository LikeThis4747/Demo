# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

当前最短可玩闭环为：PCG 生成 → 地刺/磁力物 → 追猎者追击 → Exit 成功。玩家生命已能被地刺扣到 0，但没有失败/重开；下一优先是由 GameFlow 接管生命归零，形成成功与失败两端闭环，然后做多 Seed 导航/追猎验收。

## 当前索引

- 夜间审计：`claude/artifacts/nightly/2026-07-28.md`。
- PCG 静态总览：`DOC/Outputs/PCG/archive/2026-07-27-PCG代码完整分析报告.md`（仅按需读取）。
- PCG 交接：`claude/handoffs/HANDOFF-20260727-PCG代码总览与扩展.md`。
- `claude/tasks/active` 仍有三张追猎者相关卡；卡片状态与当前源码/资产/日志存在漂移，白天由对应 Owner 核对后更新或归档。

## 当前边界与风险

- 当前源码声明 18 个 Demo.PCG 测试；删除旧 Harness 前的 19/19 与 288/288 只是历史证据。
- 最新日志证明 Seed 16001、Population、RoundFlow、Health 与 Physics Control 基本链路运行；没有连续 10 次 Physics Control 压力验收或至少 10 Seed 玩家验收。
- 当前编辑器打开素材 Overview；测试关卡 NavMesh、Actor 装配和 DataAsset 引用未在本轮完整复核。
- 工作区包含 295 个未跟踪 SFCorridors LFS 资产与多条并行工作流成果；不得回退、删除或混合认领。
