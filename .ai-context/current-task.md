# 当前任务

- 制导投射物轻受击技术链路已实现：第一次有效角色阻挡命中提交 StandingImpact，玩家为 Slow 0.40 秒、速度倍率 0.55、无动画、局部物理反馈。
- 磁力箱子命中追猎者保持 Stop + 三方向动画 + 局部物理，仅增加最低可见强度；未修改磁力投掷事务。
- DemoEditor 构建成功；CharacterImpact 2 项与 HeavyImpact 5 项共 7/7 通过；SIE 已实际确认制导命中返回 Applied，并路由到 head / upperarm_r，零 NormalImpulse 时保底强度生效。
- 当前只剩用户现场判断反馈是否足够明显、恢复是否自然；如仍不满足，不继续扩大物理架构，转 Stop + 受击动画。
- 追猎者 Heavy 攻击、DA_Pursuer、BP_ZeroEscapeCharacter、玩家 1000 生命与 HeavyImpact 参数均不得因本任务回退。
