# Current Task

- 当前目标：PCG 多层关卡完整拟实现代码已按首轮 Code Review 做独立取舍并形成复审版，位于 `claude/proposals/2026-08-03-PCG-MultiFloor-CodePreview/`。首轮报告已标注处理结果并归档；复审通过且用户再次明确允许前，不把补丁应用到正式功能 `Source/`、`Content/` 或 `Config/`。
- 权威记录：`DOC/DailyPlan/2026-08-03-PCG多层关卡正式实施计划.md`、`claude/tasks/active/TASK-20260802-001-PCG多层WFC新方案讨论.md`、代码预览目录与 `claude/reviews/archive/Done-2026-08-03-PCG-MultiFloor-CodePreview-审查.md`。
- 补丁顺序：`01-data-contract-core.patch` → `02-multifloor-layout.patch` → `03a-presentation.patch` → `03b-runtime-navigation.patch` → `03c-gameflow-population.patch`。
- 架构：先完整放置跨层楼梯/高天花板房间并冻结占用、净空、开口和开口外普通连接格，再逐层复用二维 WFC；最后合并同层与楼梯内部跨层连接，一次验收整栋通行图。不做三维 WFC。
- 尺寸与楼层：每局从 DataAsset 读取实际 `GridSize`；2/3/4 层按难度权重选择。玩家与追猎者在一楼，Exit 在最高层。
- 楼梯/高厅：每对相邻楼层至少一座必需双层楼梯；额外楼梯公平分配并远离已有端点。贯通三层的楼梯间最多一座且只能作为额外捷径。高天花板房间的整栋最低数、目标权重、每层上限和间距按难度配置，任意层可为零，顶层可生成。
- 旧链：删除旧 `RoomCount/RoomSizeTiles/GeneratedRoom`；GameMode 和 Population 先迁移到明确出生结果与普通玩法候选查询，再删除 `RegionKind/RegionId`。
- 导航：Level0 已验证正式隐藏坡面，不创建临时导航实验。HISM 提交后事件驱动等待目标 `RecastNavMesh`；最多 20 个代表点、19 次实际路径查询，只记录指标。10 秒仅防止导航构建永久等待，不是性能合格线。
- Generator 生命周期：同一个 Generator Actor 只接受一次正式生成请求；`ClearGeneratedScene` 只清理，不重新开放生成。重试必须加载显式软引用的游戏关卡并创建新 World。
- 自动重试：只对换 Seed 可能改变的布局/搜索/整栋连通、导航构建超时和最终导航路径失败做有限恢复；固定配置、实例化、导航准备及玩法对象装配失败不重试。现有 GameInstance 原子保存 PendingRequest 与次数，下一 Seed 固定派生，最多重试 3 次；GameMode 的 `GameLevel` 为蓝图软引用，不写死路径。
- 首轮审查取舍：采纳一次性契约注释、Transform 公共校验、Population 重复扫描删除、两级 Spawn 预算职责注释和 Planner 分节注释；拒绝拆 Planner、新状态子系统/组件、加载界面、同 World 原地重生成、删除累计 Spawn 预算及用端点投射替代实际路径。
- 隔离验证：五片补丁顺序通过应用前检查、实际应用和应用后 diff check；UE 5.8 UHT 与 Demo 模块编译/DLL 生成通过；`Demo.PCG` 24/24，`Demo.GameFlow.AsyncSetupGate + AutomaticRetryPolicy` 2/2 通过。完整 Build 只在 Demo DLL 生成后被运行中 Editor 锁定引擎 `UnrealEditor-NetCore.dll` 阻断。
- 未验证：正式资产迁移、真实运行时 `RecastNavMesh`、跨 World PIE 转场、玩家行走、追猎者跨层移动和用户验收均尚未执行。
