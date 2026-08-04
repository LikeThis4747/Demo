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

- 玩家手感精修定位到双地面竞争与斜坡速度换算：可见 StairsB 的 22 个逐级凸包和隐藏坡面同时阻挡 Pawn；玩家 Maintain Horizontal Ground Velocity=true 会把 34.90° 坡面上的 450cm/s 水平速度换算为约 548.7cm/s 沿坡速度。
- V2 全部 12 跑改为职责分离：可见楼梯 Custom、Pawn Ignore、导航关闭；隐藏坡面 Custom、Pawn Block、Visibility/Camera Ignore、导航开启。坡面整体下压 3.5cm，端点高出基准由约 4cm 收敛到约 0.5cm，转向仍使用真实水平平台。
- 保存并重新加载 Level0 后 12/12 属性、Transform 与标签一致；精修后真实 BP_Pursuer 通过 A 上行及中央塔四跑上下行。PIE 停止、临时 Actor 为 0、Level0 非脏。
- 玩家下一步是在 BP_ZeroEscapeCharacter 的 Character Movement 关闭 Maintain Horizontal Ground Velocity 后实走；Camera Lag 暂不启用。未修改蓝图、C++、第三方素材、其他地图或 Recast 全局配置。

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

<!-- written by shiqiqiwang at 2026-08-03 10:18 UTC -->


## 2026-08-03 — PCG 多层完整代码预览

- 完成五片可顺序审查的拟实现补丁，覆盖数据合同、完整结构布局、逐层二维 WFC、HydroLab 表现、运行时导航、GameMode 与 Population。
- 复核并删除无依据的固定导航毫秒门槛；最多 20 个代表点/19 次路径查询只记录指标，10 秒仅作为异步导航构建等待超时。
- 修正结构开口外普通连接格保护、额外楼梯跨楼层公平分配、高天花板房间按实际 Walkable 占地间距、HISM 分组批量提交、Population 零目标合法跳过及导航事件归属表述。
- 隔离验证：五片顺序应用与 diff check 通过；UE 5.8 UHT 与 Demo 模块编译/DLL 链接通过；Demo.PCG 24/24、Demo.GameFlow.AsyncSetupGate 1/1 通过。
- 正式 Source/Content/Config 未应用方案；正式资产迁移、真实 RecastNavMesh、PIE、玩家/追猎者验收等待 Code Review 和用户再次授权。

<!-- written by shiqiqiwang at 2026-08-03 11:59 UTC -->


## 2026-08-03 — PCG 代码预览首轮审查处理

- 独立核对首轮审查，没有照单全收。采纳 Generator 一次性契约说明、Transform 校验合并、Population 重复扫描删除、两级 Spawn 预算职责注释和 Planner 分节注释。
- 将自动重试修正为现有 GameMode + GameInstance 的有限跨 World 恢复：只重试 Seed 可能改变的生成/最终导航失败，下一 Seed 固定派生，最多 3 次；正式游戏关卡由蓝图软引用配置，不写死路径。
- 拒绝拆分职责内聚的 Planner、新状态子系统/组件、加载界面、同 World 原地重生成、删除累计 Spawn 预算和弱化实际路径验收。
- 复审版五片补丁顺序应用与 diff check 通过；UE 5.8 UHT、Demo 模块编译/DLL 生成通过；Demo.PCG 24/24、Demo.GameFlow 2/2 通过。完整 Build 仅受运行中 Editor 的引擎 DLL 文件锁影响。
- 首轮审查已标记完成并归档；正式 Source/Content/Config 未应用该方案，真实资产、PIE、RecastNavMesh、玩家与追猎者验收仍待落盘授权后执行。


## 2026-08-03 — 局流程三阶段闭环完成 + UI 文档整理

