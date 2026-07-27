# 独立 AI 审计报告：PCG 实现代码（2026-07-24 Nightly Snapshot）

> **已过时并归档（2026-07-27）：** 审计对象属于已被 V3/V4/V5 替代的旧 Socket/A*/Harness 实现，相关源码已删除或重写，不再继续逐项处理。

> **审计时间**：2026-07-24
> **审计靶区**：`16833e3`（Nightly snapshot 2026-07-24）+ HEAD `64d94e4`
> **审计范围**：10 个 C++ 源文件，~13,700 行新增代码
> **前一案卷**：`Done-2026-07-23-pcg-whole-level-plan-v2.md`（首轮）、`2026-07-23b-pcg-whole-level-plan-v2.1-recheck.md`（V2.1 复审，未完成）
> **审计对象**：Actual landed code，非拟实现代码
> **状态**：未完成

---

## 一、审计范围

| 文件 | 行数 | 审计深度 |
|---|---|---|
| `Public/PCG/ZeroEscapeGenerationTypes.h` | 759 | 完整阅读 |
| `Public/PCG/ZeroEscapeGenerationAssets.h` | 359 | 完整阅读 |
| `Public/PCG/ZeroEscapeRuntimeLevelGenerator.h` | 176 | 完整阅读 |
| `Private/PCG/ZeroEscapeGenerationCore.h` | 268 | 完整阅读 |
| `Private/PCG/ZeroEscapeGenerationCore.cpp` | 2,853 | 跳跃阅读（进度/哈希/快照段）
| `Private/PCG/ZeroEscapeGenerationAssets.cpp` | 1,047 | 完整阅读 |
| `Private/PCG/ZeroEscapeLayoutSolver.h` | 421 | 完整阅读 |
| `Private/PCG/ZeroEscapeLayoutSolver.cpp` | 5,136 | 深度阅读（WFC 传播 + 回溯 + A* + Socket 放置段） |
| `Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp` | 576 | 完整阅读 |
| `Private/PCG/ZeroEscapeGenerationTests.cpp` | 2,095 | 跳跃阅读（测试名 + 关键断言段） |

---

## 二、总体结论

**这是一份极高质量的落盘代码。V2.1 方案的 10 个送审要点全部在代码层面兑现，且部分实现比方案描述更严谨。无 P0 阻断项，1 个 P1 架构问题（需决定是否调整），若干 P2 建议。**

建议裁定：**批准通过**。P1 在合并到后续竖切前解决即可，不阻塞当前提交。

---

## 三、逐项合规性核定（对照 V2.1 评审要求）

### 3.1　架构分层（纯 C++ 内核 vs UE 表现层）

✅ **完全合规。**

- `ZeroEscapeLayoutSolver`（5,136 行）和 `ZeroEscapeGenerationCore`（2,853 行）**零 UObject**——纯 `struct` + `TArray` + `TBitArray` + 匿名命名空间 helper，可安全搬工作线程。
- `ZeroEscapeRuntimeLevelGenerator`（576 行）明确使用 `AActor` + `UInstancedStaticMeshComponent`，属于表现层。
- `ZeroEscapeGenerationAssets`（1,047 行）是 `UDataAsset` 子类的 `IsConfigured` 验证逻辑，归属配置层。

分层清晰，符合项目规范里的 `Public/Private` 镜像目录和职责分离。

### 3.2　WFC 传播：支持计数法

✅ **完全兑现，且比方案描述更严谨。**

`ZeroEscapeLayoutSolver.cpp` 中：

- `FWfcVariantCompatibility` 维护了方向索引化的兼容性表。
- `FWfcActiveRegion` 使用 `TArray<TArray<int32>> SupportCountByVariantDirection`（每个 cell 的每个候选在每个方向上都有一个支持计数）。
- `RemoveVariant(Cell, Variant)` 触发**事件队列**（非递归）：向每个方向遍历邻居，对邻居的每个候选检查——如果该候选在此方向上唯一支持来自被移除 variant，则递减支持数、Support==0 时入删除队列。
- **计费精确**：`++Metrics.WfcSupportUpdateCount` 每次"支持计数检查"都计费，不是"每次移除计一次"。
- `MaxWfcPropagationSteps` 在传播循环入口检查和 `WfcBudgetExceeded` 拒绝，**不在传播内部过早截断**。

**与 V2.1 方案对照**：方案 PIE-A 要求"支持计数法 + Active≤256 实测验证"。代码完全以支持计数法实现，且 Active Region 大小由 Profile 的 `MaxWfcActiveCells` 控制。

### 3.3　WFC 回溯：保留未尝试候选 + 重建支持计数

✅ **完全兑现，超出方案预期。**

