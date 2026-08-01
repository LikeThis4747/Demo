# TASK-20260728-002 — HydroLab 错层房间装配验证

- Owner：Codex / 当前会话
- Status：active
- Stage：三层 PCG 场景 V1 已落盘并冻结；正在独立 V2 副本中进行 WFC 接口、楼梯安全与视觉精修
- Created：2026-07-28
- Updated：2026-08-01

## 目标与验收

- 目标：只读参考 SciFiHydroLab 的 Demonstration 与 Overview，在 Level0 中搭建一套低重复、可行走、包含楼梯和错层平台的完整房间组合原型，用真实装配结果反推后续 WFC 约束。
- 验收：场景具有入口、主空间、楼梯、上层或半层平台、侧向空间和有意开放边界；素材保持原比例；关键拼缝、碰撞、楼梯净空和基本行走可验证；Level0 可保存并重新加载。
- 非目标：不修改第三方 HydroLab 资产，不修改 PCG/WFC 代码，不接入 SFCorridors，不构建大量重复走廊，不把本轮原型直接宣称为正式生成模块。

## 修改范围

- 允许修改：`D:\UE5projects\Demo\Content\Levels\Level0.umap`
- AI 工作记录：`D:\UE5projects\Demo\claude\tasks\active\TASK-20260728-002-HydroLab错层房间装配验证.md`
- 当日计划：`D:\UE5projects\Demo\DOC\DailyPlan\2026-07-28-HydroLab错层房间装配验证.md`
- 导航补齐计划：`D:\UE5projects\Demo\DOC\DailyPlan\2026-07-29-Level0-PCG组合导航补齐.md`
- 会话结束时按规范更新：`.ai-context/current-task.md`、`memory-bank/activeContext.md`、`memory-bank/daily.md`；形成稳定结论后再更新 `memory-bank/progress.md` 或 `memory-bank/decisions.md`。
- 共享/潜在冲突：Level0 可能已有用户或其他任务的未保存修改；切图和保存前必须核对 Dirty 状态，不覆盖无关 Actor。
- 并行拆分/依赖：不并行修改 Level0；Demonstration/Overview 仅只读分析，Level0 是唯一资产写入目标。
- 用户已授权范围：允许按已讨论流程搭建并保存 Level0。

## 计划与检查点

- [x] 只读检查 Demonstration，提取楼梯、平台、WallPieceB、PillarD、FenceA/B/C2/D 的真实组合方式。
- [x] 只读检查 Overview/Asset Registry 与关键静态网格尺寸、Pivot 和材质表现。
- [x] 加载 Level0，确认原有关卡内容和可用隔离区域。
- [x] 在独立 Outliner 文件夹中搭建错层 HydroLab 主房间与服务回路连接段。
- [x] 保存 Level0；编辑器崩溃重启后从磁盘重新载入并核对 Actor 与 Transform。
- [x] 验证四角封口、出入口、地面回路、楼梯高度序列、平台接缝、护栏通口和短时 PIE 启动。
- [x] 汇总可转化为 WFC 的多格占用、垂直 Socket、落脚区、收边和护栏约束。
- [x] 在隔离样板区依次摆出封闭、直通、转角、Portal600→Door450、楼梯上升、上层护栏门和层高过渡 7 类单元。
- [x] 核对样板的原比例、600cm 边界、接缝、通行净空、落差防护和允许/禁止邻接。
- [x] 仅保存 Level0，并向用户交付当前约束与候选扩展约束的清晰分层。
- [x] 将 600cm 求解 Cell 与可独立成立的 1x1/2x1/2x2 Module 明确分层，冻结积木内部验收门槛与 Socket 合同。
- [x] 复核 StairsA/B/C 的真实上升高度；StairsC 仅作 +135cm 室内错层，二楼/夹层必须使用经验证的独立配方和标高。
- [x] 撤掉首版 7 个孤立盒状样板；摆出独立 B00 与真实相接的 B00→B01 兼容对，作为第一轮视觉复核检查点。
- [x] 搭建带 StairsC 的室内错层积木、A+B 与 A+B+C 兼容链，以及由 7 类积木组成的完整可玩组合段。
- [ ] 用户在 Level0 中完成整体审美、玩家实际行走与 AI/NavMesh 验收。
- [x] 将现有 NavMeshBounds 扩展到同时覆盖原测试区与 V3 完整组合，并验证入口到 Deck135 的五段导航路径。
- [x] 冻结 V4 楼梯房，在独立区域搭建不依赖楼梯的大小房单层回路样例。
- [x] 验证小房直连、大房单口接入、Low300→Tall750 过渡、分流/汇合与目标房的连续通行。
- [x] 检查新样例的墙缝、天花缝、穿模、共面重复和接口前 300cm 净空，只保存并回读 Level0。

## 验证

