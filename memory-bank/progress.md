# Progress — Demo

## 2026-08-11 真实弹体预装技术交付（待用户画面验收）

- 壁挂预判抛射机关改为同一个 Projectile Actor 从 Loaded 预装到离膛后开启 Chaos；没有预览替身、Projectile Tick 或飞行中追踪。
- Demo 模块已链接；保存日志证明两台 Launcher 均完成 loaded -> started -> released loaded projectile，关键 Blueprint/DA 当前干净且引用正确。
- 技术证据不替代用户画面验收；正式一局接入仍需先解决同场景 Recast 追逐风险。
- 2026-08-12 夜间发现 Level0 与冲撞动画存在 Editor Dirty/磁盘删除分叉，必须由用户白天确认保存或放弃。

## 2026-08-11 磁力投掷物碰撞破碎 P0 验收归档

- 初版命中 AI 后“停住再下落”的问题已修正：合格 Hit 当帧冻结运动，按真实接触方向和 NormalImpulse / 质量恢复有限法向速度。
- 当前稳定参数为门槛 5000 kg·cm/s、速度保留 0.6、继承上限 5000 cm/s、碎片分离 350 cm/s、半径倍率 1.25。
- DemoEditor 构建、官方 UE5.8 MCP 属性回读、两个 Blueprint 编译、隔离 PIE 碎片散开/清理和用户 Level0 核心力量感验收均已通过。
- Review、DailyPlan 与任务卡已归档；薄墙、角落、多物体竞态保留为回归清单，P1 可再次投掷碎片另立任务。

## M0 基础设施与 UE5.8

- [x] C++ 优先单机 Demo、Project Memory MCP、内部工蜂 Git/LFS 与夜间只读维护。
- [x] UE5.8 DemoEditor 构建、官方/本地双 MCP 协同规范。
- [ ] UE5.8 下磁力 PIE 手感回归。

## M1 Runtime PCG 与多层场景

- [x] 16 OpeningMask Grid-WFC、最低带权熵、Domain Trail、有界回溯、Count/Connected/Tarjan 与确定性重试。
- [x] 600 cm 逻辑 Tile 展开为 300 cm 结构、Runtime HISM、HydroLab Presentation 与独立 Population。
- [x] 正式多层合同：跨层宏结构先冻结、逐层二维 WFC、三维占用/净空、整栋逻辑连通、明确 Start/Pursuer/Exit 与动态导航验收。
- [x] UE5.8 完整构建与 `Demo.PCG 22/22 + Demo.GameFlow 2/2`；Seed 30794 原问题楼梯已由用户确认追猎者可上。
- [ ] 玩家连续实走全部楼梯/旋转组合，检查碰撞、净空、护栏、相机和最终视觉。
- [ ] 正常玩法中的真实追猎者完整跨层追逐；当前 Level0 静态存在 Recast，但最新保存 PIE 日志仍有 Recast 缺失。
- [ ] Easy/Normal/Hard 各至少 300 Seed 的成功率、分布、重试和耗时统计。
- [ ] 目标机软件 Lumen 与无 Lumen 室内补光双档验收。

## M2 玩法压力、物理机关与追猎者

- [x] 追猎者 C++ Timer 状态机、攻击时机、DataAsset 与 locomotion 装配。
- [x] 旧追猎者局部 Physics Control 受击保留为停用回退路径。
- [x] Level0 常驻摆锤完成原型与用户初步手感验收。
- [x] 自动周期冲锤、相机更新顺序/位置延迟与壁挂式一次性制导机关完成各自实现和既定构建/静态证据。
- [x] 壁挂式制导机关已替换旧推进方案：预警期用 UE5.8 弹道求解预测移动交点并机械转向，发射后只施加一次质心冲量且全程由 Chaos 处理；默认资产、Blueprint、Level0 正面迎击布局和 Demo-only 正式链接均通过。
- [x] 玩家/追猎者共享 HeavyImpact 已完成阶段验收：真实 Chaos 决定位移，Physics Control 维持有限关节张力，提前 Snapshot 混入起身 Montage，并包含同来源防夹。最终参数为玩家 0.15/0.50 秒、追猎者 0.15/3.0 秒；官方 MCP 回读、Demo 模块构建和 5/5 自动化均已通过。
- [x] HeavyImpact 防夹与最终恢复现场复测完成；用户确认当前效果可作为稳定阶段基线，极端封闭空间与细小动画差异转为非阻塞候选优化。
- [x] 壁挂式预判抛射机关基础机制已由用户验收：当前权威参数为 900 cm / 18 deg / 50 kg / 8 s。
- [ ] 壁挂式预判抛射机关后续专项：移动目标命中、近距静止、急停/横移躲避、连续 Chaos 反弹、薄墙连续多发，以及模型/表现和轻受击接入。
- [ ] 轻受击第一版已完成代码、配置、DataAsset、Blueprint 装配与技术验证：磁力验证 AI Stop，地刺验证玩家 Stop / AI Slow，Heavy 优先级与空中保留 Z 已落地；真实手感和交叉场景待用户验收。
- [ ] 冲锤玩家/追猎者手感和安全窗口验收；接入正式一局前只选择一个已验收原型。
- [ ] 近战/方向受击、磁力 Camera 通道、重复投掷和连续局部 Physics Control 命中的当前版本验收。

