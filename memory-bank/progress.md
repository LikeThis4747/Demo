# Progress — Demo

## M0 基础设施与 UE 5.8

- [x] C++ 优先单机 Demo、轻量渲染基线、项目 Memory MCP、Git LFS 与内部工蜂备份
- [x] UE 5.8 DemoEditor 构建成功；本地 UE Editor MCP 与官方 MCP 协同规范已归档
- [ ] UE 5.8 下磁力 PIE 手感回归

## M1 实时 PCG 场景

- [x] V4 全图 16 OpeningMask Grid-WFC；最低带权熵、Domain Trail、有界 chronological backtracking
- [x] Count、MaxConsecutive、Connected 五节点展开图与迭代 Tarjan 传播
- [x] NoValid 完整树无解立即停止；仅预算失败进行确定性有限尝试并共享整局预算
- [x] 600 cm 逻辑 Tile 展开为 300 cm Floor/Ceiling/Wall/Trim/Pillar，Runtime HISM 实例化
- [x] HydroLab Presentation、V4 Profile、Generator/Harness 装配；根材质 HISM Usage 已处理
- [x] UE 5.8 构建、`Demo.PCG` 19/19、288/288 Seed Sweep
- [x] SelectedViewport 技术烟测：48 Cells、798 Instances、5 HISM，Harness 传送成功
- [x] 只读 UE 审计：Generator/Harness Blueprint UpToDate、无自定义连线
- [ ] 玩家抽查至少 10 个 Seed，验收路线、接缝、碰撞、净空与 Start→Exit
- [ ] 补齐真实导航证据；当前测试关卡未见 NavMesh Bounds/Recast actor
- [ ] 玩家验收后接入追猎者，再做单局玩法闭环

## 当前边界

构建、自动化、Seed Sweep、资产烟测和 Runtime 日志不等于玩家验收。当前无已确认代码阻断；Planning Max 约 622 ms 需目标设备体验。导航、玩家走通和视觉/碰撞验收通过前，不进入追猎者或室内灯扩展。
