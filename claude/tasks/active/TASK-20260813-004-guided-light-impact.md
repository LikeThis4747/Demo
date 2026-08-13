# TASK-20260813-004 制导投射物轻受击收口

- Owner: Codex `/root`
- Status: active
- Stage: implementation authorized
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
- 当日计划：`DOC/DailyPlan/2026-08-13-制导投射物与磁力投掷物轻受击收口.md`。

## Implementation checkpoint

- C++ 已完成：制导投射物在既有首次有效阻挡命中中提交 `StandingImpact`，没有新增 Hit 监听、Tick、碰撞通道或 Heavy 分支。
- 来源强度支持策划下限；制导来源在 `NormalImpulse` 缺失或过小时使用自身保底强度，磁力来源也获得有限的最低可见强度。
- 局部物理增加头部区域，并把实际命中点限制在受力骨骼附近后施加，不再固定打在骨骼中心。
- 资产已由官方 UE5.8 MCP 创建、装配、保存并持久回读；目标资产均为 `is_dirty=false`。
- DemoEditor Win64 Development 构建成功；CharacterImpact 2 项和 HeavyImpact 5 项自动化共 7/7 通过。
- SIE 已实际观察到制导投射物命中玩家后返回 Applied，并分别路由到 `head`、`upperarm_r`；`NormalImpulse=0` 时保底强度也生效。
- 技术链路已通过，用户对物理反馈是否足够明显的现场观感验收仍待进行；任务保持 active，不归档。