## M3 正式一局流程

- [x] 主菜单 Seed/难度 → PCG → Population → 玩家/追猎者/陷阱/资源装配。
- [x] GameState 局状态机、Exit 判胜、Health 归零判负、结算、重开/下一把/选关/回主菜单与 ESC 暂停。
- [ ] 生成失败重试、暂停/恢复、胜负、同 Seed 重开与新 Seed 下一把的多层联合回归。
- [ ] UI 审美统一打磨（排期最后）。

## 当前边界

- 多层 PCG 与整局流程属于“已实现并有自动/代表性证据，待完整可玩验收”。
- HeavyImpact 当前重受击效果已完成用户最终验收并归档；下一阶段只讨论轻受击如何与其共享输入和仲裁，不默认复用完整全身物理/倒地起身状态机。
- 壁挂式预判抛射机关当前基础机制已获用户验收；模型/表现、轻受击接入与移动命中、躲避、连续反弹、薄墙等专项仍是独立后续，不扩大为全部边界已验收。

<!-- written by shiqiqiwang at 2026-08-10 11:41 UTC -->

## 2026-08-10 — 预判抛射机关触发缺陷修复

- 用户首次 PIE 暴露两个 Level0 旧实例因 Muzzle 父级仍为 SceneRoot 而在 BeginPlay 自禁用。
- 已修正两个关卡实例父级，并为 C++ 增加旧实例自愈与详细诊断。
- Demo -NoLink、正式链接和定点 PIE 触发链通过；画面、命中、躲避、反弹与薄墙仍待用户验收。

<!-- written by shiqiqiwang at 2026-08-10 12:09 UTC -->

## 2026-08-10 — 预判抛射机关手感增量

- 默认速度由约 1000 cm/s 提高到 1224.97 cm/s，质量由 25 kg 提高到 50 kg。
- 新增发射后 8 秒 Actor LifeSpan 自动清除，不启用 Tick。
- Demo -NoLink、正式链接、官方 DataAsset 回读和最小 PIE 生命周期验证通过；最终手感仍待用户验收。

<!-- written by shiqiqiwang at 2026-08-10 12:22 UTC -->

## 2026-08-10 — 预判抛射机关基础版本验收

- 用户确认当前 900 cm / 18 deg / 50 kg / 8 s 版本验收通过。
- 模型更换、表现细节和轻受击接入保留为后续独立增量。
- 下一阶段先提交验收版，再讨论旧代码与文档权威入口清理。


## 2026-08-10 — 预判抛射机关验收后清理

- 删除弹体重复的首次阻挡布尔状态，`Phase` 成为首次碰撞阶段的唯一状态来源。
- 关闭权威胶囊无效的 Overlap 事件生成；HeavyImpact `PreparationVolume` 仍独立负责可选 Pawn Overlap。
- 禁用失败弹体增加 1 秒 LifeSpan 回收；正常弹体继续使用 DataAsset 的 8 秒寿命。
- 当前方案权威统一为正式 DailyPlan、DailyReport、Source 与默认 DataAsset；旧推进文档只作历史。
- Demo `-NoLink` 与 DemoEditor 正式链接成功，只构建 Demo 项目模块；Blueprint、DataAsset、Level0、模型和轻受击均未修改。


## 2026-08-10 — 统一轻受击响应第一版技术交付

- 新增共享 Standing Impact 输入、来源 Profile、角色协调组件与 None/Slow/Stop 结果；HeavyImpact 内部保持不变。
- 磁力物当前映射为玩家 None / 追猎者 Stop 0.60 秒；地刺为玩家 Stop 0.25 秒 / 追猎者 Slow 0.60 秒、速度倍率 0.45。
- 空中 Stop 只清水平速度；AI Slow/Stop 都断攻击，只有 Stop 取消 PathFollowing；Stop 后立即恢复追击但不提前结束攻击冷却；Heavy 真正 Prepared 后原子清 Light。
- Demo-only 构建成功，CharacterImpact 2/2 + HeavyImpact 5/5，Blueprint/DataAsset/运行时碰撞基线经官方 MCP 回读。
- 真实磁力命中、地刺手感、Light/Heavy 交叉与不同帧率仍待用户验收；玩家动画待人工重定向。

