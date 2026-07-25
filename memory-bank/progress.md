# Progress — Demo

## M0 基础设施与 UE 5.8

- [x] C++ 优先单机 Demo、轻量渲染基线、项目 Memory MCP、Git LFS 与内部工蜂备份
- [x] 从 UE 5.7.4 升级到 UE 5.8；DemoEditor 构建成功
- [x] 本地 UE Editor MCP 与官方 UE5.8 MCP 共存规则、能力矩阵已归档
- [ ] UE 5.8 下磁力 PIE 手感回归

## M1 实时 PCG 场景

- [x] V3.2 Progression + 构造性 Grid Layout + 16 Mask 无回溯 WFC
- [x] 600 cm 逻辑 Tile 展开为 300 cm Floor/Ceiling/Wall/Trim/Pillar，并以 Runtime HISM 实例化
- [x] 删除旧 Graph/Socket/Portal/Catalog/A*/回溯 Solver 链与死配置
- [x] 项目自有 HydroLab Presentation 与 Generator 装配
- [x] UE 5.8 构建、`Demo.PCG` 13/13、288/288 Seed Sweep
- [x] 第三方共同根材质 `M_HydroLab` 启用 Instanced Static Mesh Usage；无副本、映射或 Runtime 绕过
- [x] 全新正常渲染 NewWindow PIE：27 Cells、444 Instances、5 HISM，Harness 传送成功，零相关 Usage 警告
- [ ] 用户验收材质、接缝、碰撞、净空与 Start→Exit 实际走通
- [ ] 通过 PCG 验收后接入追猎者，再做单局玩法闭环

## 当前边界

构建、自动化、Seed Sweep、资产烟测和 Runtime 日志不等于玩家验收。当前没有已知源码或素材设置阻塞；旧 `DA_LevelModuleCatalog` 在用户验收前不删除。
