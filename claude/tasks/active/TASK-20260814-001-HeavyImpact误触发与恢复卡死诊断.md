# TASK-20260814-001 HeavyImpact 误触发与恢复卡死诊断

- Owner：Codex `/root`
- Status：implementing
- Stage：用户已授权实现；等待完整基线 commit/push 后落盘功能
- Created：2026-08-14

## 用户反馈

1. Heavy 即将触发后经常失败并返回动画状态，视觉上出戏。
2. 倒地后偶尔长时间无法起身，不符合玩法节奏。
3. 尚未发生可信接触时角色已经开始击飞，存在“碰瓷”感。

## 只读范围

- `UHeavyImpactResponseComponent` 的 Prepare、Commit、超时、Landing、Downed、Recovering 与回滚时序。
- 摆锤、冲锤、追猎者攻击和制导机关的 Heavy 预测、真实接触与 Commit 入口。
- 玩家/追猎者 Heavy DataAsset、Physics Control Asset、Blueprint 装配和当前 PIE 日志。
- Heavy 自动化仅作合同证据；视觉与 Chaos 时序必须由真实 PIE 复现补证。

## 排除范围

- 本轮不修改 C++、Blueprint、DataAsset、Physics Asset、AnimBP、关卡或配置。
- 不触碰当前并行的刺轮修改。
- 未完成根因排序前不延长超时、不缩短起身时间、不放宽预测距离，也不增加强制动画回退。

## 输出

- 将三个症状分别对应到真实状态转换、触发源和日志证据。
- 区分确定根因、强嫌疑与待复现项。
- 给出最小修复顺序、风险和需要用户决定的玩法取舍；获得明确授权后再实施。

## 2026-08-14 已确认约束与方案默认

1. 机关候选预测可以静默消失，但不得让玩家看到 `Prepared -> Inactive` 的回正。
2. 接收端一旦返回 `Accepted`，本次 Heavy 即进入不可回滚阶段；正常超时仍继续 Heavy，只有进入 `Accepted` 之前的组件/资产基础设施失败允许内部恢复并写 Error 日志。
3. 物理飞行、落地和滚动没有总时长硬上限；不再使用 5 秒自由回退与 10 秒强制 Downed 作为正常玩法。
4. 不在第一次落地时抢跑起身，也不要求绝对静止。身体获得可行走支撑并持续降到低能量后进入 Downed；小幅噪声只衰减稳定进度，不每帧清零。
5. Downed 睡眠完成后立即尝试安全起身，不再固定等待 0.5 秒。正常站位受阻时每 0.2 秒重试，最多 3.0 秒。
6. 方案默认：3 秒到期仍无本地安全位置时，使用受击前已知安全的 Capsule 位置作为有界兜底；这可能产生极少数可见的小幅位置回退，需要用户在实施授权时一并确认。
7. Snapshot 到起身动画的显式淡入从 0.22 秒调整为 0.30 秒。

## 已授权最小实施范围

- 收紧 `ABatteringRamHazard` 与 `APendulumHazard` 的预测：PreparationVolume 只收集候选，最终请求必须通过锤头真实盒体对接收 Mesh 的短时几何 Sweep。
- `UHeavyImpactResponseComponent` 保持唯一 Heavy/Physics Control Owner，不新增组件、接口或状态；删除正常 Prepared 回滚、Settling 抢跑起身与 5/10 秒正常硬切。
- 清理已失效的 `FalsePositive` 命名、旧调参字段与对应测试，不保留双路径或兼容开关。
- 不修改 Light、追猎者攻击、Heavy PCA、AnimBP、起身动画、关卡或其他机关。

## 实施检查点

- [x] 根因审计、方案和拟实现代码经用户确认。
- [x] 创建 `DOC/DailyPlan/2026-08-14-HeavyImpact不可回滚与有界恢复.md`。
- [ ] 完成全工作区基线 commit/push、远端哈希与干净状态核验。
- [ ] 完成 C++、DataAsset 与自动化修改。
- [ ] 完成构建、官方 MCP 回读、自动化和 PIE 边界验证。
- [ ] 等待用户最终视觉与手感验收。
