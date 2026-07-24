# 已完成：V3.2 已按审计意见实施；旧 Solver/测试/死配置均删除，Core 已简化，构建、13/13 自动化、288 Seed Sweep 与 Runtime PIE 通过。

# 审计：PCG Grid-WFC 重规划 V3.2 讨论稿 + 现有 PCG 代码

- 审计日期：2026-07-24
- 讨论稿：`claude/docs/2026-07-24-pcg-grid-wfc-redesign-v3-draft.md`
- 代码基线：git `10611c6`（2026-07-24 20:50，UE5.7.4 pre-upgrade PCG snapshot）
- 结论：**方案通过，建议放入 DailyPlan 实施**。现有 PCG 代码确实严重过度设计，讨论稿的删减判断准确、论证严谨。

## 现有 PCG 代码规模（精确行数）

| 文件 | 行数 |
|---|---|
| ZeroEscapeLayoutSolver.cpp | **5136** |
| ZeroEscapeGenerationCore.cpp | 2853 |
| ZeroEscapeGenerationTests.cpp | 2095 |
| ZeroEscapeGenerationAssets.cpp | 1047 |
| ZeroEscapeGenerationTypes.h | 759 |
| ZeroEscapeRuntimeLevelGenerator.cpp | 692 |
| ZeroEscapeLayoutSolver.h | 421 |
| ZeroEscapeRuntimeGenerationTestHarness.cpp | 430 |
| ZeroEscapeGenerationAssets.h | 360 |
| ZeroEscapeGenerationCore.h | 268 |
| RuntimeLevelGenerator.h / TestHarness.h | 191 / 125 |
| **PCG 合计** | **≈ 16,377 行** |

## 是否冗余：是，且严重

一个"三周单机 Demo 首个还没跑通的关卡生成器"写到 1.6 万行、单文件 5136 行，明显超配。`ZeroEscapeLayoutSolver.h` 已暴露主体是**为已废弃素材形态服务的机制**：

- **整数 A***：`FAStarStateKey` / `FAStarNodeRecord` / `RouteGraphEdgesWithAStar` / `FindGridPath` + `AStarStraightStepCost/TurnPenalty` 预算
- **Socket 系统**：`SocketModule` / `Strong-Weak Anchor` / `PlaceRequiredSocketModules`(MRV+DFS) / `StableSocketId` / `AssignedSocketIds` 回溯
- **WFC 完整回溯**：`FWfcDecision` / `FWfcDomainSnapshot` / `NestedResidentBytes` 快照驻留限制 / Backtrack
- **Connector 对齐**：`FConnectorSignature`(WidthClass/HeightLayer) / `SolveModuleLocalTransform` 浮点 Portal 对齐

讨论稿 §7 逐条列出要删这些，判断与代码实际一致。删除理由成立：素材已从"完整房间块 + 非统一尺寸 + Socket 对接"改为"300cm 统一地板/墙/天花板组件"，A*（无障碍无代价差的凸矩形网格）、Socket 对齐（分离式组件无需浮点对接）、WFC 回溯（§5.1 已证明构造性保证有解）在新约束下全部失去职责，属于"为未出现的需求预建的兼容层"。保留只会掩盖不变量 Bug。

## 讨论稿本身的质量：高

- §5.1「为什么不会无解」用"每条公共边看作二值变量 + 任一侧 RequiredOpen 即开放 → 拼出显式合法解"证明了首版 CSP 构造性可满足，逻辑严谨，支撑了"删回溯"的决定。
- §5.2「为什么不需要 A*」论证凸网格 + Manhattan 正交雕刻必连通，成立。
- §9 分 A~E 五个检查点，每个都是可独立验证的最小闭环（先固定 Fixture 验装配，再最小整关，再接约束 WFC），符合项目"先闭环再扩展"的门禁。
- 诚实：明确写"数量为零仍是合法结果""不设配额""Domain 空则报不变量错误而非降级",避免了上一版靠硬造 T/Cross 数量的伪需求。

## 通过前需明确的点（不阻塞，实施时落实）

### 高回报
1. **删除范围要一次到位，不留转发壳**。§8 已说旧 `ZeroEscapeLayoutSolver.h/.cpp` 直接删、不保留包装——务必执行。5136 行删干净比"注释掉/if(false)"重要，否则冗余仍在。
2. **`ZeroEscapeGenerationTests.cpp`(2095 行) 和 GenerationCore 里的 Socket/A* 测试同步删**。§7 最后一行"Socket/Portal/Closure 测试删除并替换"要落实，否则删了实现留着测试会编译失败或测试失效。
3. **`FResolvedGenerationBudget` 里的 `AStarStraightStepCost/AStarTurnPenalty`、`ERandomDomain::SocketLayout`、`ShortLeafBranchCount/ForwardRejoinBranchCount` 等字段一并清理**（Core.h 里仍在）。讨论稿 §5 说改用可选地标上限替代精确分支数，那这些字段就是死配置，别留。

### 可延后
4. §8 把 Grid 求解器 + WFC 求解器 + 验证塞进 `ZeroEscapeGridLayoutSolver` 一个文件——首版可以，但要盯住行数。若单文件再次逼近 1500 行就按检查点边界拆（地标放置 / 正交雕刻 / WFC / 验证）。这次教训就是单文件 5136 行没人能维护。
5. 保留的 `FFlow/K-of-N/Progression Intent/Signature/Hash` 是好东西（确定性复现是 PPT 技术点），但 §7 说"保留并简化"——简化到什么程度要在 DailyPlan 写清，别只保留不简化又变成新包袱。

## 目标行数预期（供实施后自查）

删除 A*/Socket/回溯/Catalog + 对应测试后，首版按讨论稿范围（600cm 单层 WFC + 正交雕刻 + 无回溯 + 分离装配）合理规模应在 **3000~5000 行**量级（含测试）。若实施完仍 >8000 行，说明又超配了，需回头查。

## 结论

方案可以进 DailyPlan。它不是加功能，而是**用更贴合当前素材的简单模型替换掉一个为废弃形态过度设计的 1.6 万行求解器**——方向正确、论证扎实、分阶段可验证。实施时把"删干净 + 不留转发壳 + 测试同步删 + 死配置字段清理"作为硬性验收项。
