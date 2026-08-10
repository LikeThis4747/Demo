# Current Task

## 当前任务

磁力投掷物 P0 方案已经用户确认；用户已授权把当前全部已确认改动形成并推送完整 Git 基线，并在门禁通过后于 Level0 测试区手工制作首个 Geometry Collection。C++、Blueprint 运行逻辑、材质和正式关卡装配仍未授权。

## 已确认方案

- 抓取、拉取、持有、普通放下、安全释放和普通外力碰撞均不获得破碎资格。
- 只有现有 `ThrowHeldObject()` 正式投掷入口写入道具自身的破碎资格；该状态不复用有独立受击时限的 `AttackProjectile` Tag。
- 命中前重新抓取会取消旧资格，确保拉取途中不会因历史投掷状态误碎。
- 第一次 Blocking Hit 只排队一次；下一帧读取原刚体碰撞后的 Transform、线速度和角速度，生成短命 Geometry Collection 替身，成功后再移除完整 Actor。
- P0 所有叶子碎片通过 Remove On Break 在约 1～1.5 秒内清空；P1 才考虑另生成独立小磁力物，不把某个 Geometry Collection 骨骼直接变成可抓取物。

## 当前证据

- 官方 UE MCP 确认 `BP_MagneticProp` 继承 `/Script/Demo.MagneticPrototypeProp`，根组件是 20 kg 的模拟物理静态网格刚体，使用 `SM_crate4`，资产非 Dirty。
- `SM_crate4` 约 80 cm 立方、LOD0 492 三角形/510 顶点、2 LOD、1 材质槽，资产非 Dirty。
- UE5.8 引擎头文件确认 `UGeometryCollectionComponent` 支持用户指定初始速度、`CrumbleActiveClusters()`、`ApplyExternalStrain()` 和 Remove On Break。
- 正式方案：`DOC/DailyPlan/2026-08-10-磁力投掷物碰撞破碎实施计划.md`。
- 任务卡：`claude/tasks/active/TASK-20260810-002-磁力投掷物碰撞破碎方案.md`。

## 实现门禁

用户已统一确认当前制导机关 DataAsset、制导机关新方案、轻受击任务/方案、磁力破碎方案和项目记忆全部保留并纳入一次基线。正在执行 commit、内部工蜂 push、远端包含核验和 clean status；任一步未完成前不创建 Geometry Collection。

## 并行边界

- 壁挂式物理制导一次性机关仍等待用户在 Level0 低天花板转角走廊做 PIE 画面、命中率与反弹手感验收。
- HeavyImpact 继续冻结为已验收阶段基线；轻受击是独立讨论任务，本方案不修改角色受击或既有机关。
- 本轮只创建任务卡、DailyPlan 和 DOC 索引，没有构建、Blueprint 编译、PIE 或功能完成结论。

## 下一步

1. 完成当前全工作区 Git 基线提交、内部工蜂推送、远端包含核验和 clean status。
2. 在 Level0 空地放置临时 `SM_crate4`，逐步制作 `GC_MagneticCrate4_P0`；Level0 当前官方 MCP 回读为非 Dirty。
3. 资产外观和碎片数量验证后，再由用户决定是否授权 C++/Blueprint 运行逻辑。
