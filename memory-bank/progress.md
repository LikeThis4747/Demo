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
- [ ] 正常玩法中的真实追猎者完整跨层追逐。
- [ ] Easy/Normal/Hard 各至少 300 Seed 的成功率、分布、重试和耗时统计。
- [ ] 目标机软件 Lumen 与无 Lumen 室内补光双档验收。

## M2 玩法压力、物理机关与追猎者

- [x] 追猎者 C++ Timer 状态机、攻击时机、DataAsset 与 locomotion 装配。
- [x] 旧追猎者局部 Physics Control 受击保留为停用回退路径。
- [x] Level0 常驻摆锤完成原型与用户初步手感验收。
- [x] 自动周期冲锤、相机更新顺序/位置延迟、壁挂式一次性制导机关完成各自实现与既定构建/静态证据。
- [x] 玩家/追猎者共享 HeavyImpact 真实 Chaos 受击与起身恢复桥；当前 DLL、5/5 自动化和真实恢复日志具备。
- [ ] HeavyImpact 正躺/趴倒、墙边/墙角、完全堵塞/解除、二次接触门槛、Montage 混合和恢复移动的用户画面验收。
- [ ] 在具备 Recast 的正式环境确认追猎者起身后恢复追逐。
- [ ] 冲锤与制导机关玩家/追猎者手感和安全窗口验收；接入正式一局前只选择一个已验收原型。
- [ ] 近战/方向受击、磁力 Camera 通道、重复投掷和连续局部 Physics Control 命中的当前版本验收。

## M3 正式一局流程

- [x] 主菜单 Seed/难度 → PCG → Population → 玩家/追猎者/陷阱/资源装配。
- [x] GameState 局状态机、Exit 判胜、Health 归零判负、结算、重开/下一把/选关/回主菜单与 ESC 暂停。
- [ ] 生成失败重试、暂停/恢复、胜负、同 Seed 重开与新 Seed 下一把的多层联合回归。
- [ ] UI 审美统一打磨（排期最后）。

## 当前边界

- 多层 PCG 与整局流程属于“已实现并有自动/代表性证据，待完整可玩验收”。
- HeavyImpact 已从代码阶段推进到真实玩家/追猎者恢复日志，但少量恢复阻塞、编辑器内三项脏资产和用户画面验收仍未闭合。
- 静态回读、自动化、日志和夜间 Git 快照均不能替代玩家实际行走、真实追逐与用户体验验收。
