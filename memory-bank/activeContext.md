# Active Context — Demo

## 当前权威基线

- UE5.8 C++ 优先单机 Demo；公开 Seed 是地图身份，软质量目标不得静默换 Seed。
- 多层生成采用跨层宏结构、逐层二维 WFC、整栋通行图；Population 使用 300cm 普通机关站，安全上限仍为硬约束。
- 正式流程为菜单 Seed/难度 → 开场镜头 → PCG/Population → 玩家/追猎者 → 光团门槛 → Exit/死亡 → 结算/重开。
- Heavy 接触统一保留 0.15 秒 PhysicsBody 阻挡；追猎者保持持续追踪、攻击高度边界与镜头外同层 NavMesh 恢复。
- 2026-08-18 已形成 HUD 目标行、开场镜头、胜负过场、出口提示与“开始逃亡”提示；Shipping Cook/Pak/Stage 成功。

## 当前 P0

1. 确认并恢复官方 UE5.8 MCP 工作流：当前磁盘 Demo.uproject 已移除 ModelContextProtocol，且本夜官方工具未暴露；打包规避应优先使用 Cook 参数而非长期关闭项目插件。
2. 在同一正式 PIE/Shipping 运行中证明动态 Recast 与真实追猎者跨层追逐；当前日志仍反复出现 Unable to find RecastNavMesh。
3. 跑通机关、光团、锁门提示、出口、死亡/重开，并检查开场镜头、HUD、安全区、视角倍率与胜负过场的玩家体验。
4. 在目标机启动 Shipping 包并检查追猎者骨骼/动画；Cook 仍报告 /Engine/EngineMeshes/Humanoid 缺失依赖。
5. 清理前先由白天 Owner 核对活跃任务卡、临时脚本和报告归属；夜间不删除、不归档。

## 恢复工作注意

- 本夜蓝图审计未执行；不得把 C++、二进制时间戳、Cook 成功或静态资产状态当作蓝图/玩家体验验收。
- Shipping 构建与 Pak/Stage 成功不证明目标机启动、动态导航或真实追逐。
- 参数问题先调现有 DataAsset/曲线；不新增任务框架或常驻 Tick。
