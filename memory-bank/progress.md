# Progress — Demo

## M0 基础设施与 UE 5.8

- [x] C++ 优先单机 Demo、项目 Memory MCP、Git LFS 与内部工蜂备份
- [x] UE 5.8 DemoEditor 构建与双 MCP 协同规范
- [x] 2026-08-03 当前工作区完整构建成功；`Demo.PCG` 21/21、`Demo.GameFlow` 2/2
- [ ] UE 5.8 下磁力 PIE 手感回归

## M1 实时 PCG 场景

- [x] 全图 16 OpeningMask Grid-WFC；最低带权熵、Domain Trail、有界时间序回溯
- [x] Count、MaxConsecutive、Connected/Tarjan 全局约束与确定性有限重试
- [x] 600 cm 逻辑 Tile 展开为 300 cm 结构并用 Runtime HISM 实例化
- [x] HydroLab Presentation、Generation Profile、根材质 HISM Usage、运行时灯光与独立 Population
- [x] 多层合同与正式实现：完整楼梯/高厅预放置、三维占用/净空、逐层二维 WFC、整栋逻辑连通、明确 Start/Pursuer/Exit、结构表现与动态导航门
- [x] 2026-08-03 代表性 PIE：跨 World 换 Seed 后成功生成 3 层，11 点投射、10 条路径、24 个玩法对象
- [ ] 玩家连续实走双层楼梯与贯通三层楼梯间，检查碰撞、净空、护栏、相机和最终视觉
- [ ] 正式追逐中的真实追猎者跨层上/下楼；开局路径存在性检查不能替代此项
- [ ] Easy/Normal/Hard 各至少 300 Seed 的成功率、楼层/楼梯/高厅分布、重试和耗时统计
- [ ] 目标机软件 Lumen 与无 Lumen 室内补光双档验收

## M2 玩法压力与追猎者

- [x] 追猎者 C++ Timer 状态机、追击/攻击时机、DataAsset 与 BP 装配
- [x] Physics Control 局部受击源码、调参 DataAsset 与历史多肢体命中
- [x] 最小 locomotion AnimBP/BlendSpace 资产与历史 UpToDate 证据
- [x] 地刺、玩家 HealthComponent 与生命归零广播接入正式胜负流程
- [ ] 近战/方向受击、AttackProjectile Tag、磁力 Camera 通道与重复投掷边界的当前验收
- [ ] Physics Control 连续至少 10 次命中、目标区域与恢复压力验收
- [ ] 追猎者对地刺的免疫、受伤或受阻语义

## M3 正式一局流程

- [x] 主菜单 Seed/难度 → PCG → 玩家/追猎者/陷阱/资源装配
- [x] GameState 局状态机、ExitVolume 判胜、Health 归零判负
- [x] 结算界面下一把/重开/选择关卡/回主菜单、ESC 暂停菜单；2026-08-03 用户验收
- [x] 多层生成完成导航验收后再装配玩法对象；可恢复失败确定性换 Seed 跨 World 重试
- [ ] 生成失败重试、暂停/恢复、胜负、同 Seed 重开与新 Seed 下一把的多层联合回归
- [ ] UI 审美统一打磨（排期最后）

## 当前边界

正式多层实现已落入工作区并有构建、23 项自动化与代表性 PIE 证据，但仍属于“已实现、待可玩验收”。静态路径查询、自动化和夜间快照都不能替代玩家实际行走、真实追猎者追逐与用户验收。

<!-- written by shiqiqiwang at 2026-08-04 03:58 UTC -->

## 2026-08-04 PCG 修正

- [x] 楼梯平台与顶层天花板固定灯装配：正式 DataAsset 数量 2/3/0，复用 LampA 与既有事务回滚。
- [x] PCG 生成灯的 LightComponent 在装配后统一设为 Movable，清除未烘焙静态阴影的 “Preview” 来源，不修改第三方 LampA。
- [x] 追猎者占一楼主路线起点；玩家选择满足 1200cm 后最近普通格，并重算真实玩家到 Exit 路线。
- [x] 删除无作用的 PresentationVersion；完整构建与 Demo.PCG 22/22 通过。
- [ ] 用户 PIE 验收灯光、开局追逐与跨层移动。

<!-- written by shiqiqiwang at 2026-08-04 05:55 UTC -->


