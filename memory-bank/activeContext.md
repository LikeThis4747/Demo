# Active Context — Demo

## 当前焦点

- 磁力投掷物碰撞破碎 P0 已完成技术落地，等待用户 Level0 完整输入链与手感验收。
- 统一轻受击第一版已完成技术交付，仍待真实磁力命中、地刺、空中 Stop 与 Light/Heavy 交叉手感。
- Light/Heavy 统一物理身体重构是独立并行讨论稿；磁力任务只保持共享 Hit 接口边界，不代为实施该重构。
- 最新保存日志曾有 Unable to find RecastNavMesh；仍需在同一正式一局建立真实追逐证据。

## 已验证边界

- 磁力破碎：DemoEditor 构建成功，官方 Blueprint/属性回读通过；隔离运行时确认显式解簇与约 2.06 秒 Remove On Break 清理。
- 破碎碎片不参与抓取、Light/Heavy、Pawn/Camera/PhysicsBody 二次碰撞或导航；6 秒 Actor LifeSpan 只作异常兜底。
- 破碎只消费 MagneticObject 的共享正式投掷 Hit，没有第二套 Hit/CCD/碰撞快照。
- 无头环境未能驱动真实玩家投掷输入，因此完整输入链和边界手感没有伪报通过。

## 下一步

1. 用户在 Level0 验收磁力物抓取 -> 正式投掷 -> 合格碰撞破碎，以及低冲量、薄墙/角落、角色命中和多物体竞态。
2. 验收通过后归档磁力 Review、DailyPlan 与任务卡；P1 可再次投掷碎片另立任务。
3. 统一轻受击、Light/Heavy 重构和 Recast 追逐按各自任务继续，不与磁力破碎收口混写。
