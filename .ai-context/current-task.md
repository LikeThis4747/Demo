# Current Task

## 当前主线

- PCG 机关与资源分层放置已完成并由用户在 2026-08-12 确认验收通过，正式计划、交付报告和任务卡均已归档。
- 当前权威配置：Easy / Normal / Hard 机关密度为 26 / 28 / 30 每 100 格，最小通路间距统一为 2；地刺/冲锤/发射器权重为 5:3:2 / 1:1:1 / 2:3:5；资源为 10/100 格、间距 3。
- 架构基线保持一个 GameplayPopulator 内机关优先、资源后置；高厅摆锤必放，普通机关类型先抽且操作格互斥，资源独立采样并随机格内 X/Y。
- 功能提交 `cad49998863d98c604c956b5dd4d593fdfaf0673` 已推送并核验内部工蜂 `origin/main`。

## 当前待办

1. 轻受击动画与上半身局部物理仍等待用户画面和 Light/Heavy 交叉验收。
2. PCG 多 Seed 统计、极端净距和长期难度曲线只作为后续回归/独立调优，不再阻塞本次验收。
3. 后续若调整 PCG，只优先修改 DataAsset 密度、间距与权重；没有新证据时不恢复旧规则或增加每层上限、每类保底、固定节奏、动态 Director。

<!-- written by shiqiqiwang at 2026-08-12 11:45 UTC -->


## 2026-08-12 轻受击增量

- 当前正在收口玩家磁力投掷轻受击：PlayerReaction=Slow 0.40 秒、SpeedMultiplier=0.55、无动画、启用局部物理；地刺不改。
- 玩家 StandingImpact 上半身局部物理已装配并通过官方 MCP 持久化回读、CharacterImpact 2/2、HeavyImpact 5/5 与短 SIE 启动检查。
- 技术提交后等待用户 PIE 验收胸/左右命中反馈、恢复及 Light→Heavy；若效果仍不足，只做有限参数 A/B，不扩写来源特判。

<!-- written by shiqiqiwang at 2026-08-12 13:16 UTC -->

## 2026-08-12 追猎者预判跑跳攻击

- 技术实现已收口：中距离约 220-650 cm 使用全身跑跳下砸，离地时按玩家水平速度预判 0.35 秒并一次锁点，CharacterMovement 固定时间抛物线位移，真实 Landed 结算 160 cm 范围；近距离使用斧击 Sweep。
- 跑跳动画已重定向到追猎者骨骼并创建 DefaultSlot Montage；Root Motion 关闭且锁 Root，不修改 AnimBP 图。攻击命中使用 ApplyDamage + StandingImpact，不冒充 Heavy Impact。
- DemoEditor 完整构建、PredictionAndBallistics 1/1、资产回读和 Level0 PIE 通过；PIE 先记录 30 点跑跳命中，后记录 18 点近战命中。最终移动玩家压力、躲避公平性和动画观感待用户验收。