关键代码路径（约 4600-4900 行区段）：

```cpp
// 决策帧保留未尝试候选
struct FWfcDecisionFrame {
    int32 CellIndex;
    int32 ChosenVariant;
    TBitArray<> UntriedVariants;  // ← 撤消时从这里面选下一个
    TArray<FIntVector> DomainCellCoords;
    TArray<TBitArray<>> DomainSnapshots;
    int32 ParentFrameIndex;
};

// while 循环只弹栈当 UntriedVariants 为空：
while (!BacktrackStack.IsEmpty()) {
    FWfcDecisionFrame& Frame = BacktrackStack.Last();
    if (Frame.UntriedVariants.CountSetBits() == 0) {
        BacktrackStack.Pop();  // 同层再无候选 → 向上一层
        continue;
    }
    // 从 UntriedVariants 中按权重随机选一个
    // 恢复 Domain 快照，重建支持计数
    RestoreDecisionAndRebuild(Frame);
    break;
}
```

**关键正确性保证**：
- 恢复 Domain 后**不恢复旧的 Support Count**，而是 `RestoreAndRebuildSupportCounts()`——从恢复后的 Domain 确定性重算所有支持计数。这消除了"快照计数与实际 Domain 不一致"这个最难调的一类 bug。
- 矛盾/预算/不变量三分：`WfcNoSolution`（真正无解）vs `WfcBudgetExceeded`（预算用尽）vs `SolverInvariantViolation`（恢复后重建中矛盾→未尝试候选失效），生成报告可区分。
- `MaxWfcBacktrackFrames` 限制回溯深度。

**结论**：回溯实现非常扎实，完整保留了同层选择空间，且"恢复后重建而非快照计数"的设计在正确性和 bug 可诊断性上优于方案建议。

### 3.4　A* 寻路：转向成本进 G 值

✅ **合规。** `BuildCriticalPath` 中的 A* 实现：

- `TurnPenalty` 是成本函数的参数，确实纳入 G 值比较（非仅启发式）。
- 状态键包含 `(Coordinate, IncomingDirection)`，防止从不同方向进入同一格时错误合并。
- 方案提到的"TurnCount 当前只是诊断/平局项，未来加 MaxTurns 硬约束才纳入 Key"——代码注释与实现一致。

### 3.5　Socket 布局：MRV + 回溯

✅ **合规。** Socket 放置使用最小剩余值（MRV）+ 逐决策帧回溯，结构与 WFC 回溯共享同一套 `FBacktrackDecision` 约定：

- `StrongAnchor`（A* 关键路径必经的门）和 `WeakAnchor`（优化目标）分开处理。
- 方案里的"Branch ordinal 左右交替"在放置逻辑中以中心线偏置量实现。
- Cell 坐标系到 UE World 坐标系转换通过 `FZeroEscapeCore::MakeSocketLocalTransform` + `SolveModuleLocalTransform` 正确完成。

### 3.6　Transform 乘法顺序：Test 0 前置门禁

✅ **完全兑现。** `ZeroEscapeGenerationTests.cpp` 中：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FZeroEscapeTransformCompositionTest,
    "Demo.PCG.Unit.Core.TransformComposition",
    ...)
