# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源。

## 当前焦点

HeavyImpact “物理姿势 → 起身动画开头”连续过渡已完成实现和技术验证；下一步只做用户真实命中画面、边界和追逐恢复验收，并解释当前保存日志中的无支撑硬超时与 PIE Recast 缺失，不扩展新受击或机关框架。

## 已确认

- 真实 Chaos 接触仍决定角色位移、翻滚与最终倒地点；角色侧没有 `AddImpulse`、`LaunchCharacter` 或骨盆线性动画驱动。
- 玩家与 AI AnimBP 父类均为项目的 `HeavyImpactAnimInstance`，本地 MCP 回读 `UpToDate`；两份 AnimBP、两份 DataAsset/PCA 与 Level0 当前均非脏。
- 两份 DataAsset 均回读准备时间 0.40 秒、初始控制比例 0.30、正反面采样 0 秒、二次接触门槛 1000、最大水平找位 60 cm；角色 BP → DataAsset → PCA 引用闭合。
- Demo 模块最终构建成功且 DLL 晚于源码；既有 `Demo.Physics.HeavyImpact.*` 5/5。
- 当前 Level0 编辑器世界存在 NavMeshBoundsVolume 与 RecastNavMesh，但这不等于 PIE 运行时导航验收。

## 当前门槛

1. 用户验收玩家/AI 正躺、趴倒、开阔地、墙边、墙角、堵塞解除、准备中再次受撞，以及起身后控制/追逐恢复。
2. 在同一正式追逐场景记录 Recast 创建、追猎路径与 HeavyImpact 恢复，解释编辑器静态 Recast 与 PIE 缺失日志的差异。
3. 复现无地面支撑硬超时，先区分调试定点/场景净空与正常机关命中；原因明确前不改超时和睡眠策略。
4. HeavyImpact 通过后，只从摆锤、冲锤、制导机关中选一个已验收原型接入正式一局。