## 2026-08-04 Seed 30794 导航修正

- [x] 用户确认当前多层 PCG 路线初步验收成功；正式主菜单进入、多层生成与 Seed 30794 原问题楼梯通行已通过当前验收。
- [x] 正式 Presentation DataAsset 的 6 段楼梯隐形坡道统一上移 3.5cm；用户用 Seed 30794 实测确认追猎者可以上原问题楼梯。
- [x] 证伪修改已清理：磁性资源物继续作为导航障碍，出口的冗余导航设置已删除；没有保留全局导航代理尺寸、临时 Seed、调试 Actor 或导航轮询。
- [x] 地刺按独立玩法规则处理：格栅/尖刺/伤害盒不参与导航，尖刺忽略 Pawn，追猎者不受地刺阻断，玩家伤害仍由 HurtZone 结算。
- [x] DemoEditor 完整构建与 `Demo.PCG 22/22 + Demo.GameFlow 2/2` 通过。
- [ ] 继续完成全部楼梯、正常玩法条件下的完整跨层追逐与整局联合验收。

<!-- written by shiqiqiwang at 2026-08-04 17:05 UTC -->

## 2026-08-05 夜间核对

- [x] Seed 30794 原问题楼梯已由用户实测确认追猎者可上楼；正式多层路线维持“初步验收成功”。
- [x] 夜间只读回读确认 L_Game、正式 Presentation DataAsset 非脏，4 个关键蓝图 UpToDate。
- [ ] 全部楼梯/旋转组合实走、完整跨层追逐、整局联合回归与三档各 300 Seed 校准仍未完成。
- [ ] PCG 物理玩法与普通/重型受击分工仍待用户决策，尚未实施。

<!-- written by shiqiqiwang at 2026-08-05 12:52 UTC -->

## 2026-08-05 Level0 物理摆锤

- [x] 实现自由物理摆 + 最低点轻量补能的常驻摆锤；约束允许碰撞自然改变轨迹，未加入按物体质量或速度筛选的特殊门槛。
- [x] 新建摆锤调参 DataAsset 与无玩法图表的装配蓝图；DemoEditor Win64 Development 完整构建成功。
- [x] 读取既有 1×2 高厅尺寸和构件配方，在 Level0 空白区域独立搭建 36 件高层测试房并放入摆锤；原高厅未修改。
- [x] 以关闭补能的实测损耗校准每次补能上限为 20 cm/s；20kg 磁性物碰撞已验证会自然改变摆锤速度和自身运动。
- [x] 2026-08-05 用户确认 Level0 摆锤“算还行”，物理原型初步验收通过。
- [ ] 重新确定地牢房型中的摆锤/冲锤路线职责、追猎者一次迟滞后必定通过的规则；Pawn 受击与 PCG 接入尚未完成。

<!-- written by shiqiqiwang at 2026-08-05 14:57 UTC -->

## 2026-08-05 重冲击物理受击原型（代码阶段，未验收）

- 已实现共享请求/接口、Physics Control 权威组件、玩家与追猎者适配、磁力中断、摆锤预测接入和三组自动化测试源码。
- 旧追猎者局部受击完整保留，只暂停运行装配；未修改 Physics Asset、AnimGraph、Level、PCG Population、Content 或 Config。
- UHT 与 Demo 模块 no-link 编译多轮通过，最终 4 个动作成功；静态复核无剩余 P0/P1。
- 未完成：完整 DLL 链接、两份 PCA 创建/Compile、DA/CDO/Blueprint 装配、自动化、PIE 与用户画面验收。不得把本项标为功能完成。


## 2026-08-06 夜间核对

- [x] Level0 摆锤原型、调参资产和测试房已有完整构建、碰撞标定与用户初步手感验收记录。
- [x] 共享重冲击 C++、摆锤 ETA 预测和三组自动化测试源码已落盘，UHT/无链接编译通过。
- [ ] 当前 Editor 仍加载旧 Demo DLL；完整链接、新模块加载、两份 PCA、两份 HeavyImpact DA、角色/摆锤装配、自动化和 PIE 均未完成。
- [ ] 首版 Downed/恢复与正式整局语义、墙边/斜坡/角落、连续冲击和 Downed 二次碰撞仍需真实运行验收。
