# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

先把 Level0 三层 V2 从静态样板推进到可玩验证：补导航覆盖，完成玩家与真实追猎者三层连续移动；随后完成菜单到 Exit/死亡/重开的最小一局闭环。多层 WFC 数据合同与低价值重构后置。

## 当前索引

- Level0 V1/V2：V1=1836 Actor，V2=1857 Actor；静态精修有历史证据，连续实走和 AI 导航没有当前验收。
- 导航：历史核对的 NavMeshBoundsVolume X=-17600..22400 cm，不覆盖约 X=45000 cm 的 V2。
- 正式一局：GameMode 只负责生成和开局摆放；HealthComponent 归零仅记日志，Exit/结算/重开未实现。
- WFC：当前公开数据仍是二维 FIntPoint GridSize 与四方向 OpeningMask；多层占用和垂直接口未进入正式合同。
- 回归：当前完整构建、Demo.PCG 与至少 10 Seed 基线待重跑。
- SFCorridors：仅保留只读筛选；退场需要依赖闭包、精确清单和用户授权。

## 当前边界与风险

- 2026-08-03 夜间无新提交或工作区变化，也没有新增构建、自动化、PIE 或资产审计证据。
- 官方 UE5.8 MCP 在线但本地 UE Editor MCP 离线；本轮蓝图审计未执行。
- 静态截图、射线与 Actor 数不能替代玩家胶囊、Recast 和真实追猎者连续移动。
