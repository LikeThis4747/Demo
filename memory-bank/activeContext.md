# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

2026-07-27 用户将当前关键路径切回 PCG 玩家可读性：先以最小改动加入运行时室内顶灯，完成构建、DataAsset 装配与主视口 PIE 验收；顶灯通过后再讨论小地图。追猎者相关任务保留但本轮不修改其文件。

## 活跃任务

- `TASK-20260727-005`：PCG 室内光源已授权实现，当前唯一正在修改的任务；小地图尚未进入实现。
- `TASK-20260723-002`：V4 PCG 技术验证已完成，仍需玩家多 Seed/导航验收。
- Pursuer、角色素材与其他旧 active 卡暂停；不并行修改重叠文件。

## 当前边界与风险

- 只改四个现有 PCG C++ 文件与项目自有 Presentation DataAsset；不动 WFC、第三方素材、Level0、测试 Harness 或小地图。
- 顶灯使用 HydroLab 原始 LampA Blueprint；PCG 不复制 RectLight 参数，实际性能取决于其阴影、半径与重叠范围，需 PIE 验收。
- 新增反射属性需要完整 C++ 构建并重开编辑器；不使用 Live Coding 或其他绕过。
- 完成门槛包含构建、资产保存、主视口 PIE 与用户视觉/性能验收。
