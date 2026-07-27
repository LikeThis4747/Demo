# Current Task

- ID：`TASK-20260727-001`
- 目标：按用户授权，将 `physics-control-hit-spike-minimal-2026-07-27` 接入现有追猎者，并完成本机 UE5.8 兼容、构建、蓝图装配与 PIE 验证。
- 范围：修改 `Demo.Build.cs`、`PursuerCharacter.h/.cpp`；新增 Physics Control 局部受击 Component、调参 DataAsset 类型与 `DA_PursuerPhysicsControlHit`；仅设置 `BP_Pursuer.PhysicsControlHitTuning`。
- 明确排除：不修改 `DA_Pursuer` 参数、Level0、Manny、Physics Asset、AnimBP、磁力系统、伤害、倒地或 AI 逻辑。
- UE5.8 兼容：三个 Physics Control Set API 返回 `void`；`FPhysicsControlData` 无 `bUseAccelerationDriveMode`；`FPhysicsControlModifierData` 使用 6 参数构造。
- 验收：DemoEditor 构建；BP 编译保存；LeftArm/RightArm/Torso；至少 10 次连续命中；约 0.55 秒恢复；Capsule/追击/攻击稳定；无新增 Error。
- 当前阶段：已授权实现，正在接入源码。
- 其他任务：V4 PCG 玩家验收暂缓但未取消；完成本次接入后恢复其验收门槛。
