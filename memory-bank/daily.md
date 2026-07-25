# Daily Log — Demo

> 按日期倒序；只保留完成、验证、决定与遗留，过程细节见任务卡和审计归档。

## 2026-07-25

- HydroLab 材质门禁关闭：用户授权后，只为共同根材质 `M_HydroLab` 启用并保存 Instanced Static Mesh Usage；未建副本、映射、Runtime 绕过或永久脚本。
- 验证：全新正常渲染 NewWindow PIE 生成 27 Cells / 444 Instances / 5 HISM，`success=1`；Harness Teleport/Transfer 成功，零 HydroLab/HISM Usage 警告。
- 玩家路线验收未通过：固定中央长直路、路线数量过少和缺少运行时室内灯需要重构；旧 V3.2 自动化成功不再代表路线质量通过。
- 最新独立审计批准“完整可玩 Grid WFC + Connected/Count/MaxConsecutive + 有界普通回溯”进入 DailyPlan；正式计划为 `DOC/DailyPlan/2026-07-25-PCG-WFC路线重构实施方案.md`。
- 审计采纳项：删除固定主干/Optional Envelope、旧构造性见证和重复 WFC 热路径复核；预算须在新 Seed Sweep 取得 P50/P95/Max 后冻结。
- 按用户要求完成拟实现代码评审稿 `claude/docs/2026-07-25-pcg-wfc-route-refactor-code-proposal.md`；代码层补上完整候选路线超限后的回溯边界、Difficulty 权重规模约束、Candidate Attempt/Backtrack 精确计数和测试拆分。仅写方案/任务/记忆，未修改 Source/UAsset。
- 测试边界：Automation 回归长期保留；Runtime Harness 只保留到正式 GameFlow 接管生成和出生，之后另行申请删除，不把测试装配整体并入产品。
- PIE 偏好已从浮动窗口改为主编辑器视口；以后人工/MCP 验收显式使用 `SelectedViewport`。
- 本轮只完成计划和本机 PIE 偏好修正，未修改 PCG C++、UAsset、关卡或第三方素材；源码实施等待素材迁移交接和用户明确授权。
- 工作方式：同类“已确认必要、单项、可回退”的素材兼容设置可直接处理后通知；外观、几何、碰撞语义或批量第三方修改不在该授权内。
- 工作树含素材迁移、追猎者和来源待确认的并行改动；PCG 后续不得覆盖，未清理。

## 2026-07-24

- PCG V3.2：旧 Graph/Socket/Portal/Catalog/A*/带回溯 WFC 原子替换为构造性主干、Objective 双入口短回路、无回溯 Grid-WFC 与 300 cm 分离结构展开；UE 5.8 构建、13/13 自动化、288/288 Seed Sweep 和运行时生成通过。
- 项目自有 HydroLab Presentation 已装配；素材表现与玩家通行验收未完成，旧 `DA_LevelModuleCatalog` 在验收前暂不删除。
- Demo 已从 UE 5.7.4 升级到 5.8；官方 MCP 与既有 UE MCP 的能力矩阵和协同规范已归档。

## 2026-07-23

- PCG 顺序冻结为“实时整关生成 → 追猎者 → 地图内玩法闭环”；SFC 整块路线因结构与拼接不适配停止作为主方案。
- CorridorEnvironment 与 Sicka 两包结构覆盖不足；SciFiHydroLab 经 300 cm 分离结构实拼入选。

## 2026-07-18

- 创建 C++ Demo、轻量渲染与 C++ 优先工作流；初始化 Git LFS、内部工蜂备份和夜间只读维护。
