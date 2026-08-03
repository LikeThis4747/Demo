# 03 表现装配、运行时导航与正式开局流程：代码预览说明

## 状态与应用顺序

- 本组文件只用于代码审查，没有应用到 `Source/`、`Content/` 或 `Config/`。
- 完整应用顺序固定为：`01-data-contract-core.patch` → `02-multifloor-layout.patch` → `03a-presentation.patch` → `03b-runtime-navigation.patch` → `03c-gameflow-population.patch`。
- 已在全新隔离副本中按上述完整顺序通过应用前检查、实际应用和应用后 `git diff --check`。UE 5.8 隔离编译已成功生成 `UnrealEditor-Demo.lib/.dll`，且没有 Demo 模块编译警告；总构建只因正在运行的 Editor 锁定引擎自身 `UnrealEditor-NetCore.dll` 而返回失败。隔离 Automation 中 `Demo.PCG` 为 `24/24` 成功，`Demo.GameFlow.AsyncSetupGate` 与 `Demo.GameFlow.AutomaticRetryPolicy` 合计 `2/2` 成功。尚未进行正式 Editor、PIE、真实 `RecastNavMesh`、玩家行走或追猎者移动验收。
- 网格尺寸始终读取 DataAsset 的 `GridSize`，没有把 `20×12`、`24×16` 或其他当前值写死进运行时代码。

## 03a：表现装配

- 普通格继续生成地面、天花板、墙和立柱；楼梯间与高天花板房间则由 DataAsset 中与逻辑结构一一对应的表现配方完整装配。
- 逻辑结构和表现配方按 `DefinitionId`、结构类型与开口组双向校验；缺配方、孤立配方、结构类型不一致或开口集合不一致都会在生成前失败。
- 结构占用格、实体格和净空格的外边由结构配方统一负责。普通墙不会封住结构开口，也不会在结构封闭边上重复造墙。
- `QuarterTurnCount` 表示顺时针旋转；映射到 UE 旋转时使用负的 Yaw。自动化覆盖四种旋转、结构开口、封闭边、普通格去重、分组批次数量和非零 `FloorTopZCm`。
- 隐藏导航坡面只阻挡 Pawn、参与导航，并关闭主通道、深度通道、光线追踪、场景捕获、反射捕获、实时天空捕获、光追反射和阴影；可见楼梯不阻挡 Pawn，也不参与导航。`bVisibleInReflections` 已按项目实际 UE 5.8 源码确认存在。双层楼梯间要求两段隐藏坡面，三层楼梯间要求四段，高天花板房间不允许隐藏坡面。
- 同一 `GroupId` 的全部局部变换只调用一次 UE 5.8 `AddInstances(..., true, false, true)`；返回索引数必须与输入变换数完全一致，之后才累计实例数和 HISM 组件数。数量不一致会进入统一失败回滚，避免逐实例更新 HISM 树和动态 `RecastNavMesh`。
- 共享边键不依赖 UE 5.8 没有提供的 `FIntVector` 整体哈希，而是稳定组合 X/Y/Z 与轴；测试辅助计数使用显式循环，不依赖该版本不存在的 `TArray::CountByPredicate`。
- 正式 DataAsset 仍必须录入 Level0 已验收坡面的精确网格、尺寸与相对变换。当前参考尺寸约为 `393.23 × 205 × 8 cm`，代码没有硬编码素材路径或这组尺寸。

## 03b：Generator 与运行时导航

- `AZeroEscapeRuntimeLevelGenerator` 保持可被现有蓝图继承，不改成 `final`。同一个 Generator Actor 生命周期只接受一次正式生成请求；`ClearGeneratedScene` 只回收 HISM、灯光、Plan 和导航等待，不复位这项一次性约束。自动重试或玩家重开都加载显式配置的关卡并创建新 World，不在旧 World 内换 Seed 重生成。
- 导航门禁 Automation 只需要两个不同对象验证身份过滤，因此使用两个瞬态 `UZeroEscapeLevelGenerationProfile` 实例；UE 5.8 的 `UObject` 是抽象类，不能直接 `NewObject<UObject>()`。
- 纯数据解析器和布局求解器不持有运行时 `OperationId`。Generator 只在统一的 `FinishGeneration` 中把当前编号写入最终报告，再广播一次成功或失败，避免解析器或求解器重置报告后反复补写编号。
- 在提交正式 HISM 几何前绑定目标 `RecastNavMesh` 的生成完成事件；仅接受目标导航数据、全部几何已经提交且流程仍在等待中的事件。不使用 Tick、轮询或全局强制重建命令。
- UE 的导航完成事件本身不携带本次 PCG 的编号。因此事件归属依赖三个边界：单 World 单次正式生成、目标导航数据对象一致、全部本轮几何已提交。编号只用于拒绝旧 Timer 和重复终态回调，不能伪装成引擎事件自带的关联信息。
- 如果提交前收到事件会忽略；之后没有新事件时，由一次性 `10 秒` 容错超时明确失败。若提交后恰好收到无关完成事件，可能提前进入最终路径校验；路径不成立时只会让本局失败，不会错误进入 Ready。这个边界必须用正式 PCG 楼梯 HISM 的 PIE 流程验证。
- 导航完成后，最多选择 20 个不重复代表点并执行最多 19 次路径存在性检查：追猎者点为根，包含玩家、Exit、必需楼梯口和其他楼梯口；高天花板房间不加入代表点。
- 每个点先投射到目标 `RecastNavMesh`，再用真实追猎者导航代理参数进行同步路径检查，拒绝部分路径。记录投射数、路径数、访问节点数和耗时，但不设置固定的毫秒性能门槛；后续在目标机器的 Seed 批测中统计 P50/P95/P99 后再决定预算。
- 所有楼层高度计算均包含 `FloorTopZCm`。玩家、追猎者、Exit、Population 候选、灯光和导航代表点都从同一份 Plan 与表现高度合同计算。
- DataAsset 与 Structure Builder 原先两份逐字段相同的正 Scale Transform 校验已经合并到现有 `FGenerationCore`；没有为一个函数新增公共工具模块。

