# Current Task

- 当前任务：完成玩家/追猎者共享 HeavyImpact 物理受击原型的正式链接、A/B 对照、PIE 视觉验收，并规划真实倒地点起身恢复。
- 已完成：玩家/追猎者两份 PCA 已创建、Compile、保存并接入各自 Tuning DataAsset；角色 Blueprint 已装配；四项 HeavyImpact 自动化曾在上一版正式 DLL 中通过。
- 已完成：摆锤改为沿走廊纵向摆动，振幅 35°、主轴限位 40°、PreparationLookAheadDistance 600 cm。运行态已确认 Bob 为 QueryAndPhysics、Simulate=true、CCD=true、1000 kg，并沿 Y 轴连续摆动。
- 已完成：机关预测改为查询 HeavyImpact Receiver 提供的真实 Skeletal Mesh，并使用 SkeletalMesh 的全 Physics Asset 距离查询，避免只看 Capsule 或根刚体。
- 新增待链接代码：Prepared/Flight/Landing 分阶段 ParentSpace 控制倍率；开发 CVar `demo.HeavyImpact.PureRagdoll 0/1` 在同一碰撞、BodyModifier、状态机下切换受控 Physics Control 与 Controls 全关的普通 ragdoll。
- 新倍率默认值：Prepared Strength/Damping/Torque = 2/1.25/3；Flight = 1.75/1/3；Landing = 2.25/1.35/3.5。无 WorldSpace 控制、LinearStrength 仍为 0，不锁定骨盆世界位移。
- 新代码已通过 `DemoEditor Win64 Development -Module=Demo -NoLink -NoEngineChanges -NoHotReloadFromIDE -WaitMutex`；正式 DLL 尚未链接，运行时 A/B 尚未验证。
- 用户已关闭 Editor，并要求先提交/推送当前完整改动，再正式构建。
- 起身现状：Downed 保留真实倒地点；Recovering 只有枚举、尚无实现。玩家/AI AnimBP 均有 DefaultSlot，但项目内没有地面起身动画，且两者骨骼不同。
- 下一步：提交并推送 -> 正式链接 -> 重启 Editor -> 官方 MCP 回读新倍率 -> 自动化回归 -> PIE A/B；随后提交详细起身实现方案与资产/代码分工。
