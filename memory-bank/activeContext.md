# Active Context — Demo

## 当前焦点

- 磁力投掷物碰撞破碎 P0 冲击感修正版已完成技术落地，等待用户 Level0 投掷命中 AI 的力量感与边界验收。
- 统一轻受击第一版已完成技术交付，仍待真实磁力命中、地刺、空中 Stop 与 Light/Heavy 交叉手感。
- Light/Heavy 统一物理身体重构是已暂停的独立比较研究；磁力任务只保持共享 Hit 接口边界，不代为实施该重构。
- 最新保存日志曾有 Unable to find RecastNavMesh；仍需在同一正式一局建立真实追逐证据。

## 已验证边界

- 磁力破碎修正版：门槛 5000 kg·cm/s、速度保留 0.6、继承上限 5000 cm/s、碎片分离 350 cm/s、半径倍率 1.25。
- DemoEditor 构建、官方 Blueprint/属性回读和隔离 PIE 碎片散开/清理通过；用户真实投掷手感未伪报通过。
- 破碎碎片不参与抓取、Light/Heavy、Pawn/Camera/PhysicsBody 二次碰撞或导航；6 秒 Actor LifeSpan 只作异常兜底。
- 破碎只消费 MagneticObject 的共享正式投掷 Hit，没有第二套 Hit/CCD/碰撞快照；运动在 Hit 当帧冻结，next-tick 只做安全替换。

## 下一步

1. 用户在 Level0 验收正式投掷命中 AI：接触处立即碎裂、保持撞击方向并有限散开。
2. 补验低冲量、薄墙/角落、角色命中和多物体竞态。
3. 验收通过后归档磁力 Review、DailyPlan 与任务卡；P1 可再次投掷碎片另立任务。
4. 统一轻受击、Light/Heavy 研究和 Recast 追逐按各自任务继续，不与磁力破碎收口混写。

<!-- written by shiqiqiwang at 2026-08-11 06:40 UTC -->

## 2026-08-11 预判抛射机关第一版外观试装

- 基础机制参数仍为 `900 cm / 18° / 50 kg / 8 s`，本次未改 C++、DataAsset 或 Level0。
- Launcher Blueprint 已用 UE 基础几何增加固定墙板、轴承、随 `AimPivot` 转动的 U 形托架和警示灯罩；新增视觉网格均 `NoCollision`，引用 SFCorridors 现成材质但未修改第三方资产。
- Projectile Blueprint 的 `BodyMesh` 已换为 `SM_barrel3`，Scale=`0.78`、Relative Z=`-40.04 cm`；旧 `NoseMesh` 隐藏，唯一物理碰撞仍为 `ProjectileBody` 胶囊。
- 两个 Blueprint 以 Warning 视为 Error 编译通过；重启后 Level0 实例正确继承，临时 PIE 覆盖 Warning/Fire，最终造型与机械感待用户画面验收。
