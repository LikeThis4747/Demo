# Active Context — Demo

## 当前权威基线

- UE5.8 C++ 优先单机 Demo；公开 Seed 是地图身份，路线、密度和结构目标只做软评分或 underfill，不得静默换 Seed。
- 多层生成采用跨层宏结构、逐层二维 WFC、整栋通行图；Population 使用 600cm 图与 300cm 普通机关站，直廊前后站可共存，转角/T 路口重叠站互斥。
- 机关预算与普通资源数是期望量；达到后只有覆盖缺口可补位，Actor 安全上限仍是硬约束。
- 奖励支线端点光团已形成闭环：实际总数由 Population 提供，GameState 保存目标/收集数，GameMode 裁决拾取与出口；Easy/Normal/Hard 比例为 35%/50%/75%，不进入随机域。
- 爆裂投掷正式为开局 1/3、每 30 秒恢复 1 次；光团补 1 次且不溢出，满 3/3 的光团仍计入出口目标。
- Heavy 接触统一保留 0.15 秒 PhysicsBody 阻挡；正常恢复之外有约三秒后的一次性受限应急搜索。摆锤/冲锤伤害 30，制导推进伤害 15；正式生命 Easy 800、Normal 600、Hard 400，当前调试覆盖 1000。
- 追猎者保持持续追踪和 70cm 楼梯攻击高度边界；异常持续默认 18 秒后可尝试镜头外同层 NavMesh 重放置。
- 楼梯已有灯位被解释为前 N-1 个蓝色 SpotLight、最后一个蓝色 PointLight；磁力空手候选以 0.25 秒单 Timer 切换蓝色 Overlay，抓取前恢复。
- 2026-08-16 技术证据：DemoEditor 多次完整构建；Population 17/17、PublicSeedStability900 Success；光团/出口正式 L_Game PIE 闭环；Heavy/CharacterImpact 7/7；Windows Development 阶段包和 IoStore/UnrealPak Success。夜间未复跑。
- UE 只读回读：Level0 打开、PIE 停止；关键关卡/资产非 Dirty；BP_MagneticProp、摆锤、冲锤、光团父类正确，Population 关键引用完整。

## 当前 P0

1. 在同一正式包或正式 PIE 完成动态 Recast、真实追猎者跨层追逐、机关/光团/出口/死亡的完整一局。
2. 用户集中验收多 Seed PCG 节奏、300cm 站位、高厅摆锤、光团可读性、楼梯蓝灯与磁力闪光。
3. 复测 Heavy 0.15 秒阻挡、三秒应急恢复、摆锤/冲锤穿模；验收追猎者 18 秒镜头外恢复与攻击公平性。
4. 在目标机启动 Development 包，重点检查追猎者骨骼/动画；保存日志仍记录 /Engine/EngineMeshes/Humanoid 缺失依赖。
5. 在正式一局复验 BP_MagneticProp 破碎配置错误是否仍可重现，再决定是否修复。
6. P0 音频：追猎者脚步/攻击预警、机关危险提示、磁力操作确认。

## 恢复工作注意

- 构建、自动化、静态 Recast Actor、资产非 Dirty 与一次日志不能替代正式一局和玩家手感验收。
- 参数问题先调现有 DataAsset/曲线；不新增评分通道、任务框架或常驻 Tick。
- 不恢复全身常驻受控物理、Light/Heavy 大一体化、距离放弃或隐形追猎者连续重定位。
