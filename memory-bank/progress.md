# Progress — Demo

## M0 基础设施与交付

- [x] UE5.8 C++ 优先单机 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 规范。
- [x] DemoEditor 构建、自动化与官方 MCP 资产回读路径形成；不修改 UE5.8 主引擎。
- [ ] 首轮 Development 打包、目标机运行与完整一局回归。

## M1 Runtime PCG 与多层场景

- [x] Grid-WFC、Runtime HISM、多层宏结构/逐层二维 WFC/整栋通行图、动态导航门与 Population 分层放置。
- [x] 公开 Seed 不自动改变；软质量只影响搜索/评分；最多三候选择优并保留同层硬合法兜底；每栋至少两个高厅且至少一个非顶层。
- [x] Population 路线覆盖软奖励、12/100 资源密度、高厅双灯、刺轮接入、发射器单格占用与转角/远距组合、周期机关确定性相位完成。
- [x] 最新技术证据：Demo 模块构建；Population 15/15、Navigation Gate 1/1；Seed 12345 两次正式 L_Game 布局哈希、数量与首摆锤相位一致。
- [ ] 用户多 Seed 验收密度、空白段、高厅照度、发射器组合/周期节奏、同 Seed 重进与全部楼梯组合。
- [ ] 同一正式一局动态 Recast + 真实追猎者完整多层追逐。

## M2 玩法压力、物理机关与追猎者

- [x] 摆锤、冲锤、预判抛射、磁力破碎 P0、StandingImpact 与 HeavyImpact 已形成阶段证据。
- [x] HeavyImpact 完成真实短时 Sweep、Accepted 后不可正常回滚、无支撑低能量收口、0.20 秒重试/2 秒有界恢复、PostPhysics 起身交接；冲锤来源响应比例为 0.60，其他来源默认 1.0。
- [x] 刺轮完成半埋、三类单格轨迹、确定性选路、Stop 动画和火星技术修正。
- [x] 追猎者近战/跑跳 Heavy、全局持续追踪、70 cm 楼梯攻击高度边界与隔层严格位置寻路已提交并通过技术验证。
- [ ] 用户验收 Heavy 正撞/擦边/墙边/起身/二次命中、冲锤单次顶推、刺轮路线压力/火星、追猎者攻击公平性与折返楼梯。

## M3 正式一局与感知层

- [x] 主菜单 Seed/难度 → 生成/Population → 玩家/追猎者 → Exit/失败/暂停/结算/重开流程。
- [ ] 灰盒最小目标链：优先“磁吸电芯送达出口插槽”，复用磁力、Exit 与现有机关。
- [ ] 接入 P0 音频：追猎者脚步/攻击预警、机关危险提示、磁力操作确认。
- [ ] UI 审美统一最后处理。

## 当前边界

- 自动化、静态 Recast Actor 和资产非 dirty 不等于正式一局动态导航或玩家手感验收。
- 最近保存日志中的 Recast 缺失/Heavy 恢复错误早于后续修正，只能作为复验目标，不作为当前故障定论。
- 不恢复全身常驻受控物理、Light/Heavy 大一体化或跨对象磁力事务方案。

<!-- written by shiqiqiwang at 2026-08-15 08:14 UTC -->

### 摆锤/冲锤视觉替换（2026-08-15，待用户验收）

- `BP_BatteringRamHazard`：VentB1 外壳 + 无碰撞金属背盖，视觉尺寸对齐 120x280x280 cm。
- `BP_PendulumHazard` / `DA_PendulumHazard_Default`：锤体 440x80x150 cm，PivotHeight 650 cm，最低点理论净空 55 cm；应用 HydroLab 工业材质。
- 自动验证通过；玩家手感与外观验收未完成。

<!-- written by shiqiqiwang at 2026-08-15 08:21 UTC -->

- Level0 已完成实例级落地：冲锤与摆锤各一个，无旧实例残留；地图保存与 PIE smoke 通过，待玩家手感验收。

<!-- written by shiqiqiwang at 2026-08-15 12:42 UTC -->

## M1 增量：路线结构、上下文机关与能量光团（2026-08-15，待用户验收）

- [x] 坍缩前软路线提示、成功树后图分析、RewardBranch schema/hash、上下文机关 log2 权重、整数预算成本、资源硬间距移除。
- [x] 每条最终奖励支线端点独立放置能量光团；正式 DataAsset/Blueprint/材质已迁移、编译和回读。
- [x] UE 完整构建；Demo.PCG 43/43、Population 16/16、PublicSeedStability900、RouteQuality90 与正式 L_Game Seed 12345 运行通过。
- [ ] 玩家多 Seed 验收；当前约 1.77 奖励支线/层且替代路线覆盖接近 0，3/4/5 支线中心值与多主路仍未稳定达到。
