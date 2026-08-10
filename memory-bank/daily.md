# Daily Log — Demo

> 按日期倒序；只保留完成、验证、决定与遗留，过程细节见任务卡、日报和审计归档。

## 2026-08-10

- 用户确认当前全部工作区改动保留并授权完整基线 commit/push；基线通过后可在 Level0 测试区手工制作首个 Geometry Collection，C++/Blueprint 运行逻辑仍需另行授权。
- 磁力投掷物 P0 方案成文：只有正式 `ThrowHeldObject()` 后的第一次 Blocking Hit 才破碎；拉取、持有、普通放下和命中前重新抓取均不破碎，所有碎片经 Remove On Break 清空。
- 官方 UE MCP 回读 `BP_MagneticProp` 为 20 kg 模拟物理静态网格刚体，使用第三方 `SM_crate4`；Blueprint 与源网格均非 Dirty。UE5.8 引擎接口确认初始速度、显式解簇和 Remove On Break 路径可用。
- 新增 `DOC/DailyPlan/2026-08-10-磁力投掷物碰撞破碎实施计划.md` 和对应任务卡；本轮未修改 C++/UE 资产、未构建、未运行 PIE，功能实施仍待用户明确授权和完整 Git 基线门禁。
- 用户否决旧“短时推进、飞行中制导”表现后，壁挂式机关重构为预警期预测移动交点、炮管机械转向、一次质心冲量和发射后全程 Chaos；保留旧类名仅为资产兼容。
- 默认机关更新为约 1000 cm/s、25 kg、22/45 cm 胶囊、0.55 秒预警、30/18 度机械限角，HeavyImpact 准备关闭；无 Thruster、ProjectileMovement、Actor Tick 或 Powered 控制。
- Launcher/Projectile Blueprint 以警告视为错误编译保存，DataAsset 与组件树逐项回读；Level0 西端新增南半幅端墙并保留北侧通路，转角 Launcher 朝东正面迎击，出口灯移出默认弹道。
- UHT、Demo 无链接编译和 Demo-only 正式链接成功，只更新 `UnrealEditor-Demo.dll`，没有编译 UE5.8 引擎；机关资产和 Level0 均非 Dirty。
- 新增 `DOC/DailyReport/2026-08-10-狭窄走廊预判抛射Chaos机关实施报告.md`。按用户要求未运行 PIE，画面可读性、移动命中、躲避、反弹与薄墙穿透仍待用户验收。
- 新增 `DOC/DailyReport/2026-08-10-重冲击阶段验收收口.md`；两份 HeavyImpact 任务卡及四份已完成/被取代的 DailyPlan 已归档，旧物理姿势整理稿明确标为禁止恢复。
- 用户完成 HeavyImpact 最终阶段验收：真实摆锤击飞、提前 Snapshot → Montage 起身、恢复控制/追逐及同机关防夹均可接受；当前效果作为稳定基线，后续细调另记。
- 实现防夹增量：真实 Chaos 接触保留 0.15 秒后，仅将角色物理网格对 PhysicsBody 改为 Ignore；WorldStatic/WorldDynamic 仍保持原碰撞，恢复完成时统一还原角色基线。
- 恢复后仅屏蔽同一来源 Actor：用户把玩家最终 DataAsset 调为 0.50 秒，追猎者保持 3.0 秒；保护期内同来源重复请求返回 Busy 并刷新期限，其他机关来源仍可命中。
- 官方 MCP 最终回读玩家 0.15/0.50、追猎者 0.15/3.0，玩家资产已保存且非 Editor Dirty；HeavyImpact 自动化 5/5、短 PIE 与最终 DemoEditor `-Module=Demo -NoEngineChanges` 构建通过。
- 用户否决落地后可见的全物理姿势准备/蠕动路线；实现改为正常空间稳定 0.10 秒后保存当前物理姿势，同步启动起身 Montage，并用单一 Alpha 在 0.22 秒内混入原有 DefaultSlot。
- 删除 PreparingPose、恢复姿势 Sequence Evaluator、准备时间/控制比例、额外计时器和开发期切换开关；没有保留并行旧路线或失效修复代码。
- 修正墙角搜索前置死锁：最终位置仍要求完整站立 Capsule 无重叠；骨盆正上方无站立空间时，允许最多 60 cm、且骨盆退让路径不穿墙的小范围找位。完全封闭时保持 Downed 并持续重试。
- 玩家/AI AnimBP 均简化为 HeavyImpactDownedPose(A) + DefaultSlot(B) + HeavyImpactRecoveryBlendAlpha，warnings-as-errors 编译成功；两份 DataAsset 重启回读为 0.10/0.22 秒与 60/20 cm。
- HeavyImpact 自动化 5/5、短 PIE 初始化与 DemoEditor `-Module=Demo -NoEngineChanges` 构建通过；主效果与防夹随后均由用户现场验收。
- 用户已授权恢复 `origin`，内部工蜂 baseline `3350b78c7708ca170ecb6641d32e240d57077f34` 已推送并远端核验；本轮最终实现按完整提交收口。

