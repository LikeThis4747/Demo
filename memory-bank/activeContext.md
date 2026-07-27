# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

2026-07-27 用户明确授权接入追猎者 Physics Control 局部受击，包括本机 UE5.8 API 兼容修正，但不修改 `DA_Pursuer` 参数。当前关键路径临时切换为 `TASK-20260727-001`：源码接入 → 构建 → `BP_Pursuer` 装配 → PIE 连续命中与恢复验证 → 用户视觉验收。

## 活跃任务

- `TASK-20260727-001`：追猎者 Physics Control 局部受击，已授权实现，当前唯一正在修改的任务。
- `TASK-20260723-002`：V4 PCG 技术验证已完成，玩家多 Seed 验收暂缓，完成追猎者接入后恢复。
- 其他旧 active 卡暂停；不并行修改重叠文件。

## 当前边界与风险

- 不修改 `DA_Pursuer`、Level0、Manny、Physics Asset、AnimBP、磁力系统或 AI/伤害逻辑。
- 本机 UE5.8 Physics Control API 与交接源码存在返回值、字段和构造函数差异，必须按本机引擎头文件适配后重新构建。
- 新增反射类可能需要完整编辑器重启；关闭编辑器前必须先确认无未保存资产。
- 最终完成门槛包含真实物理命中、连续至少 10 次、恢复、Capsule/追击/攻击稳定及用户视觉验收。
