# Active Context — Demo

## 当前权威基线

- PCG 公开 Seed 不自动改变；软质量影响搜索权重/候选评分但不终局拒绝；WFC 最多三候选择优并有同层硬合法兜底；每栋至少两个高厅且至少一个非顶层。
- Light/Heavy 与新机关接入边界以 `DOC/Outputs/Physics/CHARACTER_IMPACT_INTEGRATION_GUIDE.md` 为准；不扩写全身常驻物理或大一体化。
- HeavyImpact 本轮代码与资产已完成技术验证：摆锤/冲锤短时真实盒体 Sweep；`Accepted` 后不可正常回滚；timeout 不伪造 `OnImpactCommitted`；自然飞行/滚动不设总时长硬切；Downed 起身空间最多阻塞 3.0s，最终按已确认玩法取舍回到受击前记录变换以避免永久倒地；Snapshot 淡入 0.30s。
- Heavy 技术证据：DemoEditor 构建成功；官方 UE5.8 MCP 四份 DataAsset 回读正确且非 dirty；Heavy 5 项与 CharacterImpact 2 项自动化 `7/7 Success`。真实 Chaos 画面仍待用户 PIE 验收。
- 刺轮半埋轮体、三种一格轨迹与实例确定性选路已推送；火星技术修正完成，用户最终视觉复验仍待确认。

## 当前 P0

1. 用户验收 HeavyImpact：摆锤/冲锤正撞与擦边、奔跑/跳跃、长滚动、3 秒阻塞兜底、面朝上/下、Downed 二次命中、30/60/120 FPS。
2. 用户复验刺轮火星与穿越/攻击公平性。
3. 用户验收困难档加载、地图密度/长直线/高厅观感与同 Seed 重进。
4. 在同一正式一局证明动态 Recast 和真实追猎者多层追逐。
5. 灰盒化最小目标链并接入首批音效；完成首轮 Development 打包与整局回归。

## 恢复工作注意

- HeavyImpact 任务处于 `awaiting_acceptance`；构建和自动化不能替代真实机关观感。用户验收前不得归档。
- 3 秒常规路径仍优先完整胶囊安全搜索；只有持续无解时才允许罕见的可见位置回退，以有限恢复时间优先于永久 Downed。
- 不把本轮 Heavy 改动扩展到追猎者攻击、Light、Heavy PCA、AnimBP、起身动画、Level0 或其他机关。
