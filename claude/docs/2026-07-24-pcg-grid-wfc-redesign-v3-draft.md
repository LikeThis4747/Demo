# PCG Grid-WFC 重规划 V3.2（讨论稿）

> 状态：讨论中，尚未冻结，禁止据此直接修改 PCG 源码或项目资产。
>
> 背景：SFCorridors 的完整房间块、非统一尺寸与逻辑 Socket 适配已证明不适合当前最小实现；当前结构素材改为 SciFiHydroLab 的 300 cm 统一地板、墙、天花板与 Trim 组件。V3.2 延续 600 cm WFC 逻辑 Tile，并进一步删除当前约束下没有实际职责的 A*、WFC 回溯、随机重试、骨架降级和逻辑 Tile Catalog DataAsset。

## 1. 本次必须吸取的教训

1. 先确定最可能成功的结构表达，再设计求解器；不先为未知素材、楼梯、多层、大厅或未来插件预建复杂兼容层。
2. 逻辑结构与表现素材分离，但只分离到当前确实需要的边界；不增加映射副本、包装 Actor 或自动修正层来绕过一次可直接解决的素材设置。
3. 每个阶段先形成可独立验证的最小闭环：逻辑格正确、结构装配正确、WFC 正确、整关流程正确，不能只凭编译或单元测试宣称可玩。
4. 不保留“也许未来会用”的死代码。未来真有不规则大厅、楼梯或手工房间预制体时，再以独立扩展接入，不让首版背负 Socket 系统。

## 2. 结论：首版移除 Socket 放置系统

这里需要区分三种概念：

- UE Static Mesh Socket：当前素材和算法都不需要。
- 旧方案的逻辑 Socket/Portal 对齐：用于完整房间块、多格特殊模块与浮点 Transform 对接；当前 HydroLab 分离式组件不需要。
- WFC 的边邻接条件：仍然需要，但改名并收敛为四向 `OpenEdgeMask` / `EdgeLabel`，它只是整数格之间的开口规则，不执行 Socket 搜索、浮点对齐或特殊模块回溯。

因此首版删除 `SocketModule`、Strong/Weak Anchor、Socket MRV/DFS、Portal Transform 对齐、Cap/Closure Placement 与 Socket 专用预算。未来若确有非网格特殊模块，再新增隔离的适配阶段；当前不保留空壳或兼容分支。

## 3. 推荐生成管线

```text
Request（Seed、Difficulty、Flow）
  -> Progression Intent（Start、Exit、目标候选与 K-of-N，不预建复杂空间图）
  -> Deterministic Landmark Placement（按固定进度带与 Lane 槽放置 600 cm 房间区域）
  -> Orthogonal Route Carving（矩形网格内 X-first / Y-first 的确定性正交路径）
  -> Grid Constraints（RequiredOpen / RequiredClosed / Optional）
  -> Guaranteed Simple-Tiled WFC（最小熵观察、权重选择、开闭边传播；不回溯）
  -> Invariant Validation（连通、K-of-N、折返、边对称、孤岛）
  -> Structure Assembly（Floor / Ceiling / exposed-edge Wall / Trim / Corner Pillar）
  -> Runtime HISM Commit
```

WFC 不单独负责通关流程。Start、Exit 和所有 Objective Candidate 先由确定性地标与必要路线连入同一骨架；K-of-N 只决定玩家需要其中几个，不决定房间是否生成或是否连通。全局 BFS 仍保留，但用途是发现代码/配置不变量被破坏，而不是用来筛掉“运气不好”的 Seed。

## 4. 逻辑 Tile 与表现单元

V3.2 不把 300 cm 素材单元直接等同于 WFC Tile。原因是 600 cm 双格宽直走廊在 300 cm 邻接图中，每个格都会横向连向另一格，从单格度数看会被误判成 T 字；这种 Tile 分类与玩家看到的路口形态不一致。

首版只固定两种尺度，不实现通用多层网格：

- WFC 逻辑 Tile：600×600 cm；Straight、Corner、T、Cross 直接表达玩家看到的宽走廊形态。
- HydroLab 表现单元：300×300 cm；一个逻辑 Tile 固定展开为 2×2 个地板和天花板单元。

