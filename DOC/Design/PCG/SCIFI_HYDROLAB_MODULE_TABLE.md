# SciFiHydroLab PCG 结构素材尺寸与 Trim 表

> 状态：结构测量、Level0 代表组合、V3.2 Runtime Presentation 与 HydroLab 根材质 HISM Usage 均已完成；自动化和正常渲染 NewWindow PIE 已通过，当前只待玩家视觉/通行验收。

## 1. 测量口径

- 数据来源：UE Editor 中通过只读检查扫描当前素材路径 `/Game/Assets/SciFiHydroLab`。
- 扫描结果：312 个 Static Mesh，312 个成功，0 个加载失败。
- 尺寸单位：厘米；以下显示值按 0.01 cm 四舍五入。
- Pivot：来自 Static Mesh 局部 Bounds 相对局部原点的位置，不依赖 Actor 名称或手工拖入关卡。
- 示例使用次数：来自供应商 `Demonstration` 关卡的 2110 个 StaticMeshActor，只用于判断素材成熟度，不代表项目必须照搬。
- 逻辑网格：首版采用 300×300 cm；厚度、装饰外挑和 3.75 cm 级小误差不参与 WFC 邻接判断。

## 2. 首版结构核心

| 用途 | 候选素材 | 尺寸（X×Y×Z） | Pivot | Demonstration 使用 | 首版结论 |
|---|---|---:|---|---:|---|
| 单格地板 | `SM_HydroLab_LargeFloorB` | 300×300×3.75 | 角点 | 61 | 主地板；一格一个，最简单 |
| 单格地板变体 | `SM_HydroLab_LargeFloorA` | 300×300×3.75 | 角点 | 7 | 可作为低权重视觉变体 |
| 半格地板族 | `SM_HydroLab_FloorA–L` | 150×300×3.75 | 相同角点 | A 30、B 79、F 23、L 13 等 | 一格放两块；用于丰富纹理，不改变拓扑 |
| 主墙 | `SM_HydroLab_WallB1` | 10.85×300×300 | 底部端点，厚度轴在原点两侧 | 10 | 已在 Level0 闭合验证；可继续作为主墙 |
| 薄墙变体 | `SM_HydroLab_WallC1` | 3.75×300×300 | 角点 | 10 | 可选视觉变体，需单独验证与 B1 混接 |
| 薄墙变体 | `SM_HydroLab_WallG` | 3.75×300×300 | 角点 | 5 | 可选 |
| 薄墙变体 | `SM_HydroLab_WallH` | 3.75×300×300 | 角点 | 35 | 示例高频，可优先作为第二墙面 |
| 半宽墙 | `SM_HydroLab_WallB2` | 10.85×150×300 | 底部端点 | 2 | 只用于门洞/局部组合，不作为普通闭边 |
| 半宽薄墙 | `SM_HydroLab_WallI` | 3.75×150×300 | 角点 | 80 | 示例高频；适合 150 cm 子分段 |
| 单格天花板 | `SM_HydroLab_CeilingC` | 300×300×3.75 | 角点 | 82 | 首选天花板 |
| 单格天花板变体 | `SM_HydroLab_CeilingB` | 300×300×14.53 | 角点 | 20 | 可选；有明确 BTrim 配件 |
| 单格天花板变体 | `SM_HydroLab_CeilingD` | 300×300×3.75 | 角点 | 3 | 低权重视觉变体 |

## 3. 同规格地板变体

`SM_HydroLab_FloorA` 到 `SM_HydroLab_FloorL` 的 Bounds 与 Pivot 一致：

| 属性 | 值 |
|---|---|
| 尺寸 | 150×300×3.75 |
| Bounds Min | -150，0，-3.75 |
| Bounds Max | 0，300，0 |
| Pivot | X 最大边界、Y 最小边界、Z 最大边界（角点） |
| PCG 摆放 | 一个 300×300 单元放两块；只随机 Mesh，不改变逻辑占地 |

这组素材是当前最适合安全扩充场景差异的部分，因为替换变体不需要改变位置、旋转、碰撞边界或 WFC 状态。

## 4. 长度 300 cm 的 Trim 候选

### 4.1 普通边缘 Trim

