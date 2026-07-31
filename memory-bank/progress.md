# Progress — Demo

## M0 基础设施与 UE 5.8

- [x] C++ 优先单机 Demo、项目 Memory MCP、Git LFS 与内部工蜂备份
- [x] UE 5.8 DemoEditor 历史构建成功；本地 UE Editor MCP 与官方 MCP 协同规范已归档
- [ ] 当前工作区完整构建与自动化回归；GameFlow/UI 已有 2026-07-31 白天构建产物并被编辑器加载，但本轮未重跑完整构建
- [ ] UE 5.8 下磁力 PIE 手感回归

## M1 实时 PCG 场景

- [x] 全图 16 OpeningMask Grid-WFC；最低带权熵、Domain Trail、有界时间序回溯
- [x] Count、MaxConsecutive、Connected/Tarjan 全局约束与确定性有限重试
- [x] 600 cm 逻辑 Tile 展开为 300 cm Floor/Ceiling/Wall/Trim/Pillar，Runtime HISM 实例化
- [x] HydroLab Presentation、Generation Profile、根材质 HISM Usage 与运行时顶灯
- [x] 独立 Population 层；支持区域、Start/Exit 邻域规避、直走廊筛选、横向并排和 Z 偏移
- [x] 历史 UE 5.8 构建、Demo.PCG 19/19 与 288/288 Seed Sweep
- [ ] 当前 18 项 Demo.PCG、完整构建与 Seed Sweep 回归基线
- [ ] 至少 10 Seed 玩家路线、接缝、碰撞、净空与 Start→Exit 验收
- [ ] Runtime 动态导航与真实追猎者多 Seed 寻路证据
- [ ] Level0 V5 与三层楼梯塔的玩家、Recast、真实追猎者验收及后续 Runtime WFC 取舍
- [ ] 目标机软件 Lumen 与无 Lumen 室内补光双档验收

## M2 玩法压力与追猎者

- [x] 追猎者 C++ Timer 状态机、追击/攻击时机、DataAsset 与 BP 装配
- [x] Physics Control 局部受击源码、调参 DataAsset 与 BP 引用；历史日志确认 ready 和多肢体命中
- [x] 最小 locomotion AnimBP/BlendSpace 资产存在且历史 Blueprint 状态 UpToDate
- [x] 地刺 Timeline/Overlap/ApplyDamage 与玩家 HealthComponent 已接入；历史日志确认 100→0
- [ ] 近战/方向受击、AttackProjectile Tag、磁力 Camera 通道与重复投掷边界的当前验收
- [ ] Physics Control 连续至少 10 次命中、目标区域与恢复压力验收
- [ ] 生命归零后的失败/重开闭环
- [ ] 追猎者对地刺的免疫、受伤或受阻语义

## M3 正式一局流程

- [ ] 阶段一已有白天构建与资产/日志证据：菜单 Seed 12345→单次 PCG→4 地刺/8 磁性物→玩家/追猎者就位；仍待玩家实操和联合验收后标记完成
- [ ] 修正开局实际 separation_cm=1138 小于配置 1200 cm 的验证风险
- [ ] 阶段二：Exit/生命归零结算、下一把/同 Seed 重开/回主菜单与暂停菜单
- [ ] 阶段三：把胜负、重开与 UI 完成联合验收

## 当前边界

Generator 拥有空间，Population 拥有批量玩法对象，正式 GameMode 负责开局编排；胜负状态的唯一管理者仍需在阶段二明确。单 Seed 开局成功不能替代整局玩家验收、18 项自动化、10 Seed 或真实 AI 导航证据。