每个 WFC Cell 保存：

- `OpenEdgeMask`：N/E/S/W 四位；0 唯一表示 Empty，非零表示 Walkable，每个开放边代表完整 600 cm 通路。
- `RequiredOpenMask`：主路、房间内部、房间入口必须开放的边。
- `RequiredClosedMask`：地图边界、房间外围或禁用区域必须关闭的边。
- `RegionKind`：Corridor、Room、Start、Exit、Objective；只限制候选与玩法锚点，不绑定具体 Mesh。
- `RegionId`：标识连续走廊段或房间；首版全部使用默认表现，不先实现多 Palette。

房间不再是特殊 Mesh 模块。首版小房间使用 2×2 WFC Tile，即 1200×1200 cm 开放区域；内部共享边强制开放，外围关闭，仅按拓扑度数留下入口或出口。相比此前 900×900 cm 代表房间略大，但能与 600 cm 通道严格整除，并提供更可靠的绕柱与追猎空间。

## 5. WFC 首版范围

首版 Variant 只表达逻辑连接，不包含灯、门、墙面花纹或 Trim 风格：

- Empty；
- DeadEnd；
- Straight；
- Corner；
- T；
- Cross；
- RoomInterior / RoomBoundary 通过 Cell Constraint 派生，不增加独立表现状态。

逻辑 Variant 由代码直接生成 `OpeningMask 0..15`，而不是再维护一份 Tile Catalog DataAsset。WFC Domain 先按 RequiredOpen/Closed 和 Region 过滤，再做开口对开口、闭边对闭边传播。表现随机在 WFC 之后按 Region 选择，避免一条走廊逐格换风格。

Straight、Corner、T、Cross 都只是允许出现的候选：

- 不增加 `MinTJunctionCount`、`RequireCross` 等字段；
- 不在 Validator 中要求每局必须出现某种形态；
- Generation Profile 只提供六类形态的相对权重，T 与 Cross 可用较低权重自然出现；
- 生成报告记录各形态数量用于调参，但数量为零仍然是合法结果；
- 高层骨架只固定必达边，周围候选区允许 Empty 与各种连接形态，给 WFC 留出产生支路、转弯和路口的空间。

当前 Difficulty 中精确的 `ShortLeafBranchCount` / `ForwardRejoinBranchCount` 会间接强制分岔，因此 V3.2 改为可选地标/连接上限。所有必需地标从预先验证过容量的合法进度带/Lane 槽中选择；多个目标可以共享同一推进带的不同 Lane，Hard 增加目标数量时不扩大地图或显著增长关键路线。可选结构没有空槽就跳过，不触发重试；更高上限只提高复杂结构的期望，不保证某局必须出现 T 或 Cross。

最小竖切允许部分 Cell 被硬约束成唯一 Variant；不为了证明“每格都有熵”而增加无价值状态。随后只在可选连接带中增加 WFC 的真实选择，用于局部环路、短支路或邻近路线汇合。

### 5.1 为什么本版 WFC 不会因 Seed 无解

首版强制以下不变量：

1. 逻辑 Variant 完整包含 `OpeningMask 0..15`；四条边的任何开闭组合都有对应状态。
2. `RequiredOpen` 在相邻两格间双向写入，不允许指向网格外或 `Outside`。
3. `RequiredOpenMask` 与 `RequiredClosedMask` 不相交。
4. 每个 Required Cell 至少有一条 RequiredOpen；房间内部、入口和路线构造天然满足这一点。
5. Optional 允许 Empty；边界和 Outside 固定关闭。

把每条公共边看成一个二值变量：只要任一侧要求开放，该边就开放，否则可以关闭。再把每格四条边拼成 0..15 的 Mask，就得到一个显式合法解。Optional 若需要响应相邻 Required 开口，会选择相应非零 Mask，而不是强行 Empty。