| 同规格组 | 素材 | 尺寸 | 示例使用 | 建议用途 |
|---|---|---:|---:|---|
| 37.5 宽平条 | `TrimA/C/D/J/K/Q` | 37.5×300×3.75 | D 34、Q 17、C 16 等 | 地板/天花板边缘表现变体 |
| 30 宽平条 | `TrimE/I/M/O/P` | 30×300×3.75 | I 36、P 12、E 8 | 同上 |
| 22.5 宽平条 | `TrimH/T` | 22.5×300×3.75 | T 3 | 同上 |
| 15 宽平条 | `TrimL` | 15×300×3.75 | 16 | 窄边封条 |
| 特殊高度 | `TrimB` | 15×300×4.5 | 26 | 可选边条 |
| 特殊高度 | `TrimF` | 22.5×300×8.25 | 6 | 可选边条 |
| 特殊高度 | `TrimR` | 37.5×300×7.5 | 16 | 可选边条 |
| 特殊高度 | `TrimS` | 45×300×7.5 | 4 | 可选边条 |

### 4.2 地板 Trim

| 素材 | 尺寸 | Pivot | 示例使用 | 建议 |
|---|---:|---|---:|---|
| `FloorTrimB` | 90×300×10.15 | 边 | 21 | 首版地板边缘候选 |
| `FloorTrimA` | 90×300×10.15 | 边 | 0 | 与 B 同规格，可作后续变体 |
| `FloorTrimC1` | 48.75×300×46.48 | 边 | Blueprint 版本 20 | 高差/平台边，不用于普通平地 |
| `FloorTrimJ` | 26.10×300.03×26.10 | 边 | 8 | 近似 300，可作管状边缘 |
| `FloorTrimH` | 3.75×149.63×37.5 | 角点 | 8 | 150 cm 局部封边 |

### 4.3 墙面 Trim

| 素材 | 尺寸 | 示例使用 | 建议用途 |
|---|---:|---:|---|
| `WallTrimC` | 22.5×300×112.5 | 26 | 墙面宽装饰带 |
| `WallTrimD` | 26.25×300×37.5 | 0 | 墙面窄带候选 |
| `WallTrimE` | 30×300×37.5 | 10 | 墙面窄带 |
| `WallTrimF` | 22.5×300×37.5 | 0 | 墙面窄带变体 |
| `WallTrimG` | 22.5×300×37.5 | 18 | 墙面窄带变体 |
| `WallTrimI` | 37.5×300×22.5 | 17 | 墙面水平装饰 |
| `WallTrimJ1` | 28.13×300×30 | Blueprint 版本 24 | 墙面水平装饰 |
| `WallTrimB2` | 25.18×300×112.5 | 11 | Pivot 在 Y 轴 Bounds 外，暂缓自动装配 |

### 4.4 天花板 Trim

| 素材 | 尺寸 | 示例使用 | 建议 |
|---|---:|---:|---|
| `CeilingTrimA` | 67.5×300×3.75 | 3 | CeilingC/D 的边缘候选，需 Level0 配对确认 |
| `CeilingTrimB` | 75×300×3.75 | 6 | 首版天花板边缘候选 |
| `CeilingTrimC/D` | 105×300×10.15 | 0 | 后续视觉变体 |
| `CeilingBTrim1` | 67.5×300×14.53 | 4 | 名称和厚度均明确对应 CeilingB |
| `CeilingBTrim2` | 30×300×14.53 | 0 | CeilingB 的第二类边缘配件 |

## 5. 首版表现组合建议

Trim 不进入 WFC Tile 状态，也不改变逻辑连通性。首版只建立一套经过实拼验证的表现组合：

| 组合字段 | 第一候选 | 规则 |
|---|---|---|
| Floor | `LargeFloorB`，或两块 `FloorA–L` | 二选一；逻辑占地始终为 300×300 |
| Wall | `WallH` | 仅生成在逻辑闭边；当前 Runtime 基线使用该薄墙 |
| Ceiling | `CeilingC` | 每个可行走单元一块；可配置关闭 |
| Floor edge trim | 首版关闭 | `TrimB + TrimD` 已确认可组成外接平台边条，但不再假定它适合普通走廊墙脚 |
| Wall trim | `WallTrimG` 或 `WallTrimI` | 作为可选装饰，不影响碰撞和通行 |
| Ceiling edge trim | 首版关闭 | `CeilingBTrim1` 已确认是面板边缘补片，不是墙顶填缝条 |

