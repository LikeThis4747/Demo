# TASK-20260813-004 制导投射物轻受击收口

- Owner: Codex `/root`
- Status: completed
- Stage: user accepted and archived
- Baseline: `79262a743157638d0fd8d1eb6522519b447176c7`

## Goal

- 制导投射物第一次有效命中玩家时提交现有 `StandingImpact`，执行 `Slow + 明显局部物理`。
- 磁力箱子命中追猎者时保持现有 `Stop + 三方向动画`，并为有效来源冲量增加最低可见表现强度。

## Scope

- 制导弹体与其 Tuning DataAsset 的轻受击接线。
- CharacterImpact 来源强度保底、Head 区域、受限真实命中点施力。
- CharacterImpact 数据合同测试与既有 Heavy 自动化回归。
- 新建制导 Light Profile，并有限调整玩家/追猎者 StandingImpact 与磁力来源参数。

## Exclusions

- 不修改追猎者攻击、`DA_Pursuer`、`BP_ZeroEscapeCharacter`、玩家生命值。
- 不修改 Heavy 状态机/PCA/参数，不修改磁力事务、地刺、气罐、ABP、Level0、Config。
- 不新增共享身体组件、预测、来源租约、全身常驻物理或碰撞通道。

## Acceptance

1. 制导弹体命中玩家胸、左、右均触发 Slow 和可见局部偏转；无动画且保持可操控。
2. 磁力箱子命中追猎者保持 Stop + 动画，物理反馈不再接近零。
3. 局部物理按时恢复，无永久模拟、碰撞警告或 Heavy 回归。
4. 一次参数 A/B 后仍失败则停止扩写，转 `Stop + 玩家受击动画`。

## Current checkpoint

- 完整 Git 门禁已通过；本地、`origin/main`、远端 `main` 均为基线哈希，工作区起始干净。
- UE 当前关闭；先完成 C++/构建，再启动编辑器并使用官方 MCP 装配、测试和回读资产。
- 当日计划：`DOC/DailyPlan/archive/Done-2026-08-13-制导投射物与磁力投掷物轻受击收口.md`。

## Implementation checkpoint

- C++ 已完成：制导投射物在既有首次有效阻挡命中中提交 `StandingImpact`，没有新增 Hit 监听、Tick、碰撞通道或 Heavy 分支。
- 来源强度支持策划下限；制导来源在 `NormalImpulse` 缺失或过小时使用自身保底强度，磁力来源也获得有限的最低可见强度。
- 局部物理增加头部区域，并把实际命中点限制在受力骨骼附近后施加，不再固定打在骨骼中心。
- 资产已由官方 UE5.8 MCP 创建、装配、保存并持久回读；目标资产均为 `is_dirty=false`。
- DemoEditor Win64 Development 构建成功；CharacterImpact 2 项和 HeavyImpact 5 项自动化共 7/7 通过。
- SIE 已实际观察到制导投射物命中玩家后返回 Applied，并分别路由到 `head`、`upperarm_r`；`NormalImpulse=0` 时保底强度也生效。
- 技术链路已通过，用户对物理反馈是否足够明显的现场观感验收仍待进行；任务保持 active，不归档。

## 现场反馈后的有限收尾修正

- 用户确认 `Slow + 局部物理` 的方向成立，但发现奔跑跳跃中被制导弹命中会向前异常位移，头部反馈也偏弱。
- 制导弹在第一次有效角色命中并提交 Light 后，只把自身 `Pawn` 响应切为 Ignore；它仍保留 WorldStatic/WorldDynamic 等环境碰撞，避免后续帧持续顶住角色 Capsule。
- Light 物理窗口会暂时把角色 Mesh 对 `PhysicsBody` 的响应切为 Ignore，防止同一枚 50 kg 制导弹再次挤压刚启用物理的表现 Mesh；退出时完整恢复 Collision Profile、CollisionEnabled 与响应容器。
- 玩家局部物理首轮参数调整为：满强度冲量 `13000`、保持 `0.11s`、BlendOut `0.22s`；Heavy 参数、CharacterMovement 速度/Transform 均未修改。
- 修正后 DemoEditor 模块构建成功；CharacterImpact 2 项与 HeavyImpact 5 项仍为 7/7。
- 标准 PIE 自动命中确认玩家 `upperarm_l` 收到 `13000` 表现冲量，连续三发制导命中均提交 Applied；日志中没有无效 AddImpulse 或碰撞基线恢复警告。
- 该条是修正完成时的历史检查点；最终用户验收见下节。

## Final acceptance

- 2026-08-13 用户确认本次异常位移修正没有问题。
- 用户认为局部物理表现仍偏弱，但当前勉强够用；保留 `Slow + 局部物理`，不再扩大本轮物理架构。
- 新机关接入合同已整理到 `DOC/Outputs/Physics/CHARACTER_IMPACT_INTEGRATION_GUIDE.md`。
- 玩家 Stop 方向动画作为独立 P0 任务继续：`claude/tasks/active/TASK-20260813-005-玩家Stop方向受击动画补齐.md`。
