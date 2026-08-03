# 2026-07-29 Level0 PCG 组合导航补齐

> 状态：已被后续计划取代并归档；导航补齐和样板搭建记录已完成，剩余楼梯玩家实走由 2026-08-03 隐藏导航坡面计划继续跟踪。

## 目标

在不改动 V3 HydroLab 积木、PCG/WFC 代码或第三方资产的前提下，让 Level0 现有导航边界同时覆盖原测试区与 `HydroLab_WFCBrickStudyV3/C_PlayableComposition`，为入口到 Deck135 错层平台的玩家/AI 验收建立导航前提。

## 范围

- 修改：`D:\UE5projects\Demo\Content\Levels\Level0.umap`
- 记录：`D:\UE5projects\Demo\claude\tasks\active\TASK-20260728-002-HydroLab错层房间装配验证.md`
- 索引：`D:\UE5projects\Demo\DOC\README.md`
- 不修改：PlayerStart、BP_Pursuer、`Content/Assets/SciFiHydroLab/**`、PCG/WFC C++、DataAsset 与 SFCorridors。

## 最小方案

1. 保留现有 `NavMeshBoundsVolume_1`，将位置从 `(2400,1080,0)` 调为 `(2400,5540,0)`，将缩放从 `(1,1,1)` 调为 `(1,2,1)`。
2. 保持 `RecastNavMesh-Default.RuntimeGeneration=Static`，先验证静态示例关卡；本次不把运行时 PCG 导航模式变更混入房间装配验收。
3. 只保存 Level0，磁盘重载后复核 V3 538 Actor、完整组合 235 Actor 与导航体积边界。
4. 若静态 Recast 证明 StairsC 与 Deck135 各自有导航面、但两者在已验证连续的落脚拼缝处被分成导航孤岛，则只在该内部拼缝增加一个双向 `NavLinkProxy`；不移动/缩放结构网格，不修改第三方资产。

## 验证与回退

- 几何：导航边界应覆盖原区域以及完整组合 `X≈593..4207, Y≈12000..15007, Z≈-4..754` 中的可行走标高。
- 路径：检查入口→主空间→T 形分支→2x2 房间→SplitLevel/Deck135 五段路径；每段必须 `valid=true` 且 `partial=false`。若命令行静态查询受 Commandlet 导航初始化限制，则以正常 PIE 的 Recast/AI 实际寻路为最终证据，不把工具缺口写成通过。
- 运行：短时 PIE 不得出现新的 Error/Fatal；玩家连续实走和 AI 楼梯寻路仍需联合验收。
- 回退：恢复 `NavMeshBoundsVolume_1` 的原位置 `(2400,1080,0)` 与原缩放 `(1,1,1)`；若创建了本任务专用导航连接，则只删除该 AI 新建 Actor。
- 状态：用户已在 2026-07-29 自动任务中明确要求“不满足则继续工作”；最终视觉与实玩仍待用户验收。

## 晚间补充：大小房单层组合与楼梯房接口冻结

### 目标

- 暂停导航和追猎者楼梯验证，不再修改现有 V4 楼梯房几何。
- 只读复用 Demonstration 与 Level0 中已经验证的 HydroLab 结构配方，在独立区域搭建一套由 600cm 接口连接的小房、过渡段、2x2 大房和回路组成的单层可玩样例。
- 把楼梯房的 G0/F2 接口保留为后续约束，不为连接尚未成立的高度类别强行开洞。

### 范围

- 修改：`D:\UE5projects\Demo\Content\Levels\Level0.umap`
- 记录：`D:\UE5projects\Demo\claude\tasks\active\TASK-20260728-002-HydroLab错层房间装配验证.md`
- AI 辅助脚本/截图：`D:\UE5projects\Demo\claude\artifacts\ue58-*`
- 不修改：现有 `HL_WFC4_*` 楼梯房、导航 Actor、PCG/WFC C++、DataAsset、第三方 `Content/Assets/SciFiHydroLab/**` 和 SFCorridors。

### 组合方案

1. 以 600cm 为平面地址，复用已验证的 Low300 直通、RiseResolver、Tall750 直通/转角/T 形/死路和 2x2 Tall750 大房配方。
2. 样例拓扑为：Low300 入口 → 高度过渡 → Tall750 T 形分流；北路穿过第一间 2x2 大房，东路绕行小房链，两路在第二个 T 形重新汇合，再进入第二间 2x2 大房和目标小房。
3. 第一间大房使用少量 Demonstration 已验证的 HydroLab 通风口和控制台增强识别度；装饰不得进入接口前 300cm 转身净空，也不得重复生成边界墙/地板/天花板。
4. 楼梯房只记录接口合同：G0 仅接 `FloorZ0/Open600/Tall750`，F2 仅接 `FloorZ450/Open600/Low300`；其他顶高或标高必须先有显式过渡模块。

### 验证与回退

- 检查所有共享边只有一个边界收边所有者，完全重复 Mesh/Transform 为 0；重点近看墙缝、天花缝、穿模和 z-fighting。
- 用碰撞射线验证入口、两条分路、两间大房、汇合点和目标房连续畅通；本轮不把 Recast 或追猎者移动写成通过。
- 使用引擎内部视口捕获整体和关键接口；只保存 `/Game/Levels/Level0`，保存后回读 Actor 数量、文件夹和关键 Transform。
- 回退只移除本轮 `HL_WFC5_*` Actor 与 `PCG_AssemblyStudy/HydroLab_RoomNetworkV5` 文件夹，不触碰既有 V3/V4 或用户 Actor。

### 晚间实施结果

- 已在 `PCG_AssemblyStudy/HydroLab_RoomNetworkV5` 建成并保存单层样例：10 个结构模块实例、两间 2x2 大房、一个低高顶解析器、一条分流后重新汇合的侧环路和一个目标支路；装饰只使用少量 VentC、风扇、ConsoleA 与 WallPropF。
- 只读回到 Demonstration 复核真实门框用法后，在第二间大房和目标小房之间增加单位缩放的 `Portal450` 共享边解析器。它由 450cm DoorFrame、四个 75x150 PillarF1 和两个上部 WallPieceF 共 7 件填满 600×750cm 墙湾，没有缩放、裂缝或双份共享墙。
- 官方 MCP 曾在第一次创建 Portal 后错误回报目标文件夹不存在，重试造成 14 件；已限定在本轮 AI 新建 Portal 子文件夹内按标签去重为 7 件。最终重载后 7 个标签唯一。
- 保存并重载后 V5 稳定为 259 Actor / 15 个叶文件夹。整网 15 项、600cm 小房→大房接口 9 项、450cm Portal 12 项射线/地板支撑检查全部通过；门槛表面约高 3.75cm。
- `WallH` 早先三条失败检查是射线起点恰好没有穿过薄墙面；双向、斜向和整室贯穿复查均命中，故没有增加隐形碰撞，也没有修改 HydroLab 静态网格。
- 引擎内部截图：`claude/artifacts/room-network-v5-overview-clean.png`、`room-network-v5-entry.png`、`room-network-v5-small-to-large.png`、`room-network-v5-portal.png`。视口最终停在 Portal450。
- 本轮没有继续导航、追猎者或楼梯验证；没有移动 `PlayerStart_0`。用户可在 V5 入口使用 Play From Here 实走；视觉和玩家胶囊验收仍未完成，任务不标记完成。
