# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源。

## 当前焦点

HeavyImpact 技术实现和资产装配保持完成状态；当前只做真实机关命中画面、墙角/堵塞/二次受撞边界与 Recast 追逐恢复的同场验收，不扩展新受击或机关框架。

## 已确认

- 2026-08-10 夜间前没有新提交、工作区改动或未跟踪文件；本轮没有新增构建、自动化或 PIE 证据。
- 玩家与 AI AnimBP 父类均为项目的 `HeavyImpactAnimInstance` 且 UpToDate；Level0、两份 AnimBP、两份 DataAsset/PCA 与 BP_Pursuer 当前均非脏。
- 两份 HeavyImpact DataAsset 到对应 PCA、BP_Pursuer 到追猎者 HeavyImpact DataAsset 的依赖闭合。
- 当前 Level0 编辑器世界存在 NavMeshBoundsVolume 与 RecastNavMesh，但最新保存 PIE 日志仍有运行时 Recast 缺失和无地面支撑硬超时；静态 Actor 不能替代运行验证。

## Git 阻塞

- `origin` 当前为 `git@github.com:LikeThis4747/Demo.git`，不等于内部工蜂地址；2026-08-10 夜间未 add、commit 或 push，报告与记忆更新仍仅在本地。

## 当前门槛

1. 用户验收玩家/AI 正躺、趴倒、开阔地、墙边、墙角、堵塞解除、准备中再次受撞，以及起身后控制/追逐恢复。
2. 在同一正式追逐场景记录 Recast 创建、追猎路径与 HeavyImpact 恢复，并复现无地面支撑硬超时。
3. HeavyImpact 与追逐通过后，只从摆锤、冲锤、制导机关中选一个已验收原型接入正式一局。
