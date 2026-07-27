# Current Task

- 当前状态：PCG、Population、最小 RoundFlow、地刺、玩家 Health 与追猎者 Physics Control/最小 locomotion 已形成运行证据；夜间只读审计见 `claude/artifacts/nightly/2026-07-28.md`。
- 当前最短目标：讨论并授权“生命归零 → GameFlow 失败 → 同 Seed 重开”的最小闭环；未获授权前不实现。
- 下一验收：在最新直走廊并排地刺配置下覆盖至少 10 个 Seed，检查路线、接缝、碰撞、净空、Start→Exit、动态导航和追猎者寻路。
- 测试边界：当前源码声明 18 个 Demo.PCG 测试；需要重新完整构建、运行当前测试与 Seed Sweep，不能沿用删除旧 Harness 前的 19/19 结论。
- Physics Control 边界：日志确认 ready 和多肢体命中，但连续至少 10 次、目标区域与恢复压力验收仍缺证据。
- UE 审计边界：本地 MCP 在线且六个相关 Blueprint/AnimBP 为 UpToDate；当前打开素材 Overview，官方 MCP 未暴露，因此测试关卡、NavMesh、DataAsset 属性和引用未完整复核。
- 工作区边界：包含并行对话的 Source、二进制资产、关卡、归档、记忆及 295 个 SFCorridors LFS 资产；不得回退、删除或混合认领。
- 活动记录：`claude/tasks/active` 三张追猎者卡与实际状态存在漂移，白天由对应 Owner 核对。
