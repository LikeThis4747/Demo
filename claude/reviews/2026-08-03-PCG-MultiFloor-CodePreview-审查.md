# 2026-08-03 PCG 多层关卡代码预览（Code Preview）审查报告

-审查对象：`claude/proposals/2026-08-03-PCG-MultiFloor-CodePreview/` 下5 个补丁（01、02、03a、03b、03c）
- 审查性质：这是**代码预览补丁（*.patch）**，尚未落盘到 `Source/`；本报告只做静态阅读审查，未编译、未 PIE。
- 审查视角：独立审查，不完全采信补丁作者自带的 `CODE_REVIEW_PROMPT.md` 结论，重点找冗余/绕过/测试假成功，并质疑方案本身。
- 结论速览：**整体质量高、职责清晰、确定性处理扎实，几乎没有死代码/调试残留/绕过逻辑。** 主要问题不在"写了脏代码"，而在**方案层面的两个取舍**和**几处轻微冗余**，见下。

---

## 一、代码行数预计（基于 `git apply --numstat`，为补丁净增删，非最终文件规模）

> 说明：下列是补丁相对当前 `Source/` 的 diff 行数。"删除"里有很大一部分是把旧的单层 Room 求解器、旧结构展开、旧 GameMode 流程整体重写，不是纯删功能。

### 总计

| 类别 | 新增(+) | 删除(-) | 净变化 |
|---|---:|---:|---:|
| 生产代码 | 约 **6 500** | 约 **1 900** | **+4 600** |
| 测试代码 | 约 **2 350** | 约 **1 370** | **+980** |
| 合计 | 约 **8 850** | 约 **3 270** | **+5 580** |

> 生产代码净增约 4600 行里，**单文件 `ZeroEscapeMultiFloorLayoutPlanner.cpp` 就占 3728 行纯新增**，是绝对大头（多层楼梯/高厅放置 + 整栋连通性 + 出生点 + 主重试循环）。

### 分模块（生产代码，按补丁）

| 补丁 | 主要生产文件 | +新增 | -删除 | 备注 |
|---|---|---:|---:|---|
| 01 数据契约+Core | GenerationTypes.h / Assets.h/.cpp / Core.h/.cpp | 1 499 | 276 | 数据合同扩为多层+结构；Hash 重写 |
| 02 多层布局+WFC | **MultiFloorLayoutPlanner.cpp(+3728)** / .h / GridLayoutSolver.cpp/.h / WfcSolver.h | 4 154 | 923 | Planner 全新；GridLayoutSolver 从 Room 求解器重构为 `SolveConstrainedFloor`（-844/+325） |
| 03a 表现装配 | StructureBuilder.cpp/.h / GenerationAssets.cpp/.h | 1 101 | 0 | 结构展开从 GridLayoutSolver 搬家至此并扩展多层/HISM |
| 03b 运行时导航 | RuntimeLevelGenerator.cpp/.h / RuntimeNavigationGate.h | 899 | 395 | 新增跨帧导航等待状态机 |
| 03c 开局+Population | GameMode.cpp/.h / GameplayPopulator.cpp/.h / PlacementPolicy.h / GameSetupGate.h / PlacementTypes.h | 493 | 282 | GameMode 改为异步原子开局+失败回主菜单 |

### 分模块（测试代码）

| 文件 | +新增 | -删除 | 说明 |
|---|---:|---:|---|
| MultiFloorDataContractTests.cpp | 719 | 0 | 新|
| MultiFloorLayoutTests.cpp | 893 | 0 | 新 |
| StructurePresentationTests.cpp | 349 | 0 | 新 |
| RuntimeNavigationTests.cpp | 113 | 0 | 新（纯值 Gate） |
| GameSetupTests.cpp | 43 | 0 | 新（纯值 Gate） |
| PopulationPlacementTests.cpp | 59 | 0 | 新（纯值预算） |
| GenerationContractTests.cpp | 69 | 453 | 旧单层契约测试大幅缩减 |
| WfcLayoutTests.cpp | 3 | 467 | 旧单层 WFC 测试基本移除（求解器接口变了） |

---

## 二、冗余 / 绕过 / 测试假成功 排查结论

**这是本次审查的重点，结论是：基本没有。** 具体：