第二套表现组合只替换 Mesh 与固定局部变换，不修改 WFC 邻接、房间拓扑或路径算法。

## 6. 暂不进入首版 PCG 的结构件

| 素材/类别 | 原因 |
|---|---|
| `CeilingA` 450×450 | 与 300 主网格不整除 |
| `DoorFrame` 45×450×300、Door 420 宽 | 不是 300 cm 门洞；需要独立门模块方案 |
| `WindowFrame` 570 宽、WindowLarge 600×900 | 组合较复杂，首版不是必需结构 |
| `LargeFloorD* / F*` | 330、405、622.5 等非基础占地，适合特殊房间而非普通单元 |
| `WallPiece* / CeilingPiece*` | 多为大型组合、坡面或局部件，先作为特殊房间候选 |
| `WallTrimB2` | Pivot 位于自身 Y Bounds 外，自动摆放前需单独验证 |
| Blueprint 组合 Trim | 可能包含多个组件或额外逻辑，首版优先直接 Static Mesh |

## 7. Level0 首版摆放规则

在 Level0 搭一段 600 cm 净宽的双格 L 形代表走廊：

1. 可行走区域横向固定为两个 300×300 单元，直段与转弯均保持 600 cm 净宽。
2. 地板采用 `LargeFloorB`，两侧边界采用 `WallH` 或 `WallB1`。
3. 天花板采用 `CeilingC`；暂不放 Ceiling Trim。
4. 首轮关闭 Floor/Ceiling Trim，避免把供应商的平台边条误当成走廊必需结构。
5. `WallTrimG` 放在直墙，转角由 `PillarC` 覆盖墙和装饰条端头；不复刻依赖 1.3 缩放的 45° 倒角。
6. 检查接缝、Z-fighting、600 cm 地板宽度、转弯视野、碰撞和第三人称净空。

### 7.1 固定局部变换

以下规则来自 Level0 实拼，不再从 Mesh 名称或近邻统计猜测：

| 部件 | 摆放规则 |
|---|---|
| 逻辑格 | `300×300 cm`；走廊占两个横向格，即 600 cm 地板宽度 |
| `LargeFloorB` | Pivot 在局部 `X Max / Y Min / Z Max`；单元左下角为 `(X,Y)` 时放在 `(X+300,Y,FloorTopZ)` |
| `WallH` | 只沿可行走格集合的暴露边生成；墙体厚度放在可行走区域外侧 |
| `CeilingC` | XY 放在单元左下角；Pivot Z 保持 `WallBaseZ + WallHeight`，当前为 `305`，不得下沉或让相邻天花板外表面重叠 |
| 墙顶防漏光 | 复用 `WallTrimG` 作为竖向墙顶压条；沿墙内侧摆放，Pivot Z=`CeilingUndersideZ + 1 - WallTrimG.BoundsMaxZ`，当前约 `278.28`，使压条顶部穿入天花板约 1 cm |
| `WallTrimG` | 相对墙向室内偏移 18.75 cm，Pivot Z 为 `WallBaseZ+10`；只用于直墙 |
| `PillarC` | Pivot 位于柱体中心；放在边界顶点，覆盖相邻墙和 WallTrim 端头 |
| Floor/水平 Ceiling Trim | 首版关闭；墙顶压条属于表现层派生件，不参与结构闭合，也不进入 WFC 状态 |

### 7.2 房间代表组合

- V3.2 的 Objective 小房间首个代表尺寸为 `2×2` 个 600 cm 逻辑 Tile，即 `1200×1200 cm`；每个逻辑 Tile 再展开为 `2×2` 个 300 cm 表现单元。
- 房间默认使用两个朝向主干的入口，构成短距离穿行回路；第三个前向入口只是可选变化，不参与基础可解性保证。
- Objective Anchor 放在靠主干的入口行，保证完成单个目标相对主干直达路线的额外代价不超过两个 600 cm 逻辑格。
- `PillarC` 只在结构边界顶点生成，用于覆盖墙与 Trim 端头；未来可另行增加不影响通路净空的房间内装饰柱。
- 房间与走廊复用同一套 Floor/Wall/Ceiling/Pillar 摆放公式，不新增“房间专用拼接算法”。
- 当前组合已经写入项目自有 Presentation DataAsset；仍需用户完成正常材质下的视觉、碰撞和 PIE 通行验收。

