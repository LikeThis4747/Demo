# Current Task

- 当前目标：由用户在 Level0 V2 手动验证全部楼梯的玩家手感；AI/Recast、保存重载与碰撞职责精修已完成。通过后再讨论多层 WFC 数据合同，未经新授权不修改 PCG/WFC 代码。
- 2026-08-03 最终 Level0 配方：V1 冻结；V2 共 12 跑。可见 `StairsB` 为实例级 `Custom + QueryAndPhysics`、`Pawn=Ignore`、不影响导航；`ZE_NavOnlyRamp` 为 `Custom + QueryAndPhysics`、`Pawn=Block`、`Visibility/Camera=Ignore`、影响导航。
- 坡面参数：每跑约 393.23×205×8cm、34.90°，每一跑独立；转向使用真实水平平台。12 段整体下压 3.5cm，端点顶面约高出楼层/平台基准 0.5cm。
- 验证：保存并重载 Level0 后 12/12 组件属性、Transform 与标签一致；真实 `BP_Pursuer` 在精修后通过 A 上行及中央四跑塔 G0↔F3。此前 A/B/C/D/T 双向路线证据保留。PIE 已停止、临时 Actor 为 0、Level0 非脏。
- 玩家待办：打开 `/Game/ZeroEscape/Characters/BP_ZeroEscapeCharacter`，在 Character Movement 中关闭 `Maintain Horizontal Ground Velocity`，编译保存后实走 A 与中央塔双向路线；确认无突然加速、脚底悬空、平台接缝跳变或镜头回缩。Camera Lag 暂不启用。
- WFC 边界：隐藏坡面属于完整楼梯宏模块每一跑的内部固定子件，不是独立 1x1 Tile；可见楼梯、坡面、平台、净空和栏杆边界必须整体生成与验证。
- 记录：任务卡 `TASK-20260728-002-HydroLab错层房间装配验证.md` 保持 Active；正式配方见 `DOC/Design/PCG/SCIFI_HYDROLAB_MODULE_TABLE.md` 第 11 节。
- 本轮未修改蓝图、C++、DataAsset、第三方素材、其他地图或 Recast 全局配置；玩家蓝图由用户手动改。
