# Progress — Demo

## M0 基础设施与 UE 5.8

- [x] C++ 优先单机 Demo、项目 Memory MCP、Git LFS 与内部工蜂备份
- [x] UE 5.8 DemoEditor 历史构建成功；本地 UE Editor MCP 与官方 MCP 协同规范已归档
- [ ] 当前工作区完整构建与 UE MCP 连接恢复
- [ ] UE 5.8 下磁力 PIE 手感回归

## M1 实时 PCG 场景

- [x] 全图 16 OpeningMask Grid-WFC；最低带权熵、Domain Trail、有界时间序回溯
- [x] Count、MaxConsecutive、Connected/Tarjan 全局约束与确定性有限重试
- [x] 600 cm 逻辑 Tile 展开为 300 cm Floor/Ceiling/Wall/Trim/Pillar，Runtime HISM 实例化
- [x] HydroLab Presentation、Generation Profile、根材质 HISM Usage 与运行时顶灯
- [x] 独立 Population 层；支持区域、Start/Exit 邻域规避、直走廊筛选、横向并排和 Z 偏移
- [x] 历史 UE 5.8 构建、Demo.PCG 19/19 与 288/288 Seed Sweep
- [ ] Level0 静态样例：V5 为 259 Actor / 15 叶文件夹；三层楼梯塔任务卡记录 211 Actor / 13 文件夹与四个 600cm 接口。均未转入 Runtime WFC，待玩家、Recast 与真实 AI 验收
- [ ] 对当前 18 项 Demo.PCG 测试重新建立完整构建、自动化与 Seed Sweep 基线
- [ ] 至少 10 Seed 玩家验收路线、接缝、碰撞、净空与 Start→Exit；同一 Seed 15339 的重复日志不计为多 Seed 放行
- [ ] 补齐 Runtime 动态导航证据并验收追猎者多 Seed 实际寻路
- [ ] 渲染双档验收：目标机软件 Lumen 帧时间与无 Lumen 低配室内补光

## M2 玩法压力与追猎者

- [x] 追猎者 C++ Timer 状态机、追击/攻击时机、DataAsset 与 BP 装配
- [x] Physics Control 局部受击源码、调参 DataAsset 与 BP 引用；历史日志确认 ready 和多肢体命中
- [x] 最小 locomotion AnimBP/BlendSpace 资产存在且历史 Blueprint 状态 UpToDate
- [x] 地刺 Timeline/Overlap/ApplyDamage 与玩家 HealthComponent 已接入；历史日志确认 100→0
- [ ] 近战攻击/方向受击、AttackProjectile Tag 与磁力 Camera 通道改动的当前构建、PIE 和边界验收
- [ ] Physics Control 连续至少 10 次命中、目标区域与恢复压力验收
- [ ] 生命归零后的失败/重开闭环
- [ ] 追猎者对地刺的免疫、受伤或受阻语义
- [ ] 正式失败/成功 UI 与玩家联合验收

## M3 正式一局流程

- [ ] 阶段一 C++ 骨架已落盘：GameInstance 传参、主菜单逻辑、正式 GameMode 生成与初始摆放；尚未构建、UE 资产装配或 PIE
- [ ] 阶段二：结算、下一把/同 Seed 重开/回主菜单与暂停菜单
- [ ] 阶段三：Health 归零判负与 Exit 判胜统一接入
- [ ] 静态风险待验证：正式 GameMode 玩家蓝图装配、重复 Generator、开局失败后的半初始化状态

## 当前边界

Generator 拥有空间，Population 拥有批量玩法对象；新的正式 GameMode 只负责开局编排，后续胜负状态仍待明确唯一 Owner。当前阶段一只可称为未验证源码骨架，不能标记可玩。Level0 的 V5 与三层楼梯塔只具备静态任务记录，玩家、Recast、真实追猎者与 Runtime WFC 均未验收。
