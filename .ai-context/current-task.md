# Current Task

## 当前任务

壁挂式预判抛射 Chaos 机关的当前基础机制版本已获用户验收。当前权威参数为 `ReferenceRange=900 cm`、`PreferredLaunchAngle=18 deg`、`ProjectileMassKilograms=50 kg`、`ProjectileLifetimeSeconds=8 s`，实际初速约 `1224.97 cm/s`。

## 验收证据

- Demo `-NoLink` 与 `DemoEditor Win64 Development` 正式链接成功，只构建 Demo 模块。
- 官方 MCP 回读默认 DataAsset 为 900 / 18 / 50 / 8，非 Dirty。
- 最小 PIE 记录实际初速 1224.97 cm/s、50 kg、8 s；发射后存在，9 秒后 Actor 已清除，PIE 已停止。
- 用户确认当前基础机关先验收通过；模型更换、表现细节和轻受击接入属于后续独立增量。

## 当前门禁

1. 先把当前验收版完整 commit/push，核对内部工蜂远端与 clean status。
2. 在新干净基线后，只读完成 Level0 和旧实现残留审计。
3. 向用户展示精确清理预览；未经再次明确授权，不落盘技术债清理。

## 清理边界

- 不把模型更换或轻受击接入混进本次清理。
- HeavyImpact Preparation 与 Exhaust 表现挂点属于后续接口，不按旧推进残留删除。
- 旧讨论文档可保留，但必须标明当前 2026-08-10 预判抛射 Chaos 方案与 Source/DataAsset 为权威。
