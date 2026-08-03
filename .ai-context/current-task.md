# Current Task

- 当前目标：PCG 多层关卡正式 Plan 已批准，整套拟实现代码预览已完成，等待 Code Review。补丁位于 `claude/proposals/2026-08-03-PCG-MultiFloor-CodePreview/`；审查通过且用户再次明确允许前，不修改正式功能 `Source/`、`Content/` 或 `Config/`。
- 权威记录：`DOC/DailyPlan/2026-08-03-PCG多层关卡正式实施计划.md`、`claude/tasks/active/TASK-20260802-001-PCG多层WFC新方案讨论.md` 与代码预览目录。
- 补丁顺序：`01-data-contract-core.patch` → `02-multifloor-layout.patch` → `03a-presentation.patch` → `03b-runtime-navigation.patch` → `03c-gameflow-population.patch`。
- 架构：先完整放置跨层楼梯/高天花板房间并冻结占用、净空、开口和开口外普通连接格，再把各层投影给现有二维 WFC；最后合并同层与楼梯内部跨层连接，一次验收整栋通行图。不做三维 WFC。
- 尺寸与楼层：每局从 DataAsset 读取实际 `GridSize`；首版 2/3/4 层按难度权重选择。玩家与追猎者在一楼，Exit 在最高层。
- 楼梯：每对相邻楼层至少一座必需双层楼梯；必需端点先扩大每层路线跨度，靠边只是次级偏好。额外楼梯按楼层对独立抽取，再用 Seed 决定轮转起点、按轮次公平消耗整栋上限，逐座远离全部已有端点。贯通三层的楼梯间最多一座，只是额外捷径，不能替代必需楼梯。
- 高天花板房间：整栋最低数、目标权重、每层上限和间距按难度配置；任意层可为零，顶层可生成。间距比较同层实际 Walkable 占地轮廓；只有真实上层存在时才占用上层净空。
- 旧链：删除旧 `RoomCount/RoomSizeTiles/GeneratedRoom`；GameMode 和 Population 先迁移到明确出生结果与普通玩法候选查询，再删除 `RegionKind/RegionId`。
- 导航：Level0 已验证正式隐藏坡面，不创建临时导航实验。正式 HISM 提交后事件驱动等待目标 `RecastNavMesh`，不使用 Tick 或全局 Build。导航查询没有固定 10ms 或其他毫秒门槛；最多检查 20 个代表点、19 次实际路径，只记录查询数、访问节点与耗时。可配置的 10 秒仅是防止异步等待永久锁死的导航构建超时，不是性能目标。
- 事件边界：`OnNavigationGenerationFinished` 不携带 PCG 操作编号；操作编号只过滤旧 Timer/重复终态。成功仍依赖单 Generator 单局、目标导航数据、全部几何已提交和最终路径存在性检查。
- GameMode/Population：请求前绑定并锁输入；Generator 最终成功后才放置玩家、追猎者、Exit，显式调用 Population、绑定死亡/胜负并解锁。Population 没候选或整数密度目标为零是合法跳过；配置、查询、加载或实际 Spawn 失败才原子回滚。
- 隔离验证：五片补丁在全新副本中顺序通过 apply check、实际应用和 diff check；UE 5.8 UHT 与 Demo 模块编译通过，生成 `UnrealEditor-Demo.lib/.dll`，无 Demo 编译警告；`Demo.PCG` 24/24、`Demo.GameFlow.AsyncSetupGate` 1/1 通过。完整 Build 只因正在运行的 Editor 锁定引擎 `UnrealEditor-NetCore.dll` 返回失败，发生在 Demo DLL 已生成后。
- 未验证：正式资产迁移、真实运行时 `RecastNavMesh`、PIE、玩家行走、追猎者跨层移动和用户验收均尚未执行。
