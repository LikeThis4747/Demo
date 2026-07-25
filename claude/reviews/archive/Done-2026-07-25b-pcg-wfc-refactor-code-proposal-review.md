# 审计：PCG 全图 WFC 路线重构 拟实现代码 V1

> 处理状态：已完成（2026-07-25）。已落实根固定点、活动决策叶与根完成叶回溯边界；单次稠密输入构建；Grid 唯一完成态验收；内部下标 `check`；最低带权 Shannon 熵；Count / MaxConsecutive / Connected；总预算分片与仅预算失败重试。UE 5.8 构建、19 项 `Demo.PCG`、288/288 Seed Sweep、真实 Profile 字段 smoke 和 SelectedViewport PIE 技术烟测均通过；独立终审无代码阻断，玩家验收仍单独保留。

- 审计日期：2026-07-25（当日第 2 次）
- 提案：`claude/docs/2026-07-25-pcg-wfc-route-refactor-code-proposal.md`（未落盘、未编译）
- 对照源码：当前 `ZeroEscapeWfcSolver.cpp`(838) / `GridLayoutSolver.cpp`(1519) / Types / Assets / Core
- 代码基线：git `41bcb7d`（2026-07-25 01:05 Nightly）
- 结论：**设计正确、可实施**。回溯状态机、约束/求解器职责分离、完成态验收接口都实现得干净。有 2 处正确性风险必须实施时确认，防御性检查总体合理但有 3 处过度，规模不靠删注释可再降。

## 一、正确性风险（实施时必须确认，非阻塞设计）

1. **根固定点 Trail 清空后，若首个决策就 contradiction 会怎样**。§8 主循环 `bRootState` 分支在根稳定后 `Trail.Reset()` + `bRootState=false`。此后第一个决策失败进入回溯 while，`Decisions` 里只有这一帧，弹空后 `bFoundAlternative=false` → `ReportNoValidSolution`。**这是对的**（根级已无候选可退）。但要确认：根固定点产生的 Ban 被丢弃 Trail 后，Domains 仍保留（只是不可回滚）——代码逻辑成立，因为 Reset 的是 Trail 不是 Domains。**实施时加一个单测**：构造"根传播后已 singleton 化、首决策必矛盾"的夹具，验证返回 `NoValidWfcSolution` 而非崩溃或死循环。

2. **完成态验收 Reject 时 `BranchResult` 被伪造为 Contradiction 后进入回溯，但此时没有活跃 Decision 帧的 TrailStart 对应这个"叶子"**。§8 叶子 Reject 后 `bNeedBacktrack=true`，回溯 while 弹最近决策帧并 `RestoreDomains(Frame.TrailStart)`。因为叶子状态是最后一个决策 + 后续传播产生的，最近帧的 TrailStart 确实覆盖到叶子——逻辑正确。但**风险点**：如果最后一次到达叶子是"传播直接收敛、没有新决策帧"（所有 Cell 被 Count/Connected Ban 逼成 singleton），则 `Decisions` 可能不含覆盖该状态的帧，回溯会退过头。§11 的 `RejectFirstCompleteCandidate` 测试正是防这个，务必确保夹具能覆盖"无决策帧叶子"场景。

## 二、规模：可再降，且不靠删注释

提案自估完成后约 6970-7470 行。以下是**不删注释**的结构性精简点：

### 高回报
3. **导出后逐边复核（当前 L775-833，约 58 行）迁 Automation 是对的，但别只迁不删**。§2 说"等价回归放进 Solver Automation，Grid 最终验证继续保留"。注意：Grid 的 `ValidateFinalPlan` 新增的 Count/MaxConsecutive 复核（§9，约 55 行）+ 保留的连通/路线复核，与 Solver 内部约束在语义上**部分重叠**。建议：Solver 侧生产路径**完全不做**导出复核（移交 Grid 的 ValidateFinalPlan 唯一入口 + Automation），避免同一份不变量在 Solver 导出、Grid 验收两处各写一遍。这能省掉迁移后 Solver 里可能残留的复核骨架。