## 当前夜间优先级

- 先完成统一轻受击真实命中/空中 Stop/Light-Heavy 交叉手感与同场景 Recast 追逐证据；之后只选一个已验收机关组合进正式一局，磁力破碎 P0 已验收归档；P1 可再次投掷碎片仍需另行方案与授权。

<!-- written by shiqiqiwang at 2026-08-11 06:40 UTC -->

## 2026-08-11 — 预判抛射机关外观试装

- 完成 `BP_ThrustGuidedHazardLauncher` 的墙板、轴承、随动 U 形托架、灯座/灯罩纯视觉装配；全部为 `NoCollision`。
- 完成 `BP_ThrustGuidedHazardProjectile` 的 `SM_barrel3` 视觉替换与胶囊内居中，未改变发射、质量、弹道或 Chaos 碰撞合同。
- 两个 Blueprint 编译零 Warning/Error；Level0 未纳入最终改动；临时 PIE 确认机关仍能锁定并发射。用户画面验收待办。

<!-- written by shiqiqiwang at 2026-08-11 09:29 UTC -->

## 2026-08-11 — 物理轻受击状态纠正

- 2026-08-10 的 Standing Impact 第一版技术交付事实保留，但其 `None/Slow/Stop`、攻击打断与 Montage 只属于玩法/动画层；用户已现场否定它作为“物理轻受击”画面，不能再描述为只待普通手感验收。
- 全身常驻受控物理、Light/Heavy 大一体化，以及磁力物—角色 `Reserve/Prepare/Commit/Clearing` 跨对象租约均非权威方案，不得据此实施。
- 当前唯一授权方向是隔离追猎者效果探针：固定 Capsule 与 Idle、碰撞前已模拟的上半身、独立普通 Chaos 箱子；生产 Heavy、磁力、玩家和 ABP 保持不变。
- 探针先以 60 FPS 画面判断箱子失速/偏转、命中身体链让位、骨盆/脚稳定与自然恢复。画面未通过前不抽共享生产组件，也不实现预测、租约或兼容状态机。

<!-- written by shiqiqiwang at 2026-08-11 12:07 UTC -->

## 2026-08-11 — 物理轻受击效果原型进一步收敛

- 上一条“隔离探针 + 独立 Chaos 箱子/固定 Capsule”描述已被本条覆盖：当前原型不生成、不绑定、不识别任何箱子，也没有 Capsule。
- 第一轮新增 C++ 为 0，只创建独立测试 Physics Control Asset、普通 Actor Blueprint 测试目标和独立关卡；用户可用任意现有真实模拟物体直接撞击。
- 目标只验证九个追猎者上半身刚体受动画约束时的真实双向碰撞、部位相关让位、站立稳定与自然回稳；生产 Heavy、磁力、玩家、追猎者和 Level0 全部冻结。
- 画面通过前不抽共享组件、不预留来源接口；最多三组参数仍无明显改善即止损。

<!-- written by shiqiqiwang at 2026-08-12 08:33 UTC -->


## 2026-08-12 轻受击动画与局部物理融合

- 已实现 StandingImpact 可选上半身局部 Physical Animation 表现，并保留 None/Slow/Stop、Stop 方向动画、AI 攻击打断和移动规则。
- 追猎者与磁力来源首轮装配完成；玩家与地刺局部物理保持关闭。
- Heavy 仅新增校验后、捕获前清场 Delegate，既有状态机/PCA/击飞/倒地/起身未改。
- Demo 模块构建、CharacterImpact 2/2、HeavyImpact 5/5、官方 MCP 回读和短 SIE 通过。
- 状态：技术交付完成，用户 PIE 画面与交叉行为验收待办。

<!-- written by shiqiqiwang at 2026-08-12 10:15 UTC -->


## 2026-08-12 PCG 机关与资源分层放置验收归档

- [x] 摆锤、地刺、冲锤、制导发射器与磁力资源接入一个 GameplayPopulator；旧 OneEveryNCells + MaxCount 规则直接移除。
- [x] 高厅摆锤必放；普通机关类型先抽；机关实际操作格互斥；资源独立图距离采样并在格内安全区域随机 X/Y。
- [x] 三档纯值配置、真实 Actor 预算、原子 Spawn/Rollback、发射器预装/离膛弹体清理与生成时快照完成。
- [x] Demo 模块链接、Population 8/8、完整 PCG 29/29、官方 MCP 资产回读与 L_Game Normal Seed 12345 接入通过；机关 36/36、资源 13/13。
- [x] 用户连续否决过稀初值后，机关密度收口为 Easy/Normal/Hard 26/28/30、间距统一 2，并用权重 5:3:2 / 1:1:1 / 2:3:5 区分构成；用户已确认当前版本验收通过，多 Seed 统计转为后续回归。
