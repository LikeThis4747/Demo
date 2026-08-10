# Current Task

## 当前任务

壁挂式预判抛射 Chaos 机关的当前基础机制版本已获用户验收，验收版提交为 `f7ae4f44f427b562ec87cfd4a0ce9a109a873747`。当前权威参数为 `ReferenceRange=900 cm`、`PreferredLaunchAngle=18 deg`、`ProjectileMassKilograms=50 kg`、`ProjectileLifetimeSeconds=8 s`。

## 本轮清理

- 删除与 `Ballistic -> FreePhysics` 完全重复的首次阻挡布尔状态，`Phase` 是唯一阶段来源。
- `ProjectileBody` 关闭无效 Overlap 事件生成；HeavyImpact `PreparationVolume` 仍独立负责可选 Pawn Overlap。
- 配置或生成失败的禁用弹体增加 1 秒 LifeSpan 回收；正常发射仍使用 DataAsset 的 8 秒 LifeSpan。
- 正式文档权威统一为 2026-08-10 狭窄走廊 DailyPlan、DailyReport、当前 Source 与默认 DataAsset；旧推进文档保留历史标记。

## 验证

- Demo `-NoLink` 编译成功。
- `DemoEditor Win64 Development` 正式链接成功，只重建 Demo 项目模块。
- 两轮独立只读复核无 Blocker/High；`git diff --check` 通过。
- 未修改 Blueprint、DataAsset、Level0、模型或轻受击系统。

## 后续边界

本轮清理由最终 Git 提交收口；模型更换、表现细节、轻受击接入和边界专项均另行处理。