4. **`ValidateInitialConstraints` 删构造性见证后（原 L519-564，44 行），剩余的一元+镜像校验与 `BuildDenseConstraintView` 有重复遍历**。原 `ValidateGuaranteedSolvableConstraints` 内部先 `BuildDenseConstraintView` 一次，`Solve` 入口又调一次（L595）。§7 入口顺序仍是先 `ValidateInitialConstraints` 再 `BuildDenseConstraintView`——两次构建稠密视图。建议 `ValidateInitialConstraints` 直接复用已构建的 `ConstraintsByIndex`（改为接收稠密视图参数），省一次 O(N) 构建 + 一处重复的坐标/重复校验逻辑。

### 可延后
5. 三约束 `EvaluateCount`/`EvaluateConsecutiveLine`/`EvaluateConnected`（§6，约 210 行）实现紧凑、无冗余，**不要动**。这是新增的净功能，不是膨胀。
6. 测试预计净增 420-620 行。§11 的双夹具（回溯输出 == 排除失败分支的干净求解）是高价值等价性测试，保留。但"独立覆盖"清单有 10 项，实施时按"每项一个最小夹具"控制，别让单个测试函数堆叠多个断言场景。

## 三、过度防御性检查（可安全绕过/降级）

7. **`NarrowDomain` 里 `!InOutDomains.IsValidIndex(CellIndex)` 返回 InvariantFailure（§8 L904-909）在热路径**。CellIndex 全部来自内部 `FindMinimumEntropyCell` / 传播邻格 / 约束 Ban，都是代码内部产生的合法下标，不是外部输入。这是每次 Domain 修改都跑的最热函数。建议降为 `check(InOutDomains.IsValidIndex(CellIndex))`（Debug 断言），生产构建不为内部不变量付分支成本——与项目"外部输入才做运行时校验、内部不变量用 check" 的分层一致。

8. **`TryNextCandidate` 里 `check(bAssigned && Result.Status == Stable)`（§8 L979）之后紧跟 `++AttemptCount`**。这里 `NarrowDomain` 对一个"从当前 Domain 选出的 singleton bit"做收窄，数学上不可能产空——`check` 正确且应保留（Debug）。但注意 `NarrowDomain` 内部又跑了一遍 `Next==0` 判断并写 Contradiction，对这条调用路径是死分支。可接受（共用入口的代价），记录即可，别为它单独开特化。

9. **`ValidateCanonicalVariants` 每次 Solve 都全量校验 16 个 Variant 的顺序/权重（当前 L114-156）**。生产路径的 Variants 来自 `BuildCanonicalVariants`，是代码构造的、不可能乱序。这是每次生成都跑的校验。建议：生产路径信任 `BuildCanonicalVariants`，把这个全量校验只保留在接收外部/测试 Variants 的入口（或 Debug）。省一次每生成 16 次的循环 + 是纯防御。

## 四、判断汇总

- **设计**：回溯 + 三约束 + 完成态验收接口，选型和分层正确，DeBroglie 职责映射准确，无通用框架过度设计。§6 末"故意没有通用接口/割点/Loop 配额"、§12 六条自我约束，克制得当。
- **规模**：净增主要是回溯状态机 + 三约束 + 测试，属于必要功能，不是膨胀。可再省的是"同一不变量多处复核"（点 3、4）和"内部不变量用运行时校验"（点 7、9），合计约能压 80-150 行且降低热路径开销。
- **冗余可绕过项**：点 3（Solver 导出复核 vs Grid 验收重叠）、点 4（两次 BuildDenseConstraintView）、点 7/9（内部不变量降 check）。

## 五、给实施的硬项

1. 补两个针对性单测：根级首决策矛盾（点1）、无决策帧叶子 Reject（点2）——这是回溯正确性的两个最易漏边界。
2. 不变量校验分层：外部输入/配置用运行时 return failure；纯内部下标/顺序用 `check`（点7、9）。
3. 同一产品不变量只在一处做生产校验（Grid `ValidateFinalPlan`）+ Automation 回归，Solver 生产路径不重复（点3）。
4. `ValidateInitialConstraints` 复用稠密视图，消除两次构建（点4）。
5. 其余按提案 §13 顺序实施；预算冻结前必须有 288 Seed Sweep 的 P95/Max（提案已列）。