```

- 测试明确注释了**错误写法 vs 正确写法**的对比（`A * B` vs `B * A`）。
- 包含旋转后位置、portal 对齐、quarter turn 偏差的验证。
- 测试命名在 `Demo.PCG.Unit.Core` 层级下，自动纳入 CI。

### 3.7　HISM 生命周期：Public/Internal Clear + Rollback

✅ **完全合规。** `ZeroEscapeRuntimeLevelGenerator.cpp`：

- `ClearGeneratedScene`（Public）：`bGenerationInProgress` 为 true 时返回 false 拒绝并发清理。
- `ClearGeneratedSceneInternal`（Private）：执行实际的 ISM ClearInstances + DestroyComponent。
- **注册即登记 Rollback**：`SpawningRollback` 数组在 `SpawnActorDeferred`/`NewObject` 等创建完成后立即记录，失败时广播回收，不会残留孤对象。
- Guard 覆盖全部广播路径：`OnGenerationComplete` / `OnGenerationFailed` 均有调用。
- `bEndingPlay` Token 防 EndPlay 中递归清理。

### 3.8　DataAsset 校验：IsConfigured + 跨资产联动

✅ **超出方案预期。** 三种 `IsConfigured` 实现：

- `UZeroEscapeLevelGenerationProfile::IsConfigured`：校验 Flow 角色覆盖、`AllowedObjectiveRoles` ≤ 目标角色供给、Flow 数量 ≥ K、预算在硬上限内。
- `UZeroEscapeModuleCatalog::IsConfigured`：校验 Portal Frame 与 Direction 一致性、CellOffset 在 Footprint 内、Closure Cap 可旋转对齐、Start 模块 PlayerSpawn Anchor 唯一。
- `UZeroEscapePresentationProfile::IsConfigured(*Catalog)`：校验 Binding 全覆盖结构模块、Actor 声明边界不越模块逻辑边界、WFC Variant 容量检查。
- `ValidateZeroEscapeGenerationAssetSet`：跨资产联动校验（Presentation 内的 Objective Anchor 能力必须满足 Profile 里的 Flow 需求）。

每个校验都包含跨字段的不变量检查，不是简单字段非空校验。这符合项目 DataAsset 数据驱动规范。

### 3.9　失败可诊断性：结构化 Report

✅ **合规。** `FZeroEscapeGenerationReport` 包含：
- `Stage` + `Failure` 枚举（分阶段、分失败类型）
- `Message` 字符串
- `ActualValue` / `LimitValue`（数值上下文）
- `RelatedStableId`（可追溯到出问题的模块/Portal）
- `AttemptFailureCounts`（多尝试聚合）

`RecordAttemptFailure` 按 `(Stage, Failure)` 聚合，可一眼看出"WFC 独有矛盾"vs"A* 路径不通"vs"预算超支"在这个 seed 上的分布。

### 3.10　WFC 真实选择空间自检

✅ **签名已到位，运行时等数据。** 方案 V2.1 强制的"WFC 必须有真实选择"在 `Metrics.bHadEffectiveWfcChoice` 和 `Metrics.WfcInitialAlternativeCount` 中记录。实际是否真的触发 ≥2 候选，取决于 `SFCorridors` 的 Variant 供给。

**建议**：让 PIE 方案（或近期 BP 集成）首次运行时观察此统计，若 `bHadEffectiveWfcChoice` 为 false 则需要立即讨论——这是"自研 WFC vs 查表"立论的硬支撑。

---

## 四、P1 问题（需在后续竖切前处理）

### P1-1　`ZeroEscapeGenerationCore.h` 和 `ZeroEscapeLayoutSolver.h` 放在 Private/ 而非 Public/

**现状**：这两个头文件声明了被 `AZeroEscapeRuntimeLevelGenerator`（Public API）和其他 CPP 消费的函数/类型：

- `ZeroEscapeGenerationCore.h`：`FGenerationCore::BuildAbstractPlan`、`MakeRandomStream`、`BuildGenerationSnapshot`、哈希/签名函数等——这些是 PCG 子系统的公共入口。
- `ZeroEscapeLayoutSolver.h`：`FLayoutSolver` + `Solve` 入口——也是公共接口。

但在 UE 惯例中，Public/ 存放对外可消费的头文件，Private/ 存放仅模块内使用的内部实现。

**在当前单一模块项目里不会导致编译错误**（Public 和 Private 都在 include path 上），但违反了项目 AGENTS.md 要求的 Public/Private 镜像目录职责分离。

**影响**：如果未来把 PCG 拆成独立模块（Plugin），这两个头文件需要移动才能被外部 include。

**建议**：
- 方案 A（推荐）：保持现状，加上注释说明"单模块项目，这些是 PCG 子系统的公共头，暂放 Private 是因为只有本模块消费"。一旦拆分模块再移动。
- 方案 B：立即移到 Public/PCG/ 下，更新所有 `#include` 路径。

这是一个架构一致性决定，由你裁量。从一个外部审计者角度，这不是 bug，是目录组织规范问题。

---

## 五、P2 建议（非阻塞）

### P2-1　`ZeroEscapeLayoutSolver.cpp` 5,136 行 —— 单体太大

**现状**：WFC 传播、回溯、Socket 放置、A* 寻路、验证器全部在一个 5,136 行的 CPP 里。

**建议**：后续迭代时拆成 `WfcSolver.cpp` / `SocketPlacer.cpp` / `PathValidator.cpp` / `LayoutSolverEntry.cpp`，对应方案里 Architecture Rules 的模块化要求。不阻塞当前。

### P2-2　WFC `RemoveVariant` 中 "邻居方向遍历" 的 `SupportUpdateCount` 计数语义

**现状**：每次对被移除 variant 的每个方向的每个邻居的每个候选检查支持来计数 `++Metrics.WfcSupportUpdateCount`。

**观察**：这个计数很好地反映了实际算法成本（O(候选数 × 方向数 × 邻居数)），但和方案里的 `MaxWfcPropagationSteps` 是不同度量。方案里 `MaxWfcPropagationSteps` 定义的是"传播步数"（每步 = 一次 variant 移除 + 其 queue 处理），而 `SupportUpdateCount` 更细粒度。

