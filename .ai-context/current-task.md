# Current Task

## 主任务

- ID：TASK-20260723-002
- 目标：实现《零号逃亡》实时、非工具性 PCG 整关生成；SFCorridors 是当前正式、未来可替换的场景素材。
- 阶段：用户已确认 V2.1 并明确授权实施。当前先提交并推送实施前工作区快照；内部远程必须为 `git@git.woa.com:shiqiqiwang/Demo.git`，推送成功前不创建 PCG 源码或项目自有适配资产。
- 当前方案：Progression/Spatial Graph → Special Socket → 确定性 A* 必需连接 → 有限 Simple-Tiled WFC → Closure → 全局验证 → Runtime 实例化。
- 实施门禁：
  - Transform Test 0 先于 Socket 和实例化业务。
  - WFC 使用候选删除事件 + 四向 Support Count，并以慢速全量 Oracle 对照。
  - 多层回溯恢复后 Domain/Support/Queue 必须与 Oracle 一致。
  - Runtime Public/Internal Clear 分离，生成 Guard 覆盖所有广播路径；对象创建即登记 rollback。
  - 先测同步分阶段 P50/P95；没有数据不提前引入后台线程。
- 素材边界：只适配 PIE-A 实际使用的最小 SFCorridors 结构集；第三方目录只读。Plan 只保存 Stable Id、离散约束和 Transform，资源路径只在 Presentation Profile。
- 文件结构：代码集中在 `Source/Demo/Public/PCG` 与 `Source/Demo/Private/PCG`；职责真实扩大后才拆 WFC/Socket 子目录。
- 当前文档：`DOC/DailyPlan/2026-07-23-PCG整关场景方案冻结.md` V2.1。
- 已处理评审：`claude/reviews/Done-2026-07-23-pcg-whole-level-plan-v2.md`、`claude/reviews/Done-2026-07-23b-pcg-whole-level-plan-v2.1-recheck.md`。
- 验证边界：当前只有文档/静态复核；尚无 UHT、Build、Automation、Blueprint 编译或 PIE 结果。
- 下一步：完成实施前内部 Git 快照后，按 Test 0/基础 Types → Profile/Catalog → Socket/A* → Support-count WFC → Runtime 生命周期 → Automation/PIE 实施。

## 关联任务

- TASK-20260723-003：Demo MCP 可用性与蓝图访问诊断，独立任务。
- TASK-20260723-004：SFCorridors 动态光照与性能验收，独立任务；其 Config 改动不属于 PCG 实现。
