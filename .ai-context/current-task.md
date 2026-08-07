# Current Task

- 当前任务：HeavyImpact 倒地起身恢复桥收口；实现、当前 DLL、5/5 自动化和真实玩家/追猎者恢复日志已具备，但仍不得标记用户画面或边界验收完成。
- 当前磁盘增量：两份项目 AnimBP，以及 HeavyImpact 恢复组件、调参和测试；C++ 增加倒地二次接触最小 Chaos 冲量门槛，并抬高恢复 Capsule 路径起点以减少贴地假重叠。
- 资产只读状态：Level0 打开、PIE Stopped；两份 AnimBP 父类均为 `/Script/Demo.HeavyImpactAnimInstance` 且 `UpToDate`，但玩家 AnimBP、追猎者 AnimBP 与 Level0 均为编辑器内脏状态，本轮未保存。
- 当前证据：`UnrealEditor-Demo.dll` 晚于源码；`Demo.Physics.HeavyImpact.*` 5/5 成功；日志中玩家与追猎者均出现 `committed → Downed → recovery completed`。
- 当前风险：少量恢复仍因最终骨盆附近无可信空闲 Capsule Sweep 起点而保持阻塞；Level0 日志缺少 RecastNavMesh，不能证明追猎者起身后恢复正式追逐。
- 下一步：用户先决定保存或放弃三项脏资产，再验收正躺/趴倒、墙边/墙角、堵塞解除、二次接触、画面衔接、玩家控制与 AI 追逐恢复。