1. **无调试残留 / 绕过标记**：全量搜索 `TODO / FIXME / HACK / 占位 / placeholder / DrawDebug / stub / 写死 / 暂时 / 绕过` —— 0 处出现在代码里（仅出现在注释里做"不生成临时坡面/临时回退照明"这类正面说明）。没有 `DrawDebug*`、没有临时 `UE_LOG`刷屏、没有硬编码 `return true` 跳过校验。

2. **无测试假成功**：6 个测试文件全部是**纯值逻辑门 / 纯整数预算 / 数据契约**测试，断言都有实际语义（例如"旧 OperationId 不得接管当前操作""EndPlay 后回调必须被忽略""0候选是合法零放置"）。没有 `TestTrue(TEXT("x"), true)` 这种空跑。Gate 类（`FRuntimeNavigationGate`/`FGameSetupGate`/`FPopulationPlacementPolicy`）被刻意抽成**无 UObject、无 World 的纯函数**，就是为了可单测——这是加分项，不是绕过。

3. **删除的旧代码是真重构，不是删功能**：
   - `BuildCanonicalStructureInstances` + `EStructurePieceKind` 从 `GridLayoutSolver` 删除→ 搬到 03a `StructureBuilder` 并扩展成多层 HISM。功能没丢，是职责归位。
   - `FGridLayoutSolver::Solve`(整关) → `SolveConstrainedFloor`(单层受约束)：从"自己摆房间"退化为"在Planner 给的固定格/禁用格上跑 WFC"，房间概念(`RegionId/RegionKind`)整体移除。这与"多层由 Planner 统筹、单层只做二维 WFC"的新架构一致。

4. **发现的少量真·冗余（都很轻，非阻断）**：
   - **`IsFinitePositiveScaleTransform` 重复实现**：`StructureBuilder.cpp` 匿名命名空间 与 `GenerationAssets.cpp` 匿名命名空间 各写了一份**逐字段完全一样**的实现。建议提到 `FGenerationCore` 或一个公共 util 里共用一份。
   - **Populator 里"候选 Transform 全量 NaN/Scale 校验"**（03c 约 890行）与它调用的 `GetGeneratedOrdinaryGameplayCellWorldTransforms` 内部的 `ConvertLocalToWorld`(已做 `IsFiniteUnitScaleTransform`) 存在**防御性重复**。不算错（跨模块防御合理），但属于"同一批Transform 校验两遍"。
   - **总Spawn 预算上限校验重复**：`FPopulationPlacementPolicy::Evaluate` 内部已用 `MaxGridCells×MaxFloorCount` 卡了 `ActorCount`，Populator 调用后又用同一常数再卡一次 `SpawnedActors.Num()+PlannedActorCount`。第二次是跨规则累计，语义上略有不同（累计 vs 单条），保留可接受，但注释未说明这层差异，容易被误读为纯冗余。

> 这三处都属于"多写了一点防御"，不是"绕过"。对一个要展示编码严谨度的 Demo，防御性重复甚至是正向的；只是从"精简"角度可以合并。

---

## 三、对方案本身的质疑（不被作者提示词带走）

### 质疑 1（重要）：`bHasAcceptedGenerationRequest` 永久锁 —— 一个 Generator 实例一辈子只能生成一次

`RuntimeLevelGenerator` 里 `bHasAcceptedGenerationRequest` 一旦置 true永不复位，`CanAcceptGenerationRequest()` 从此恒 false。头注释明说"正式 L_Game 一次 Actor 生命周期只接受一局；重开通过返回主菜单加载新 World"。

- **合理之处**：单机逃杀 Demo，一局一世界，配合 03c GameMode 失败即 `OpenLevel` 回主菜单，模型自洽，也天然杜绝"生成中再次触发"的重入地狱。
- **质疑之处**：
  1. 这把"换种子重生成/同一关卡重开"从**运行时能力**降级为**必须重载整个 World**。对三周Demo 够用，但如果后续想做"死了原地重开同一 seed"或编辑器里反复调参预览，就得改这个设计。**建议在方案文档里显式写明这是一个有意的、不可运行时重生成的约束**，避免下游（比如你自己两周后）以为能热重生成。
  2. `ClearGeneratedScene()` 在 `bHasAcceptedGenerationRequest=true` 后仍可清场景，但清完也无法再 `Generate()` —— 会出现"能清、不能再生"的半状态。建议要么 Clear 时一并复位允许重生成，要么在 `ClearGeneratedScene` 文档里点明"清了也不能再生成"。