## 2026-08-09

- 上一夜间快照后完成 HeavyImpact 物理姿势准备与起身 Montage 连续交接；另新增壁挂式制导机关稳定性 Review，Review 建议尚未实施。
- 既有证据为 Demo 模块链接成功与 HeavyImpact 5/5；本次夜间未构建、未跑测试、未启动 PIE。
- 双 MCP 在线；Level0 打开且 PIE 停止，两份 AnimBP 父类正确并为 UpToDate，Level0、两份 AnimBP、两份 DataAsset/PCA 均非脏，角色 BP → DataAsset → PCA 引用闭合。
- 当前编辑器世界有 NavMeshBoundsVolume/RecastNavMesh，但最新保存 PIE 日志仍有 Recast 缺失；同时有玩家/追猎者无地面支撑硬超时，均待白天建立复现场景。
- 下一步优先完成真实命中连续画面、墙角/堵塞解除与恢复追逐验收；随后只选一个已验收机关接入正式一局。

## 2026-08-08

- HeavyImpact 新增物理姿势准备：落地减速后全身刚体继续由 Chaos 模拟，AnimBP 提供从当前物理姿势到起身开头的渐变目标，Physics Control 只施加有限 ParentSpace 角向控制。
- 玩家/AI AnimBP 均由官方 MCP 接入 Snapshot、Sequence Evaluator、Two Way Blend 与显式门；warnings-as-errors 编译保存成功，两份 DataAsset 新参数回读一致。
- 实施基线 `df833aa896624d5d3a0436b7cc1d59f520f0871b` 已推送核验；最终 Demo 模块 `-Module=Demo -NoEngineChanges` 构建成功，HeavyImpact 自动化 5/5。
- 短 PIE 未形成可确认的真实 Chaos 命中；物理收拢、Montage 闪切、墙边/墙角和恢复控制仍由用户画面验收。

## 2026-08-07

- 实现 HeavyImpact 起身恢复桥：Pose Snapshot、正反面判断、安全 Capsule 找位、动态起身 Montage、失败重试与恢复收尾；真实 Chaos 位移来源保持不变。
- 官方 MCP 装配玩家/AI AnimBP、玩家 AnimClass 和两份 DataAsset；完整链接成功，HeavyImpact 自动化 5/5 通过。
- 现场定位 A Pose 为 Physics Control 初始化时序问题；当前磁盘修正让 Inactive Profile 完成一次 PrePhysics 应用后再关闭 Tick。
- 真实日志后续已覆盖玩家/追猎者倒地到恢复；用户画面、墙边/堵塞和正式追逐恢复仍待验收。

## 2026-08-06

- Level0 常驻物理摆锤完成构建、碰撞标定与用户初步手感验收；真实物理碰撞会改变摆锤与磁性物运动。
- 自动周期冲锤、相机顺序修正、壁挂式一次性制导机关与 HeavyImpact 共享受击原型完成实现和各自既定技术证据；用户手感与完整追逐仍待验收。

## 2026-08-04 至 2026-08-05

