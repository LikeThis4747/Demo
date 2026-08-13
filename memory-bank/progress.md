# Progress — Demo

## M0 基础设施与交付

- [x] UE5.8 C++ 优先单机 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与官方/本地双 MCP 规范。
- [x] DemoEditor 构建与项目模块验证路径形成；不修改 UE5.8 主引擎。
- [ ] 首轮 Development 打包、目标机运行与完整一局回归。

## M1 Runtime PCG 与多层场景

- [x] 16 OpeningMask Grid-WFC、带权熵、Trail/回溯、连通/Tarjan、确定性重试、Runtime HISM。
- [x] 多层合同：跨层宏结构先冻结、逐层二维 WFC、三维占用/净空、整栋逻辑连通、Start/Pursuer/Exit 与动态导航门。
- [x] Seed 30794 原问题楼梯经用户确认追猎者可上；既有完整构建与 Demo.PCG 22/22 + Demo.GameFlow 2/2。
- [x] Population 分层放置用户验收：高厅摆锤必放，普通机关权重抽取且操作格互斥，资源独立图间距采样；Normal Seed 12345 机关 36/36、资源 13/13，Population 8/8、完整 PCG 29/29。
- [ ] 同一正式一局动态 Recast + 真实追猎者完整多层追逐；Level0 静态有 NavMesh Actor，但保存 PIE 日志仍有 Unable to find RecastNavMesh。
- [ ] 玩家实走全部楼梯/旋转组合；Easy/Normal/Hard 各 300 Seed 统计和双档照明为非阻塞回归。

## M2 玩法压力、物理机关与追猎者

- [x] 摆锤原型、自动冲锤、预判抛射 Chaos 机关基础机制、磁力投掷破碎 P0 均已有各自阶段/用户验收。
- [x] HeavyImpact 阶段验收：真实 Chaos、有限 Physics Control、Snapshot 起身、同来源防夹；玩家 0.15/0.50 秒、追猎者 0.15/3.0 秒，HeavyImpact 5/5。
- [x] 追猎者近战斧击 + 预判跑跳下砸技术交付：18/30 伤害、一次锁点、Landed 结算；完整构建、PredictionAndBallistics 1/1、资产回读和短 PIE 通过。
- [ ] 用户验收追猎者攻击预警、持续横跑/急转躲避、落空恢复、压力与动画观感。
- [x] 玩家磁力轻受击技术装配：Slow 0.40 秒、倍率 0.55、无动画、上半身局部物理；CharacterImpact 2/2 + HeavyImpact 5/5。
- [ ] 用户验收玩家胸/左右物理反馈、恢复与 Light→Heavy；地刺不改，只允许有限参数 A/B。
- [ ] 预判抛射机关 Loaded 外观、移动目标/躲避/反弹/薄墙等专项仍待验收；不扩大为全部边界已完成。

## M3 正式一局流程与感知层

- [x] 主菜单 Seed/难度 → 生成/Population → 玩家/追猎者，以及 Exit 胜利、Health 归零失败、暂停、结算、重开/下一把/选关/回主菜单。
- [ ] 生成失败重试、胜负、暂停、同 Seed 重开和新 Seed 下一把的多层联合回归。
- [ ] 最小目标链仍待确认；当前建议为 2–3 个短时驻留终端解锁出口，复用现有 GameState/Exit/交互。
- [ ] P0 音频尚未接入；先做追猎者脚步/攻击预警、机关危险提示与磁力操作确认。
- [ ] UI 审美统一最后处理。

## 当前边界

- Population 当前参数是用户验收基线；多 Seed、极端净距与长期难度曲线不阻塞现阶段。
- 多层 PCG 与整局流程属于“已实现并有自动/代表性证据，待完整可玩验收”。
- 禁止恢复全身常驻受控物理、Light/Heavy 大一体化或跨对象磁力事务方案。
- 2026-08-13 夜间只读审计未构建、测试、运行 PIE、编译或保存资产。

<!-- compacted by Codex nightly at 2026-08-13 01:08 +08:00 -->

<!-- written by shiqiqiwang at 2026-08-13 07:38 UTC -->

## 2026-08-13 追猎者攻击
- 已实现跑跳下砸/近战挥斧真实 Heavy Impact、夸张可调击飞、命中后等待玩家起身、额外喘息与成功冷却；追猎者攻击侧轻受击已移除。
- 玩家最大生命值已调为 1000；构建成功，相关自动化 6/6 通过。用户已看到并确认击飞效果。

<!-- written by shiqiqiwang at 2026-08-13 08:10 UTC -->

## 2026-08-13 制导投射物轻受击

- [x] 制导投射物第一次有效角色阻挡命中接入玩家 Slow + 局部物理；CharacterImpact 2/2、HeavyImpact 5/5，SIE 提交/部位路由/零冲量保底技术证据通过。
- [x] 磁力追猎者路径保留 Stop + 三方向动画，并增加最低可见物理强度；未修改 HeavyImpact、追猎者攻击、磁力事务或玩家蓝图。
- [ ] 用户验收制导命中玩家胸/头/左右臂的可见偏转和自然恢复，以及磁力箱子命中追猎者的动画+物理观感。

<!-- written by shiqiqiwang at 2026-08-13 08:43 UTC -->

## 2026-08-13 制导轻受击现场反馈修正

- 已实现制导弹首次 Light 结算后的 Pawn 清场，以及 Light Mesh 对 PhysicsBody 的临时隔离和完整碰撞基线恢复。
- 已把玩家局部物理满强度冲量调至 13000，Hold 调至 0.11 秒，BlendOut 调至 0.22 秒。
- DemoEditor 构建成功；CharacterImpact + HeavyImpact 7/7；标准 PIE 技术证据通过。玩家奔跑跳跃手感与最终部位可见度仍待验收。
