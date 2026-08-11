# Active Context — Demo

## 当前焦点

- 磁力投掷物碰撞破碎 P0 冲击感修正版已由用户验收并归档；当前不再占用磁力/Light 共享文件写入窗口。
- 统一轻受击第一版已完成技术交付，但用户否决了纯动画观感；新的真实物理 Light / Heavy 共享身体结论仍处于暂停比较研究，尚无权威实现授权。
- 最新保存日志曾有 Unable to find RecastNavMesh；仍需在同一正式一局建立真实追逐证据。

## 已验证边界

- 磁力破碎稳定基线：门槛 5000 kg·cm/s、速度保留 0.6、继承上限 5000 cm/s、碎片分离 350 cm/s、半径倍率 1.25。
- DemoEditor 构建、官方 Blueprint/属性回读、隔离 PIE 碎片散开/清理和用户 Level0 核心力量感验收均已通过。
- 破碎碎片不参与抓取、Light/Heavy、Pawn/Camera/PhysicsBody 二次碰撞或导航；6 秒 Actor LifeSpan 只作异常兜底。
- 破碎只消费 MagneticObject 的共享正式投掷 Hit，没有第二套 Hit/CCD/碰撞快照；运动在 Hit 当帧冻结，next-tick 只做安全替换。
- 薄墙、角落与多物体竞态保留为回归清单，不扩大解释为用户逐项现场确认。

## 后续

1. P1“大物体破碎后保留一块可再次投掷的小磁力物”必须另立任务并重新授权。
2. Light/Heavy 研究和 Recast 追逐按各自任务继续，不与已归档磁力 P0 混写。

<!-- written by shiqiqiwang at 2026-08-11 06:40 UTC -->

## 2026-08-11 预判抛射机关第一版外观试装

- 基础机制参数仍为 `900 cm / 18° / 50 kg / 8 s`，本次未改 C++、DataAsset 或 Level0。
- Launcher Blueprint 已用 UE 基础几何增加固定墙板、轴承、随 `AimPivot` 转动的 U 形托架和警示灯罩；新增视觉网格均 `NoCollision`，引用 SFCorridors 现成材质但未修改第三方资产。
- Projectile Blueprint 的 `BodyMesh` 已换为 `SM_barrel3`，Scale=`0.78`、Relative Z=`-40.04 cm`；旧 `NoseMesh` 隐藏，唯一物理碰撞仍为 `ProjectileBody` 胶囊。
- 两个 Blueprint 以 Warning 视为 Error 编译通过；重启后 Level0 实例正确继承，临时 PIE 覆盖 Warning/Fire，最终造型与机械感待用户画面验收。
