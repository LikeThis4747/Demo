# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

V4 全图 Grid-WFC 已完成源码、真实 Profile、构建、19/19 自动化、288/288 Seed Sweep 和 SelectedViewport 技术烟测。2026-07-27 无新项目变更；唯一关键路径仍是玩家多 Seed 的路线、接缝、碰撞、净空、导航与 Start→Exit 验收。

## 活跃任务

- `TASK-20260723-002`：等待 V4 玩家验收；当前唯一关键路径。
- `TASK-20260723-004`、追猎者卡、静态网格体检卡：暂停，不抢占关键路径；白天归档/修正重复 `TASK-20260724-001` ID。

## 当前证据与门槛

- 2026-07-27 本地 UE Editor MCP 只读审计：Editor/World/Asset Registry Ready，PIE Stopped；Generator/Harness Blueprint UpToDate，测试关卡装配完整。
- 官方 UE5.8 MCP 三步入口未暴露，因此通用 DataAsset 属性/引用审计未执行。
- 当前 Outliner 仅见 AbstractNavData，未见 `NavMeshBoundsVolume` 或 `RecastNavMesh`；导航仍未验收。
- 室内灯保持独立增量；Runtime Harness/测试资产在正式 GameFlow 接管前保留。
