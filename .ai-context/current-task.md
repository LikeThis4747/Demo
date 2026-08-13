# 当前任务

- 当前受击系统新机关接入权威：`DOC/Outputs/Physics/CHARACTER_IMPACT_INTEGRATION_GUIDE.md`。
- 2026-08-13 用户已接受制导/磁力轻受击收口：异常前冲已修复；`Slow + 局部物理` 方向成立，物理反馈偏弱但当前勉强够用，不再扩大本轮物理架构。
- Light 接入使用命中后的 `ICharacterImpactReceiver::SubmitStandingImpact`，来源 Profile 分别映射玩家/追猎者的 None、Slow、Stop；新增机关不应修改角色接收核心。
- Heavy 使用接触前 `IHeavyImpactReceiver::PrepareForHeavyImpact`；机关必须提供自身几何/运动的预测并随后发生真实 Chaos 接触，不能只配枚举。
- 当前唯一受击完整度 P0：玩家 Stop 的 Front/Left/Right 动画引用仍为空。下一任务为 `TASK-20260813-005-玩家Stop方向受击动画补齐`。
- 玩家 Stop 首轮计划：复用追猎者三条方向 Sequence，按已验收 GetUp 工作流制作玩家 Skeleton 副本，装配 `DA_PlayerStandingImpact`；第一轮不改 CharacterImpact/HeavyImpact/AnimBP。
