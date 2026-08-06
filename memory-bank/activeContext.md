# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源。

## 当前焦点

先把 HeavyImpact、摆锤修复、分阶段控制倍率和普通 ragdoll A/B 形成完整内部 Git 检查点，再正式链接并做运行验收；并行收敛真实倒地点起身方案。

## 已确认

- 玩家/追猎者共享 HeavyImpact 状态机、PCA、DA 与 Blueprint 装配已存在；旧追猎者局部受击保留但运行时 dormant。
- 真实 Chaos 接触决定击飞位移；角色侧不调用 AddImpulse/LaunchCharacter。
- 摆锤沿走廊纵向摆动；35° 振幅、40° 限位、600 cm 预测范围已消除机关主动停用问题。
- HeavyImpact 预测组件由玩家/追猎者接口返回 Skeletal Mesh；距离查询遍历 Physics Asset bodies。
- 分阶段倍率和严格 A/B CVar 已通过无链接编译；PCA 无需重新 Compile。
- A/B 的 Pure 模式只关闭 Controls，保留同一 Profile 的 Simulated/Collision/Gravity/Blend/CCD 与状态机。
- Downed 的 Actor/Capsule 已跟随到真实骨盆落点；只有未提交 Prepared 误判才允许恢复受击前 Transform。
- 玩家 ABP_Unarmed 与 AI ABP_Pursuer_Locomotion 都有 DefaultSlot；项目没有地面起身动画，两个角色骨骼不同。

## 当前门槛

- 用户已关闭 Editor。
- 先提交并推送当前完整工作区，再执行正式 DemoEditor 链接。
- 链接后重启 Editor，用官方 MCP 回读新倍率，跑自动化与 PIE A/B。
- 起身视觉必须等待兼容的仰面/俯面 in-place 动画；本轮先给出骨骼路线、代码范围与用户分工，不写假恢复。
