> 已完成，仅存档，无需继续阅读。复审已批准 V2.1 进入落盘实施；实施期门禁已纳入任务顺序，不代表功能已经完成或验收。

# 复审：PCG 整关场景方案 V2.1（吸收首轮评审后）

- 审计日期：2026-07-23（第二轮）
- 代码基线：git `0847ed7` 之后的工作区未提交改动；被审对象为 `DOC/DailyPlan/2026-07-23-PCG整关场景方案冻结.md` 已升级为 **V2.1**（第十三节标题「已吸收评审」，新增 13.15「独立评审处理结果与实施门禁」，文档 ~5543 行）。仍未落盘到 `Source/`。
- 前置约束不变：开局难度固定、不动态重生成、不流式无限世界、靠 Seed 稳定复现。
- 本轮方法：不采信 13.15 的自述表格，逐条到实际拟稿代码核对首轮 3 个阻塞项 + 高回报项是否真改。

## 结论

**首轮 10 项意见全部在代码层面真实落实，非纸面承诺。3 个阻塞项已解决，质量超出预期。复审裁定：批准进入落盘实施，无新增阻塞项。** 剩余仅为"实施期必须兑现的门禁"与少量可延后观察项，不阻断落盘。

后台线程决策维持首版结论：V2.1 明确"首版仍保持同步，不因可能卡顿预先引入线程"，正确，无需改。

## 阻塞项核实结果（逐条到代码）

1. **WFC 传播复杂度 → 已解决**。确认改为标准支持计数法：`FWfcCell::SupportCountByVariantDirection`、`RemoveVariant` 产生删除事件入队、`PropagateRemovedVariants` 只在 `Support==0` 时移除候选。彻底消除"每脏 cell 每方向重算全集"。每个兼容检查、支持递减、候选删除都单独计入 `MaxTotalWorkUnits`，另设 `MaxWfcSupportUpdates`。计费不再按"看过一个邻格"粗粒度。达标。

2. **Domain 快照内存 → 已解决且更严谨**。确认硬上限 Active≤256、Variant≤64、`ActiveCellCount*Variant*4` 以 int64 查溢出；`MeasureDecisionNestedBytes` + `EstimateDecisionNestedBytesBeforeCopy` 建立"复制前保守上界 + 复制后实测"双重账本，16 MiB 实时 / 64 MiB 累计复制上限，8 MiB 预警；`InvariantViolation` 在 Shipping 也释放临时快照。换增量 Trail 的触发条件已量化（>256 cell / >64 variant / 300 Seed 峰值达 8 MiB / P95 超阈值）。达标。

3. **`OrderedObjectives` 半暴露 → 已解决**。确认从首版 Blueprint 枚举、Profile 与验证分支整体移除，只保留 Escape/All/K-of-N。不再存在"策划能选到必然失败项"。达标。

## 高回报项核实结果

4. **Transform 顺序 → 已收口**。三个纯 helper（`SolveModuleLocalTransform` / `MakePresentationLocalTransform` / `MakePresentationWorldTransform`）成为乘法顺序唯一入口，`TryAlignAndPlaceModule` 与实例化处均改为调用它们。Test 0 明确"先于任何 Socket/实例化业务调用通过"，且要求用不可交换的平移+Yaw+PivotCorrection 验证同一测试点世界坐标一致。达标——实施时须真正让 Test 0 前置。

5. **A* 转向成本 → 已解决**。`AStarTurnPenalty` 进入 G 值，Manhattan 启发只计直行成本（保持可采纳/不高估）。状态键仍 `(Cell, IncomingDirection)`，并明确说明 TurnCount 当前仅诊断/平局项、未来加 `MaxTurns` 硬约束才须纳入 Key——这个边界说明是对的。达标。

6. **HISM/重复 Generate 生命周期 → 已解决且完善**。`ClearGeneratedScene`（Public，忙碌返回 false）与 `ClearGeneratedSceneInternal`（回滚）分离；"对象创建成功即登记 rollback 集"；Guard 覆盖全部广播路径（回调内 Generate/Clear 被拒，防 Ready+空场景/递归）；故障注入点覆盖 NewObject/Register/AddInstances/SpawnDeferred/FinishSpawning；EndPlay 先失效再清理。达标。

7. **WFC 真实选择空间 → 已解决**。`MeasureInitialChoiceSpace` + `bHadEffectiveWfcChoice`；验收 Profile 无真实选择返回 `WfcNoEffectiveChoice`；要求灰盒 Catalog 提供至少两个同 Connector Signature、不同 `(StableModuleId,QuarterTurns)` 的真实结果，且 20 固定 Seed 中二者都被选中过。这条把"自研 WFC 名副其实"变成了可验收门槛，达标。

8. **多层回溯完整性 → 已解决且更严谨**。Decision Frame 保留 `UntriedVariants`，只在同层候选真正耗尽才弹栈（修掉旧版"一弹栈丢同层选择"歧义）；恢复只走 `RestoreDomainsAndRebuildSupportCounts` 从恢复后 Domain 重建支持计数，绝不复用失败分支旧计数；恢复后若重建出矛盾报 `SolverInvariantViolation` 不伪装无解。达标。

9. **失败统计 → 已解决**。`RecordAttemptFailure` 维护 `LastAttemptFailure` + 稳定排序的 `AttemptFailureCounts(Stage,Failure)`，不用 TMap 输出、不逐 Seed 刷屏；批量 Seed 按同枚举键聚合。达标。

10. **K-of-N 魔数 → 已解决**。`FObjectiveMask` + 具名上限 + 类型 Hash + 独立计数 helper。达标。

## 实施期必须兑现的门禁（非落盘阻塞，但不可省）

- Test 0（Transform 三 helper）必须先于 Socket/实例化业务代码通过，禁止靠手调 Pivot 掩盖。
- WFC 支持计数必须有"慢速 Oracle 对照测试"（暴力重算全集作为参照），证明增量支持计数与全量计算一致。
- 多层回溯必须有 Domain/Support/Queue 三者与 Oracle 完全一致的测试。
- 落盘顺序按 13.15 冻结：Test 0/基础 Types → Profile/Catalog 校验 → Socket/A* → Support-count WFC → Runtime 生命周期 → 自动化/PIE。建议严格照此，先落最低风险层。
- 后台线程门禁维持：先出单机同步分阶段耗时 P50/P95，未超可感知卡顿阈值就保持同步；真搬线程须先过"同步/异步同 Seed Canonical Hash 一致"测试。

## 可延后观察（有数据再定，不阻塞）

- 完整 Domain 快照 vs 增量 Trail 的切换点已量化，先按快照跑；跑满 300 Seed 后按实测峰值/延迟再决定，别提前复杂化。
- `WfcCumulativeSnapshotCopyBytes` 语义已明确为"当前 Layout Attempt 内累计"，跨 Attempt 靠 `AttemptFailureCounts` 聚合——注意别在多 Attempt 场景误读单次指标。
- Actor Binding 首版仅限已审查、不外溢子 Actor 的包装类；若要注入更多玩法上下文再引入 `AZeroEscapeGeneratedModuleActor` 基类并纳入统一 rollback 契约。不提前建。

## 与首轮报告的关系

首轮报告 `Done-2026-07-23-pcg-whole-level-plan-v2.md` 已被实现 AI 采纳归档，本报告为对 V2.1 的复审，不重复其内容。首轮所有阻塞/高回报项在本轮均已核销。