## 8. Demonstration 中提取的高频组合候选

以下数据来自实际关卡中的空间邻近与局部变换统计。它们能证明作者反复使用了这些搭配，但密集场景中的“邻近”不等于正式绑定，因此先作为 Level0 实拼候选，不直接写入运行时配置。

| 结构件 | 表现件 | 重复证据 | 常见局部关系（X/Y/Z/Yaw） | 当前判断 |
|---|---|---:|---|---|
| `FloorB` | `TrimB` | 直边两侧各 6 组连续实例 | 地板旋转后，`TrimB` 紧贴地板外沿；不与地板表面重叠 | 已确认是第一层外接边条 |
| `FloorB` | `TrimD` | 直边两侧各 6 组连续实例 | `TrimD` 再接在 `TrimB` 外侧；两者合计向每侧扩出约 52.5 cm | 已确认是第二层外接边条，不是 `TrimB` 的替代项 |
| `FloorB` | `TrimJ` | 10 个邻近关系 | Y=-90，Yaw=90 | 一侧边缘候选 |
| `FloorB` | `TrimL` | 9 个邻近关系 | Y=-90，Yaw=-90 | 与 TrimJ 方向相反，可能构成左右封边 |
| `WallH` | `WallTrimG` | 直墙稳定重复；另有倒角实例 | 直墙按墙面偏移；转角使用 45° 墙段与约 1.3 倍缩放 Trim | 直墙可用；缩放倒角暂不进入首版 PCG |
| `WallH` | `WallTrimB2` | 11 个邻近关系 | Y 约 -19 或接近 0，Yaw=0 | 高频宽墙带；但 Pivot 异常，需实拼确认 |
| `WallH` | `WallTrimB1` | 7 个邻近关系 | Y=-96 或 54，Yaw=0 | 75 cm 局部墙饰，不适合作为每格必放件 |
| `WallD` | `WallTrimH` | 22 个邻近关系 | 同 Pivot 或 X=60，Yaw=0 | 高频局部封边组合 |
| `WallPieceC` | `WallTrimE` | 3 个邻近关系 | 同 Pivot，Yaw 0/180 | 明确的局部组合候选 |
| `CeilingB` | `CeilingBTrim1` | 多组同 Z 边缘实例 | 与 CeilingB 同 Z，通常沿面板边缘偏移约 24.75 cm | 是面板边缘补片，不承担墙顶到天花板的填缝；首版关闭 |

首版实拼优先级：

1. `LargeFloorB + WallH + CeilingC` 作为 300 cm 网格的稳定结构基线。
2. `WallH + WallTrimG` 用于直墙，`PillarC` 用于直角处覆盖端头。
3. Floor/Ceiling Trim 暂不进入首版；`TrimB + TrimD` 只保留为未来平台边缘候选。
4. Level0 验证使用 600 cm 双格宽 L 形走廊，不再使用 300 cm 单格宽直段作为第三人称验收样例。

### 8.1 Demonstration 灯具摆放证据

本节只记录供应商关卡中的实际摆法，用于校正当前灯光预览；不把灯具加入 WFC 结构状态。

| 灯具 | 关卡证据 | 当前结论 |
|---|---|---|
| `BP_HydroLab_LampA` | Demonstration 中有 62 个实例；抽查的标准顶装实例为单位缩放，通常 `Pitch≈0°`、`Roll≈180°`，Yaw 随走廊方向变化；灯具 Pivot 与天花板表面基本同高。部分连续实例间距约 300 cm。 | 作为首版唯一顶灯候选。规范灯位放在天花板表面，使用 `Roll=180°` 使灯面朝向室内；具体密度由 Level0 实测后确定。 |
| `BP_HydroLab_LampB` | Demonstration 中有 62 个实例；既有顶装，也有墙面、地面和斜面强调照明，旋转关系不唯一。 | 保留为未来局部强调灯，不进入当前最简实现。 |

