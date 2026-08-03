# Progress — Demo

## M0 基础设施与 UE 5.8

- [x] C++ 优先单机 Demo、项目 Memory MCP、Git LFS 与内部工蜂备份
- [x] UE 5.8 DemoEditor 构建与双 MCP 协同规范
- [x] 2026-08-03 当前工作区完整构建成功；`Demo.PCG` 21/21、`Demo.GameFlow` 2/2
- [ ] UE 5.8 下磁力 PIE 手感回归

## M1 实时 PCG 场景

- [x] 全图 16 OpeningMask Grid-WFC；最低带权熵、Domain Trail、有界时间序回溯
- [x] Count、MaxConsecutive、Connected/Tarjan 全局约束与确定性有限重试
- [x] 600 cm 逻辑 Tile 展开为 300 cm 结构并用 Runtime HISM 实例化
- [x] HydroLab Presentation、Generation Profile、根材质 HISM Usage、运行时灯光与独立 Population
- [x] 多层合同与正式实现：完整楼梯/高厅预放置、三维占用/净空、逐层二维 WFC、整栋逻辑连通、明确 Start/Pursuer/Exit、结构表现与动态导航门
- [x] 2026-08-03 代表性 PIE：跨 World 换 Seed 后成功生成 3 层，11 点投射、10 条路径、24 个玩法对象
- [ ] 玩家连续实走双层楼梯与贯通三层楼梯间，检查碰撞、净空、护栏、相机和最终视觉
- [ ] 正式追逐中的真实追猎者跨层上/下楼；开局路径存在性检查不能替代此项
- [ ] Easy/Normal/Hard 各至少 300 Seed 的成功率、楼层/楼梯/高厅分布、重试和耗时统计
- [ ] 目标机软件 Lumen 与无 Lumen 室内补光双档验收

## M2 玩法压力与追猎者

- [x] 追猎者 C++ Timer 状态机、追击/攻击时机、DataAsset 与 BP 装配
- [x] Physics Control 局部受击源码、调参 DataAsset 与历史多肢体命中
- [x] 最小 locomotion AnimBP/BlendSpace 资产与历史 UpToDate 证据
- [x] 地刺、玩家 HealthComponent 与生命归零广播接入正式胜负流程
- [ ] 近战/方向受击、AttackProjectile Tag、磁力 Camera 通道与重复投掷边界的当前验收
- [ ] Physics Control 连续至少 10 次命中、目标区域与恢复压力验收
- [ ] 追猎者对地刺的免疫、受伤或受阻语义

## M3 正式一局流程

- [x] 主菜单 Seed/难度 → PCG → 玩家/追猎者/陷阱/资源装配
- [x] GameState 局状态机、ExitVolume 判胜、Health 归零判负
- [x] 结算界面下一把/重开/选择关卡/回主菜单、ESC 暂停菜单；2026-08-03 用户验收
- [x] 多层生成完成导航验收后再装配玩法对象；可恢复失败确定性换 Seed 跨 World 重试
- [ ] 生成失败重试、暂停/恢复、胜负、同 Seed 重开与新 Seed 下一把的多层联合回归
- [ ] UI 审美统一打磨（排期最后）

## 当前边界

正式多层实现已落入工作区并有构建、23 项自动化与代表性 PIE 证据，但仍属于“已实现、待可玩验收”。静态路径查询、自动化和夜间快照都不能替代玩家实际行走、真实追猎者追逐与用户验收。