- 命令/场景：UE5.8 官方 MCP SceneTools、EditorAppToolset 与本地 UE Editor MCP；Level0 编辑视口与 PIE。
- 结果：
  - 主宏块位于 `X=3000..4200, Y=3700..4900`，以 600cm 逻辑格计算为 2x2 占用；地面 `Z=5`，错层平台 `Z=140`，天花底部 `Z=528`。
  - 对照 Demonstration 后，4 个 WallPieceB 暴露端均由 PillarD 收边；四角在 `Z=50/150/300/500` 的内外斜向射线全部命中实体，未留贯穿外墙的空洞。
  - 16 块 CeilingB 与 525cm 高墙采用约 2cm 受控咬合封光；相邻天花只有浮点级 AABB 微重叠，未发现广面共面 z-fighting 证据。
  - South/East 两个 600cm 边界 Socket 在多条横向射线与 `Z=60/120/200` 高度保持畅通；端柱最大只侵入 South 约 29.5cm、East 约 1cm。
  - 地面绕行路线以角色中心高度 `Z=101` 检查 5 段均无命中；South/East 楼梯表面采样分别形成 `5→68.375→108.875→140` 与反向下降序列。
  - 首轮复查发现 `HL_Macro_Rail_W_Long` 在 `x≈3296.7` 封死楼梯顶平台通往西侧猫道；已按原素材尺寸改为 FenceA + 150cm 通口 + FenceC2 + FenceD 三段栏。通口 `Y≈4050..4200` 的三条射线均畅通，两侧地面均为 `Z=140`；南北防坠栏与北端转角均能命中，完整上层 L 形路线复查为畅通。
  - 短时 Simulate PIE 成功进入 `Level0` Running 状态并正常停止；Error/Fatal 为 0。仅出现一次 `Failed to find valid viewport when switching between PIE and SIE`，属于本次无焦点 Simulate 视口告警，不作为行走或导航结论。
  - 官方 AssetTools 仅保存 `/Game/Levels/Level0` 返回成功；保存后 `Level0.umap` 为 841,979 bytes，时间 2026-07-28 18:13:05。`Content/Assets/SciFiHydroLab/**` 无修改。
  - 新增隔离样板区 `PCG_AssemblyStudy/HydroLab_WFCUnitGallery`，位于 `X=600..3900, Y=6000..7800`；最终 7 个子文件夹、88 个单位缩放 Actor，与旧场景最北边界保留约 10m 间隔。
  - 样板分别为 `00_Closed_Low305`、`01_Straight_WE_Low305`、`02_Corner_EN_Low305`、`03_Portal600_to_Door450`、`04_Stair_E0_to_W135`、`05_UpperPlatform_Gate274`、`06_Transition_WLow_ETall`。为便于检查内部，所有单元只保留北半顶板作为统一剖切展示；完整封顶效果仍由主场景样板承担。
  - 真实 DataAsset 为 `GridSize=24x16`、`LogicalTileSizeCm=600`、`RoomSizeTiles=2`；300cm 是视觉拆件粒度，1200cm 目前只是 2x2 Required 房间占格，不是可原子 Collapse 的语义宏 Tile。
  - 450cm 原生 DoorFrame 未缩放；它与四块 75x150cm WallO 组成完整 600cm 墙湾。对侧以 WallTrimK 做原比例 `Portal600_H260`，明确区分“600 门洞”和“450 门框”。未放功能门扇，因为原版上滑门需要至少 565cm 扫掠净高。
  - 射线确认普通开边、Portal600 主体、Door450 中心、楼梯中心、上层护栏门和层高过渡底部均畅通；封闭墙、Door450 两侧补边、门楣、楼梯/平台护栏和 305→530 Bulkhead 均正确阻挡。
  - StairsC 中心表面采样形成约 `Z=-0.5→43.125→63.375→103.875→113.985→135` 的单调序列，并连续落到 Z=135 平台。平台门净宽约 274.5cm；平台下方和楼板下方仍必须作为不可通行占用处理。
  - 最终 88 个样板 Actor 均为单位缩放，未发现缺失、Transform 偏差或完全重复 Transform；墙/地/顶仅在设计边界接触，没有双份共面构件。第二次视口机位调整被 MCP 崩溃保护安全拦截，编辑器随后 Ping 与 Ready 均正常，因此停止继续自动视口捕获。
  - 官方 AssetTools 再次仅保存 `/Game/Levels/Level0` 返回成功；保存后 `Level0.umap` 为 984,406 bytes，时间 2026-07-28 19:47:47。`Content/Assets/SciFiHydroLab/**` 仍无修改。
- 用户操作：最终在 Level0 中检查整体视觉、用玩家实际走完 Ground/Deck 两条路线，并观察 AI 是否覆盖两段楼梯与 150cm 转向口。
- 用户验收：待验收。

## 可转化为 WFC 的约束交接

- 该房间应作为原子 2x2 宏块，不可拆成 4 个独立 OpeningMask 单元后任意重排。
- 外部 Socket：South-600/Ground、East-600/Ground；内部 Portal：Ground ↔ Deck140（South StairsC）与 Deck140 ↔ Ground（East StairsC）。
- 宏块必须携带 `Footprint`、`ElevationProfile`、`SocketWidth`、`SocketHeight`、`ClearZone`、`LandingZone`、`GuardEdge` 与 `EdgeOwner`，不能只表达 NSEW 四位开口。
- WallPieceB 暴露端必须由且仅由一侧模块拥有 PillarD EndCap；墙顶/天花使用受控容差咬合，禁止随机同面叠放。
- 楼梯顶连接必须同时满足连续落脚面、胶囊净宽和护栏门洞；本轮 150cm 通口说明“有邻接”仍不足以代表可通行。
- 当前门框使用 `ScaleY≈1.3333` 才形成 600cm Socket；若未来实例配方仍强制单位缩放，需要新增原生 600cm 门框配方或显式允许受控缩放。

## 阻塞、风险与下一步

- 阻塞：当前无工具阻塞；正式完成仍等待用户视觉/实玩验收。
- 风险：楼梯高度 140cm 不整除 300/600cm 网格；点射线和短时 PIE 不能替代玩家胶囊连续 Sweep、Recast 覆盖与 AI 实际寻路。PillarD 灯条局部偏亮，留给最终灯光打磨。17:43 左右编辑器曾在视口捕获附近崩溃，但同时有其他资产编辑活动，无法确定单一根因；恢复后停止高频捕获并改用几何/碰撞检查。
- 下一步：由用户先在 Level0 审美检查并实走两条路线；通过后再把上述宏块语义约束映射到 WFC 数据结构与 Solver，不在本任务中修改 PCG/WFC 代码。

## 2026-07-28 用户视觉复核后的纠正

- 首版 `HydroLab_WFCUnitGallery` 的射线与单位缩放检查虽然通过，但视觉结果是孤立空盒、悬空半顶板和缺乏上下文的楼梯/平台；不能作为可玩 Module 验收。
- 根因是错误地把 600cm WFC Cell 直接等同于独立美术房间，并用孤立展示替代了“单块内部自洽 + Socket 成对兼容 + 多块组成场景”的验证链。
- StairsC 上升约 135cm，只能作为房间内部错层工具，不再称为二楼楼梯；若素材无法在冻结的 FloorToFloor 高度上精确闭合，则明确判定二楼模块不成立，不通过缩放或悬空落台强拼。
- 重构阶段继续只允许修改 Level0；不修改 PCG/WFC 代码和第三方 HydroLab 资产。首版 Gallery 的 88 个 Actor 均由本任务创建，用户已允许删除 AI 自建测试对象。

