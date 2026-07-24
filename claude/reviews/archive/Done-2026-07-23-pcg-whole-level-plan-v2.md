> 处理状态：**Done（2026-07-23）**。本报告的 3 项阻塞与高回报建议已吸收到 `DOC/DailyPlan/2026-07-23-PCG整关场景方案冻结.md` V2.1，并经过 WFC 与 Runtime 两轮只读静态复核；这只表示方案/拟实现代码完成修订，不代表已获用户源码实施授权，也不代表 UHT、Build、Automation 或 PIE 已通过。

# 审计：PCG 整关场景方案 V2（送审拟实现代码）

- 审计日期：2026-07-23
- 代码基线：git `0847ed7`（2026-07-23 01:03:56 +0800，Nightly snapshot 2026-07-23）；被审对象是 `DOC/DailyPlan/2026-07-23-PCG整关场景方案冻结.md` 第十三节的 V2 拟实现代码，**尚未落盘到 `Source/`**，无 UHT/Build/PIE 记录。
- 审计范围：第十三节 V2 全部拟稿——公共数据类型、Profile/Catalog/Presentation DataAsset 校验、抽象流程+确定性+K-of-N、Socket/A*/有限 WFC 求解器、Runtime Actor 实例化、测试与 PIE 门槛。第十二节 V1 为历史归档，未评审。
- 关键前置约束（用户 2026-07-23 明确）：① 开局难度固定，本局不动态改变、不动态重生成；② 不做流式/无限大世界，一局一次性生成后即固定；③ 必须靠 Seed 稳定复现某一关。以下评估均以此为准。

## 结论

架构方向正确，确定性设计是全案最强项，边界克制、合规。**核心决策"WFC 不决定宏观流程、只做局部填充"是对的**，明显优于"整关单块 WFC"的教学式做法；"特殊模块 Socket 占位 → A* 保证必需连接 → WFC 局部填充"的混合顺序符合成熟工业实践。

裁定：**有条件批准落盘。** 建议先落风险最低的竖切 A（抽象层+确定性+DataAsset），竖切 B（WFC/Socket）落盘前先处理下方 3 个阻塞项。当前仍是骨架级拟稿，多个 helper（`BuildCriticalPath`、`BuildWfcVariantsAndCompatibility`、`Compatibility.GetSupportedNeighbors`、`ExportPlacedModulesAndSemanticBindings`、`ValidateGlobalLayout` 等）未展开，评审不视其为可编译源码——与方案自述一致，无隐瞒。

下方按"阻塞交付 / 高回报 / 可延后"分级，仅供实施 AI 参考，非必办项。

## 关于后台线程（专项评估，回应用户关注的性能问题）

**结论：当前不需要后台线程，方案的"同步优先 + 分层留搬迁路径 + 实测数据决定"是正确决策，认可，不需改。**

方案第 13.6/13.14 节的处理是：首版同步生成并分阶段测量耗时（Abstract/Socket/A*/WFC/Validation/Instantiation），纯数据求解与实例化已分层解耦；只有打包实测 P95 证明卡顿时，才把"仅含值类型快照"的求解移到工作线程，UObject/Mesh/World/Spawn/HISM/NavMesh 始终留游戏线程；并明令"墙钟时间只做观测和紧急取消，不参与随机分支"。

在用户三个约束下，这比"直接上后台线程"更合适：

1. **一次性生成、不流式**：卡顿只出现在开局一次（通常有加载/转场遮挡），不是每帧持续压力，同步的可接受度远高于流式场景；无需为不存在的持续帧压力提前引入并发。
2. **Seed 稳定复现是硬需求**：多线程最大的隐患正是把线程调度/墙钟时间引入随机或分支顺序，破坏跨机复现。同步执行 + 命名随机子流是复现性最安全的组合。方案已禁止墙钟参与随机，方向正确。
3. **难度开局固定、不重生成**：没有"运行中反复异步生成新块"的诉求，后台线程的主要收益场景（流式预生成）在本设计中不存在。
4. **符合"够用就行"与项目 engine-first、按需开销原则**：过早异步 = 纯增复杂度和不确定性 bug 风险。

给实施 AI 的具体要求（把"是否上线程"变成可判定门槛，而非模糊承诺）：

- 竖切 B/D 必须产出**单机同步生成耗时实测**：固定回归 Seed + 100~300 批量 Seed 的 P50/P95（分阶段）。
- 设一个明确阈值（建议：开局同步生成 P95 单次 > 一个可感知卡顿预算，例如结合是否有转场遮挡判断）。**未超阈值就保持同步，不许"为像论文而异步"。**
- 若将来确需搬线程：只搬纯值类型快照求解，且必须新增"同一 Seed 同步版与异步版产出 Canonical Hash 完全一致"的测试，作为异步落地的前置门禁。
- 记录这一决策依据，避免后续 AI 无数据就擅自并发化。

## 阻塞交付（竖切 B 落盘前处理）

