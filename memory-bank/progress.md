# Progress — Demo

## M0 基础设施与交付

- [x] UE5.8 C++ 优先单机 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 规范。
- [x] DemoEditor 构建与项目模块验证路径形成；不修改 UE5.8 主引擎。
- [ ] 首轮 Development 打包、目标机运行与完整一局回归。

## M1 Runtime PCG 与多层场景

- [x] Grid-WFC、Runtime HISM、多层宏结构/逐层二维 WFC/整栋通行图、动态导航门与 Population 分层放置。
- [x] 固定 Seed 与软质量收敛：无自动换 Seed；最多三候选择优、同层硬合法兜底；每栋至少两个高厅且至少一个非顶层。
- [x] 白天证据：Unit 10/10、WFC 8/8、Population 8/8、GameFlow 1/1、Navigation Gate 1/1；三档 900 Seed 各重放两次，共 1800 次整栋求解通过。
- [ ] 用户验收困难档加载、密度/长直线/高厅观感、同 Seed 重进与全部楼梯组合。
- [ ] 同一正式一局动态 Recast + 真实追猎者完整多层追逐。

## M2 玩法压力、物理机关与追猎者

- [x] 摆锤、冲锤、预判抛射、磁力破碎 P0、HeavyImpact 与统一 StandingImpact 已形成阶段/用户证据。
- [x] 追猎者近战/跑跳 Heavy、全局持续追踪与楼梯攻击限制已提交；楼梯修复有用户 PIE 验收。
- [x] 制导/磁力 Light 异常前冲已修复并获当前版本接受；玩家 Stop 三方向动画已补齐并现场确认可用。
- [x] 刺轮 Level0 技术初版、半埋轮体、三种一格轨迹与实例确定性选路已完成构建、冷启动资产回读与 Simulation。
- [x] 刺轮火星技术修正：组件缩放恢复为原 `1.0`，尺寸、数量、速度和拖尾形态保持，只保留红黄色项目材质；Blueprint 编译、资产/运行时读回与 Simulation 通过，用户最终视觉复验待确认。
- [ ] 用户验收刺轮路线压力、速度/穿越手感、追猎者攻击预警/可躲性/落空恢复及其余表现细节。

## M3 正式一局与感知层

- [x] 主菜单 Seed/难度 → 生成/Population → 玩家/追猎者 → Exit/失败/暂停/结算/重开流程。
- [ ] 灰盒化最小目标链；优先尝试“磁吸电芯送达出口插槽”，复用现有磁力、Exit 与结果流程。
- [ ] 接入 P0 音频：追猎者脚步/攻击预警、机关危险提示、磁力操作确认。
- [ ] UI 审美统一最后处理。

## 当前边界

- 自动化与静态检查不等于正式一局动态导航、资产状态或玩家手感验收。
- 刺轮 2026-08-14 的移动/半埋/轨迹技术验证有效；火星已按原形态重新运行验证，技术结果通过但仍不等于用户视觉验收。
- 不恢复全身常驻受控物理、Light/Heavy 大一体化或跨对象磁力事务方案。

<!-- written by shiqiqiwang at 2026-08-14 05:06 UTC -->

## 2026-08-14 — HeavyImpact 不可回滚与有界恢复

- 完成摆锤/冲锤真实盒体短时 Sweep；候选体积不再直接决定 Heavy。
- 完成 `Accepted` 后不可正常回滚、timeout 继续物理但不伪造真实接触事件、迟到 exact source Hit 单次提交。
- 删除 Settling 抢跑起身与 5/10 秒正常硬切；Downed 睡眠后立即尝试，0.20s 重试，3.0s 截止；Snapshot 淡入 0.30s。
- DemoEditor 构建成功；官方 MCP 回读资产正确且非 dirty；Heavy 5 项 + CharacterImpact 2 项自动化 7/7 Success。
- 状态：技术实现完成，待用户 PIE 视觉与手感验收。

<!-- written by shiqiqiwang at 2026-08-14 05:41 UTC -->

## 2026-08-14 — HeavyImpact 用户复测增量

- 玩家与追猎者确认共用同一 HeavyImpact 状态机；AI 挂墙不起定位为“无可行走支撑时稳定进度永远清零”，不是 AIController 漏恢复。
- 共享修正为低线速/低角速连续 0.35 秒即可收口；有支撑走正常 Downed，无支撑处理墙边/夹缝低能量卡死。
- 两份 Heavy 起身阻塞截止从 3.0 秒改为 2.0 秒；0.20 秒重试不变。自然飞行与滚动仍无总时长硬切。
- 未保留暂停 Montage 的猜测性补丁；当前闪感记录为物理终姿到两种固定起身首姿的匹配限制，待用户现场判断。
- DemoEditor 构建成功；官方 MCP 回读两份 DA 正确且非 dirty；受击自动化 7/7 Success；5 秒 SIE 无 Heavy 告警。
- 状态：技术实现完成，待用户 PIE 验收后决定是否需要动画首姿侧调整。

<!-- written by shiqiqiwang at 2026-08-14 07:18 UTC -->

<!-- written by Codex /root at 2026-08-14 -->

## 2026-08-14 — 刺轮 Light Stop 受击动画接通

- 仅将 `DA_SpikeWheelStandingImpact.PlayerReaction.bPlayReactionAnimation` 从 `false` 改为 `true`；玩家仍为 `Stop / 0.7 s / SpeedMultiplier=0 / Physical=false`，追猎者仍为 `None`。
- `BP_SpikeWheelHazard` Warning-as-error 编译通过；CharacterImpact 配置与来源契约自动化 `2/2 Success`，资产保存后非 Dirty。
- 运行时碰撞仍为 QueryOnly Pawn Overlap，且不影响导航；用户已在 Level0 现场确认刺轮受击动画正常播放。
- 状态：本次来源动画增量完成；刺轮路线压力、火星最终观感与整体穿越手感仍按原任务继续验收。
