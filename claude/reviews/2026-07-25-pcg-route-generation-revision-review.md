# 审计：PCG 路线生成修正讨论稿 + 当前 PCG 代码量分析

- 审计日期：2026-07-25
- 讨论稿：`claude/docs/2026-07-25-pcg-route-generation-revision-draft.md`
- 代码基线：git `41bcb7d`（2026-07-25 01:05 Nightly）；对象为已落地的 V3.2 PCG 实现
- 结论：**算法方案可行、引用准确、可进 DailyPlan**。代码量已从 7-24 的 16,377 行降到当前实测 6,713 行；仍偏大，主因在测试与防御性校验，非核心算法。

## 一、方案可行性（结合成熟算法核验）

讨论稿的技术定位准确，引用真实，判断成立：

1. **"加全局约束后必须引入回溯"成立**。原始 WFC（mxgmn）是 non-backtracking greedy；Karth & Smith 论文明确研究回溯与全局约束。当前 `ZeroEscapeWfcSolver.cpp`（实测 838 行）确实是纯无回溯——contradiction 直接 `ReportInvariantFailure` 返回。一旦加入 Connected/Count/MaxConsecutive，局部构造性可解的前提消失，不加回溯会频繁失败。判断正确。
2. **三个约束对应 DeBroglie 真实能力**：`ConnectedConstraint`（relevant cells 间保证路径）、`CountConstraint`（Tile 数量上下界）、`MaxConsecutiveConstraint`（沿轴连续 Tile 上限）都是 DeBroglie 文档实有、职责与稿中描述一致。稿中"只借职责不移植 C# 库、用现有 16 OpeningMask 实现等价"的边界是合理的。
3. **连通用"保守可能图 BFS"轻传播、不复刻 AC4/tracker** —— 正确的最小实现选择。§4.2 明确"可能图连通只表示尚未证明无解"，没有夸大约束强度，诚实。
4. **chronological backtracking 而非 backjumping**、**不做自动全局重启/隐藏降级**、**预算耗尽显式报失败** —— 都符合项目"不偷偷降级"的既定原则，且与 DeBroglie `BacktrackType.Backtrack` 对应。
5. **房间图（Delaunay/MST/额外边，Whitehead 2020）作为未来备选而非本轮并行实现** —— 正确，避免两套生成器。

唯一需要盯的可行性风险：**Connected 检查每次传播稳定点做一次全图 BFS（~384 Cell）**。稿中 §8 已自认这是设计判断、必须用 Seed Sweep 的 P50/P95/Max 确认。这是对的，别在拿到数据前就冻结 `MaxSolveSteps`/`MaxBacktrackSteps`。

## 二、代码量主要在什么层面（实测行数）

当前 `Source/Demo/**/PCG` 实测 **6,713 行**（稿中 6522 是估算，差异可接受）：

| 文件 | 行数 | 层面 |
|---|---:|---|
| GridLayoutSolver.cpp | 1519 | 布局：骨架/房间/Optional/验证/结构展开 |
| Tests.cpp | 1235 | **测试** |
| WfcSolver.cpp | 838 | WFC 核心 |
| RuntimeLevelGenerator.cpp | 566 | 运行时 HISM 提交 |
| GenerationTypes.h | 446 | 数据结构 |
| GenerationCore.cpp | 444 | 抽象图/预算/Hash |
| RuntimeGenerationTestHarness.cpp | 430 | **测试脚手架** |
| Assets.cpp | 327 | DataAsset 校验 |
| Assets.h | 238 | |
| 其余头文件 | ~670 | |

**代码量集中在三个层面**：
1. **测试相关最大**：`Tests.cpp` 1235 + `TestHarness.cpp` 430 = **1665 行，占 25%**。这是行数第一来源。
2. **布局求解**：GridLayoutSolver 1519 行，其中新方案要删的固定主干/Optional（`CarveRequiredSkeleton`/`CarveOrthogonalRoute`/`BuildOptionalEnvelope`/`PruneDisconnectedOptional`）约 300-380 行。
3. **防御性校验/不变量检查**：贯穿 WfcSolver 和 Assets.cpp。仅 WfcSolver 的 838 行里，`ValidateGuaranteedSolvableConstraints` + 导出后逐边复核 + 构造性见证就占约 250 行——真正的 WFC 主循环（熵/加权选择/传播）只有约 200 行。

## 三、是否有过剩冗余

**核心算法不冗余，但有两处"防御性过度"值得在本次重构一并收敛：**

### 高回报（本次重构顺带处理）
1. **WfcSolver 的双重不变量校验偏重**。`ValidateGuaranteedSolvableConstraints`（求解前构造性见证）+ 求解后逐边独立复核，是为"无回溯必须构造性保证有解"设计的。**新方案引入回溯后，"保证可解"的前提被主动放弃了**——`ValidateGuaranteedSolvableConstraints` 里那段"构造性见证逐边核对"（约 §524-564，40+ 行）逻辑基础已不成立，应删或大幅简化，否则是自相矛盾的死校验。这点稿中没提到，建议补进计划。
2. **求解后逐边复核（§779-833，约 55 行）可降级为 `check()`/仅 Debug**。它防的是"方向 bit/邻接下标实现回归"，价值在，但每次生产求解都全图跑一遍属于过度。回溯版求解开销更大，这类 O(N) 复核建议只在测试/校验入口跑，不在热路径。

### 可延后（记录，别现在动）
3. **`Tests.cpp` 1235 行本身不算冗余**（确定性 PCG 必须靠测试兜底），但新方案 §7 预计测试净增 420-620 行 → 测试可能逼近 1800 行。届时若单文件难维护，按"约束测试 / 回溯测试 / Seed Sweep"拆分，别让它变成第二个 5000 行怪物。
4. **GridLayoutSolver 删完固定主干后**若降到 ~1150 行仍偏厚，可评估把"2×2 房间约束生成"与"最终结构展开"分离；但不是本轮必须。

### 不要动
- WfcSolver 的熵/加权观察/邻接传播主循环（~200 行）干净、标准，是真正的技术资产。
- GenerationCore 的确定性随机域/Hash/K-of-N DP —— 是 PPT 可复现性技术点，保留。

## 四、对 §7 增量估算的核对

稿中"净增约 450-950 行、完成后约 6970-7470 行"的估算**合理但偏保守**。若同时执行上述"删掉已失效的构造性见证校验"，净增可再压低约 80-100 行。建议把"删除已失效的可解性构造证明"明确写进 §7.2 的"计划删除"清单——目前那里只列了 Optional/骨架符号，漏了这条。

## 五、结论

方案通过，可进 DailyPlan。算法选型（WFC + 三约束 + 有界回溯 + 最终 BFS）是当前素材形态下的正确解，引用严谨、边界克制。代码量偏大主要来自**测试(25%)** 和**为"无回溯"设计的防御性校验**；后者随本次引入回溯正好该顺手清理。落地时把三条设为硬项：① 删固定主干/Optional 不留壳；② 删掉与"无回溯"绑定、现已失效的构造性可解证明校验；③ 冻结 Solver 预算前必须有 Seed Sweep 的 P95/Max 数据。