> **用户决策（2026-08-03）：保留一次性约束，不做运行时原地重生成。**
> - "暂停菜单选种子重开"走**重载关卡**实现：暂停菜单把新种子写入 `GameInstance` 的 PendingRequest → `OpenLevel(L_Game)` → 新World 里新建的 Generator 实例读新种子生成。因为 `bHasAcceptedGenerationRequest` 是 Generator **实例成员**，重载关卡会销毁旧实例、新建实例，锁天生为 false，所以**无需扩展 Generator**，这条链路与"从主菜单进关""失败回主菜单"是同一套`GetPendingRequest` + `OpenLevelBySoftObjectPtr` 机制。
> - 之所以不做"不重载关卡的原地重生成"：一次性约束正是 03b 导航等待状态机能写得简单、可纯值单测的前提（单 World 单次生成）。为省一次关卡加载去放开锁 + 复位所有导航等待 bool/Timer/事件，容易在"旧导航事件、旧 Timer 未清干净"上出微妙 bug，对单机 Demo 不划算。
> - **落盘建议**：不改代码逻辑，但把 `ClearGeneratedScene` 与一次性锁的文档注释补清楚——明确"清场景后本实例不可再生成，重开一律走重载关卡"，消除"能清不能再生"的语义歧义。

### 质疑 2（中等）：整个开局链是"一票否决 + 回主菜单"，没有任何降级

03c 里Generator/Populator 不唯一、MainMenuLevel 没配、任一Class 没配、导航验收失败、Population 任一规则失败……**全部 `AbortSetupAndReturnToMainMenu`**。

- **合理之处**：fail-closed，绝不产出"半成品可玩局"，对追求确定性/可复现的 PCG 是对的。
- **质疑之处**：**导航验收**（`ValidateNavigationEndpoints`：投射 + 最多 19 次 `TestPathSync`）失败会直接判本局作废回主菜单。而导航构建受 `NavigationBuildTimeoutSeconds`（默认 10s）和运行时 NavMesh 重建时机影响，**在低端机/大网格上有一定概率超时或投射失败**。这意味着"某些 seed + 某些机器"下玩家会看到"生成后被踢回主菜单"。
  - 建议：至少把"导航超时/验收失败"与"配置错误"在日志和用户体验上区分开；前者可考虑**换一次 seed 重试一轮**（当前方案明确"不重试、不换 Seed"，这是有意的，但值得你确认这是不是你想要的玩家体验）。这是一个**产品取舍问题，不是代码 bug**。

> **用户决策（2026-08-03）：导航失败不再直接踢回主菜单，改为换种子重载关卡重试。方向已定，具体代码由负责实现的 AI 编写，本文档只记录方案。**
> 落盘方案要点（**只动`ZeroEscapeGameMode`，不碰 Generator 与 03b 导航状态机**）：
> 1. **失败分两类**：
>    - **可恢复类**（`NavigationBuildTimeout` / `NavigationValidationFailed` / 布局求解失败）→ **换种子 + 重载 `L_Game` 重试**。
>    - **配置类**（NavData 非 Dynamic Recast、追猎者导航代理无效、Generator/Populator 不唯一、Class/主菜单未配）→ 换种子无用，仍回主菜单报错。
> 2. **重试上限**：把"已重试次数"存在 `GameInstance`（需跨 World 保留），建议上限 3 次，超限才真正回主菜单，避免必然失败的配置无限重载黑屏。
> 3. **换种子确定性派生**：新种子从旧种子按固定规则推进（非纯随机），保证日志可复现"第 N 次用的哪个种子"，便于排障。
> 4. **实现机制与质疑 1 同源**：都是"把新种子写入 `GameInstance` → `OpenLevel(L_Game)`"，只是触发点从"玩家点重开"变成"导航失败自动重试"。
> 5. **代价与取舍**：换种子=重载关卡，失败时会有一次短暂关卡加载；因 PCG 关卡进关本就要生成，成本相当，可接受。**明确不做"原地无加载重生成"**（理由见质疑 1 决策）。

### 质疑 3（轻微）：`MultiFloorLayoutPlanner.cpp` 单文件 3728 行

