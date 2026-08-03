# Daily Log — Demo

> 按日期倒序；只保留完成、验证、决定与遗留，过程细节见任务卡、日报和审计归档。

## 2026-08-03

- 夜间只读审计确认自 2026-08-02 01:08（+08:00）后无新提交，开始时工作区干净，main 与 origin/main 分歧 0/0；复核 5 张现有任务卡与 64 个 Source/Demo C++/Build 文件清单。
- 正式 GameMode 仍只负责生成与开局摆放，HealthComponent 生命归零仍只记日志，运行时布局仍是二维 FIntPoint 与四方向 OpeningMask；本轮无构建、自动化或 PIE 新证据。
- 官方 UE5.8 MCP 在线、本地 UE Editor MCP 返回 pong=false；蓝图审计未执行，不推断关卡、蓝图、资产引用或配置。
- 明日顺序保持：先完成 Level0 V2 的 Recast、玩家和真实追猎者三层连续移动，再完成 Exit/死亡/重开闭环，随后重跑完整构建、Demo.PCG 与至少 10 Seed。

- 白天在 Level0 V2 中央三层塔的 `NavigationTrial` 放置 4 段隐藏行走坡面：每段约 393.23×205×8cm、34.90°，最终端点顶面约高出楼层/平台基准 4cm，主体嵌入踏步；渲染完全隐藏，稳定使用 `InvisibleWall` 碰撞。
- 未调整 Recast 全局参数；最终导航绿色覆盖连续跨过四跑、两处 180° 平台和三个楼层落脚面。真实 `BP_Pursuer` 以 1.2 倍缩放完成 G0→F3 与 F3→G0 双向移动。
- 只保存并重载 `/Game/Levels/Level0`；编辑器重开后 4 段坡面名称、标签和 Transform 均持久化，临时测试 Actor 为 0，PIE 已停止。没有修改第三方资产、代码、DataAsset、其他地图或 Recast 全局配置。
- 玩家键盘连续实走仍待用户亲自验收；自动输入不作为可信证据，任务保持 Active。WFC 后续把每跑隐藏坡面作为楼梯宏模块内部固定子件，不作为独立 1x1 Tile。

- 白天扩展到 V2 的 A/B/C/D 四座双层楼梯，各新增 2 段隐藏坡面；连同中央塔共 12 段。保存重载后 A/B/C/D/T 数量为 2/2/2/2/4，12/12 的隐藏、反射/光追/投影、导航和 `InvisibleWall + QueryAndPhysics` 属性一致。
- 四座楼梯各一处约 21.571cm 的平台尾栏缺口闭合到约 0.0000003cm；楼梯入口和模块连接口保持开放。Recast 绿色覆盖连续通过每座楼梯的两跑与转向平台。
- 真实 `BP_Pursuer` 以 1.2 倍缩放完成 A/B 的 G0↔F2 和 C/D 的 F2↔F3 双向移动。C 首次下行因目标处于攻击距离而提前停在坡面尾部，移远目标后完整落到二层平面；测试结束后 PIE 停止、临时 Actor 为 0、Level0 非脏。
- 隐藏坡面方法已记录到 PCG 模块表第 11 节：它是本项目 HydroLab 楼梯宏模块内部配方，不是 UE 官方概念或独立 WFC Tile。玩家手动连续实走仍待用户验收，任务保持 Active。

## 2026-08-02

- 夜间只读审计确认白天提交 `1571eba7` 只更新 Level0、HydroLab 任务卡与记忆；无 C++ 变更，开始时工作区无未提交文件、main 超前 origin/main 1 个提交。
- 官方 UE5.8 MCP 在线、本地 UE Editor MCP 离线；Level0 非脏且 PIE 未运行，V1/V2 Actor 数仍为 1836/1857。V2 有 96 个 HydroLab 灯实例；抽查实例 RectLight 为 Movable，而第三方蓝图模板保持 Static。
- Level0 虽存在 NavMeshBoundsVolume 与 RecastNavMesh，但导航体 X 轴只覆盖 -17600..22400 cm，V2 已核对墙体约在 X=45000 cm，当前不覆盖 V2；三层 Recast、玩家连续实走与真实追猎者上下楼仍未验证。
- 当前运行时生成合同仍是二维 FIntPoint GridSize 与四方向 OpeningMask；正式 GameMode 只有开局生成/放置，没有 Exit、死亡结算或重开入口。本轮未运行构建、自动化或 PIE。
- 最短下一步：白天先补 V2 导航覆盖并完成玩家/真实追猎者三层验收，再完成菜单到 Exit/死亡/重开的整局闭环，之后确认多层 WFC 宏块/保留区/接口/共享边数据合同。

## 2026-08-01

