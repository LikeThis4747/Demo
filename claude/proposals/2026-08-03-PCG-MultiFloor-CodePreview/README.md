# PCG 多层关卡完整拟实现代码（Code Review 包）

> 状态：只供代码审查，尚未应用到正式 `Source/Demo`、`Content` 或 `Config`。
>
> 基线：2026-08-03 当前工作树。`ZeroEscapeGameMode.h/.cpp` 中用户正在进行的 ResultMenu 改动属于基线，拟代码在其上合并，不得覆盖。

## 补丁顺序

必须按以下顺序审查和应用到独立副本：

1. `01-data-contract-core.patch`：多层纯数据、DataAsset 配置、交叉校验、确定性随机域、规范 Hash 与安全预算。
2. `02-multifloor-layout.patch`：完整结构放置、必需/额外楼梯、可调高天花板房间、逐层二维 WFC、整栋通行图与纯数据测试。
3. `03a-presentation.patch`：HydroLab 结构表现配方、共享边所有权、楼梯可见构件和正式隐藏坡面。
4. `03b-runtime-navigation.patch`：运行时 Generator 生命周期、目标 `RecastNavMesh` 等待、有界端点投射与路径存在性检查。
5. `03c-gameflow-population.patch`：正式 GameMode 异步开局、玩家/追猎者/Exit 摆放、Population 原子调用、失败清理、有限跨 World 自动重试及纯值门禁测试。

对应说明为 `01-review-notes.md`、`02-review-notes.md`、`03-review-notes.md`。整体审查边界、资产迁移和落盘后验证见 `INTEGRATION_REVIEW.md`；可直接交给审查 AI 的任务见 `CODE_REVIEW_PROMPT.md`。

## 已冻结的玩法和技术规则

- 每局实际 `GridSize` 来自 DataAsset，不固定为某个默认长宽。
- 每档难度各自配置生成 2、3、4 层的权重；玩家和追猎者在一楼，Exit 在本局最高层。
- 每对相邻楼层至少有一座双层楼梯。贯通三层的楼梯间最多一座，只能作为额外捷径，不能替代必需楼梯。
- 额外双层楼梯的数量权重与整栋上限可按难度配置。必需楼梯优先最大化每层进入点和离开点的空间跨度，靠近边缘只是次级偏好；额外楼梯逐座尽量远离已有出生点、Exit 和全部楼梯口。
- 高天花板房间的整栋最低数量、目标数量权重、每层上限和间距均可按难度配置；允许任意一层为零，也允许出现在顶层。只有真实存在上层时才占用上层净空。
- 先放完整楼梯/房间，再把其占用投影为各层二维 WFC 的固定格、禁用格和固定开口边；不改为三维 WFC。最后合并普通格与结构内部连接，只做一次整栋通行图验收。
- 删除旧 `RoomCount/RoomSizeTiles/GeneratedRoom` 整链；GameMode 和 Population 先迁移到明确出生结果与普通玩法候选查询，再删除旧 `RegionKind/RegionId`。
- 所有楼层和最多四次整栋尝试共享按楼层数线性扩展的 WFC 计数预算，重试不刷新；相同 Seed、难度和版本必须得到相同 Plan 与规范 Hash。
- Level0 已验证正式隐藏坡面配方，不再加入一次性导航实验代码。正式 HISM 提交后等待目标 `RecastNavMesh`，再检查最多 20 个代表点、19 次实际路径存在性；只记录耗时，不设置无实测依据的固定毫秒门槛。10 秒只用于防止异步等待永久锁死。
- UE 导航完成事件不携带 PCG 操作编号。操作编号只过滤旧 Timer 与最终报告；成功仍须通过目标导航数据、单局持久状态和最终路径验收。
- GameMode 在请求前绑定并锁输入；最终成功后才放置玩家、追猎者和 Exit，显式调用 Population、绑定死亡/胜负并开放输入。Population 没有候选或按密度得到零目标是合法跳过；配置、查询、加载或实际 Spawn 失败才原子回滚。
- 同一个 Generator Actor 生命周期只接受一次正式生成请求，`ClearGeneratedScene` 只负责回收场景，不会重新开放生成；重试必须销毁旧 World 后进入新 World，避免旧导航事件和 Timer 接管新一局。
- 只对换 Seed 可能改变结果的布局失败、搜索预算耗尽、导航构建超时和最终路径不连通做有限自动重试。重试次数与下一 Seed 由现有 GameInstance 跨 World 保存，下一 Seed 按固定规则派生，最多重试 3 次；配置、装配和导航准备错误直接返回主菜单。正式游戏关卡使用 GameMode 蓝图显式配置的 `TSoftObjectPtr<UWorld> GameLevel`，不写死关卡路径或名称。

## 本轮审查意见的独立取舍

- 采纳：补清 Generator 一次性生命周期；合并完全重复的 Transform 校验函数；删除 Population 对同一批 Transform 的第二次全量扫描；为单规则预算与整局累计预算补清不同职责；给约 3700 行但职责内聚的 Planner 增加分节注释。
- 修正后采纳：有限自动重试有实际玩家体验价值，但不能按审查稿所写“只改 GameMode”。次数和 PendingRequest 必须放在现有 GameInstance 才能跨 World；失败分类同时看 `Stage + Failure`，只让换 Seed 可能改变的失败重试。`CapacityInsufficient` 在当前实现中会受随机楼层和结构占用影响，因此列入有限重试，而固定配置容量错误仍会更早以 `InvalidConfiguration` 失败。
- 不采纳：不拆分 Planner，不新增状态子系统、组件、加载界面或原地重生成状态机；不删除整局累计 Spawn 预算；不把实际路径检查降级成只判断端点能否投射到导航面。上述改动不能解决当前阻断问题，反而会扩大实现和验证面。

## 当前验证边界

- 五个补丁已经在全新独立副本中按既定顺序通过应用前检查、实际应用和应用后 `git diff --check`。
- UE 5.8 隔离验证已完成 UHT 和 Demo 模块编译，成功生成 `UnrealEditor-Demo.lib/.dll`，没有 Demo 模块编译警告。完整 `Build.bat` 最终只因当前正在运行的 Editor 锁定引擎自身 `UnrealEditor-NetCore.dll` 而返回失败；该锁发生在 Demo DLL 已生成之后。
- 隔离 Automation 已通过 `Demo.PCG` `24/24`；`Demo.GameFlow.AsyncSetupGate` 与 `Demo.GameFlow.AutomaticRetryPolicy` 合计 `2/2` 通过。这些结果证明补丁能编译且纯数据/状态合同通过，不能替代正式资产、真实 `RecastNavMesh`、PIE、玩家行走或追猎者移动验收。
- 本目录仍不是已落盘功能，也不代表 DataAsset、蓝图或关卡已迁移；正式 `Source/`、`Content/`、`Config/` 未应用本方案。
- Code Review 通过且用户再次明确允许后，才会按补丁顺序修改正式源码、迁移资产、构建并进入 UE/PIE 验证。
