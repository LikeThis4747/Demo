# Daily Log — Demo

> 按日期倒序；只保留完成、验证、决定与遗留，过程细节见任务卡和审计归档。

## 2026-07-26

- 夜间只读审计：本地 UE Editor MCP 在线；PIE 已停止，Generator/Harness Blueprint 均 UpToDate、无自定义连线，测试关卡装配 Generator、Harness、Staging、PlayerStart 与三盏基础灯。官方 UE5.8 MCP 入口未暴露，未补充通用 DataAsset 属性审计。
- 最新磁盘证据保持：DemoEditor 构建成功，`Demo.PCG` 19/19，288/288 Seed Sweep，Planning P50/P95/Max=23.145/233.470/622.386 ms；SelectedViewport PIE 为 48 Cells / 798 Instances / 5 HISM，Harness 传送成功。
- 新发现：停止状态测试关卡 Outliner 未见 `NavMeshBoundsVolume` 或 `RecastNavMesh`；导航仍无真实可达证据，追猎者接入前必须补齐。
- 遗留：玩家抽查至少 10 个 Seed，验收路线、接缝、碰撞、净空、导航与 Start→Exit；四张 active 任务卡中只保留 PCG 玩家验收为关键路径，其他白天暂停/归档并修正重复 ID。
- 夜间未运行测试、编译、PIE 或保存；未修改项目代码、资产、配置或规范。

## 2026-07-25

- 用户授权后，仅为共同根材质 `M_HydroLab` 启用 Instanced Static Mesh Usage；无副本、映射、Runtime 绕过或永久脚本。
- V3.2 路线观感未通过后实施 V4 全图 Grid-WFC：删除固定主干/Gate/Optional Envelope/孤岛剪枝；实现最低带权 Shannon 熵、Domain Trail、chronological backtracking、Count、MaxConsecutive 与 Connected/Tarjan。
- `NoValidWfcSolution` 是完整树无解证明并立即停止；仅预算耗尽建立确定性有限尝试，全部尝试共享整局预算。
- 实施前快照 `b8a8c6e30b5f5e7c73c382e64c62c0e4d1706e78` 已推送内部工蜂。
- 真实 V4 Profile 已从磁盘验证；构建、19/19 自动化、288/288 Seed Sweep 与 SelectedViewport 技术烟测通过，代码终审无阻断。
- 玩家视觉、碰撞、净空、接缝、导航与 Start→Exit 验收未完成；Harness/测试资产在正式 GameFlow 接管前保留。

## 2026-07-24

- PCG V3.2 完成构造性 Progression、无回溯 Grid-WFC、300 cm 分离结构展开、Runtime HISM、13/13 自动化与 288/288 Seed Sweep。
- Demo 升级 UE 5.8；双 MCP 能力矩阵与协同规范归档。

## 2026-07-23

- 开发顺序冻结为“实时整关生成 → 追猎者 → 地图内玩法闭环”；SciFiHydroLab 经 300 cm 分离结构实拼入选。

## 2026-07-18

- 创建 C++ Demo、轻量渲染与 C++ 优先工作流；初始化 Git LFS、内部工蜂备份和夜间只读维护。