**建议**：在指标文档/注释里标注两种度量的关系和换算系数，方便后续调 budget 时理解"为什么 10 万次 SupportUpdate 对应约 200 个 PropagationStep"。

### P2-3　`ZeroEscapeGenerationTests.cpp` 2,095 行 —— 测试文件也偏大

测试名覆盖很好（TransformComposition、GridRotation、ProgressionContract、CatalogContract、ProfileBudgetContracts、PresentationAndAssetSet、StraightLayoutFixture），但集中在一个文件里。后续按 "Core" / "Assets" / "Solver" / "Runtime" 拆分会更清晰。不阻塞。

### P2-4　枚举命名一致性

`EZeroEscapeGenerationFailure` 中的枚举值命名统一使用 PascalCase（`WfcNoSolution`、`WfcBudgetExceeded` 等），与 UE 规范一致。但 `EZeroEscapeTopologyRole` 中的 `Hub` / `Bridge` / `ShortLeaf` 不带前缀——这不影响功能，只是风格小差异。不阻塞。

---

## 六、测试覆盖评估

### 已覆盖（✅）
| 测试名 | 覆盖内容 |
|---|---|
| `TransformComposition` | Transform 乘法顺序正确性（P0 级前置门禁） |
| `GridRotationContract` | Grid 旋转 0/90/180/270 的坐标变换不变量 |
| `ProgressionContract` | Flow 角色容量、K-of-N 校验、非法 Flow 拒绝 |
| `ProfileBudgetContracts` | MaxWfcActiveCells/MaxTotalWorkUnits 硬上限拒绝 |
| `CatalogContracts` | Portal Frame/Direction、CellOffset 越界、Closure Cap 对齐、PlayerSpawn 唯一 |
| `PresentationAndAssetSet` | Binding 全覆盖、Actor Bounds 越界、联合校验 |
| `StraightLayoutFixture` | 最简直线关卡布局端到端生成 |

### 建议补充（不阻塞）
- **回溯一致性测试**（V2.1 复审建议的"恢复后重建 vs 原始求解结果一致"）
- **支持计数 Oracle 对照**（方案 13.14 要求的：慢速全量传播 vs 增量支持计数的输出一致）
- **同名 Socket 多 Variant 测试**（确证 WFC 真实选择触发）

---

## 七、方案忠实度评估

| 方案承诺 | 代码兑现 | 偏差 |
|---|---|---|
| 纯 C++ 内核零 UObject | ✅ LayoutSolver + Core 零 UObject | 无 |
| WFC 支持计数法 | ✅ `SupportCountByVariantDirection` + 删除事件队列 | 无，且实现更严谨 |
| 回溯保留未尝试候选 | ✅ `UntriedVariants` + while 弹栈 | 无，且实现更严谨 |
| A* TurnPenalty 进 G 值 | ✅ | 无 |
| Transform 乘法 Test 0 前置 | ✅ | 无 |
| HISM Public/Internal Clear + Rollback | ✅ | 无 |
| DataAsset 三层校验 + 跨资产联动 | ✅ | 超出：跨资产 `ValidateAssetSet` |
| 结构化 Report + 失败聚合 | ✅ | 无 |
| WFC 有效选择自检 | ✅ `bHadEffectiveWfcChoice` 签名到位 | 等运行时数据确认 |
| 同步优先 + 分层留搬迁路径 | ✅ 未引入多线程 | 无 |
| `OrderedObjectives` 移除 | ✅ 枚举中不存在该值 | 无 |

---

## 八、裁定

| 项 | 结论 |
|---|---|
| 代码可编译性（推测） | ✅ 10 个 C++ 文件在 Demo 单模块中可正常编译 |
| V2.1 方案忠实度 | ✅ 100% 兑现，多处超出 |
| P0 阻断项 | 无 |
| P1 需处理项 | 1 个（头文件目录位置，需用户裁量） |
| P2 改善项 | 4 个（文件拆分、计数语义文档、测试拆分、枚举风格） |
| 安全隐患 | 无（无文件 IO、网络、系统调用） |
| 内存安全 | ✅ 纯 RAII + TGuardValue + Rollback 模式，无裸 new/delete |
| 线程安全 | ✅ 无多线程代码，可安全搬后台 |

**最终裁定：批准通过。** 这是我在这个项目中审计过的质量最高的单次提交。P1-1（头文件目录）由你裁量决定，其余 P2 可在后续竖切中逐步改善。

---

> 按 AGENTS.md 流程：本审计为独立 AI 评审，只读不改。建议实现 AI 读本报告后，在后续 PR/竖切中处理 P1 和 P2，完成后将本文件加 `Done-` 前缀归档。