## 2026-07-28 积木化重构第一检查点

- 冻结四层含义：`Piece` 是 HydroLab 原始网格；`Cell` 是 600cm 求解地址；`Module/Brick` 是可原子选择、内部自洽的 1x1/1x2/2x2 配方；`Composition` 才是多个积木按 Socket 约束形成的可玩场景。`OpeningMask=0` 只表示 Empty/Outside，不再伪装成封闭可玩房间。
- 积木硬门槛：单位缩放与明确占格、除 Socket 外壳体闭合、内部可达图连续、行走面/胶囊净空、楼梯落脚区与防坠边、占用体积、接口唯一收边所有者、无空洞/共面重复、旋转后仍合法、同一视觉家族，以及最低限度的房间内容。
- Socket 合同至少携带：所在边及偏移、净宽、`FloorZ`、净高、通行类别、顶高类别、门洞/开边处理、Owner/Receiver/Resolver、落脚深度、净空、左右手性与 StyleFamily；仅有 NSEW OpeningMask 不足以判断可拼接和可通行。
- 已删除 `PCG_AssemblyStudy/HydroLab_WFCUnitGallery` 下本任务创建的 88 个失败样板 Actor；确认 `HL_WFCU_*` 为 0。未删除或移动其他文件夹内容。
- 新展示根目录为 `PCG_AssemblyStudy/HydroLab_WFCBrickStudy`：
  - `A_Standalone/B00_Connector_NS_1x1`：600x600cm，低顶约 305cm，南北 Ground Socket，两侧墙封闭，完整地板/天花与一盏 HydroLab 灯；13 Actor。
  - `B_Pair/B00_Connector_NS_1x1`：B00 的第二个实例；13 Actor。
  - `B_Pair/B01_Threshold_LowToTall_1x2`：600x1200cm，南端低顶约 305cm、北端高顶约 530cm，标高始终为 Ground Z0；接口处只封 305→530cm 的上部 Bulkhead，底部通行净空保持连续；32 Actor。
- B00→B01 当前约束：`Ground/Open600/Low305` 可接 B01 南侧同类 Receiver；B01 北侧输出 `Ground/Open600/Tall530`，只能继续接 Tall530 兼容积木。不同顶高不能直接相接，必须由 B01 这样的 HeightTransition Resolver 承担封口。
- 58 个新 Actor 均为单位缩放，完全重复 Transform 为 0。几何射线确认：独立 B00 南北通路畅通、侧墙命中；B00→B01 全路径与拼缝畅通；Bulkhead 下方畅通且其 305→530cm 高区正确阻挡；低/高侧墙命中，B01 北 Socket 畅通。地面采样 Z=0，低顶表面约 Z=308.75，高顶表面约 Z=544.53。
- 楼梯按真实可行走净升高而非包围盒重算：StairsA 原生约 +45cm；StairsB 在 Demonstration 中原生约 +187.5cm；StairsC 原生 +135cm。旧 Level0 用 StairsB 强接 +225cm 时，首尾存在约 22.5/33.75cm 缝，最多只能视为角色步高可能跨越的实验，不是自洽积木，也没有 AI/Nav 证据。
- 因此本阶段不成立“二楼楼梯”：StairsC 仅进入后续 B02 作为 +135cm 室内错层；双 StairsC 的理论净升高为 +270cm，仍不等于现有 300cm 层高；当前素材没有经验证的原生 +300cm 配方。若以后做真正二楼，必须先冻结 FloorToFloor，再找到精确闭合、带落脚与护栏且通过玩家/AI 导航的独立积木。
- UE5.8 官方 AssetTools 仅保存 `/Game/Levels/Level0` 成功；保存后 `Level0.umap` 为 937,093 bytes，时间 2026-07-28 20:18:25。`Content/Assets/SciFiHydroLab/**` 无修改；PCG/WFC 代码无修改。

## 2026-07-28 V3 最小积木与完整组合交付检查点