`BP_HydroLab_LampA/B` 都是“灯具 Static Mesh + RectLightComponent”的素材 Blueprint。当前 PCG 若采用 LampA，应直接生成原始 Blueprint，保留素材自己的灯光默认值；Presentation Profile 只保存可替换的 Actor 类、Pivot/旋转修正和灯距，不复制颜色、强度、半径或材质参数。

### 8.2 其他布局的未来组合候选

Demonstration 的 `Part_1` 到 `Part_5` 是供应商场景分区，不等同于已经验证的 PCG 模块。下面只把高频组合整理为未来表现层 Palette 线索；当前仍只实现已确认的最简房间与走廊。

| 示例分区 | 观察到的主要组合 | 未来可能用途 |
|---|---|---|
| `Part_1` | `FloorA/B`、`WallB/C`、`CeilingC/CeilingPieceD`、`PillarA/C`、`WaterPipeB`、通风口与窗 | 管线较多的设备走廊或服务区表现 Palette |
| `Part_2` | `WallB/H/K`、`CeilingPieceB/C/E`、`PillarG`、`TrimC`、`PipeD`、种植槽与控制台 | 紧凑种植房或设备房 Palette |
| `Part_3` | `WallD + WallTrimH`、`WallI`、`PillarC/F`、`CeilingC/CeilingPieceD/E` | 有明显框架的房间—走廊过渡 Palette |
| `Part_4` | `WindowFrame/WindowA/B`、`FloorB/F/LargeFloorB`、`WallI`、平台、楼梯与门框 | 窗区、平台或轻量高差的特殊房间候选 |
| `Part_5` | `WallH/D`、`WallTrimB/C/G/H`、`CeilingC`、`WallPieceF`、`PillarC`、大型平台与管道 | 较大房间、平台区或管线区 Palette |

未来真正加入某个组合前，仍需单独确认占地、Pivot、局部变换、碰撞与玩家净空。仅视觉不同但占地一致的组合应留在表现层；会改变通路、层高或可走区域的组合才需要扩展结构规则。

## 9. 风格区域约束（为未来扩展保留）

当前不实现完整风格系统，但 PCG 表现层应遵守以下边界：

- 同一条连续走廊段使用同一个风格组合，不能每个网格随机换墙、地板和 Trim。
- 风格切换优先发生在转角、门洞、房间入口或明确的区域边界。
- 房间未来可以根据属性选择风格，例如目标房、奖励房、危险房、设备房使用不同 Palette。
- WFC 仍只处理逻辑结构和连通；风格选择不能改变出口方向、占地或可通行宽度。
- 同风格内允许同尺寸 Mesh 做低幅度随机变化，例如 `FloorA–L` 的纹理变化或少量墙面装饰变化。
- Style/Palette 应由区域或房间持有，而不是由每个 Tile 独立随机决定。
- 同一个 Seed 下，区域风格和变体选择必须可复现。

首版只采用一个主风格，并允许少量同尺寸变化：

| 层级 | 当前变化幅度 |
|---|---|
| 区域风格 | 全图暂时 1 套 |
| 地板 | `LargeFloorB` 为主；局部可用两块 `FloorA/B/F/L` |
| 墙 | `WallH` 为主；后续只在验证相同占地/局部修正后加入同规格变体 |
| 天花板 | `CeilingC` 为主，少量 `CeilingB` |
| Trim | 每段走廊固定一套，不逐格切换；可整体开启或关闭 |

未来如果实现数据资产，可将每套风格收敛成一个 `Style Palette`：包含兼容的 Floor、Wall、Ceiling 与 Trim 组合及权重。该设想只定义扩展方向，当前不新增类或配置层。

## 10. V3.2 Runtime 绑定与验证记录

项目自有 `/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SciFiHydroLab` 当前直接绑定以下结构件；固定局部修正只消化供应商 Mesh 的 Pivot，不参与 WFC 邻接：

| 角色 | Mesh | 固定局部平移（cm） | 碰撞 |
|---|---|---:|---|
| Floor | `SM_HydroLab_LargeFloorB` | `(150,-150,0)` | `BlockAll` |
| Ceiling | `SM_HydroLab_CeilingC` | `(-150,-150,0)` | `BlockAll` |
| Wall | `SM_HydroLab_WallH` | `(0,-150,0)` | `BlockAll` |
| WallTopTrim | `SM_HydroLab_WallTrimG` | `(18.75,-150,-26.7222)` | `NoCollision` |
| Pillar | `SM_HydroLab_PillarC` | `(0,0,0)` | `BlockAll` |

