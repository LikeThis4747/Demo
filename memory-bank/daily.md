# Daily Log — Demo

> 按日期倒序；仅保留完成、验证、决定与遗留，过程细节见任务卡、DailyReport 与审计归档。

## 2026-08-15 夜间只读审计（覆盖上次夜跑后的 2026-08-14 工作）

- PCG：发射器收敛为单格占用，新增转角/2～3 格远距组合软奖励，正式权重 3/4/6；摆锤、地刺、冲锤、刺轮使用公开 Seed/类型/锚点派生确定性相位，合法性与公开 Seed 语义不变。
- PCG 验证沿用白天证据：Demo 模块构建成功；Population 15/15、Runtime Navigation Gate 1/1；L_Game Seed 12345 两次布局哈希、机关/资源数量与首摆锤相位一致。
- HeavyImpact：完成 Accepted 后不可正常回滚、低能量无支撑收口、2 秒有界恢复、PostPhysics 起身交接和冲锤 0.60 来源响应比例；构建、7/7 受击自动化和关键资产回读通过，真实机关/墙边/起身画面仍待用户验收。
- 追猎者：近战增加 70 cm 高度差上限，隔层改为严格位置寻路；构建与攻击自动化 1/1 通过，原折返楼梯需用户 PIE 复验。
- 资产只读审计已执行：官方与本地 UE MCP 在线，L_Game 打开、PIE 停止；L_Game、Level0、玩家/追猎者、Population/Presentation、冲锤和刺轮关键资产非 dirty，蓝图父类与主要 DataAsset 引用正确。
- 运行风险：L_Game 静态存在 Runtime Generator、NavMeshBoundsVolume 与 RecastNavMesh，但较早保存日志仍出现运行时 Recast 缺失和 Heavy 恢复错误；日志早于后续提交，不证明当前仍失败，必须在同一正式一局复验。
- 新玩法优先建议：灰盒“磁吸电芯送达出口插槽”，复用现有磁力、Exit 与机关路线压力，不新增任务框架。
- Git：origin/快照/推送结果记录在 `claude/artifacts/nightly/2026-08-15.md` 和自动化回报。

## 2026-08-14 白天汇总

- 固定 Seed/软质量 PCG、刺轮初版与火星修正、玩家 Stop 三方向、制导/磁力 Light、追猎者全局追踪/楼梯限制、HeavyImpact 恢复和冲锤响应比例均形成阶段技术证据。
- PCG 空白段与高厅灯光使用既有评分内的软覆盖奖励；资源密度 12/100，高厅两盏 Movable 灯；未增加硬拒绝或生成重试。
- 未完成项统一为：正式一局动态 Recast/真实追猎者多层追逐，Heavy/刺轮/攻击/PCG 节奏用户验收，最小目标链、首批音效和 Development 打包。

## 2026-08-01 至 2026-08-13 摘要

- 主菜单 Seed/难度 → PCG/Population → 玩家/追猎者 → Exit/死亡/暂停/结算/重开闭环已形成。
- 多层路线采用“跨层宏结构 → 逐层二维 WFC → 合并整栋通行图”；Population 已按机关优先、资源后置分层。
- 摆锤、冲锤、预判抛射、HeavyImpact 起身恢复、磁力破碎 P0 与 StandingImpact 形成阶段证据；正式一局动态导航与首轮 Development 打包未闭环。

## 2026-07 月度摘要

- 建立 UE5.8 C++ 优先 Demo、Project Memory MCP、内部工蜂 Git/LFS、夜间只读维护与双 MCP 协同规范。
- PCG 从二维 Grid-WFC 推进到 Runtime HISM、Population、最小 RoundFlow；追猎者、磁力、生命链路和第三方资产筛选形成基础。

<!-- written by shiqiqiwang at 2026-08-15 08:14 UTC -->

## 2026-08-15 — 摆锤/冲锤灰盒替换（Codex /root）

- 完成：冲锤使用 `SM_HydroLab_VentB1`，补无碰撞薄实体背盖；摆锤保留长方体并应用 HydroLab 工业材质，加宽至 440 cm、支点抬高至 650 cm。
- 验证：两个 Blueprint 编译保存；DataAsset 与组件属性回读、非 Dirty 检查通过；PIE Simulate 启动运行正常且本次未新增 Error。
- 遗留：用户在关卡中验收冲锤绕背封闭、摆锤侧穿与倒地夹体边界。

<!-- written by shiqiqiwang at 2026-08-15 08:21 UTC -->

- Level0 落地补充：旧机关实例存在历史组件覆盖，已按原位置/名称/标签用更新后的同一 Blueprint 重建；地图保存后新视觉、摆锤 220 半宽碰撞与 650 支点回读正确，PIE 未新增 Error。