因此首版不会出现正常的 `WfcNoSolution`，也不需要 Snapshot、Decision Trail、回溯、换 Seed、缩区重试或 `SkeletonFallback`。WFC 仍执行标准的“最小熵观察 + 权重选择 + 邻接传播”；只是当前完整规则集保证每次合法观察都可扩展成完整解。若 Domain 为空，应直接报告代码或配置不变量错误，不能用降级地图掩盖。

未来只有在加入不完整 Tile 集、带类型的门、不可穿越禁区、不规则多格模块、楼层/高度或其他耦合约束后，才重新评估回溯。求解器接口保持独立，届时可以只在 `FWfcSolver` 内增加 Decision Trail，不污染 Plan 与表现层。

### 5.2 为什么本版不需要 A*

旧 A* 用于绕开已占用的 Socket 房间、禁区和不规则占格，并在多条有代价路线中选较优解。本版没有这些障碍，矩形网格是凸的，路线允许共享、交叉和合并；任意两个合法 Gate 都能由 X-first 或 Y-first 的 Manhattan 路径连接，且不会因 Seed 失败。

一次 L 形雕刻自然产生 Corner；支路从已有路线接出形成 T；正交路线交叉形成 Cross；前向支路重新并入主路形成两个 T。这些形态依旧是可能结果，不设配额。未来出现真实禁区、手工房间、不同风险代价或多层结构时：统一代价先考虑 BFS，不同代价再使用 A*。

## 6. HydroLab 表现装配

首版只采用已经实拼确认的基线。每个非 Empty WFC Tile 通过确定性公式展开：

- 2×2 个 300 cm 地板：`LargeFloorB`；
- 2×2 个 300 cm 天花板：`CeilingC`；
- 每条 600 cm 闭边展开为两段 300 cm `WallH` 或最终确认的主墙，厚度朝可走区域外；
- 每段墙顶复用已确认位置的 `WallTrimG` 防漏光；
- 300 cm 边界顶点按需放置 `PillarC`，使用整数顶点 Key 去重；
- Floor/Ceiling 水平 Trim、DoorFrame、450 cm 门、非 300 cm 大型件首版关闭。

结构装配按“300 cm 面单元 / 边段 / 顶点”去重，禁止同一共享边生成两面墙或两个共面部件，从数据层消除 Z-fighting。

## 7. 现有代码去留审计

| 部分 | 处理 | 原因 |
|---|---|---|
| Request、Seed、Signature、Difficulty、Flow、K-of-N | 保留并简化 | 属于未来流程可变性的稳定边界 |
| Flow、K-of-N、Progression Intent | 保留并收敛 | 保留流程可变性，只输出需要放置的地标，不继续维护复杂空间图 |
| 抽象图的精确分支结构 | 删除首版依赖 | T/Cross/支路不再由固定节点数量硬造；必要流程由地标顺序表达 |
| 整数 A* | 删除首版依赖 | 当前无障碍、无代价差异；确定性正交雕刻已保证连接 |
| WFC 最小熵观察、权重选择、邻接传播 | 保留并简化 | 这是当前真正使用的 WFC；Variant 固定完整覆盖 0..15 |
| WFC Snapshot、回溯、重试、骨架降级 | 删除 | 当前 CSP 构造性保证有解；保留只会掩盖不变量 Bug |
| SocketModule / Strong-Weak Anchor / MRV Socket DFS | 删除 | 当前统一组件没有对应需求 |
| Portal LocalTransform / StableSocketId / WidthClass / HeightLayer | 删除 | 单层单宽首版用四向整数边即可 |
| Cap / Closure Module / ClosedPortals | 删除 | 闭边直接生成墙，不再额外放封口模块 |
| PortalConnections | 改为 Grid 邻接或按需派生 | 连接由相邻格和 OpenEdgeMask 唯一确定 |
| 每 Module 一张 Mesh 的 Presentation Binding | 替换 | 当前一个逻辑格由地板、墙、天花板、Trim、柱子共同组成 |
| Runtime HISM 事务提交与 Harness | 保留并适配 | 运行时、回滚和 PIE 证据仍有价值 |
| Socket/Portal/Closure 自动化测试 | 删除并替换 | 不再对应产品结构 |