- 先回到真实 `Demonstration` 复核素材使用频率和装配方式：`WallPieceB` 全关卡仅 2 个，属于门侧窄条特殊件，判定为不可规格化结构件并彻底淘汰；高墙改用示例中重复出现的 `WallH @ Z0 + WallPieceF @ Z300` 配方。示例中找到 20 组同 XY、上下相差 300cm 的真实配对，形成约 750cm 高墙；V3 中 `WallPieceB` 使用量为 0。
- 新根目录为 `PCG_AssemblyStudy/HydroLab_WFCBrickStudyV3`，磁盘重载后共 538 Actor，全部保持单位缩放。按实际网格语义检查，完全相同 Mesh 语义与 Transform 的重复组为 0；106 个普通 `WallPieceF` 上墙段全部能在同 XY、低 300cm 位置找到 `WallH` 下墙段。
- `A_Standalone` 摆出 7 个内部自洽的最小单元，共 216 Actor：`G00_LowStraight_NS_1x1`、`G01_TallStraight_NS_1x1`、`G02_TallCorner_NE_1x1`、`G03_TallT_NES_1x1`、`G04_TallDeadEnd_S_1x1`、`G05_TallChamber_SE_2x2`、`G06_TallSplitLevel_S_2x2`。所有单元均具有完整地面/天花、除 Socket 外闭合的外壳和同一 HydroLab 视觉家族。
- `B_Compatibility` 共 87 Actor：`B01_Low300_to_Tall750_AB` 展示 Low300 直通块经 `RiseResolver` 接 Tall750 直通块；共享边只由两块 `WallPieceF` 封住 300→750cm 的上部高度差，0→300cm 通路保持开放。`B02_Tall_LRoute_ABC` 展示 TallStraight → TallCorner → TallDeadEnd 三块连续相接，三个 Socket 均为 `Ground/Open600/Tall750`。
- `G06` 和完整组合的 `F07` 使用原生 `StairsC`，净上升冻结为 +135cm，只承担房间内部 Ground0→Deck135 错层；平台下方由 `WallK` 封为不可通行占用，暴露落差边由 FenceB/C2/D 连续防护。它不是 300cm 二楼楼梯；当前 HydroLab 素材未找到经示例验证且能原生精确闭合 +300cm 的楼梯配方。
- `C_PlayableComposition` 磁盘重载后稳定保留 235 Actor，并分为 9 个 Outliner 子组：LowEntry → RiseResolver → TallStraight → TallCorner → TallStraight → TallT；T 形东支以 DeadEnd 收束，北支进入 2x2 TallChamber，再从房间东侧进入带 StairsC 和 Deck135 的 2x2 SplitLevel。完整场景使用 7 类积木，结构件 225 个、示例验证的 `BP_HydroLab_LampA` 10 盏。
- 首次重载暴露完整组合的 10 盏灯与 Outliner 分组未稳定保存：结构件 225 个仍在。已补回灯具、重新归组，并使用 MCP 已实现但 schema 未公开的 `only_maps=true` 路径只保存当前 `/Game/Levels/Level0`。第二次切换到 Demonstration 再返回 Level0 后，V3 仍为 538、完整组合仍为 235、灯具仍为 10、9 个子组均存在；`Level0.umap` 为 1,711,552 bytes，时间 2026-07-28 22:24:02。
- 最终短时 Simulate PIE 成功进入 `Level0` Running 并正常停止。最新运行警告为无焦点 SIE/PIE 视口告警，以及 `Unable to find RecastNavMesh instance`；因此只能证明关卡可加载运行，不能证明玩家胶囊连续通行或 AI 可达。MCP 的 `open_asset_editor` 在地图切换时仍记录崩溃保护错误，但两次均实际完成地图切换，编辑器保持存活，Actor 数量和重载结果正常。
- 当前编辑器视口已定位到完整组合区域。仍待用户完成：整体审美复核、从入口连续走到房间/错层平台、楼梯边缘是否会卡落、所有外墙接缝近距离检查，以及补建 Recast 后的 AI 实际寻路验收。

### V3 邻接合同

- 基础平面地址仍为 600cm；1x1 与 2x2 是原子 Module 占格，不允许把 2x2 Chamber/SplitLevel 拆成四个独立 OpeningMask 后重排。
- 平面 Socket 必须同时匹配 `DirectionOpposite + Offset + Width600 + FloorZ0 + CeilingClass + StyleFamily`；开口中心重合并不等于可通行。
- `Low300 ↔ Tall750` 禁止直接相接，必须插入 `RiseResolver`；Resolver 只拥有高度差上部封口，禁止在 0→300cm 通行区再生成下墙或门楣。
- `Tall750 ↔ Tall750` 可按开放边相接；共享边上的墙、地、顶由唯一 `EdgeOwner` 生成，禁止双方各放一份造成共面 z-fighting。
- `SplitLevel` 的外部 Socket 仍是 `Ground0/Open600/Tall750`；`Deck135` 只存在于积木内部。只要存在 Deck135，就必须同时生成 `StairsC + LandingZone + UnderDeckBlock + GuardEdge`，任一项缺失都使该变体非法。
- DeadEnd 只能接收一个入口并封闭其余三边；Corner/T/Straight 的旋转变体必须旋转 OpeningMask、Socket 偏移和护栏/楼梯手性，不能只旋转可见网格。

## 2026-07-29 自动导航补齐检查点

- 并发复核：工作区起始为 clean；本轮仅观察到一个 `UnrealEditor.exe`，`claude/tasks/active/` 除本卡外没有新的摆放任务或近期写入。Codex 全局任务列表接口连续超时，因此结论仅为“未发现可见并发摆放”，不能写成绝对排除。
- 磁盘只读审计确认 V3 仍为 538 Actor / 22 文件夹、完整组合仍为 235 Actor / 9 文件夹，7 个最小单元与 A+B、A+B+C 分组数量均保持不变；没有修改任何 HydroLab 第三方资产或 PCG/WFC 代码。
- 原 `NavMeshBoundsVolume_1` 的 Y 覆盖仅约 `-3920..6080`，完整组合位于 `Y≈12000..15007`。已将该 Volume 改为位置 `(2400,5540,0)`、缩放 `(1,2,1)`，磁盘重载后边界为 Origin `(2400,5540,0)` / Extent `(5000,10000,300)`，同时保留原测试区覆盖。
- 保持 `RecastNavMesh-Default.RuntimeGeneration=Static`。使用 UE5.8 `ResavePackages -BuildNavigationData` 重建；仅对该命令行临时关闭异步加载等待锁，不写项目配置。重载后 Recast 边界为 Origin `(2470,5434,120)` / Extent `(5434,10374,120)`。
- 首次路径复核证明主路线前四段完整，但 StairsC 与 Deck135 虽各自有连续导航面，拼缝处为两个导航岛。没有挪动或缩放结构网格；只新增双向 `ZE_WFC3_F07_StairDeck_NavLink`，位于 `PCG_AssemblyStudy/NavigationValidation`，连接楼梯顶 `(3490,14820,≈120)` 与 Deck `(3700,14850,140)`。
- 最终磁盘重载：总 Actor 1012，V3 仍为 538，完整组合仍为 235。入口→主空间→T 分支→2x2 房间→StairsC→Deck135 五段均为 `valid=true, partial=false`；Deck135 内部路径同样 `valid=true, partial=false`。
- 编辑器切到 `L_PCG_RuntimeTest` 再重载 `Level0` 后，导航连接、V3 538 Actor 与新 Volume Transform 均可见。短时 SelectedViewport PIE 正常进入 `Level0` Running；运行阶段 Warning/Error/Fatal 为 0，停止后仅出现既有 CrowdManager 析构期 `Unable to find RecastNavMesh instance` 警告。
- 尚未替代的用户验收：整体视觉近看、玩家胶囊从入口连续走到 Deck135、楼梯边缘手感，以及场景中真实 AI Actor 执行整段移动。任务保持 Active，不标记完成。

