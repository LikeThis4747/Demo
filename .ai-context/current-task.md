# Current Task

- ID：`TASK-20260723-002`
- 目标：完成《零号逃亡》V4 实时 PCG 玩家验收，再进入追猎者与玩法闭环。
- 实现：全图 16 OpeningMask WFC；最低带权 Shannon 熵；Domain Trail + 有界 chronological backtracking；Count、MaxConsecutive、Connected 五节点展开图/迭代 Tarjan。
- 重试：`NoValidWfcSolution` 为完整树无解并立即停止；仅 `SolverBudgetExhausted` 使用确定性有限尝试，全部尝试共享总预算。
- 配置：24x16、48..72 Walkable、MaxStraight=4、Candidate/Backtrack=100000/25000、SolveAttempts=10。
- 验证：UE 5.8 构建成功；`Demo.PCG` 19/19；288/288 Seed Sweep；Planning P50/P95/Max=23.145/233.470/622.386 ms。
- PIE：SelectedViewport，`ZE_PCG_RESULT schema=4 success=1`，48 Cells、798 Instances、5 HISM；Harness 传送成功。
- 2026-07-27 只读审计：无新项目变更；本地 UE Editor MCP Ready、PIE Stopped；Generator/Harness Blueprint UpToDate。官方 UE5.8 MCP 入口未暴露，通用 DataAsset 属性/引用审计未执行。
- 当前门槛：玩家抽查至少 10 个 Seed，验收路线、接缝、碰撞、净空、导航与 Start→Exit。
- 导航风险：当前 Outliner 仅见 AbstractNavData，未见 `NavMeshBoundsVolume` 或 `RecastNavMesh`；追猎者接入前必须补齐真实导航可达证据。
- 边界：只保留本任务为关键路径；室内灯、素材体检、追猎者和 GameFlow 均不抢占；Harness/测试资产保留。
