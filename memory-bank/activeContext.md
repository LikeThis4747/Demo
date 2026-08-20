# Active Context — Demo

## 当前权威基线

- UE5.8 C++ 优先单机 Demo；公开 Seed 是地图身份，软质量目标不得静默换 Seed。
- 正式流程为菜单 Seed/难度 → L_Game → 开场镜头 → PCG/Population → 玩家/追猎者 → 光团门槛 → Exit/死亡 → 结算/重开。
- Runtime Generator 只接受追猎者对应的 Dynamic RecastNavMesh，并在 Ready 前验证多层代表点与完整路径。
- Heavy 接触统一保留 0.15 秒 PhysicsBody 阻挡；追猎者持续追踪并带镜头外同层 NavMesh 恢复。
- BGM、菜单灵敏度/音乐/音效设置、磁力/追猎者/脚步音效及三层楼层箭头已提交。

## 当前 P0

1. 用正式 L_Game/最终 Shipping 在同一轮证明动态 Recast、真实跨层追逐、三层箭头、机关、光团、出口、死亡/重开；当前编辑器停在 Level0，导航关闭且 Recast 为 Static。
2. 同轮复核 HeavyImpact preparation timeout 与 penetrating-start recovery；没有新运行证据前不自动修复。
3. 在目标机验证 BGM、菜单音量、磁力/攻击/脚步音效及耳机/扬声器切换；当前编辑器持续有 WASAPI 设备切换错误。
4. 由白天 Owner 核对 38 张活跃任务卡中的已交付、重复编号与真实待办；夜间不删除或归档。

## 恢复工作注意

- 2026-08-21 夜间没有新提交、构建、自动化、PIE 或玩家验收。
- 两个 UE MCP 在线，PIE 停止；Level0/L_Game 与关键 Blueprint 当前非 Dirty。
- 静态 Recast Actor、非 Dirty 资产和旧原型 PIE都不能替代正式 L_Game 的同轮动态导航证据。
