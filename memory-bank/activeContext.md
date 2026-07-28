# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

最短可玩闭环仍是：PCG 生成 → 地刺/磁力投掷 → 追猎者追击/受击 → Exit 成功，并补上生命归零后的失败/同 Seed 重开。Level0 的 HydroLab V3 积木组合已形成静态原型，但必须先完成玩家连续实走与 Recast/AI 验收，才能作为 WFC 语义模块依据。

## 当前索引

- Active 卡共 2 张：`TASK-20260728-001-SFCorridors物件筛选与退场.md`、`TASK-20260728-002-HydroLab错层房间装配验证.md`。
- HydroLab 卡等待用户整体视觉、玩家实走与 AI/NavMesh 验收；SFCorridors 卡仍停留在候选确认和依赖审计前。
- 追猎者近战/方向受击改动已落盘，但没有独立 active 卡记录当前构建与 PIE 验收边界。

## 当前边界与风险

- Level0 日志明确缺少 RecastNavMesh；短时 Simulate 和静态射线不能证明玩家胶囊连续通行或 AI 可达。
- 追猎者新代码与动画资产本轮只做静态/UpToDate 检查，未执行 C++ 构建、PIE、连续命中或重复投掷验收。
- AttackProjectile Tag 通过独立 Timer 到期移除；同一物体在旧 Timer 到期前再次投掷可能被旧 Timer 提前清 Tag，需白天复现后再决定修复。
- 295 个 SFCorridors LFS 资产不删除；任何退场都需完整依赖闭包、精确清单和用户明确授权。
- 当前 18 项 Demo.PCG、多 Seed 玩家/导航和生命归零失败闭环仍待推进。
