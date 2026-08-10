# Current Task

## 当前任务

壁挂式一次性机关已重构为“发射前预测移动交点 → 炮管机械转向 → 一次质心冲量 → 全程 Chaos”。用户首次 PIE 发现两个机关完全不触发；运行日志确认 Level0 两个旧摆放实例仍保存重构前的 `Muzzle -> SceneRoot`，在 BeginPlay 组件层级校验处自禁用。

## 已完成修复

- UE5.8 官方 MCP 已把 Level0 两个实例正式保存为 `AimPivot -> Muzzle -> ProjectileSpawnPoint`，重启 Editor 后逐实例回读仍正确。
- Launcher C++ 增加旧关卡实例自愈：父级不符时按顺序以 KeepRelativeTransform 恢复层级，严格复核；失败才禁用并输出具体父级。
- 修复通过 Demo `-NoLink` 与 `DemoEditor Win64 Development` 正式链接，只构建 Demo 项目模块，未编译引擎。
- 非破坏定点 PIE 中两个实例均成功 `armed`；运行时玩家从 Trigger 外移入后，两个实例均完成 `BeginOverlap -> 锁定角色 -> 0.55s Warning -> fired`，无 disabled/failed 日志。
- PIE 已正常停止；Level0、两个机关 Blueprint 和默认 DataAsset 均为非 Dirty。

## 当前边界

- 触发链缺陷已覆盖，但用户尚未验收画面可读性、移动目标命中率、急停/横移躲避、连续 Chaos 反弹和薄墙穿透；不得写成功能或玩法已验收。
- 磁力碎裂与轻受击是并行独立任务；本机关不修改其 Source/Content。轻受击线程当前仅修改两份 Markdown，提交前需由其 Owner 确认稳定归属。

## 下一步

1. 更新实施报告、任务与项目记忆。
2. 与轻受击文档 Owner 收口当前完整工作区，完成一次 commit/push、远端核验和 clean status。
3. 由用户重新运行 Level0，继续画面与玩法验收。
