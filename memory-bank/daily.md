# Daily Log — Demo

> 按日期倒序；仅保留完成、验证、决定与遗留，过程细节见任务卡、DailyReport 与审计归档。

## 2026-08-14 白天：刺轮反馈修正

- 刺轮轮心降至地面，只露上半圆；一格路线新增 DiagonalM 与 CrossLoop，基础 Seed 混入实例逻辑格坐标。
- Demo `-NoLink` 与正式 Demo-only link 成功；冷启动 Blueprint/材质 0 error、0 warning；Simulation 证实半埋和移动正常且无 `LogSpikeWheel` 警告。
- 提交 `19ed4b27b5a81484053e1b51d0497d3916f41893` 已推送并核验。
- 用户随后否决火星表现：当前粒子太小、几乎不可见且形态被改变；确认直接原因是组件缩放曾由 `1.0` 错改为 `0.45`。
- 已把 Blueprint 默认值与 Level0 房间实例恢复为精确 `(1,1,1)`，不再改粒子尺寸、数量、速度或拖尾，只保留红黄材质；Blueprint 编译、运行时属性读回、Simulation 可见度及 `LogSpikeWheel` 日志检查通过，用户最终视觉复验待确认。

## 2026-08-14 夜间只读审计

- 上次夜跑后 20 个白天提交完成：追猎者全局持续追踪与楼梯攻击限制、刺轮 Level0 初版、制导/磁力轻受击收口、玩家 Stop 三方向动画、PCG 固定 Seed 与软质量目标收敛。
- 既有白天证据：DemoEditor 构建通过；PCG Unit 10/10、WFC 8/8、Population 8/8、GameFlow 1/1、Navigation Gate 1/1；三档各 300 Seed、每个重放两次，共 1800 次整栋求解通过。夜间只读审计未重跑构建、测试或 PIE。
- 本夜两个 UE MCP 均不可用于资产审计：本地 MCP `pong=false`，官方 UE5.8 MCP 工具未暴露；蓝图审计未执行，不推断关卡、资产引用、Dirty 或配置状态。
- 当前 P0：用户验收困难档加载/地图观感/同 Seed 重进，并在同一正式一局证明动态 Recast、真实追猎者跨层追逐；随后验收刺轮路线压力和追猎者攻击公平性。
- 记录风险：追猎者追丢诊断任务卡仍描述已被全局追踪提交替代的距离放弃逻辑，次日应按当前代码/运行证据收口。
- 新玩法建议：只做一个“磁吸电芯送达出口插槽”的短目标，搬运路线由现有刺轮/摆锤/冲锤制造取舍，先复用现有磁力、Exit 与结果流程，不新增任务框架。
- Git：origin 已核验为内部工蜂；本夜快照结果见夜报与自动化回报。

## 2026-08-13 白天收口

- PCG：公开 Seed 不再自动改变；软质量从搜索阶段影响权重/评分，WFC 最多三候选择优并有同层硬合法兜底；每栋至少两个高厅且至少一个非顶层。
- 玩法：刺轮 Level0 技术初版、玩家 Stop 三方向动画、制导/磁力 Light 收口、追猎者全局追踪与楼梯攻击限制已提交；刺轮与部分攻击观感仍待用户验收。
- 角色冲击边界保持：StandingImpact 负责 None/Slow/Stop；HeavyImpact 负责真实击飞与恢复；不恢复全身常驻受控物理或 Light/Heavy 大一体化。

## 2026-08-01 至 2026-08-12 摘要

- 主菜单 Seed/难度 → PCG/Population → 玩家/追猎者 → Exit/死亡/暂停/结算/重开闭环已形成。
- 多层路线采用“跨层宏结构 → 逐层二维 WFC → 合并整栋通行图”；Population 已按机关优先、资源后置分层并获代表性验收。
- 摆锤、冲锤、预判抛射、HeavyImpact 起身恢复、磁力破碎 P0 与受击统一入口均形成阶段证据；正式一局动态导航和首轮 Development 打包仍未闭环。

## 2026-07 月度摘要

- 建立 UE5.8 C++ 优先 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 协同规范。
- PCG 从二维 Grid-WFC 推进到 Runtime HISM、Population、最小 RoundFlow；追猎者、磁力、生命链路和第三方资产筛选形成基础。

