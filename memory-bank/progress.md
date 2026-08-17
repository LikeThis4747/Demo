# Progress — Demo

## 已形成阶段证据

- [x] UE5.8 C++ 优先单机 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 流程。
- [x] Runtime Grid-WFC/HISM、多层宏结构、逐层二维 WFC、整栋通行图、动态导航门与 Population 分层放置。
- [x] 菜单 Seed/难度、生成/Population、玩家/追猎者、Exit/失败/暂停/结算/重开闭环。
- [x] 光团补充爆裂次数并形成难度比例出口门槛；爆裂投掷、磁力/普通投掷、摆锤/冲锤/刺轮与 Heavy/StandingImpact 有阶段技术证据。
- [x] 2026-08-16：DemoEditor 构建、Population 17/17、PublicSeedStability900、Heavy/CharacterImpact 7/7、正式 L_Game 光团/出口 PIE；Windows Development 阶段包生成成功。
- [x] 2026-08-17：楼梯蓝色引导、追猎者录制攻击开关、蓝色传送门、Gameplay HUD 与顶部目标行已提交；DemoEditor Development 构建，相关蓝图 UpToDate 且非 Dirty。

## 待正式验收

- [ ] 顶部目标行最终布局：当前左上锚点 + 0.5 水平对齐存在出屏风险，最终提交后无新 PIE 证据。
- [ ] 录制后恢复 DA_Pursuer.Enable Attacks=true，并验证攻击、受击、追逐。
- [ ] 同一正式一局动态 Recast、真实跨层追逐、机关/光团/出口/死亡/重开。
- [ ] 目标机 Development 启动与追猎者骨骼/动画；排查 Humanoid 缺失依赖。
- [ ] 多 Seed 节奏、300cm 机关站、高厅摆锤、楼梯灯、磁力闪光、Heavy 穿模与追猎者恢复的玩家验收。
- [ ] P0 音频：追猎者脚步/攻击预警、机关危险提示、磁力操作、出口解锁。

## 验收边界

- 构建、自动化、静态 Recast Actor、Blueprint UpToDate、资产非 Dirty、阶段包生成和单次日志不能替代目标机正式一局与玩家手感验收。