- 暂停菜单用户验收通过：ESC 弹/关、继续、选择关卡开新局、返回主菜单；至此 GAME_LOOP_PLAN 三阶段（主菜单进游戏 / 结算重开暂停 / 胜负判定）全部完成。
- 决策：保留内嵌 `ULevelSetupWidget`（中途换种子/难度不退主菜单），审美统一打磨排期到最后；用户确认老主菜单样式暂时可接受。
- 正式文档落盘 `DOC/Design/UI/GAME_FLOW_UI.md`（闭环、类分工、胜负链、ESC 双路径、LevelSetup 复用、资产清单、踩坑）；`DOC/README.md` 同步索引；`claude/plans/` 三份规划稿标记完成留痕；`progress.md` M3 更新。
- 分工确认：多层 PCG 落盘由另一对话负责，本对话不负责；下一候选任务为追猎者攻击玩家的伤害系统补齐。

<!-- written by shiqiqiwang at 2026-08-03 17:05 UTC -->


## 2026-08-04 — 夜间只读审计

- 审计确认 2026-08-03 白天完成正式一局三阶段闭环，并将多层 PCG 正式实现落入当前工作区；开始时 main 与 origin/main 分歧 0/0，但实现、4 个 UE 资产及相关记录尚未提交。
- 当前 Demo DLL 与日志证明完整构建成功；`Demo.PCG` 21/21、`Demo.GameFlow` 2/2，合计 23/23。代表性白天 PIE 经一次确定性换 Seed 后成功生成 3 层并完成 10 条导航路径和玩法装配。
- 本地 UE Editor MCP 返回 pong=false，官方 UE5.8 MCP 工具未暴露；蓝图审计未执行，不推断关卡、蓝图、DataAsset、资产引用或配置。
- 当前状态为“已实现、待验收”：下一步先玩家连续实走与真实追猎者跨层追逐，再联合回归，最后 Easy/Normal/Hard 各至少 300 Seed 校准。
- 夜报：`claude/artifacts/nightly/2026-08-04.md`；本轮未修改项目代码、资产、配置或规范。

<!-- written by shiqiqiwang at 2026-08-04 03:58 UTC -->

## 2026-08-04 — PCG 初验修正

- 楼梯平台与顶层天花板灯位 2/3/0、追猎者占一楼起点、玩家两格外就近出生已落盘；删除无作用的 PresentationVersion。
- DemoEditor 构建、Demo.PCG 22/22 与 Demo.GameFlow 2/2 通过；Runtime Generator 已把生成灯的 LightComponent 统一设为 Movable，第三方 LampA 未改，等待用户 PIE 验收 “Preview” 消失及灯光表现。

<!-- written by shiqiqiwang at 2026-08-04 05:55 UTC -->


## 2026-08-04 — PCG 路线初步验收与 Seed 30794 楼梯导航修正

- 用户确认当前多层 PCG 路线初步验收成功；该里程碑不等同于全部楼梯组合、完整跨层追逐、整局流程或 900 Seed 校准完成。
- 用户实测 Seed 30794 时确认楼梯隐形坡道与平台的绿色导航面存在缺口，追猎者无法上楼。
- 第一轮把原因归到 Population 后生成的地刺、磁性资源物和出口组件，用户复测证明关闭它们的导航影响无效。磁性资源物已恢复为导航障碍，出口冗余设置已删除；没有遗留全局导航参数、临时 Seed、调试 Actor 或导航轮询。
- 地刺保留独立玩法规则：格栅、尖刺和伤害盒不参与导航，尖刺忽略 Pawn，追猎者不被地刺碰撞或伤害阻断；玩家仍由 HurtZone 结算伤害。
- 正式 Presentation DataAsset 的双层楼梯 2 段、贯通三层楼梯间 4 段隐形坡道统一上移 3.5cm，端部约高出相邻表面 4cm；未新增运行时分支或测试代码。
- 完整构建、24 项自动化和正式主菜单 Seed 30794 生成通过；用户随后实测确认追猎者可以上原问题楼梯，本问题验收通过。
- 后续摆锤默认不以移动网格驱动导航重建；硬闯、受击和避让交给碰撞/行为，只有长期封路的离散状态才更新固定导航修改区域。