## 8. 紧凑文件方案

不为每个类创建子文件夹，继续使用现有 `PCG` 目录：

- `Source/Demo/Public/PCG/ZeroEscapeGenerationTypes.h`：公开 Request、Grid Cell、Plan、Report。
- `Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h`：Generation Profile、六类形态权重、HydroLab 表现配置；不再保留逻辑 Tile Catalog。
- `Source/Demo/Private/PCG/ZeroEscapeGenerationCore.h/.cpp`：Flow、难度、K-of-N 与轻量 Progression Intent。
- `Source/Demo/Private/PCG/ZeroEscapeGridLayoutSolver.h/.cpp`：进度带/Lane 地标放置、确定性正交路径、全局不变量验证。
- `Source/Demo/Private/PCG/ZeroEscapeWfcSolver.h/.cpp`：纯值 WFC，只保留 Domain、熵、权重观察和传播。
- `Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp`：新数据契约校验。
- `Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp`：结构实例清单转 HISM；当前规模内不另建 Assembler 文件。
- `Source/Demo/Private/PCG/ZeroEscapeGenerationTests.cpp`：首版仍保留一个测试文件；只有再次明显膨胀时再拆。

旧 `ZeroEscapeLayoutSolver.h/.cpp` 在新 Grid/WFC 两个求解器接管后删除，不保留转发包装。

## 9. 实施顺序与门禁

### 检查点 A：结构装配夹具

先用固定 Grid Fixture 生成与 Level0 同尺度的 600 cm L 形走廊 + 2×2 WFC Tile（1200 cm）房间。验证每个逻辑 Tile 正确展开为 2×2 表现单元、墙只在闭边、转角柱不重复、无共面实例、天花板与墙顶 Trim 位置正确。未通过时不进入随机生成。

### 检查点 B：最小整关 Grid

生成 Start -> 一条 600 cm 宽路线 -> Exit，固定 Seed 可复现；运行时 HISM 成功，玩家能走通。此时不加 Objective、支路、风格变体，也不要求必须出现转弯、T 或 Cross。

### 检查点 C：地标与约束 WFC

接回可选地标、前向汇合、房间与 WFC。Straight、Corner、T、Cross 由约束与权重自然产生，不设最低数量；批量 Seed 只观察其分布，同时任何结果都满足起终点连通、600 cm 主走廊宽度和折返上限。

自动化必须验证多组随机观察都不发生回溯或重试；若 Domain 变空，测试直接失败并定位不变量，不输出备用骨架。

### 检查点 D：Flow 扩展

恢复 EscapeOnly / Collect All / K-of-N；Objective Anchor 从房间/Region 直接派生。困难只调整分支、目标与危险预算，不放宽折返上限或显著增长关键路线。

### 检查点 E：表现丰富

在基础结构稳定后才增加同规格地板、墙或天花板变体；风格以 Region 为单位选择。门、窗、特殊房间、多层或非 300 cm 模块单独评审，不能反向污染首版 WFC。

## 10. 回退与资产处理

- 已有 Git 提交 `10611c6` 是改造前 5.7.4 快照。
- 不建立旧 Socket 数据到新 Grid 数据的兼容映射；测试 DataAsset 直接重建更清晰。
- 首轮重构不删除 SFCorridors 或其他素材包。新 HydroLab 版本通过 PIE 后，再单独列出旧 Presentation DataAsset、无引用资产和第三方包，由用户决定是否删除。
- UE 5.8 升级完成并确认项目可编译前，不开始本次源码落盘。

## 11. 待确认的核心决策

建议确认以下一句即可进入拟实现代码阶段：

> 首版采用 600 cm 单层 WFC Tile（固定展开为 2×2 个 300 cm HydroLab 单元）+ 固定进度带/Lane 地标 + 确定性正交路径 + 保证可满足的无回溯 WFC + 分离式结构装配；Straight/Corner/T/Cross 仅为可选候选，不设出现配额；删除现有 Socket/Portal/Cap、A*、WFC 回溯/重试/降级与逻辑 Tile Catalog，不为尚未出现的结构约束保留兼容层。
