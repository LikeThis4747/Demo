# Progress — Demo

## M0 基础设施与交付

- [x] UE5.8 C++ 优先单机 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 规范。
- [x] DemoEditor 构建、自动化和官方 MCP 资产回读路径。
- [ ] Development 打包、目标机运行与完整一局回归。

## M1 Runtime PCG 与多层场景

- [x] Runtime Grid-WFC/HISM、跨层宏结构、逐层二维 WFC、整栋通行图、动态导航门与 Population 分层放置。
- [x] 公开 Seed 稳定；软质量不改变合法性；奖励支线/端点能量光团、上下文机关、资源间距移除和高厅主路覆盖软奖励已落地。
- [x] 最新技术证据：UE5.8 完整构建；Demo.PCG 43/43，含 PublicSeedStability900；90 Seed P95 约 519/765/775 ms。
- [ ] 用户多 Seed 验收路线形状、空白段、高厅摆锤、机关/资源密度、奖励支线和光团可读性。
- [ ] 同一正式一局动态 Recast + 真实追猎者完整多层追逐。

## M2 物理机关、投掷与追猎者

- [x] 摆锤/冲锤、预判抛射、爆裂投掷表现、普通投掷 Stop、磁力破碎、StandingImpact 与 HeavyImpact 已形成阶段技术证据。
- [x] Heavy 统一 0.15 秒 PhysicsBody 阻挡窗口；Accepted 后不可正常回滚；有界恢复；冲锤来源响应比例 0.60。
- [x] 追猎者持续追踪、70 cm 楼梯攻击高度边界与隔层严格位置寻路已提交。
- [ ] 用户验收摆锤/冲锤穿模、Heavy 墙边/起身/二次命中、爆裂表现、Stop 动画和追猎者攻击公平性。
- [ ] 复验保存日志中的 BP_MagneticProp 破碎配置错误，确认当前正式链路是否仍受影响。

## M3 正式一局与感知层

- [x] 主菜单 Seed/难度 → 生成/Population → 玩家/追猎者 → Exit/失败/暂停/结算/重开流程。
- [ ] 灰盒最小目标链：“磁吸电芯送达出口插槽”。
- [ ] P0 音频：追猎者脚步/攻击预警、机关危险提示、磁力操作确认。
- [ ] UI 审美统一最后处理。

## 验收边界

- 自动化、静态 Recast Actor、资产非 Dirty 和单次运行日志不能替代正式一局动态导航与玩家手感验收。
