# Current Task

- 当前任务：HeavyImpact 提前 Snapshot 起身交接已完成实现与技术验证，等待用户真实摆锤命中的正反面、开阔地、墙角和恢复控制/追逐画面验收；不得标记视觉或可玩验收完成。
- 当前实现：正常空间在最短物理模拟后连续低速且有地面支撑 0.10 秒，即保存当前物理姿势、同步启动动态 Montage，并在 0.22 秒内用单一 Alpha 混到原 DefaultSlot；旧可见物理姿势准备/蠕动路线已删除。
- 空间边界：最终落点必须容纳完整站立 Capsule；骨盆正上方受阻时可在 60 cm 内沿不穿阻挡几何的小范围路径退让。完全封闭时保持 Downed、无 Tick 睡眠并持续重试，不强制穿墙起身。
- 当前证据：HeavyImpact 自动化 5/5；两个 AnimBP warnings-as-errors 编译、重启回读和短 PIE 初始化通过；最终 DemoEditor `-Module=Demo -NoEngineChanges` 构建成功，未编译引擎。
- Git：用户已授权恢复内部工蜂 origin；baseline `3350b78c7708ca170ecb6641d32e240d57077f34` 已推送并远端核验。
- 下一步：用户现场测试玩家/AI 正躺、趴倒和墙角；若只需调观感，优先调整 Snapshot 混合时间或提前交接稳定时间，不恢复旧物理蠕动分支。
