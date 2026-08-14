# TASK-20260814-001 HeavyImpact 误触发与恢复卡死诊断

- Owner：Codex `/root`
- Status：diagnosing
- Stage：只读复现与根因审计；未授权修复
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