- 正式多层 PCG 完成初步验收：跨层宏结构、逐层二维 WFC、三维占用/净空、整栋连通、Start/Pursuer/Exit、动态导航门与结构表现已落盘。
- Seed 30794 原问题楼梯经用户实测确认追猎者可上；UE5.8 完整构建与 `Demo.PCG 22/22 + Demo.GameFlow 2/2` 通过。
- 全部楼梯/旋转实走、完整跨层追逐、整局回归和三档各 300 Seed 校准仍未完成。

## 2026-08-01 至 2026-08-03

- 主菜单 Seed/难度 → PCG 关卡 → Population → 玩家/追猎者，以及 Exit/死亡/暂停/结算/重开闭环已实现并通过用户阶段验收。
- 多层正式路线冻结为“先完整跨层宏结构，再逐层二维 WFC，最后合并整栋通行图”；代表性三层 PIE 与自动化通过，但仍需玩家实走和真实追逐验收。

## 2026-07 月度摘要

- 建立 UE5.8 C++ 优先 Demo、内部工蜂 Git/LFS、夜间只读维护与双 MCP 协同规范。
- PCG 从二维 Grid-WFC 推进到 Runtime HISM、Population、最小 RoundFlow，并形成跨层宏结构 + 逐层二维 WFC 的正式方向。
- 追猎者 Timer 状态机、locomotion、Physics Control 局部受击、地刺、磁性抓取和玩家生命基础链路形成；当前受击与多层追逐仍需现版本验收。
- 第三方 SFCorridors 仅只读筛选；任何删除仍需依赖闭包、精确清单与用户授权。

<!-- written by shiqiqiwang at 2026-08-10 11:41 UTC -->

### 2026-08-10 — 机关首次 PIE 触发回归

- 用户报告机关完全不触发；日志确认两个 Launcher 在 BeginPlay 因旧关卡实例 Muzzle 父级错误而 Disabled。
- 官方 MCP 修正并保存两个 Level0 实例为 Muzzle -> AimPivot；C++ 增加旧实例层级自愈。
- Demo-only 编译/链接成功；定点 PIE 中两个实例均完成 armed、锁定、0.55 秒预警和 fired，无 disabled/failed。
- 未完成用户画面与玩法验收，薄墙和连续反弹专项仍开放。

<!-- written by shiqiqiwang at 2026-08-10 12:09 UTC -->

### 2026-08-10 — 机关速度、质量与自动清除

- 用户确认将机关设计初速提高到约 1225 cm/s、弹体质量提高到 50 kg，并新增发射后 8 秒清除。
- C++ 寿命合同、校验和 Actor LifeSpan 已落盘；DataAsset 回读为 900 cm / 18 deg / 50 kg / 8 s。
- Demo-only 编译/链接成功；最小 PIE 日志确认实际 1224.97 cm/s、50 kg、8 s，且 9 秒后弹体已清除。
- 速度、碰撞重量感和物理道具推动强度仍待用户现场验收。

<!-- written by shiqiqiwang at 2026-08-10 12:22 UTC -->

### 2026-08-10 — 机关基础机制验收

- 用户验收当前 900 cm / 18 deg / 50 kg / 8 s 预判抛射 Chaos 机关版本。
- 已明确后续模型、表现细节与轻受击不混入本轮技术债清理。
- 按门禁先提交验收版，随后只读审计并向用户展示清理预览。

<!-- written by shiqiqiwang at 2026-08-10 12:43 UTC -->

### 2026-08-10 — 机关验收后技术债清理

- 删除与 Ballistic/FreePhysics 阶段完全重复的首次阻挡布尔状态，Phase 成为唯一状态来源。
- 关闭 ProjectileBody 无效的 Overlap 事件生成；HeavyImpact PreparationVolume 保持独立可选接缝。
- 配置或生成失败的禁用弹体增加 1 秒 LifeSpan，正常发射仍使用 8 秒 DataAsset 寿命。
- 当前文档权威统一为 2026-08-10 狭窄走廊 Plan/Report、当前 Source 与默认 DataAsset；旧推进方案保留历史标记。
- Demo -NoLink 与 DemoEditor 正式链接成功，只构建 Demo 项目模块；未修改 Blueprint、DataAsset、Level0、模型或轻受击。
