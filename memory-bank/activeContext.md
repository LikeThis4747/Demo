# Active Context — Demo

## 当前权威基线

- PCG 公开 Seed 不自动改变；软质量影响搜索权重/候选评分但不终局拒绝；WFC 最多三候选择优并有同层硬合法兜底；每栋至少两个高厅且至少一个非顶层。
- Light/Heavy 与新机关接入边界以 `DOC/Outputs/Physics/CHARACTER_IMPACT_INTEGRATION_GUIDE.md` 为准；不扩写全身常驻物理或大一体化。
- 玩家与追猎者使用同一 `UHeavyImpactResponseComponent`。HeavyImpact 当前修正包括：摆锤/冲锤短时真实盒体 Sweep、Accepted 后不可正常回滚、timeout 不伪造真实接触、自然飞行/滚动无总时长硬切。
- 用户复测后的共享修正：无地面支撑但连续 0.35 秒低线速/低角速的墙边或夹缝姿势也进入 Downed；起身安全站位 0.20 秒重试、最多阻塞 2.0 秒。
- 起身 Snapshot 淡入保持 0.30 秒；当前只有正躺/趴躺两种固定动画首姿，任意物理终姿的匹配观感仍待用户 PIE 验收，不以暂停 Montage 或延长 Blend 猜修。
- Heavy 技术证据：DemoEditor 构建成功；官方 UE5.8 MCP 回读 Player/Pursuer Heavy DA 为 2.0/.20/.30 且非 dirty；Heavy 5 项与 CharacterImpact 2 项自动化 7/7 Success；5 秒 SIE 无 Heavy 初始化告警。
- 刺轮移动/半埋/三轨迹和火星技术修正已有技术证据，仍待用户最终视觉与玩法复验。

## 当前 P0

1. 用户复验 HeavyImpact：AI 挂墙/夹缝低能量收口、玩家/AI 面朝上/下起身、2 秒阻塞兜底、起身交接观感、Downed 二次命中及 30/60/120 FPS。
2. 用户复验刺轮火星、路线压力与攻击公平性。
3. 用户验收困难档加载、地图密度/长直线/高厅观感与同 Seed 重进。
4. 在同一正式一局证明动态 Recast 和真实追猎者多层追逐。
5. 灰盒化最小目标链并接入首批音效；完成首轮 Development 打包与整局回归。

## 恢复工作注意

- HeavyImpact 任务处于 `awaiting_acceptance`；构建和自动化不能替代真实机关与起身画面验收。
- 无支撑低能量出口属于共享 Heavy 状态机，不新增 AIController 特判。
- 不恢复无证据的 Montage 暂停、双 Refresh 或全身常驻物理；若姿势闪感仍不可接受，下一步应比较动画首姿/姿势候选，而不是继续加时序补丁。

<!-- written by shiqiqiwang at 2026-08-14 11:27 UTC -->


## 2026-08-14 PCG 体验反馈增量

- Population 已在原评分内加入路线覆盖软奖励：机关 0.5 log2、资源 1.0 log2；没有最大空白长度硬拒绝或额外重试。
- 三档资源目标现为 12/100；正式高厅配方有两盏顶灯，PCG 生成灯组件为 Movable，共享 LampA 默认值未改。
- 技术证据：Development Editor 构建成功；Population 14/14、Presentation 4/4、Navigation Gate 1/1；Seed 12345 Normal PIE 成功且日志无 Preview/静态灯告警。
- 仍待用户多 Seed 验收空白段频率、资源体感和高厅照度。
