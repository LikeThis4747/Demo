# Progress — Demo

## 2026-08-11 磁力投掷物碰撞破碎 P0 冲击感修正

- 初版命中 AI 后出现“停住再下落”的力量感问题；原因是 next-tick 重读了受 Chaos 接触约束继续衰减的速度，且解簇叶子没有分离速度。
- 修正版在合格 Hit 当帧冻结运动，以真实接触方向和 NormalImpulse / 质量估算损失法向速度，默认保留 0.6，最大继承线速度 5000 cm/s；最小破碎冲量调整为 5000 kg·cm/s。
- Geometry Collection 解簇后对自身叶子施加 350 cm/s、包围球半径 1.25 倍的受控径向速度；它只分开碎片，不向命中目标重复提交 Light/Heavy 或伤害。
- DemoEditor Win64 Development 构建、官方 UE5.8 MCP 属性回读、两个 Blueprint warnings-as-errors 编译和隔离 PIE 碎片散开/清理通过。
- 当前仍待用户在 Level0 验收真实投掷命中 AI 的力量感、低冲量、薄墙/角落和竞态；验收前任务与 Review 保持 active。

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

- 先完成统一轻受击真实命中/空中 Stop/Light-Heavy 交叉手感与同场景 Recast 追逐证据；之后只选一个已验收机关组合进正式一局，磁力破碎运行逻辑仍待用户另行授权。