## 2026-07-29 V4 二层楼梯房与大小占地对比检查点

- 冻结楼层语义：`G0=Z0` 与 `F2=Z450` 由可行走面决定；`Low300/Tall750` 只描述该走行面上方净空，不再把“高天花板”本身称为二楼。楼梯房是 `G0 + Tall750 + 内部 F2` 的多格超模块，接在 `Z450` 的 Low300 房间属于二层房间。
- 回到 Demonstration 复核两处原生 `StairsB`。示例没有按楼梯包围盒首尾硬拼，而是让开放踏步端部轻微插入相邻平台；据此淘汰此前按 322.5cm 包围长度排列的接法。
- `EnclosedComposition` 保留 1200x1200 大楼梯房：`E00=87`、G0 房 `E01=45`、F2 房 `E02=72`，合计 204 Actor。`CompactComparison` 新增 1200x900 紧凑楼梯房 `C00=71`，并直连 600x900 的 G0 小房 `C01=19` 与 F2 小房 `C02=36`，合计 126 Actor。
- 大版与紧凑版均把转角平台、第二跑楼梯及随平台护栏向接口方向收紧 50cm；第一跑最高踏步到平台原约 45cm 空段被消除，第二跑顶部直接进入 `Z450` 平台。开放踏步自身仍会让单点竖向射线穿过约 5--12.5cm 的踏步缝，这与素材结构一致，不再存在平台之间的大空洞。
- 两版 G0 接缝两侧均为 `Z0`，F2 接缝两侧均为 `Z450`；300cm 接口在三个横向位置、三个角色高度上保持无遮挡，接口外墙、平台西/南/北护栏与 F2 非接口边界均正确命中碰撞。楼梯/平台代表点向上 180cm 射线无遮挡；330 个 V4 Actor 的完全重复 Transform 组为 0。
- 只保存 `Content/Levels/Level0.umap`。磁盘重载后关键修正仍为 `E00 Flight2 X=15477.5 / Landing X=15177.5`、`C00 Flight2 X=18777.5 / Landing X=18477.5`，六个模块计数保持不变，临时保存哨兵 Actor 不存在。没有修改 HydroLab 第三方资产或 PCG/WFC 代码。
- 重要 WFC 边界：当前求解逻辑地址仍是 600cm，因此 1200x900 楼梯壳体与 600x900 小房目前只是几何/素材装配验证，不能直接宣称为正式原子积木。落地时优先把紧凑楼梯壳体放进 1200x1200 的 2x2 逻辑包络，并将未用 300cm 条带标成 `ReservedVoid/ConnectorApron`；若要把 900cm 真正变成可占格尺寸，则需引入 300cm 子格/占用掩码并重写既有模块约束，成本更高。
- 静态碰撞射线不能替代 Recast 与真实追猎者转身测试。当前仅能确认 300cm Socket 相对玩家 42cm 胶囊半径有充足几何余量；紧凑版是否作为默认楼梯房，仍以玩家实走、NavMesh 连续覆盖和追猎者上下两跑不掉落/不卡转角为验收门槛。

## 2026-07-29 V5 大小房网络与接口解析器检查点

- 按用户要求暂停导航与追猎者验证，并暂时封存 V4 楼梯房；本轮没有挪动楼梯房、NavMesh 或追猎者资产。新场景根目录为 `PCG_AssemblyStudy/HydroLab_RoomNetworkV5`，使用同一 `FloorZ=0` 的单层网络验证房间尺度、顶高和门洞约束。
- V5 由 10 个结构模块实例构成：Low300 入口、Tall750 直通、T 形分流、横向直通、转角、两间原子 2x2 Tall750 大房、汇合 T、目标前直通与目标小房；拓扑形成一条主路径、一条可回到汇合点的侧环路和一个收束目标支路，而不是重复直廊。
- 保存并重载后稳定为 259 Actor / 15 个叶文件夹；`Portal450` 子组恰好 7 Actor、7 个标签唯一。此前官方 MCP 在“文件夹不存在”回报后实际已部分执行，曾短暂生成双份 Portal；已只在该 AI 新建子组内按标签去重，最终没有保留双份门框。
- `Low300→Tall750` 仍必须经过共享边 `RiseResolver`：`Z=0..300` 通路开放，`Z=300..750` 由两块示例验证的 `WallPieceF` 封住；低顶与高顶不可直接相邻。重载后低侧墙、高侧墙、低顶、高顶、Resolver 上封口碰撞和下部通路全部按预期命中/畅通。
- Tall750 小房与 2x2 Tall750 大房使用 `Open600` 直接连接，但 2x2 大房只能在声明的 600cm 边段和偏移上接入，不能把 1200cm 整边视为任意开口。目标前小房到目标大房的共享边在中心及左右安全余量、`Z=100/250` 均畅通，开口外两侧阻挡，边界前后与边界正中地板表面均连续为 `Z=0`。
- 回到 `/Game/Assets/SciFiHydroLab/Levels/Demonstration` 复核后，确认 `SM_HydroLab_DoorFrame` 在示例中作为结构化门洞使用。V5 新增原比例 `Portal450` 共享边解析器：1 个 450cm DoorFrame、南北各 2 个 75x150 PillarF1、上方 2 个 300cm WallPieceF，共 7 件填满 600cm 宽、750cm 高的共享墙湾；只有 Resolver 拥有该共享边，避免双方重复墙体与 z-fighting。
- Portal 重载后多高度中心线 `Z=50/100/190/250`、横向安全余量 `Y=9450/9750` 均畅通；门洞外 `Y=9350/9850` 与上部门楣正确阻挡。门前、门槛、门后均有地板支撑，门槛表面只比相邻地板高约 3.75cm，低于当前 35cm 步高参数，但真实玩家/AI 胶囊仍留到后续实测。
- 整体 15 项路线/封闭/顶高检查、小房→大房 9 项接口检查、Portal 12 项门洞与地板检查，在保存并重新加载 Level0 后全部再次通过。墙体碰撞早先的三项“未命中”由检测线没有穿过薄墙面造成；改用双向、斜向和整室贯穿线后 WallH 均稳定命中，不需要叠加隐形碰撞或修改第三方网格。
- 引擎内部截图已留存：`claude/artifacts/room-network-v5-overview-clean.png`、`room-network-v5-entry.png`、`room-network-v5-small-to-large.png`、`room-network-v5-portal.png`。当前视口停在 Portal450，由用户可直接近看；可在入口附近使用 Play From Here 实走，未移动 Level0 原有 `PlayerStart_0`。
- 楼梯房暂不接入 V5。保留的接口合同为：G0 侧只能接 `FloorZ0/Open600/Tall750`（若来源是 Low300，先经过 RiseResolver）；F2 侧只能接 `FloorZ450/Open600/Low300` 的上层房；两端必须有至少 300cm ConnectorApron，且 2x2 大房只能在声明的 Socket 偏移接入。楼梯宏块允许比 1x1 大，但必须整体原子占格，不能让小房随意贴到楼梯外壳任意位置。
- 本轮只保存 `/Game/Levels/Level0`，没有修改 `Content/Assets/SciFiHydroLab/**`、PCG/WFC 代码、SFCorridors、植物、水处理槽或梯子。任务仍为 Active：用户审美/玩家实走以及明日导航与追猎者验收尚未完成。
## 2026-07-30 三层连续楼梯塔试搭