截至 2026-07-25 的验证结果：

- UE 5.8 `DemoEditor Win64 Development` 完整构建成功。
- `Demo.PCG` 新测试集 13/13 通过；其中 3 个难度 × 3 个 Flow × 32 个 Seed 共 288 组均满足连通、K-of-N、路线长度和确定性约束。
- 真实 HydroLab 资产集成烟测通过；补齐 Pivot 旋转断言后的最终 13/13 报告位于 `Saved/Automation/PCG-V32-FinalAudit-20260725-0050`。
- `/Game/Levels/L_PCG_RuntimeTest` 的 NewWindow PIE 成功生成 444 个实例、5 个 HISM 组；Seed 12345、Normal、EscapeOnly 的 `ZE_PCG_RESULT` 为 Success，Harness 成功将玩家传送到生成区。
- 用户于 2026-07-25 授权后，已只在共同根材质 `/Game/SciFiHydroLab/Materials/Parents/M_HydroLab` 启用并保存 `Used with Instanced Static Meshes`；没有建立材质副本、映射或运行时绕过。
- 全新正常渲染 NewWindow PIE 再次生成 444 个实例、5 个 HISM 组，日志中不再出现 HydroLab/HISM Usage 警告；Generator 与 Harness 仍成功。
- 上述证据不替代最终玩家验收；正常材质观感、接缝、碰撞、净空和 Start→Exit 实际走通仍需用户在当前 PIE 窗口中确认。

## 11. HydroLab 楼梯宏模块的隐藏行走坡面配方

> 本节是 Demo 项目针对 HydroLab 楼梯原型形成的项目内配方，不是 UE 官方楼梯导航概念，也不是一种新的 WFC 算法。它解决的是：保留可见踏步与栏杆外观，同时给角色胶囊和 Recast 提供连续、平滑且可复现的行走面。

### 11.1 问题与选择

- 原比例 `StairsB` 的单级踏步、接缝和两跑转向平台会让 Recast 生成结果对碰撞细节敏感；仅看到局部绿色覆盖，不能证明 AI 能完整上下楼。
- 本 Demo 不制作专门的上下楼动画，因此优先控制脚底高度、起收步和平台接缝，避免明显浮空或卡顿。
- 不通过放宽全局 `AgentMaxSlope`、`AgentMaxStepHeight` 或缩小代理尺寸来掩盖单个模块问题；Level0 继续保持 `AgentRadius=35`、`AgentHeight=144`、`AgentMaxSlope=44`、`AgentMaxStepHeight=35`。
- 每一跑楼梯增加一块不可见的薄坡面。可见楼梯仍负责外观，隐藏坡面同时作为 Pawn 的平滑实体碰撞面和 Recast 的导航采样面。

### 11.2 当前冻结参数

单跑水平投影为 `322.5cm`，净升高为 `225cm`：

- 坡面长度：`sqrt(322.5² + 225²) ≈ 393.23cm`。
- 坡度：`atan(225 / 322.5) ≈ 34.90°`，低于当前 Recast 最大坡度 `44°`。
- 净宽：`205cm`，相对两侧楼梯/栏杆各收进约 `10cm`。
- 厚度：`8cm`；主体嵌入可见踏步，最终端点顶面只比楼层/平台基准高约 `0.5cm`。
- 基础件：`/Engine/BasicShapes/Cube`，当前缩放约为 `(3.932318, 2.05, 0.08)`；旋转和中心点由该跑的起终点与朝向派生。
- 每一跑单独生成一块坡面；180° 转向平台继续使用真实水平地板，禁止用一块斜面跨过两跑和转角。

坡面实例统一使用 `ZE_NavOnlyRamp` 标签，并满足：