<!-- written by shiqiqiwang at 2026-08-14 05:06 UTC -->

## 2026-08-14 — HeavyImpact 误触发与恢复卡死

- 以 `d819a1c2bc9540a9222585681cf3ee452484f083` 为功能基线完成 HeavyImpact 修复。
- 收紧摆锤/冲锤预测；Accepted 后不再出现正常 Prepared 回正；真实接触事件与准备 timeout 分离。
- 起身改为自然低能量后 Downed，立即尝试安全起身，0.20s 重试、3.0s 截止并有最终玩法恢复兜底；淡入 0.30s。
- 验证：DemoEditor 完整构建成功；官方 MCP 回读四份资产；自动化 7/7 Success，无警告/错误。
- 遗留：用户 PIE 验收正撞/擦边、奔跑/跳跃、长滚动、3 秒阻塞、方向起身、二次命中与 30/60/120 FPS。

<!-- written by shiqiqiwang at 2026-08-14 05:41 UTC -->

## 2026-08-14 白天：HeavyImpact 用户复测增量

- 用户反馈 AI 挂墙与倒地不起。确认玩家/追猎者共用同一 `UHeavyImpactResponseComponent`；旧状态机只有检测到可行走地面支撑才累计 0.35 秒稳定窗，墙边低能量姿势因此永远到不了 Downed。
- 改为低线速/低角速连续 0.35 秒即可结束物理运动；有支撑走正常落地，无支撑作为墙边/夹缝卡死收口。自由飞行顶点低速窗口不足 0.35 秒，不增加 AI 特判。
- 玩家/追猎者起身安全站位截止统一为 2.0 秒，重试仍为 0.20 秒。
- 起身闪感复核：同帧 Snapshot 求值正常，四条 GetUp 开头基本静止；撤销暂停 Montage 的无证据试探。剩余问题是任意物理终姿只选择正躺/趴躺固定首姿，后续是否调整动画首姿由用户复验决定。
- 验证：DemoEditor 构建成功（11 actions）；官方 MCP 回读两份 Heavy DA 为 2.0/.20/.30 且非 dirty；Heavy 5 + CharacterImpact 2 自动化 7/7 Success；Level0 5 秒 SIE 无 LogHeavyImpact 告警。
- 遗留：真实 AI 墙边收口、玩家/AI方向起身、2 秒阻塞兜底与起身交接仍需用户 PIE 画面验收。

<!-- written by shiqiqiwang at 2026-08-14 07:07 UTC -->


## 2026-08-14 — AI 并行任务规则调整

- 将并行门禁收窄为“同一时刻只有一个实现文件写入会话”；纯文档任务不占用写入权，可与 C++/资产实现并行。
- Git 基线不再要求为无关纯文档改动清空整个工作区；已知 Owner 和路径的文档可保持未暂存，实现会话不得代为修改、暂存或提交。
- 已同步 `AGENTS.md`、`AI_WORKFLOW.md`、`GIT_INTERNAL.md` 与任务卡模板，并通过规则关键词搜索和 `git diff --check`；未修改 C++、Blueprint、资产、配置或其他任务持有的文档。

<!-- written by shiqiqiwang at 2026-08-14 11:27 UTC -->


<!-- written by Codex /root at 2026-08-14 -->

## 2026-08-14 白天：PCG 空白段与高厅灯光

- 在现有 Population 位置/资源权重中加入有界覆盖奖励，机关强度 0.5、资源强度 1.0；所有合法候选仍保留非零概率，不增加质量硬拒绝。
- 三档资源密度由 10 提至 12/100；正式高厅增加两个 LampA 灯位，不修改共享第三方 Blueprint，PCG 继续局部设 Movable。
- 512 Seed 累计机关空窗 6500→6146，完全空白 2463→2423/2431；原刺轮组合合同继续通过。
- DemoEditor 构建、Population 14/14、Presentation 4/4、Navigation Gate 1/1 通过；Seed 12345 Normal PIE 一次成功，日志无 Preview/静态灯警告。
- 遗留：用户多 Seed 现场验收空白段频率、资源数量与高厅照度。