- Owner：Codex（本会话）。
- 用户授权：允许在 `Level0` 中实际搭建并保存三层楼梯塔原型；本轮不修改 PCG/WFC 代码与第三方 HydroLab 资产。
- 写入范围：仅 `Content/Levels/Level0.umap` 与本任务卡；新增场景根目录为 `PCG_AssemblyStudy/HydroLab_ThreeLevelStairTowerV1`，不移动、不删除既有 V4/V5 Actor。
- 冻结标高：一楼 `Z=0`、二楼 `Z=450`、三楼 `Z=900`；转向平台 `Z=225/675`；唯一屋顶 `Z=1200`，禁止在 `Z=750` 生成会截断第二组楼梯的中间天花板。
- 结构方案：复用已核对的 `StairsB + FloorB + LargeFloorB + Fence* + WallH/CeilingC` 原比例装配；两组楼梯共用二楼平台，不复制共面的楼板、护栏或外墙。
- 计划接口：一楼、二楼、三楼分别保留可识别的房间连接口与落脚平台；接口外侧补测试连接平台，避免门洞直接通向悬空边。
- 验证边界：检查楼梯/平台接缝、头部空间、外壳漏洞、碰撞、重复 Transform 与 z-fighting 风险；允许短 PIE/引擎内截图。追猎者 Recast/AI 上楼与玩家完整实走仍是后续验收，不因静态装配通过而宣称完成。
- [x] 在隔离区域完成四跑楼梯与三层平台。
- [x] 完成统一材质家族的外壳、屋顶、护栏和三层连接口。
- [x] 完成静态几何检查、仅保存 Level0，并记录可按 F 定位的 Actor 名称与引擎内截图。

### 本轮实际结果

- 用户补充要求二楼两个出口、三楼一个出口后，原型冻结为：一楼南向 600cm 入口；二楼东向 600cm 出口 + 北向 450cm 原生 DoorFrame 出口；三楼东向 600cm 出口。二楼北口补齐连续地板、门框、外侧落脚平台和两侧防坠护栏，不是只在墙上挖洞。
- 楼梯内部与各层外墙开口分开验证：四跑 `StairsB` 只负责 `Z0→225→450→675→900` 连通；每层可独立选择开口方向与宽度，外墙、门框、连接平台和护栏再按结果装配。二楼作为分流层省略会挡住第二组 `Flight1` 的 `Guard_Upper_S`；三楼终点层恢复该护栏。
- 最终根目录 `PCG_AssemblyStudy/HydroLab_ThreeLevelStairTowerV1`，共 141 Actor / 7 个叶文件夹，全部单位缩放；一楼/二楼/三楼地板标高为 `0/450/900`，唯一屋顶为 `Z=1200`，没有 `Z=750` 中间天花板。
- 静态检查：141 个 Actor 无完全重复 Transform；塔体 Actor 位置范围 `X=21600..23100, Y=5700..7503.72, Z=0..1200`，该范围内没有其他非本塔 Actor；22/22 条碰撞射线通过，覆盖三层地板、屋顶、三处开放通路、非开口墙湾、二三楼之间的封墙带与二楼北门。
- 引擎内部截图：`claude/artifacts/three-level-stair-tower-v1-exterior.png`、`three-level-stair-tower-v1-north-exit.png`、`three-level-stair-tower-v1-ground-entry.png`、`three-level-stair-tower-v1-f2-two-exits.png`。可按 F 定位：`HL_T3_G0_F2_Stair_Flight1`（一楼起步）、`HL_T3_F2_North_DoorFrame450`（二楼北出口）、`HL_T3_F2_F3_Stair_Flight2`（通往三楼）、`HL_T3_F3_ConnectorApron_E_00`（三楼东出口）。
- 官方 AssetTools 仅保存 `/Game/Levels/Level0` 成功；保存后 `Level0.umap` 为 4,007,347 bytes，时间 2026-07-30 20:27:43。`Content/Assets/SciFiHydroLab/**`、PCG/WFC 代码和既有 V4/V5 Actor 均未修改。
- 验证边界：当前证明的是原比例素材静态装配、开口与楼面碰撞成立；玩家连续走完四跑、Recast 覆盖和追猎者上楼仍未执行，任务继续保持 Active。

### 2026-07-30 三层楼梯塔接口纠错

