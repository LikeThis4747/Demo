# Progress — Demo

## M0 基础设施与交付

- [x] UE5.8 C++ 优先单机 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 规范。
- [x] DemoEditor 构建、自动化和官方 MCP 资产回读路径。
- [x] Windows Development 阶段包已生成；IoStore/UnrealPak Success。
- [ ] 目标机启动、追猎者骨骼/动画检查与完整一局回归。

## M1 Runtime PCG 与多层场景

- [x] Runtime Grid-WFC/HISM、跨层宏结构、逐层二维 WFC、整栋通行图、动态导航门与 Population 分层放置。
- [x] 公开 Seed 稳定；软质量不改变合法性；奖励支线/端点光团、上下文机关、高厅主路覆盖和期望量补位已落地。
- [x] 600cm WFC 图上的 300cm 普通机关站：直廊前后站可共存，转角重叠互斥，双地刺横向覆盖。
- [x] 2026-08-16 增量证据：DemoEditor 构建；Population 17/17；PublicSeedStability900 Success；L_Game Seed 12345 为机关 91、资源 32、光团 11。
- [x] 楼梯已有灯位生成蓝色转角 SpotLight 与终点 PointLight；技术编译通过，待画面验收。
- [ ] 用户多 Seed 验收路线、高厅、机关/资源密度、300cm 站位、奖励支线与楼梯引导。
- [ ] 同一正式一局动态 Recast + 真实追猎者完整多层追逐。

## M2 物理机关、投掷与追猎者

- [x] 摆锤/冲锤、预判抛射、爆裂投掷、普通投掷 Stop、磁力破碎、StandingImpact 与 HeavyImpact 已形成阶段技术证据。
- [x] Heavy 统一 0.15 秒 PhysicsBody 阻挡；约三秒后的一次性受限应急搜索已通过构建与 7/7 自动化。
- [x] 机关玩家伤害：摆锤 30、冲锤 30、制导推进 15；玩家生命 Easy 800 / Normal 600 / Hard 400，当前调试覆盖 1000。
- [x] 追猎者持续追踪、70cm 隔层攻击边界与默认 18 秒镜头外同层 NavMesh 恢复已提交并构建。
- [x] 磁力空手候选蓝色 Overlay 使用单个 0.25 秒 Timer；抓取前恢复，编译与资产回读通过。
- [ ] 用户验收 Heavy/机关穿模、追猎者恢复、爆裂/Stop、楼梯蓝灯与磁力闪光。
- [ ] 复验 BP_MagneticProp 破碎配置错误；确认正式链路是否仍受影响。

## M3 正式一局与感知层

- [x] 主菜单 Seed/难度 → 生成/Population → 玩家/追猎者 → Exit/失败/暂停/结算/重开流程。
- [x] 光团 → 爆裂次数补充 → 实际总数比例门槛 → Exit 判胜已通过正式 L_Game PIE。
- [ ] UI：爆裂次数 1/3 与本轮 0~100 被动恢复进度。
- [ ] P0 音频：追猎者脚步/攻击预警、机关危险提示、磁力操作确认。
- [ ] UI 审美统一最后处理。

## 验收边界

- 自动化、构建、静态 Recast Actor、资产非 Dirty、阶段包生成和单次日志不能替代目标机正式一局与玩家手感验收。

<!-- written by shiqiqiwang at 2026-08-17 02:57 UTC -->

## 2026-08-17 楼梯灯位纠错

- 已恢复 PCG 楼梯全部原普通固定顶灯，并将蓝色入口聚光灯/出口点光源改为基于实际结构 Opening 的额外灯；编译通过，待用户视觉验收。

<!-- written by shiqiqiwang at 2026-08-17 03:05 UTC -->

- 楼梯入口/出口蓝灯根据现场反馈改为外侧普通格中心的低位高亮点光源，取消入口向下聚光，待视觉验收。

<!-- written by shiqiqiwang at 2026-08-17 03:16 UTC -->


## 2026-08-17 追猎者录制调试开关

- 在 `UPursuerConfig` 增加 `bEnableAttacks`（编辑器显示为 `Enable Attacks`），仅由 `APursuerAIController::Think()` 拦截攻击启动；AI 移动与 Heavy/Light 受击组件保持启用。
- `/Game/ZeroEscape/Enemies/DA_Pursuer` 已保存为 `bEnableAttacks=false`，用于 Level0 录制。
- `DemoEditor Win64 Development` 编译成功；Level0 PIE smoke 启动/停止成功，等待用户画面验收。

<!-- written by shiqiqiwang at 2026-08-17 04:18 UTC -->


## 2026-08-17 Level0 终点传送门

- [x] 使用项目已有门框与蓝色全息材质完成出口传送门原型，按 600cm 房间尺寸控制比例，并加入蓝色点光源。
- [x] 光团达到目标数后传送门短闪一次；`DemoEditor Win64 Development` 构建成功，Level0 PIE 已启动。
- [ ] 用户验收传送门在终点房间的可见性、朝向和最终美术质量。

<!-- written by shiqiqiwang at 2026-08-17 05:07 UTC -->


## 2026-08-17 Level0 传送门视觉纠偏

- [x] 将出口视觉收敛到蓝色不透景能量面 + 动态噪声边缘 + 蓝色点光源，保留光团达标短闪逻辑。
- [x] 清理临时组件/试摆球体，Level0 出口蓝图实例无残留临时组件。
- [ ] 用户验收边缘能量是否达到参考图的火焰撕裂质感。


## 2026-08-17 Level0 测试地图摆放修正

- [x] Level0 测试地图新增 `Portal_Test_PlayerStart`，放在 PlayerStart 前方约 600cm；PIE 出生点附近可直接看到蓝色传送门。
- [ ] 用户验收门框朝向、蓝光强度和全息面最终表现。
