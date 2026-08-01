# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

Level0 三层 V2 静态样板已保存重载；下一门槛不是继续堆静态模块，而是补齐 V2 导航覆盖，完成玩家与真实追猎者三层连续移动，再把验证结果转成多层 WFC 数据合同。

## 当前索引

- Level0 V1/V2：V1=1836 Actor，V2=1857 Actor，关卡非脏；V2 外墙、栏杆、高厅和灯光 Preview 已静态收口。
- 导航：现有 NavMeshBoundsVolume X=-17600..22400 cm，不覆盖约 X=45000 cm 的 V2；存在 Recast Actor 不代表三层连续覆盖。
- WFC：当前代码仍是二维 FIntPoint GridSize 与四方向 OpeningMask；多格垂直宏块、保留占用、接口类型/高度和共享边所有者尚未进入正式数据合同。
- 正式一局：菜单到单 Seed 开局有历史证据，但正式 GameMode 尚无 Exit/死亡结算/重开入口，整局仍待玩家验收。
- SFCorridors：仅保留只读筛选任务；退场需依赖闭包、精确清单和用户授权。

## 当前边界与风险

- 静态截图、射线和 Actor 数不能替代玩家胶囊、Recast 与真实追猎者连续移动。
- 多层 WFC 代码必须在数据合同确认、代码预览和用户明确授权后实施。
- 软件 Lumen GI + SSR 配置保持不变；本轮没有修改渲染配置或第三方灯具蓝图。
