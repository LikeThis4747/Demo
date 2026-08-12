# Daily Log — Demo

> 按日期倒序；仅保留完成、验证、决定与遗留，过程细节见任务卡、DailyReport 与审计归档。

## 2026-08-13 夜间只读审计

- 白天完成并提交 PCG 机关/资源分层 Population（用户已验收）、玩家磁力轻受击局部物理技术装配，以及追猎者近战斧击 + 预判跑跳下砸技术交付；音效需求、完整度评估和玩法诊断仍是未接入的文档/方案。
- 本次夜间未构建、未跑自动化、未启动 PIE、未编译或保存资产。双 UE MCP 在线：Level0 打开、PIE Stopped；Level0、L_Game、BP_Pursuer、追猎者 AnimBP、Population 与轻受击关键资产非 Dirty，两份追猎者 Blueprint 为 UpToDate，关键 CDO/DA 引用和攻击参数回读正确。
- Level0 静态存在 NavMeshBoundsVolume/RecastNavMesh，但 8 月 12 日保存的多次 PIE 日志仍反复报告 Unable to find RecastNavMesh；明日 P0 是同一正式一局动态 Recast + 真实追猎者追逐证据。
- 玩家局部轻受击与追猎者跑跳/近战仍待用户移动实测、画面、恢复和 Light→Heavy 验收；只做有限参数 A/B，不继续扩系统。
- 新玩法建议：用 2–3 个短时驻留电力终端解锁出口，让追猎者压力、磁力物和机关共同参与距离管理；首版复用现有 GameState/Exit/交互，不新增任务框架。
- Git：origin 精确为内部工蜂；主快照 `8d137c5cb57831d00851fad7fbcc46fd2576c39b` 已普通推送并远端核验，最终报告/错误状态回填以第二个同名快照收口。

## 2026-08-12

- PCG Population 改为机关优先、资源后置：高厅必放摆锤，普通机关按类型权重抽取且操作格互斥，资源独立采样。Demo 模块、Population 8/8、完整 PCG 29/29、资产回读和 L_Game Normal Seed 12345 通过；机关 36/36、资源 13/13，用户验收当前密度/构成。
- 机关密度收口为 Easy/Normal/Hard 26/28/30 每百格、间距统一 2；权重 5:3:2 / 1:1:1 / 2:3:5。多 Seed 统计降为非阻塞回归。
- 玩家磁力命中改为 Slow 0.40 秒、倍率 0.55、无动画、上半身局部物理；地刺不改。CharacterImpact 2/2 + HeavyImpact 5/5、官方回读和短 SIE 通过，待玩家胸/左右、恢复与 Light→Heavy 验收。
- 追猎者新增无 Tick攻击组件：近距斧击 Sweep 18 伤害，中距跑跳离地时一次预判、Landed 160 cm / 30 伤害。完整构建、PredictionAndBallistics 1/1、资产回读与短 PIE 通过，待移动玩家公平性和动画观感验收。
- 上一轮夜间发现的 Level0/冲撞动画 Dirty 与磁盘删除分叉已由白天提交状态覆盖；本轮官方回读相关资产均非 Dirty。
- 夜间快照 `07d7cf3852a419147d7e547c7f6ce49d6e8d05b2` 与回填 `c6e867bab1af1ac67c719e4797eb200016fc8783` 已推送内部工蜂。

## 2026-08-10 至 2026-08-11

- HeavyImpact 阶段验收完成：真实 Chaos 位移、有限关节张力、Snapshot 到起身 Montage、同来源防夹与恢复追逐；玩家 0.15/0.50 秒、追猎者 0.15/3.0 秒，HeavyImpact 5/5。
- 壁挂预判抛射 Chaos 机关基础机制由用户验收，权威参数 900 cm / 18 deg / 50 kg / 8 s；后续完成外观试装和同一 Projectile Actor 的 Loaded 预装生命周期，画面专项仍待验收。
- 磁力投掷物碰撞破碎 P0 完成用户验收并归档；薄墙、角落、多物体竞态为回归清单，P1 再次投掷碎片需另立任务。
- 统一 StandingImpact 玩法层与局部物理增量形成；不得恢复全身常驻受控物理、Light/Heavy 大一体化或跨对象磁力租约方案。

## 2026-08-01 至 2026-08-09 摘要

- 主菜单 Seed/难度 → PCG → Population → 玩家/追猎者，以及 Exit/死亡/暂停/结算/重开闭环已实现并有阶段证据。
- 多层路线冻结并落地为“跨层宏结构 → 逐层二维 WFC → 合并整栋通行图”；Seed 30794 原问题楼梯经用户确认追猎者可上，完整追逐/全楼梯/三档 300 Seed 仍待验收。
- 摆锤、冲锤、预判抛射机关、HeavyImpact 起身恢复桥与相机时序已形成；同场景 Recast 运行时缺失一直是未闭环风险。

## 2026-07 月度摘要

- 建立 UE5.8 C++ 优先 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 协同规范。
- PCG 从二维 Grid-WFC 推进到 Runtime HISM、Population、最小 RoundFlow，并确定跨层宏结构 + 逐层二维 WFC 的正式方向。
- 追猎者 Timer 状态机、locomotion、基础受击、地刺、磁力抓取与玩家生命链路形成；第三方 SFCorridors 只读筛选，删除仍需依赖闭包和用户授权。

<!-- compacted by Codex nightly at 2026-08-13 01:08 +08:00 -->