- 用户验收指出三项确定问题：塔外连接地板被错误装成带栏杆的悬空走台；二楼北口 450cm 与东口 600cm 不一致；多段栏杆越出真实地板或落在后续房间区域。
- 当前实时复核为 142 Actor；额外的 `HL_T3_F3_Guard_N_Cap_0` 是相对原端头偏移 10cm 的重复悬空栏杆。纠错只处理 `HydroLab_ThreeLevelStairTowerV1` 内的 AI 创建 Actor，并新增与三处 600cm 开口直接相邻的验证房间；不改 V4/V5、PCG/WFC 代码或第三方资产。
- 冻结逻辑边界仍为 `X=21600..22800, Y=6000..7200`。统一接口为：G0 南口 `X=22200..22800`；F2 东口 `Y=6600..7200`、北口 `X=22200..22800`；F3 东口 `Y=6600..7200`。所有接口均为 600cm，并落在完整的 600cm 网格边段。
- 塔体地板只铺到自身边界；边界外地板归相邻验证房间。共享边无墙、无栏杆；只有可走地板外侧确实临空时才允许生成栏杆。北口删除 450cm DoorFrame 并补 150cm 内部地板及对应上下墙带。
- 本检查点的验收为：三个相邻房间与塔楼面连续、四个 600cm 接口无遮挡；栏杆不越界、不落在房间地板内部；共享边无重复墙/地板；保存后重新检查 Actor 数、完全重复 Transform、碰撞与引擎内截图。玩家、Recast 与追猎者通行仍不在本检查点内。

### 2026-07-30 三层楼梯塔通道与灯光终检

- 用户截图证明此前新增的整块平台侵入了楼梯上行包围范围，属于确定的结构错误。纠正时没有缩放素材：两组上半跑 `Flight2`、对应三块转向平台和平台栏杆统一向西移动 50cm，使第一跑、转向平台、第二跑和楼层平台按真实网格边界首尾闭合。
- 接缝终检（允许浮点误差约 `1.53e-5cm`）：第一跑高端与转向平台最大 X 同为 `22027.5`；转向平台最大 X 与第二跑最小 X 同为 `22027.5`；第二跑最大 X 与楼层平台最小 X 同为 `22350`。新增地板没有再覆盖楼梯踏步或头部通道。
- 二、三层到达平台统一改用原比例 `150x300x4cm` 的 `FloorB` 窄地板，并各补一块 `West150`；平台表面 18 个采样点全部在预期高度命中，没有缺口。平台和转向段栏杆使用 `FenceC2 + 2x FenceA` 原比例组合补齐，取消超长栏杆，不以非单位缩放掩盖尺寸问题。
- 新增三个关卡实例灯，不修改第三方蓝图：`HL_T3_Light_Under_F2`、`HL_T3_Light_Under_F3`、`HL_T3_Light_Under_Roof`，均使用 `/Game/Assets/SciFiHydroLab/Blueprints/BP_HydroLab_LampA`，最终 `Intensity=2500`、`AttenuationRadius=500`，分别照亮一层、二层和三层楼梯/平台区域。
- 最终根目录为 211 Actor / 13 个文件夹；211 个 Actor 全部单位缩放、无同类同 Transform 重复。18 条楼梯/平台上方竖向检测均无遮挡，四个 600cm 外部接口仍保持畅通。该结论是静态几何检查，不替代玩家胶囊 Sweep、Recast 或追猎者真实移动。
- 引擎内部终检截图：`claude/artifacts/three-level-stair-tower-v8-final-2500.png`。官方 AssetTools 仅保存 `/Game/Levels/Level0` 成功；保存后 `Level0.umap` 为 4,124,110 bytes，时间 2026-07-30 22:31:00。未修改 PCG/WFC 代码或 `Content/Assets/SciFiHydroLab/**`。

### 2026-07-30 三层 WFC 场景规划（待白天落盘）

- 规划采用 `8x9x3` 个逻辑地址、每格 `600x600cm`，楼层走行面为 `Z=0/450/900`。普通路线仍以 1x1 为最小求解单元；A/B 为两座一二层 2x2 原子楼梯，C/D 为两座二三层 2x2 原子楼梯，T 为一座贯通三层的 2x2 原子楼梯塔。T 是额外垂直环路，不计入每组双层楼梯的“至少两座”。
- 每层均规划至少一处直路、转角、三岔、十字、内部死胡同和一间由两个相邻高顶格组成的 1x2 高顶房；每层路线保持单一连通分量。高顶占用向上层传播：一层高顶格在二层对应地址标为不可生成，二层高顶格在三层对应地址标为不可生成，三层高顶只向屋顶以上延伸。
- 垂直保留共 12/216 个“格子-楼层”，约占 5.6%：其中 8 个用于上层 C/D 楼梯在一层的结构支撑占用，4 个用于下层高顶房穿越相邻上层。它们只处理真实几何占用，不是大面积空白房间，不能被普通房间、陷阱或资源填充。
- 结构与 Population 保持职责分离：本规划只决定房间开口、墙、地板、天花板、高度过渡、楼梯占用和临空栏杆；地刺、资源、交互物和普通房间灯光在后续独立 Population 阶段根据结构提供的可用区域生成。楼梯塔实例灯属于宏模块内部固定装配。
- 逐格规划图已输出为 `three-floor-wfc-grid.html`。图中的路线笔画就是该格真实 NSEW 开口；未开放的边由表现层装墙。共享边只允许一方拥有墙/门框/高度过渡件，防止漏洞和 z-fighting。
- 完整三层场景未在本夜间无人值守阶段写入 Level0。依据 `DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md` 第 130--139 行，夜间自动任务禁止修改 Content；下一次白天交互应按“单座双层楼梯样板复核 → A/B/C/D 旋转实例 → 三层地板与开口 → 墙/顶/高度过渡 → 邻接派生栏杆 → 灯光 → 静态复核 → PIE/Recast/玩家/追猎者验收”的顺序落盘。任务继续保持 Active。

## 2026-08-01 三层 PCG 场景 V2 精修检查点