- `bVisible=false`、`bHiddenInGame=true`；关闭反射捕获、实时天空捕获、反射、光追和投影。
- `bCanEverAffectNavigation=true`。
- 碰撞从 `InvisibleWall` 预设出发后冻结为实例级 `Custom`：`CollisionEnabled=QueryAndPhysics`、`ObjectType=WorldStatic`、`Pawn=Block`、`Visibility=Ignore`、`Camera=Ignore`。坡面是 Pawn 与 Recast 的唯一连续楼梯行走面，但不会让相机弹簧臂回缩或遮挡可见性射线。
- 坡面必须位于楼梯宏模块自己的 `NavigationTrial` 子文件夹中，便于审计和定向回退。

对应的可见 `StairsB` 实例仍保留渲染和非 Pawn 物理职责，但碰撞冻结为实例级 `Custom + QueryAndPhysics`、`ObjectType=WorldStatic`、`Pawn=Ignore`，并设置 `bCanEverAffectNavigation=false`。这样角色胶囊和 Recast 不再同时接触楼梯网格的 22 个逐级凸包与隐藏坡面，避免双地面竞争导致的脚底抖动。

### 11.3 WFC/PCG 数据边界

- 隐藏坡面是楼梯宏模块的固定内部子件，不是独立 `1x1` 路线 Tile；它随整个楼梯宏块一起平移、旋转、生成、保存和移除。
- 楼梯宏块至少要声明：多格占地、下层/上层标高、每跑起终点、转向平台落脚区、角色胶囊净空、开放接口边、临空防护边和隐藏行走面参数。
- WFC 只决定完整楼梯宏块是否能占据指定多格空间并连接兼容楼层接口；确定摆放后，再由表现/装配阶段派生可见楼梯、墙、护栏和隐藏坡面。
- 护栏只从“临空且非通路”的边派生。楼梯起步、收步和房间连接口必须保持开放；其他平台边应连续闭合。共享边只允许一个结构所有者，避免双栏、漏栏和穿模。
- 高厅、楼梯井和上层开口都必须同时写入保留占用，普通房间不得侵入这些空间；仅有同层 NSEW 开口掩码不足以表达这类垂直宏块。

### 11.4 验证门槛与 Level0 证据

楼梯模块只有依次通过以下门槛，才能标记为可用于 WFC：

1. 保存并重新加载目标关卡，回读坡面数量、Transform、隐藏渲染、碰撞和导航属性。
2. 检查 Recast 连续覆盖每一跑、转向平台和上下层落脚区；绿色网格只算中间证据。
3. 使用真实玩家胶囊和真实 AI 角色双向走完整个连接，检查起收步、转角、平台唇边、护栏碰撞和脚底高度。
4. 若出现问题，先修正该宏块的坡面高度、宽度、端点或护栏边界；不先修改全局 Recast 参数。

截至 2026-08-03，Level0 的 V2 示例共有 12 段 `ZE_NavOnlyRamp`：中央三层塔 4 段，A/B/C/D 四座双层楼梯各 2 段。保存重载后 12/12 的隐藏、碰撞与导航属性一致，临时测试 Actor 为 0。真实 `/Game/ZeroEscape/Enemies/BP_Pursuer` 以 `1.2` 倍缩放完成：中央塔 G0↔F3，以及 A/B 的 G0↔F2、C/D 的 F2↔F3 双向移动；在上述碰撞职责精修后，又复验了 A 上行与中央塔四跑双向移动。四座双层楼梯各一处约 `21.571cm` 的平台尾栏缺口也已收敛到约 `0.0000003cm` 的浮点误差。

玩家键盘连续实走和第一人称/第三人称脚底观感仍保留为用户最终验收项；在该项通过前，任务保持 Active。

### 11.5 玩家移动手感配套

本节的 `Maintain Horizontal Ground Velocity` 是 Unreal Character Movement 组件的现有布尔属性，不是本项目自造术语。其含义是：角色上坡时维持水平面速度分量；在本例约 `34.90°` 的坡面上，`450cm/s` 的水平速度会对应约 `450 / cos(34.90°) ≈ 548.7cm/s` 的沿坡速度，因此会产生突然加速感。

对当前玩家蓝图关闭该属性，让 `MaxWalkSpeed=450cm/s` 约束实际沿地面移动速度。先完成这一步与上述碰撞职责拆分，再判断是否需要相机平滑；当前相机弹簧臂的 Camera Lag 保持关闭，避免同时引入第二个手感变量。
