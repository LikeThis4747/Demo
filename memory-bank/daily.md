# Daily Log — Demo

> 按日期倒序；只保留完成、验证、决定与遗留，过程细节见任务卡、日报和审计归档。

## 2026-07-29

- 夜间只读审计确认 Level0 已保存 HydroLab WFCBrickStudyV3：7 类独立积木、兼容链与完整组合；本地 UE MCP 可读取其 Outliner，三项相关 Blueprint/AnimBP 均为 UpToDate。
- 追猎者白天改动已加入近战攻击/方向受击蒙太奇、受击停顿、AttackApproachRadius、限时 AttackProjectile Tag 与 Camera 通道忽略；仅有静态源码和资产状态证据，本轮未构建、未跑 PIE、未做玩家验收。
- Level0 的短时 Simulate/重载证据存在，但日志仍明确缺少 RecastNavMesh；玩家连续实走、AI 寻路、10+ Seed 与生命归零失败/同 Seed 重开均未验证。
- 本次官方 UE5.8 MCP 未暴露，DataAsset 精确属性与通用引用审计未执行；本地 MCP 的 open_asset_editor 仍有断言/SEH 崩溃保护记录。
- 遗留优先级：先补 Level0 Recast 与玩家/AI 实走，再完成生命归零失败/同 Seed 重开；随后构建并验收追猎者攻击/受击链和投掷物 Tag 重复投掷边界。

## 2026-07-28

- 夜间只读审计确认 Seed 16001 PCG/Population/RoundFlow/Health/Physics Control 基本链路，并完成内部工蜂快照。
- 白天收敛三张过期追猎者 active 卡：Physics Control 改为已完成且用户 PIE 验收通过；追击卡改为“最小时机闭环已运行、正式动画/多 Seed 导航待验收”；最小 ABP 改为“GroundSpeed/EventGraph 已验证、AnimGraph/最终验收待后续”，三卡全部归档。
- `claude/tasks/active` 保留 SFCorridors 物件筛选卡，并新增 HydroLab 错层房间装配验证卡。
- UE 只读检查 70 个 SFCorridors Static Mesh 的尺寸/Pivot，并查看候选缩略图；首批建议仅保留候选 `SM_LampWallL` 与 `SM_comp`，HydroLab 继续作为 PCG 主结构。
- 295 个 SFCorridors LFS 资产本轮未删除；删除前必须完成 Asset Registry 依赖闭包、精确保留/删除清单与用户明确授权。

## 2026-07-27

- 完成 PCG 空间职责精简、运行时顶灯、独立 Population 地刺/磁力物放置和最小 RoundFlow。
- 玩家位于 PCG Start，追猎者位于身后至少 1200 cm，双方初始朝向一致，Exit 只结算一次成功。
- 接入追猎者 Physics Control 局部受击、最小 locomotion AnimBP/BlendSpace、地刺与玩家 HealthComponent；用户完成主视口位置验收、地刺首轮场景验收和 Physics Control PIE 验收。
- UE5.8 DemoEditor 完整构建及 SelectedViewport PIE 曾成功；最新静态代码审阅没有重新运行自动化。
- 遗留多 Seed、导航、追击动画、ABP 最终装配和正式失败闭环。

## 2026-07-26

- 夜间只读审计确认本地 UE Editor MCP 在线；停止状态关卡未见完整动态导航证据。
- 历史技术证据保持 DemoEditor 构建成功、Demo.PCG 19/19 和 288/288 Seed Sweep。

## 2026-07-25

- 用户授权后仅为共同根材质 M_HydroLab 启用 Instanced Static Mesh Usage。
- V4 全图 Grid-WFC、Count/MaxConsecutive/Connected/Tarjan、有界回溯与 Runtime HISM 通过历史构建、19/19 自动化和 288/288 Seed Sweep。

## 2026-07-24

- PCG V3.2 完成构造性 Progression、Grid-WFC、300 cm 分离结构展开、Runtime HISM、13/13 自动化与 288/288 Seed Sweep。
- Demo 升级 UE 5.8；双 MCP 能力矩阵与协同规范归档。

## 2026-07-23

- 开发顺序冻结为“实时整关生成 → 追猎者 → 地图内玩法闭环”；SciFiHydroLab 经 300 cm 分离结构实拼入选。

## 2026-07-18

- 创建 C++ Demo、轻量渲染与 C++ 优先工作流；初始化 Git LFS、内部工蜂备份和夜间只读维护。
