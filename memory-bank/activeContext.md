# Active Context — Demo

## 当前焦点

- 统一轻受击响应第一版已完成技术交付，等待用户现场验收。
- HeavyImpact 已验收且保持不变；Standing Impact 只负责不会击飞/击倒的 None/Slow/Stop。
- 磁力物与地刺是首轮验证来源；高压气罐和地刺 Heavy 不在本轮预建。

## 当前实现合同

- 来源决定允许的玩法结果，真实接触数据只决定结果内部的方向与强度；不做全局冲量自动判轻重。
- 固定优先级：Heavy > Stop > Slow > Inactive。Heavy 真正 Prepared 后先恢复 Light 的速度上限写入，再清 Light；Heavy Busy 不排队。
- 玩家 Stop 锁移动、跳跃、新抓取和投掷，保留镜头与松手。AI Slow/Stop 都断攻击，只有 Stop 取消 PathFollowing；Stop 结束后立即恢复追击，但不提前结束攻击冷却。
- Falling Stop 只清 XY，保留 Z、Falling 与重力。
- AttackProjectileBody：角色 Capsule Block、Mesh Ignore，避免 Light 投掷物持续顶住 Heavy/Downed 身体。

## 当前验证

- Demo-only 正式链接成功；CharacterImpact 2/2 + HeavyImpact 5/5。
- 四个 Blueprint、四份 DataAsset 与运行时装配经官方 MCP 回读。
- 短 PIE 无配置错误，但没有替代用户对真实命中画面的验收。

## 下一步

- 用户测试磁力物命中追猎者、地刺玩家/AI、空中 Stop 与 Light/Heavy 抢占。
- 玩家方向动画按既有 GetUp 工作流复制并人工 Replace Skeleton、Persona 检查后再装配。