职责其实内聚（就是"多层布局求解"这一件事），但单文件近 4k 行，后期定位/改动成本高。**不必现在拆**（过早拆分反而破坏可读性），但建议在文件头把内部 `namespace MultiFloorLayoutPrivate` 的几大块（保留格规划 / 楼梯放置回溯 / 高厅放置 / 整栋 BFS 连通 / 出生点 / 主重试循环）用分节注释标出来，便于 PPT 讲解和后续维护。

### 对确定性的正面确认（作者做得好，值得保留）

- Hash 对 `FName` 按 **UTF8 字符串内容**写入（不用进程内 Name 索引）、对 `double` 按 **IEEE 754 bit** 写入、所有 DataAsset 数组在**解析期统一排序**、Hash 时**校验坐标严格递增**（防重复/乱序）。
- `MakeFloorWfcSalt` 只由 `(WholeLayoutAttemptIndex, FloorIndex, LocalAttempt)` 三个确定量混合，**不含帧号/时间**，跨帧、跨机可复现。
- 共享 WFC 预算（solve/candidate/backtrack）跨层用 `InOutBudget` 传递并向上取整分片，避免某层吃光预算——设计严谨。

这几点是整套代码里含金量最高的部分，`RuntimeNavigationGate` / `GameSetupGate` 抽成纯值可测更是很成熟的做法。

---

## 四、需要在落盘前重点验证的点（给写代码的 AI / 你自己）

1. **编译**：01 大量类型改名（`Rules`→`SharedRules`、`GetGeneratedStartWorldTransform`→`...PlayerSpawn...`、`GetGeneratedCellWorldTransforms`→`...OrdinaryGameplayCell...`）。凡引用旧名的**蓝图节点/其他 C++** 都会断，落盘后必须全量编译 + 刷新蓝图引用。
2. **导航等待的真实行为**：`RuntimeNavigationGate` 的纯值逻辑已单测，但"HISM AddInstance → 动态 RecastNavMesh 触发 `OnNavigationGenerationFinished`"这条真实链路**只能靠 PIE 验证**，单测覆盖不到。务必在真实 L_Game 里跑通一次并看`ZE_PCG_RESULT` 日志 `nav_paths/nav_nodes`。
3. **`bHasPursuerNavigationAgent` 依赖 `PursuerClass` CDO 的 NavAgent**：若 BP_Pursuer 的 CharacterMovement AgentRadius/Height 没配好，会在开局 `PursuerNavAgentInvalid` 直接回主菜单。落盘后先确认 BP_Pursuer 导航代理参数有效。
4. **两个 `IsFinitePositiveScaleTransform` 重复**：落盘时可顺手合并成一份公共实现。
5. **落实两项用户决策（见质疑 1、2）**：① 补清 `ClearGeneratedScene` 与一次性锁的文档注释；② `ZeroEscapeGameMode` 增加"可恢复失败换种子重载重试（上限 3次、种子确定性派生）、配置类失败仍回主菜单"的逻辑，并写详细注释。
6. **补分节注释**：`MultiFloorLayoutPlanner.cpp` 按内部大块（保留格规划 / 楼梯回溯放置 / 高厅放置 / 整栋BFS 连通 / 出生点选择 / 主重试循环）加分节注释，便于讲解与维护。

---

## 五、总评

| 维度 | 评价 |
|---|---|
| 冗余/绕过/调试残留 | 几乎没有；仅 3 处轻微防御性重复 |
| 测试真实性 | 全部有语义，无假成功；纯值 Gate 可测性好 |
| 确定性 | 优秀（FName/double/排序/salt 全部到位） |
| 职责划分 | 清晰（Planner 求解 / Builder 表现 / Gate 纯值 / GameMode 编排） |
| 主要风险 | 已收敛为落盘待办：①一次性生成约束保留（重开走重载关卡，见质疑 1 决策）；②导航验收失败改为换种子重载重试（见质疑 2 决策） |

**建议**：代码质量可以落盘（先编译 + PIE 验证导航链）。质疑 1、2 两个产品级取舍已由用户拍板（**均走"重载关卡换种子"，不做运行时原地重生成**），实现时按第四节待办 5、6 落实并写详细注释即可。

---

*本报告为静态阅读审查，未编译、未运行。若采纳，请在处理完后按 AGENTS.md 规则把本报告加 `Done-` 前缀并移入 `claude/reviews/archive/`。*
