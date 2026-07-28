# TASK-20260728-001 — SFCorridors 物件筛选与退场

- Owner：Codex / 当前会话
- Status：active
- Stage：讨论与只读筛选；未获资产删除授权
- Created：2026-07-28
- Updated：2026-07-28

## 目标与边界

- 目标：从已导入的 295 个 `SFCorridors` LFS 资产中只选择少量能增强走廊识别度的装饰物，继续使用 HydroLab 作为 PCG 主结构与 Presentation。
- 非目标：不替换 HydroLab 结构集，不改变 600 cm 逻辑格，不把 SFCorridors 房间/走廊壳体接回 WFC，不在本阶段删除、移动、重命名或修改第三方资产。
- 删除边界：任何删除都必须先得到用户对“保留根资产 + 完整依赖闭包 + 删除清单”的明确授权；删除前再做 UE Asset Registry 引用审计和 Git/LFS 规模复核。

## 首批候选（只读结论）

1. `/Game/Assets/SFCorridors/Meshes/SM_LampWallL`
   - 用途：墙面识别灯/危险区或出口方向视觉标记。
   - 尺寸约 `117 × 7 × 45 cm`，中心 Pivot；适合贴墙装饰，不改变通道碰撞或拓扑。
   - 直接材质：`/Game/Assets/SFCorridors/Materials/Mi_lampwall`。
2. `/Game/Assets/SFCorridors/Meshes/SM_comp`
   - 用途：墙面终端/控制台地标，可用于出口前、分岔口或目标房间。
   - 尺寸约 `388 × 35 × 300 cm`，底部/墙面友好 Pivot；需要在 HydroLab 600 cm 模块中检查净空。
   - 直接材质：`/Game/Assets/SFCorridors/Materials/MI_comp1`。

两者共享 `M_Master` 和 `T_Metal_Detail_N` 依赖；各自另有 Albedo/Emission/Metallic/Normal 纹理。当前只确认了二进制直接引用，`M_Master` 的 Material Function/纹理闭包仍需正式 Asset Registry 审计，不能据此删除其余资产。

## 暂不进入产品链

- `SM_Corridor_Segment`、`SM_Room_*`、`SM_Corridor_Divider`、`SM_largeHall`、`SM_hub` 等结构资产：尺寸/Pivot 依赖约 660 cm 的旧 SFC 结构语义，与当前 HydroLab 600 cm 分离结构不一致。
- `BP_FlickeringLight`：当前 Blueprint 含 Tick/EventGraph 逻辑，不符合“少量静态装饰、默认无 Tick”的最小用途。
- `Maps/Overview*`、`Maps/SF_Corridors*`：仅为第三方展示地图，不是 Demo 运行关卡。
- 其余 Mesh/Material/Texture：先视为待删除候选，不立即删除。

## 下一检查点

- [ ] 用户确认首批候选是“壁灯 + 墙面终端”，或更换其中任意一个。
- [ ] 在隔离预览中检查两件物体的外观、Pivot、朝向、碰撞、净空、材质和软件 Lumen 表现；不保存第三方资产。
- [ ] 用 Asset Registry 生成完整保留依赖闭包，并单列 Maps、Blueprints、Meshes、Materials、Textures 的拟删除清单与 LFS 体积。
- [ ] 在对话中展示精确删除清单和风险，等待用户明确授权后再删除。
- [ ] 删除获批后验证引用、打开项目、构建/PIE、Git LFS 状态；第三方删除不与玩法代码修改混在同一检查点。