## 03c：GameMode 与 Population 原子开局

- GameMode 在发起生成前锁定移动和视角；同时兼容主菜单请求和直接 PIE `L_Game` 的 Generator 默认请求，但两条入口进入同一个异步等待流程。
- 只有 Generator 最终成功，并且玩家、追猎者、Exit 三个明确地址全部读取和放置成功后，GameMode 才显式调用 Population。Population 不再自己订阅 Generator，也不再持有 Generator 引用。
- Population 只查询普通玩法格，不再使用旧 `RegionKind`。无候选或密度整除后目标为零是合法跳过；规则配置、类加载、实际 Spawn 或总 Actor 安全预算失败会清空本轮全部 Population 并返回失败。
- Generator 的最终失败先经过纯值失败分类。布局/结构候选、WFC、整栋连通、导航构建超时和最终导航路径不连通等“换 Seed 可能改变结果”的失败，会把确定性派生的下一 Seed 与重试次数原子写入现有 GameInstance，然后重载蓝图显式配置的 `GameLevel`；最多重试 3 次。配置、实例化和导航准备错误不重试，超过上限也返回主菜单。
- 玩家、追猎者、Exit 或 Population 在 Generator 成功后的装配失败不会通过换 Seed 掩盖，GameMode 会回滚本轮玩法 Actor、清理 Generator 场景并返回主菜单。Population 对候选 Transform 不再重复做一次 O(n) 全量合法性扫描，Generator 的成功返回值已经明确保证有限、归一且单位缩放；单规则生成上限与跨规则累计上限继续同时保留并写明各自职责。
- 成功后才绑定死亡与结算流程并解锁输入。当前工作树已有的 `ResultMenuWidget` 结算界面集成被保留，落盘时仍需对 `BP_ZeroEscapeGameMode` 做字段和行为回归。
- `EndPlay`、重复回调、旧编号回调和失败后的延迟清理均有显式防护；失败提示至少进入结构化日志，避免无反馈卡住。

## 资产与蓝图迁移清单

- 正式 `DA_LevelGenerationProfile`：迁移多层结构定义、难度权重、楼梯分离、高天花板房间和共享总预算；保留可经常调整的实际 `GridSize`。
- 正式表现 DataAsset：为每个逻辑结构录入唯一配方、全部开口组、可见楼梯件和隐藏坡面件，并与 Level0 已验收装配逐项比对。
- 正式 Generator 蓝图/关卡实例：使用显式触发，不让 BeginPlay 自行抢跑；确认只存在一个 Generator 和一个 Population Actor。
- Population DataAsset/蓝图：移除旧 `TargetRegionKind` 与旧 Generator 引用；确认密度、侧向留白和类引用仍符合当前玩法。
- `BP_ZeroEscapeGameMode`：同时配置主菜单软关卡引用和正式 `GameLevel` 软关卡引用，保留并复测 `ResultMenuWidget`，确认玩家、追猎者和 Exit 类引用有效。两项关卡引用都不得依赖字符串路径或命名约定。

## 自动化与落盘后的验证顺序

1. 先做完整 C++ 编译，并处理 UE 5.8 导航 API、HISM 渲染属性或软关卡 API 的实际签名差异。
2. 运行 01、02、03a、03b、03c 新增的全部 Automation；这些纯数据测试只能证明合同和边界，不能替代真实导航验收。
3. 用官方 UE MCP 检查并迁移正式 DataAsset、Generator、Population、GameMode 蓝图和 `L_Game` 关卡实例；编译并保存所有受影响蓝图。
4. PIE 验证主菜单进入和直接运行 `L_Game` 两条入口：输入等待、单次生成、导航完成、玩法对象原子出现、可恢复失败最多三次跨 World 换 Seed、配置失败直接回主菜单、结算界面均要覆盖。
5. 对 2/3/4 层代表 Seed 检查 `RecastNavMesh` 覆盖、玩家实际上下楼、追猎者跨层追踪、Start 到 Exit 完整通行，以及隐藏坡面无可见残留。
6. 最后进行 Seed 批测，记录成功率、总耗时、导航耗时和访问节点分布；没有目标机器数据前不冻结毫秒级性能门槛。

## 审查时特别留意

- 正式计划中若仍写“Population 自己等待 Generator”，需要在冻结前改为“GameMode 在导航和三类关键对象成功后显式调用 Population”，与本组原子开局合同一致。
- 运行时导航事件无法直接证明属于某个 PCG 编号，这是已知引擎接口边界，不应在审查中误认成已经解决。接受本方案的前提是保持单 World 单次正式请求，并通过正式 PIE 验证完成事件时序。
- 本包没有一次性导航实验 Actor、临时坡面、测试关卡或全局 `RecastNavMesh` 参数改动；资产验证结束后也没有需要额外删除的实验代码。
- 本轮只采用能降低真实风险且保持现有架构的审查建议：没有拆分职责内聚的 Planner，没有增加新状态子系统、加载界面、原地重生成或另一套导航连通判断。Planner 只补分节注释；运行时仍以实际路径存在性作为最终门禁。
