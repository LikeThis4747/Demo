# Current Task

## 夜间交接状态（2026-08-12）

当前不是功能实施阶段。先处理资产状态分叉与可玩闭环证据，再决定进入哪项方案。

## 必须先处理

- Level0 在 Editor 中 Dirty。
- `AS_Pursuer_ChargeRun_Work`、其 Driving 资产和一个重名/临时 Driving 对象均 Dirty；前两份磁盘资产已删除。
- 夜间未保存、恢复或重建。用户必须白天明确这些 Editor 内容保存还是放弃；Git 快照只覆盖磁盘状态。
- Level0 静态存在 NavMeshBoundsVolume 与 RecastNavMesh，但最新相关保存 PIE 日志仍报告运行时找不到 Recast；需要同场景真实追猎者追逐验证。

## 已有技术证据

- 真实弹体预装机关已完成 Demo 模块链接和两台 Launcher 短 PIE：同一个 Projectile 依次记录 loaded、started、released loaded projectile。
- 关键追猎者/机关 Blueprint 为 UpToDate；机关与 HeavyImpact 的 BP -> DA -> PCA 引用闭合。
- 以上不替代用户对机关画面/机械感的验收。

## 仍待确认的方案

- PCG 机关与物理资源分层 Population：修订讨论稿已形成，未获实现授权；确认后仍需先展示代码预览。
- 物理轻受击：只允许零 C++ 的独立测试 PCA + Actor Blueprint + 测试关卡；不改生产 Heavy、磁力、玩家、追猎者或 Level0，实施前仍需完整 Git 基线和用户明确授权。

## 建议顺序

1. 处理 Dirty/删除状态归属。
2. 建立同场景 Recast 真实追逐证据。
3. 完成预装机关用户画面验收。
4. 用户选择后再推进 PCG Population 方案或隔离物理轻受击效果原型。
