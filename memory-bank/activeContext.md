# Active Context — Demo

## 当前焦点

- 磁力投掷物碰撞破碎 P0 已由用户验收并归档；其唯一 Hit/CCD/ImpactId 事务继续只归磁力物自身，不与角色身体控制绑定。
- 2026-08-10 的 Standing Impact 第一版已完成技术装配，但 None/Slow/Stop、攻击打断和 Montage 只属于玩法/动画层；用户已否定其作为“物理轻受击”画面。
- 全身常驻受控物理、Light/Heavy 大一体化，以及磁力物与角色之间 Reserve/Prepare/Commit/Clearing 均已撤销为非权威研究草稿。
- 当前只规划来源无关、零 C++ 的追猎者物理轻受击效果原型：独立测试 PCA + 普通 Actor Blueprint + 独立测试关卡。用户用任意真实模拟物体直接碰撞，效果资产不识别或管理来源。

## 已验证边界

- HeavyImpact 当前重受击效果是用户已验收基线；原型阶段不修改其 C++、PhysicsControlAsset、DataAsset、碰撞、倒地、起身或误预测回滚。
- 追猎者 Physics Asset 具备 spine_02/03、head、左右 upperarm/lowerarm/hand 刚体，可组成九个上半身测试 Body；spine_01、pelvis、腿和脚明确排除。
- 官方 UE5.8 Physics Control API 可在 Blueprint 中按 Physics Control Asset 创建 Controls/Body Modifiers；第一轮不需要新增 C++。
- “物理轻受击”的最低画面证据是：外物相对 Kinematic 对照明显失速/偏转，命中身体链按部位让位，骨盆/脚稳定，并自然回到 Idle。
- Stop/Slow/Montage、非零日志或手工补 AddImpulse 都不能替代上述画面证据。

## 下一步

1. 完成并审计 `claude/docs/2026-08-11-追猎者物理轻受击效果原型拟实现代码.md` 与任务卡。
2. 协调 Level0 与 AS_Pursuer_ChargeRun_Work 的并行改动，完成完整基线 commit/push。
3. 用户明确授权后，只创建三个测试资产；若官方 MCP 不能安全写 PCA 嵌套数据或 Blueprint 图，直接请用户按表格在编辑器操作，不写 C++ 绕过。
4. 先做 60 FPS Kinematic/Controlled 画面对照，最多三组参数；失败即止损。
5. 只有画面获用户认可，才另立生产接入方案，比较局部常开与通用预激活，并根据真实重复职责判断是否需要共享身体控制组件。
