# Progress — Demo

## M0 基础设施与 UE 5.8

- [x] C++ 优先单机 Demo、轻量渲染基线、项目 Memory MCP、Git LFS 与内部工蜂备份
- [x] UE 5.8 DemoEditor 构建成功；本地 UE Editor MCP 与官方 MCP 协同规范已归档
- [ ] UE 5.8 下磁力 PIE 手感回归

## M1 实时 PCG 场景

- [x] 全图 16 OpeningMask Grid-WFC；最低带权熵、Domain Trail、有界时间序回溯
- [x] Count、MaxConsecutive、Connected/Tarjan 全局约束与确定性有限重试
- [x] 600 cm 逻辑 Tile 展开为 300 cm Floor/Ceiling/Wall/Trim/Pillar，Runtime HISM 实例化
- [x] HydroLab Presentation、Generation Profile、根材质 HISM Usage 与运行时顶灯
- [x] 独立 Population 层；支持区域、Start/Exit 邻域规避、直走廊筛选、横向并排和 Z 偏移
- [x] 最小 GameFlow：玩家在 Start、追猎者在身后至少 1200 cm、Exit 一次性成功
- [x] 旧 RuntimeGenerationTestHarness C++ 与未引用 Blueprint Redirector 已清理
- [x] 历史 UE 5.8 构建、Demo.PCG 19/19 与 288/288 Seed Sweep
- [ ] Level0 HydroLab 静态原型：V3 已有 7 类积木、兼容链和五段 Recast；V5 已保存重载 259 Actor 的大小房单层网络，含 RiseResolver、2x2 大房、分流汇合环路与 Portal450。尚未转入 Runtime WFC，待用户实走、Recast 与真实 AI 验收
- [ ] 对当前 18 项 Demo.PCG 测试重新建立完整构建、自动化与 Seed Sweep 基线
- [ ] 至少 10 Seed 玩家验收路线、接缝、碰撞、净空与 Start→Exit；2026-07-29 日志只有同一 Seed 15339 的 6 次生成/4 次 Exit 成功，不计为多 Seed 放行
- [ ] 补齐 Runtime 动态导航证据并验收追猎者多 Seed 实际寻路

## M2 玩法压力与追猎者

- [x] 追猎者 C++ Timer 状态机、追击/攻击时机、DataAsset 与 BP 装配
- [x] Physics Control 局部受击源码、调参 DataAsset 与 BP 引用；日志确认 ready 和多肢体命中
- [x] 最小 locomotion AnimBP/BlendSpace 资产存在且 Blueprint 状态 UpToDate
- [x] 地刺 Timeline/Overlap/ApplyDamage 与玩家 HealthComponent 已接入；日志确认 100→0
- [ ] 近战攻击/方向受击蒙太奇、受击停顿、AttackApproachRadius 与限时 AttackProjectile Tag 已落盘；待当前构建、PIE 和重复投掷边界验收
- [ ] Physics Control 连续至少 10 次命中、目标区域与恢复压力验收
- [ ] 生命归零后的失败/重开闭环
- [ ] 追猎者对地刺的免疫、受伤或受阻语义
- [ ] 正式失败/成功 UI 与玩家联合验收

## 当前边界

WFC/Generator 拥有空间，Population 拥有批量玩法对象，GameFlow 拥有唯一玩家、追猎者、Exit 与局状态。Level0 当前同时保留 V3 导航样例与 V5 大小房约束样例；V5 已证明静态几何、共享边所有权和三类接口可成立，但玩家、Recast、真实 AI 与 Runtime WFC 尚未验收。楼梯房暂时封存，明日再验证 G0/F2 接口与追猎者。追猎者新攻击/受击链仍须单独构建和 PIE 验收，小地图、多敌人和通用任务系统暂不扩展。