- 冻结源场景 `PCG_AssemblyStudy/HydroLab_ThreeFloorPCGSceneV1`：1836 Actor；不移动、不删除、不在原地精修。
- [x] 将 V1 完整复制到独立根目录 `PCG_AssemblyStudy/HydroLab_ThreeFloorPCGSceneV2_Refine`，整体 X +12000cm（20 个 600cm 网格）；以 1836 Actor 的完整副本为基线精修，最终 V2 为 1857 Actor，V1 保持 1836 Actor。
- [x] 保持 600x600cm 网格、楼面 Z=0/450/900；确认路线单元 L1/L2/L3=33/27/25，A/B/C/D 双层楼梯和中央三层楼梯的多格占用未被拆分。
- [x] 复核中央楼梯四条路线侧单边开口：塔体外壳本身已封闭路线未声明的边，因此不再叠加第二层共面墙；保留三个方向、四个有效接口，并把“共享边唯一所有者”列为后续拓扑派生规则。
- [x] 保留三层高厅：L1 `(1,4)/(1,5)`、L2 `(6,4)/(6,5)`、L3 `(1,4)/(1,5)`；三层均实测约 550cm 净高，L1/L2 的向上保留占用没有挪动其他楼层路线。
- [x] 检查全部楼梯跑、转向平台和楼层平台：24 段斜栏杆改为无倾斜立柱的 `SM_HydroLab_FenceE`，受控旋转与缩放后每段沿楼梯方向覆盖 322.5cm；两处平台栏杆 21.571cm 缺口已闭合，通行口保持开放。
- [x] 统一 D 楼梯外墙正反面与共享边唯一所有者；删除 3 块南侧共面重复墙，并恢复 8 块误复制到世界原点的西外墙。栏杆使用的是用户明确允许的受控尺寸适配，不把该例外扩展到墙体。
- [x] 楼梯入口以通行为先；入口保持 600cm 通行宽度，以直线落脚区和结构收边连接，不增加会缩窄路线的功能门扇。
- [x] 完成近景审美巡检：替换三楼两块突兀银墙，修复外墙大开口、楼梯栏杆穿模和平台栏杆缺口；灯具 `Preview` 字样已消失。
- [x] 仅保存 `/Game/Levels/Level0`；重载后复核 V1=1836、V2=1857、受控缩放清单、关键墙面/接口碰撞与三组最终视角。Level0 为非脏状态，PIE 未运行。
- 验收边界：静态检查与截图不替代玩家实走、Recast 连续覆盖和追猎者上下楼；三项未执行前任务继续保持 Active。

### 2026-08-01 V2 精修实际结果

- 用户截图中的“大面积外墙空缺”定位到 V2 的 `D_Stair_L2_L3`，不是中央三层塔。`HL_3F_V2_D_Lower_Wall_W_0..3` 与 `HL_3F_V2_D_Upper_Wall_W_0..3` 共 8 块墙在复制时错误落到世界原点；现已按 V1 对应件恢复到 `X=45000`、`Y=9600/9900/10200/10500`、`Z=450/900`。重载后 8 条横向碰撞检测均在约 96.25cm 处命中，V2 不再有残留在原点附近的 Actor。
- 楼梯斜栏杆穿模的根因是 `FenceB` 自带约 117.9cm 高的竖柱：整件按楼梯坡度旋转后，竖柱也随之倾斜，包围长度从楼梯跑的 322.5cm 增至约 390cm，形成截图中的 X 形交叉。24 段斜栏杆全部换为同一 HydroLab 家族、没有竖柱的双横杆 `FenceE`；应用统一受控参数后，重载审计为 24/24 使用同一网格、24/24 沿跑向包围长度 322.5cm、异常项 0。平台与转向段继续使用独立竖直护栏承担端部防坠。
- 中央塔二层和三层各发现一处平台北侧真实缺口：`Upper_N_Tail` 到 `Upper_N_Corner` 原间距均为 21.571cm。仅延长对应尾段并平移端点，重载后两处间距均约为 `0.0000003cm`；其余开口属于楼梯或路线接口，刻意保持通行。
- D 楼梯外侧原先两块突兀银色墙板已换成与周边一致的 `WallC1`，不是尺寸约束所必需的特殊墙；D 南侧 3 块共面重复墙已移除，避免闪烁和重复碰撞。
- 地板上的 `Preview` 不是素材自带水印，而是静态/固定灯在未重建光照时的编辑器预览标记。V2 内 96 个 `BP_HydroLab_LampA` 的 RectLight 已改为 Movable；没有修改灯具蓝图或第三方素材，重载与最终截图中均不再出现该字样。
- 最终保存并重新加载 `/Game/Levels/Level0` 后：V1 仍为 1836 Actor，V2 为 1857 Actor，当前关卡非脏；从用户外墙视角、D 楼梯内视角和中央塔平台视角复查，外墙连续、斜栏杆无 X 形穿插、平台栏杆无上述缺口。

### 面向后续 WFC 的模块化审计结论

- 下列名称是本项目下一阶段建议采用的模块类别，不是 UE 官方术语：平面路线可归纳为 `Route_DeadEnd`、`Route_Straight`、`Route_Corner`、`Route_T`、`Route_Cross` 及其旋转变体；空间宏块可归纳为 2x2x2 的双层楼梯、2x2x3 的三层楼梯塔、1x2 高厅、楼梯接口落脚区，以及不可放置普通房间的保留占用。
- 楼梯、平台、净空和支撑应作为一个不可拆分的多格宏块；高厅同时声明本层房间占用与上层保留占用。不能把这些模块拆成若干独立 1x1 开口格后交给求解器随机重排。
- 墙、门框、高度过渡、栏杆和表现变体应在路线拓扑确定后派生。共享边只指定一个结构所有者；开放边生成接口，封闭边生成墙，临空且非通路的边才生成栏杆。这样可以从规则上避免本轮出现的双墙、漏墙和栏杆侵入通路。
- 当前基于同层 NSEW 开口掩码的 1x1 求解不足以表达垂直宏块、多格占用、楼层保留区、带类型与高度的接口、共享边所有者及每层楼梯数量约束。下一步讨论 WFC 实现时，应先固定垂直宏块与保留占用，再逐层求解带类型的平面路线，最后统一派生墙体、护栏与视觉变体。
- 本轮只证明模块原型的静态几何与视觉收口；玩家胶囊连续实走、Recast 三层覆盖、追猎者在两跑转角和三层塔内的真实移动尚未执行，任务保持 Active。