1. **WFC 传播的复杂度与支持集重算**。`PropagateDomains` 每处理一个脏 cell、每个方向都从头重算邻居的 `Supported` 全集（遍历 source 全候选 × 各自兼容邻居）。这不是标准高效 WFC 的做法（应维护"每候选的支持计数"，计数归零才移除）。在 `MaxWfcActiveCells=4096` + `MaxWfcPropagationSteps=200000` 下，最坏步成本不均、可能逼近或超 `MaxTotalWorkUnits`。
   - 要求：要么改支持计数法；要么首个竖切**强制把 Active Region 压到很小**并用实测证明预算充裕。不能只靠常量拍脑袋。这条同时也是"是否需要后台线程"判断的输入——先把单线程 WFC 本身做高效，很可能就不需要线程了。

2. **完整 Domain 快照回溯的内存/拷贝成本未量化**。`FWfcDecision::DomainsBeforeDecision` 每次决策快照全部 cell 的 `TBitArray`。cell 多 + 回溯深时，瞬时分配可能到很大量级。方案自评点 13.15-5 也提到了。
   - 要求：首版把实际 Active Region 上限写小（不要用到 4096），并把"换增量 Trail"的触发条件量化（如快照总内存或单次生成耗时阈值）。否则容易在 PIE 才暴露卡顿，反过来又误导"需要上线程"的判断。

3. **`OrderedObjectives` 半暴露风险**。该枚举进了 `BlueprintType` 和 DataAsset 可选值，但 `ValidateProgression` 直接返回 `UnsupportedCompletionRule`。策划在 DataAsset 里能选到一个必然失败的选项，属于"占位冒充功能"红线边缘。
   - 要求：首版从枚举移除最干净；或至少在 `IsConfigured` 阶段拒绝并给可读报错，且在编辑器可见处标注未实现。

## 高回报（实施中务必落实）

4. **Transform 乘法顺序：先过单测再写业务**。`TryAlignAndPlaceModule` 与实例化处多处 `A * B`，UE `FTransform` 乘法语义极易搞反。方案已要求单测（13.14-8：位置相等、Forward Dot≈-1、Up Dot≈1、Scale=1）。务必在写 Socket 对齐逻辑**之前**让该单测通过，避免靠手调 Pivot 掩盖顺序错误（项目已有"组件/蒙太奇迁移调 Pivot 掩盖"的教训）。

5. **A* 转弯成本与状态定义一致性**。`FAStarSearchState` 只有 `Coordinate + IncomingDirection`，未含"已用转弯数"。若成本函数含转弯惩罚，需明确惩罚是否进 g 值、平局规则（FScore→GScore→TurnCount→IncomingDirection→Y→X）是否足以保证唯一确定路径。否则复现性可能在含转弯代价时被破坏。

6. **HISM 重复 Generate 的生命周期**。`ClearGeneratedScene` 在 `bGenerationInProgress=true` 时不重置 State，配合 `GenerateFromRequest` 先 Clear 再实例化。需测试覆盖：生成中再次 Generate、生成失败残留清理、玩家站在待清理地板上重生成（PIE-A 的 Staging 流程要真正落实）。虽然开局固定不重生成，但 PIE 调试和手动重跑会触发这些路径。

7. **WFC "真实选择空间"自检必须硬执行**。方案要求"传播后至少一个 cell 仍 ≥2 候选"才算真正跑了 WFC，否则退化为查表。这条是"自研 WFC"立论能否成立的关键——若 SFCorridors 同签名可互换 Variant 不足，PIE-A 应用项目灰盒补一组双 Variant，确保 WFC 分支被真正触发，而不是名义上叫 WFC 实际是固定拼接。

## 可延后（记录，数据出来再定）

8. **回溯后传播完整性证明**。回溯 `ResetQueue(DirtyCells)` 后只重新入队失败决策 cell，依赖"整体 Domain 快照恢复即一致"。需 13.14-11 的"多层回溯恢复完全一致"测试背书；先有测试再谈换增量 Trail。

9. **失败报告只记一条**。`FZeroEscapeGenerationReport` 单条失败，批量 Seed 回归看分布得靠日志。可加轻量计数汇总，不阻塞。

10. **魔数收敛**。`N≤12`、`CollectedMask` 用 uint32，多处硬编 "12"。建议抽具名常量，避免 mask 类型与上限脱节。统一 `TBitArray::CountSetBits()` 与 `FPlatformMath::CountBits`（uint32 mask）用法，别混。

## 对送审 10 问的关键回应

- Q1 职责清楚；WFC 是否有真实选择空间存疑，取决于素材同签名 Variant 数量，须靠第 7 条自检硬保障。
- Q2 Pass/LeftTurn/RightTurn/T + 旋转变体在走廊带可形成多候选，可行；建议 PIE-A 灰盒补双 Variant 兜底。
- Q10 已列明哪些是骨架 helper（见结论），无隐瞒；评审不认定为已编译。
- 其余（Id 整数化、Portal Frame、Closure Finalization 等）方案处理到位，无阻塞，纳入上述高回报/延后项跟踪。
