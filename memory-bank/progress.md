# Progress — Demo

## 已形成阶段证据

- [x] UE5.8 C++ 优先单机 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 流程。
- [x] Runtime Grid-WFC/HISM、多层宏结构、逐层二维 WFC、整栋通行图、动态导航门与 Population 分层放置。
- [x] 菜单 Seed/难度、生成/Population、玩家/追猎者、光团门槛、Exit/失败/暂停/结算/重开闭环。
- [x] 磁力/投掷、摆锤/冲锤/刺轮、Heavy/StandingImpact 与难度生命已形成构建/自动化/阶段 PIE 证据。
- [x] 2026-08-17：楼梯引导、蓝色传送门、Gameplay HUD 与顶部光团目标行形成阶段证据。
- [x] 2026-08-18：HUD 居中与事件更新、开场镜头、胜负过场、楼梯坡面、跳跃削弱和 Presentation 基线已提交；22:12 Shipping 构建，23:16 Cook/Pak/Stage 成功；PIE 日志记录一次 2/2 光团后胜利。
- [x] 当前未提交实现加入出口锁定提示、“开始逃亡”提示、视角倍率和 Motion Blur 配置；尚待同一版本玩家验收。

## 待正式验收

- [ ] 恢复/确认官方 UE5.8 MCP 配置；当前磁盘 Demo.uproject 已移除 ModelContextProtocol，本夜官方 MCP 不可用。
- [ ] 同一正式一局动态 Recast、真实跨层追逐、机关/光团/出口/死亡/重开；日志仍反复报告 RecastNavMesh 缺失。
- [ ] 开场镜头、HUD 安全区、出口锁定提示、视角倍率、跳跃削弱与胜负过场的玩家体验。
- [ ] 目标机 Shipping 启动与追猎者骨骼/动画；排查 Humanoid 缺失依赖。
- [ ] 多 Seed 节奏、机关站、高厅摆锤、楼梯灯、磁力闪光、Heavy 穿模与追猎者恢复。
- [ ] P0 音频：追猎者脚步/攻击预警、机关危险提示、磁力操作、出口解锁。

## 验收边界

- 构建、Cook/Pak/Stage、静态 Recast Actor、Blueprint UpToDate、资产非 Dirty 和单次日志不能替代目标机正式一局、真实 AI 追逐与玩家手感验收。
