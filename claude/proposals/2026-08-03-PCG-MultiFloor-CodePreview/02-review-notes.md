# 02 多层布局与逐层 WFC：代码预览说明

## 状态与边界

- 本文件夹内容是审查用代码预览；没有把补丁应用到 `Source/`、`Content/` 或 `Config/`。
- 应用顺序固定为：`01-data-contract-core.patch` → `02-multifloor-layout.patch` → `03a/03b` 及后续整合补丁。
- 已执行 `01` 与 `02` 同时输入的 `git apply --check --recount --whitespace=error-all`，文本应用检查通过；尚未进行 C++ 编译、Automation、Editor、PIE、导航或玩家行走验收。
- 本补丁不实现或试验 RecastNavMesh，不增加临时导航 Actor/命令，也没有任何固定毫秒导航门槛。导航观测与最多 `20` 个点、`19` 条路径属于后续运行时补丁。

## 本补丁实际做什么

1. 用 `FMultiFloorLayoutPlanner` 替换旧单层 Room 布局入口。
2. 先整体放置跨层结构，再把结果投影为每层二维 WFC 的：
   - 固定可走格；
   - 禁用格；
   - 固定开边和固定闭边。
3. 保持 `FWfcSolver::Solve` 和二维约束算法不变；`FGridLayoutSolver` 只负责一层已经冻结的稠密约束。
4. 所有楼层与完整布局重试共享同一份结构候选、WFC 候选、回溯和搜索树预算；换楼层或重试不会刷新预算。
5. 各层按“固定/禁用/固定边约束数量”从多到少求解，平局按楼层号；每次 WFC 消耗同时累计到整栋指标和 `FloorWfcMetrics[FloorIndex]`。
6. 所有层成功后只额外加入跨楼层 `InternalConnections`，建立整栋无向图并执行一次最终 BFS；同层边直接使用 WFC 的稠密 OpeningMask，不重复加入结构内部边或外部开口。
7. Plan 在同一次原子提交中明确输出一楼玩家出生点、一楼追猎者出生点和顶层 Exit。追猎者只能从一楼普通可走格中选择，并按整栋图实际路线距离满足配置。
8. 每个已提交结构的开口外侧普通格由该结构临时保护：后续结构不能占用；结构回滚时先核对唯一所有权再释放。该格仍保持普通格语义，继续计入本层普通内容并交给二维 WFC 生成镜像必开边。

## 楼梯放置顺序与距离

- 每对相邻楼层先放一座必需双层楼梯。候选排序首先最大化本层进入点到本层离开楼梯落脚点的二维跨度；靠近边缘只是跨度相同时的第二排序条件。
- 三层地图仍必须分别存在 `1↔2`、`2↔3` 的必需双层楼梯。贯通三层的楼梯间最多一座，只是可选结构，不能写入必需双层楼梯映射来替代它们。
- 每对相邻楼层先独立按 `0/1/2` 权重抽取额外双层楼梯目标，再按两轮分配：第一轮让每个目标非零的楼层对最多尝试第一座，第二轮才尝试第二座。每轮从与“数量抽取”随机子流隔离的 Seed 起点循环访问楼层对，整栋上限只统计实际成功放置数；某一对放不下会继续尝试其他对，不会固定牺牲高楼层。候选使用“到已有玩家/Exit/楼梯落脚点的最小距离最大化”，不要求只能在东西边；达不到分离比例时安全跳过该楼层对的可选楼梯。
- 结构与开口使用顺时针四分之一圈：`q1=(Y,-X)`，与表现 Builder 的 `-90°` 约定一致。测试逐一核对 Walkable、Solid、Clearance、Landing 和 Opening 邻格的四种旋转。

## 整栋规模，而不是逐层复制范围

- DataAsset 的楼层数组合提供 `MinTotalWalkableCellCount` / `MaxTotalWalkableCellCount`，含义是整栋总可走量；`MinOrdinaryWalkableCellCountPerFloor` 才是每层普通 WFC 内容下限。
- 求解某层前，根据“已解总量 + 其他未解层的最低必要量/最大可能量”动态派生本层 WFC 的临时总量上下限。这样不会把整栋范围机械乘楼层数，也不会让某一层只剩楼梯。
- 所有层完成后，逐层结果总和与最终整栋 BFS 统计都会再次核对整栋 Min/Max。

