# 当前任务

- 制导投射物 Light 已完成一次现场反馈收尾：第一次有效角色命中后弹体忽略 Pawn、继续碰撞环境，避免后续帧持续顶住 Capsule。
- Light 物理窗口内角色 Mesh 暂时忽略普通 PhysicsBody；退出时恢复完整 Collision Profile、CollisionEnabled 与响应容器，防止同一弹体二次挤压表现身体或留下 Custom 状态。
- 玩家参数为 Slow 0.40 秒、速度倍率 0.55、无动画；局部物理满强度冲量 13000、Hold 0.11 秒、BlendOut 0.22 秒。
- DemoEditor 模块构建成功；CharacterImpact 2 项与 HeavyImpact 5 项共 7/7 通过；标准 PIE 连续三发制导命中均 Applied，实际记录到 upperarm_l 的 13000 冲量，相关警告为 0。
- 当前仅待用户现场复测：奔跑跳跃中命中是否不再异常前冲，以及头/胸/左右臂反馈是否足够明显。未验收前任务保持 active。
- 不修改 HeavyImpact、CharacterMovement Velocity/Transform、追猎者攻击、磁力事务、地刺或其他机关。
