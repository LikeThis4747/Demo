# Progress — Demo

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
- [ ] 壁挂式制导机关 PIE 手感验收：预警/实体可读性、默认移动目标命中、600～700 cm 静止专项、急停/横移躲避、后续纯 Chaos 反弹与薄墙连续多发。
- [ ] 轻受击融合方案：先确定共享来源/方向/强度输入、轻重状态优先级和表现边界，再决定动画、局部 Physics Control 或混合实现。
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
- 壁挂式制导机关当前是“实现、资产和构建已完成，运行手感待用户验收”；静态碰撞合同、Blueprint UpToDate、资产非脏和构建成功不能替代真实 PIE 命中与画面确认。

<!-- written by shiqiqiwang at 2026-08-10 11:41 UTC -->

## 2026-08-10 — 预判抛射机关触发缺陷修复

- 用户首次 PIE 暴露两个 Level0 旧实例因 Muzzle 父级仍为 SceneRoot 而在 BeginPlay 自禁用。
- 已修正两个关卡实例父级，并为 C++ 增加旧实例自愈与详细诊断。
- Demo -NoLink、正式链接和定点 PIE 触发链通过；画面、命中、躲避、反弹与薄墙仍待用户验收。
