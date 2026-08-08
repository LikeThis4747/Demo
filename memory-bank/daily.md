# Daily Log — Demo

> 按日期倒序；只保留完成、验证、决定与遗留，过程细节见任务卡、日报和审计归档。

## 2026-08-08

- HeavyImpact 新增物理姿势准备：落地减速后全身刚体继续由 Chaos 模拟，AnimBP 提供从当前物理姿势到起身开头的渐变目标，Physics Control 只施加有限 ParentSpace 角向控制。
- 玩家/AI AnimBP 均由官方 MCP 接入 Snapshot、Sequence Evaluator、Two Way Blend 与显式门；warnings-as-errors 编译保存成功，两份 DataAsset 新参数回读一致。
- 实施基线 `df833aa896624d5d3a0436b7cc1d59f520f0871b` 已推送核验；最终 Demo 模块 `-Module=Demo -NoEngineChanges` 构建成功，HeavyImpact 自动化 5/5。
- 短 PIE 只覆盖既有预测过迟和假阳性回滚，没有形成真实 Chaos 命中；物理收拢、Montage 闪切、墙边/墙角和恢复控制仍由用户画面验收。
- 四条起身动画、玩家 BP、官方 `ABP_Unarmed`、Physics Asset/PCA、Level0、摆锤/冲锤/制导机关、PCG 与旧追猎者局部受击均未修改。
- 夜间只读审计此前记录的 AnimBP/Level0 编辑器脏状态已经不再适用：本轮明确保存两份目标 AnimBP，Level0 与四个目标资产关闭 Editor 前均确认未脏。

## 2026-08-07

- 实现 HeavyImpact 起身恢复桥：Pose Snapshot、正反面判断、安全 Capsule 找位、动态起身 Montage、失败重试与恢复收尾；真实 Chaos 位移来源保持不变。
- 官方 MCP 装配玩家/AI AnimBP、玩家 AnimClass 和两份 DataAsset；完整链接成功，HeavyImpact 自动化 5/5 通过。
- 现场定位 A Pose 为 Physics Control 初始化时序问题；当前磁盘修正让 Inactive Profile 完成一次 PrePhysics 应用后再关闭 Tick。
- 真实日志后续已覆盖玩家/追猎者倒地到恢复；用户画面、墙边/堵塞和正式追逐恢复仍待验收。
- 白天提交包含起身实现、制导机关调参基线与 A Pose 修复计划；夜间前快照最终为 `e7a0fda698e382d49d70ddbd9555b071763f7a3c`。

## 2026-08-06

- Level0 常驻物理摆锤完成构建、碰撞标定与用户初步手感验收；真实物理碰撞会改变摆锤与磁性物运动。
- 自动周期冲锤完成 Actor/DataAsset/BP/测试走廊；普通刚体、玩家和追猎者均有真实 Chaos 接触证据，HeavyImpact 自动化当时 4/4 通过，手感与追逐通行待用户验收。
- 玩家 SpringArm 增加 HeavyImpact 更新依赖与位置延迟配置；正式链接和 Blueprint 回读通过，视觉手感由用户验收。
- 壁挂式一次性物理制导机关及低顶 L 形测试区完成静态装配、构建与保存；按用户要求未运行 PIE。
- HeavyImpact 共享受击原型完成源码、PCA/DA/BP 装配与完整链接；旧追猎者局部受击保留但停用。

## 2026-08-04 至 2026-08-05

- 正式多层 PCG 完成初步验收：跨层宏结构、逐层二维 WFC、三维占用/净空、整栋连通、Start/Pursuer/Exit、动态导航门与结构表现已落盘。
- Seed 30794 原问题楼梯经用户实测确认追猎者可上；六段隐形坡道上移 3.5 cm，地刺不阻挡追猎者，磁性资源物仍为导航障碍。
- UE5.8 完整构建与 `Demo.PCG 22/22 + Demo.GameFlow 2/2` 通过；全部楼梯/旋转实走、完整跨层追逐、整局回归和三档各 300 Seed 校准仍未完成。
- 夜间资产回读曾确认 L_Game、正式 Presentation DataAsset 非脏且关键蓝图 UpToDate。

## 2026-08-01 至 2026-08-03

- 主菜单 Seed/难度 → L_Game → PCG/Population → 玩家/追猎者，以及 Exit/死亡/暂停/结算/重开闭环已实现并通过用户阶段验收。
- 多层正式路线冻结为“先完整跨层宏结构，再逐层二维 WFC，最后合并整栋通行图”；代表性三层 PIE 与自动化通过，但仍需玩家实走和真实追逐验收。
- Level0 V2 隐藏导航坡面与真实追猎者楼梯移动已有证据；静态路径和单次 PIE 不替代完整可玩验收。

## 2026-07 月度摘要

- 建立 UE5.8 C++ 优先 Demo、内部工蜂 Git/LFS、夜间只读维护与双 MCP 协同规范。
- PCG 从二维 Grid-WFC 推进到 Runtime HISM、Population、最小 RoundFlow，并形成跨层宏结构 + 逐层二维 WFC 的正式方向。
- 追猎者 Timer 状态机、locomotion、Physics Control 局部受击、地刺、磁性抓取和玩家生命基础链路形成；当前受击与多层追逐仍需现版本验收。
- 第三方 SFCorridors 仅只读筛选；任何删除仍需依赖闭包、精确清单与用户授权。