- Level0 中冻结 `HydroLab_ThreeFloorPCGSceneV1` 为 1836 Actor，并在 X+12000cm 的独立 V2 精修；保存重载后 V2=1857、V1 未变、关卡非脏。
- 修复 V2 D 楼梯 8 块误落世界原点的西外墙、3 块共面重复墙和两块突兀银墙；重载后 8/8 外墙射线命中，原点附近无 V2 遗留 Actor。
- 24 段斜栏杆由会随坡度倾斜立柱的 FenceB 改为 FenceE 双横杆；24/24 沿跑向覆盖 322.5cm、异常 0。中央塔二三层各一处 21.571cm 平台栏杆缺口已闭合至约 0.0000003cm。
- 三层高厅均保留并复核约 550cm 净高；V2 96 盏灯改为 Movable 后 Preview 标记消失。没有修改灯具蓝图、第三方素材、代码、配置或 Git。
- WFC 结论：双层/三层楼梯及其平台、净空、支撑是不可拆分的多格宏块；高厅传播上层保留占用；先固定垂直宏块，再逐层求解平面路线，最后按唯一边所有者派生墙、门框和栏杆。
- 仍未完成玩家胶囊连续实走、三层 Recast 与真实追猎者上下楼；V2 只算静态几何、碰撞、截图和保存重载通过，任务保持 Active。
- 夜间只读审计确认自上一轮后有白天提交 15fba6c（main menu）及已保存但未提交的 L_Game 修正；相关 GameFlow/UI DLL 晚于源码并被编辑器加载，本轮未新跑构建、自动化或 PIE。
- 官方与本地 UE MCP 均在线。L_Game 当前使用 BP_ZeroEscapeGameMode，DefaultPawn 为 BP_ZeroEscapeCharacter，Generator=ExplicitOnly，Populator 已绑定 Generator 与 DA_Population_Default；四个相关蓝图 UpToDate，WBP 的 BindWidget 控件齐全。
- 现有白天日志确认主菜单 Seed 12345 只生成一次，随后放置 4 个地刺、8 个磁性物体并生成玩家/追猎者；这证明修正后的开局链路，不代表整局、18 项测试、至少 10 Seed 或导航验收。
- 新风险：配置出生下限为 1200 cm，最新成功日志实际玩家—追猎者距离为 1138 cm；后续复核碰撞调整与开局公平性，再完成菜单→Exit/死亡→结算/重开的玩家验收。
- 内容建议：用现有磁力物、陷阱、追猎者与 Exit 做最小“磁性保险丝撤离”目标，在不先扩建框架的前提下增加一局目标压力。
- Git 快照结果见当日夜报；仅在 origin 精确匹配内部工蜂后允许普通推送。

## 2026-07-31

- 配置并验证项目级 UE5.8 官方 MCP；主菜单、GameInstance、正式 GameMode 与 WBP 进入白天实现和装配。
- 夜间时新 GameFlow/UI 尚无构建或资产审计证据；该边界已被 2026-08-01 当前构建、资产属性和白天日志证据取代。
- 内部工蜂快照 204851bd8f152ba25904b349263f08ebda8061ee 与 f95379ee00f18a297113cc493772add52fc9983a 已普通推送。

## 2026-07-30

- Level0 的 HydroLab_RoomNetworkV5 为 259 Actor / 15 个叶文件夹；三层楼梯塔任务卡记录 211 Actor / 13 文件夹。玩家、Recast 与真实追猎者验收仍未完成。
- 同一 Seed 15339 的重复生成/Exit 日志只算重复运行证据，不是多 Seed 放行。
- 内部工蜂快照 fd04bdfff78034d0d5b5af0092db9309e11727a5 与 627beaecfcf690a6b51a171f07be2ebd6bb2b2c1 已普通推送。

## 2026-07-29

- 完成 Level0 V3/V4 导航样例与 V5 静态装配；追猎者近战/方向受击、AttackProjectile Tag 与磁力 Camera 通道仍待当前验收。

## 2026-07-28

- 收敛追猎者任务卡并保留 SFCorridors 只读筛选；第三方资产删除仍需依赖闭包、精确清单和用户授权。
- 历史 PIE 证据覆盖 PCG/Population/RoundFlow/Health/Physics Control 基本链路。

## 2026-07-27

- 完成 PCG 空间职责精简、运行时顶灯、Population 地刺/磁力物和最小 RoundFlow。

## 2026-07-26

- 历史技术证据保持 DemoEditor 构建成功、Demo.PCG 19/19 和 288/288 Seed Sweep；当前版本需重建基线。

## 2026-07-25

- 用户授权后为共同根材质 M_HydroLab 启用 Instanced Static Mesh Usage；V4 通过历史构建、19/19 自动化和 288/288 Seed Sweep。

## 2026-07-24

- PCG V3.2 完成构造性 Progression、Grid-WFC、300 cm 分离结构展开、Runtime HISM；Demo 升级 UE 5.8，双 MCP 协同规范归档。

## 2026-07-23

- 开发顺序冻结为实时整关生成 → 追猎者 → 地图内玩法闭环；SciFiHydroLab 入选主结构。

## 2026-07-18

- 创建 C++ Demo、轻量渲染与 C++ 优先工作流；初始化 Git LFS、内部工蜂备份和夜间维护。