## 高天花板房间

- 放置晚于必需和可选楼梯；数量来自整栋目标权重，可逐步降到整栋最低数量，低于最低数量才让完整布局失败。
- `MaxCountPerFloor` 与房间分离比例按 DataAsset 生效；某层可以为零。房间间距按同层两个 Walkable footprint 的最近格距离计算，不使用会随素材局部原点变化的 `BaseCoordinate`。
- 正式测试夹具使用本层 `1×2` Walkable、同层内部连接和上层对应两个 Clearance。
- 顶层允许放置，但只有 `HighCeilingRoom + bAllowClearanceAboveGeneratedTopFloor` 可以裁掉真实建筑顶层以上的 Clearance；Walkable、Solid 以及建筑内部 Clearance 都不能越界或被静默裁掉。

## 确定性与规范输出

- 定义按 `DefinitionId`、开口组按 `SetId` 进行稳定处理；候选平局只使用 Seed、算法版本、完整尝试号、用途序号、定义、坐标、旋转和开口组生成的稳定值。
- Walkable/Solid/Clearance 按 `Z/Y/X` 排序；无向 InternalConnection 先规范端点再排序；Opening/Landing 按稳定 ID 和坐标排序。
- 因此 DataAsset 中同语义数组的作者顺序不会改变 Plan 或 Hash。对应自动化会反转定义、格数组、连接端点、开口、落脚点和开口组后重放同一 Seed。
- 失败时 `OutPlan` 清空；不会提交半层或半栋数据。新增生产代码没有 `check/checkf/ensure`，非法纯值输入、预算耗尽和不变量冲突走结构化 `Stage/Failure/Message`。

## 测试迁移

- `ZeroEscapeWfcLayoutTests.cpp` 保留 16-mask、Count、MaxConsecutive、Connected、回溯、叶子拒绝和确定性重放等纯二维 WFC 测试；删除已被 V6 取代的旧 Room/Grid Plan 测试，并机械移除 Region 元数据写入。
- `ZeroEscapeGenerationContractTests.cpp` 保留独立的 Transform 组合顺序与九个随机子流隔离测试。V6 Profile/解析/Hash 由 `01` 新测试负责；多层布局由本补丁的新测试负责；旧普通结构展开及素材烟测应由 `03` 的新表现合同承接。
- 新测试覆盖：四旋转、相邻楼层必需双层楼梯、额外楼梯的两轮公平顺序与跨 Seed 起点变化、可选楼梯分离后跳过、三层楼梯间不能替代必需楼梯、整栋/每层数量、玩家/追猎者实际路线距离、结构开口投影、开口外普通格的提交保护与回滚释放、同一开口组的重复外部普通格拒绝、同 Seed 重放、同语义数组乱序、`1×2` 高天花板房间 footprint 最近距离、顶层高天花板房间、每层数量上限、分层 WFC 指标聚合和预算耗尽原子失败。

## 必要的二维 Solver 例外

`FGridCellConstraint` 中旧 `RegionId/RegionKind` 两字段被机械删除。`FWfcSolver.cpp` 从未读取它们，`FWfcSolver::Solve` 签名与算法没有修改；这是 `01` 删除旧区域枚举后保持模块可编译所必需的清理，不是扩展二维 WFC。

## 进入实现前仍需联合确认

- `03` 的 Runtime Generator 必须在 `ResolveGenerationInput` 后调用本 Planner，并把返回的明确出生点交给 GameMode/Population；不能继续从旧 Corridor/Region 或二维距离猜出生点。
- 正式 `DA_LevelGenerationProfile` 必须按 `01` 的结构定义和整栋规模字段迁移；本预览没有修改资产。
- 应用补丁后先做 C++ 编译和本文件列出的 Automation，再进入 Editor 资产装配、代表 Seed PIE、RecastNavMesh 导航验收和玩家实际行走。文本 apply-check 不能替代这些检查。
