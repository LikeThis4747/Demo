# Active Context — Demo

## 当前权威基线

- UE5.8 C++ 优先单机 Demo；公开 Seed 是地图身份，软质量目标不得静默换 Seed。
- 多层生成采用跨层宏结构、逐层二维 WFC、整栋通行图；Population 使用 300cm 普通机关站，安全上限仍为硬约束。
- 正式流程为菜单 Seed/难度 → L_Game → 开场镜头 → PCG/Population → 玩家/追猎者 → 光团门槛 → Exit/死亡 → 结算/重开。
- Heavy 接触统一保留 0.15 秒 PhysicsBody 阻挡；追猎者保持持续追踪、攻击高度边界与镜头外同层 NavMesh 恢复。
- 2026-08-19 已提交 BGM、菜单灵敏度/音乐/音效设置、磁力/追猎者/脚步音效和三层楼层方向箭头；主菜单 Blueprint 的 gameLevelName=L_Game。

## 当前 P0

1. 用正式 L_Game/最终包在同一轮证明动态 Recast、真实追猎者跨层追逐与三层箭头；最新 13:38 PIE 实际是 Level0 + Prototype GameMode，且 Level0 bEnableNavigationSystem=false。
2. 在同轮复核 HeavyImpact 重复 preparation timeout、penetrating start 恢复与 Pursuer 隐藏重放置；现有日志只证明风险可复现，不授权自动修复。
3. 重新打包最终当前状态：15:45 Staged 包早于 17:03 DA/GameMode Blueprint/uproject 提交，当前 Demo.uproject 仍有恢复 ModelContextProtocol 的未提交改动。
4. 在目标机验收 BGM、菜单音量、磁力/攻击/脚步音效和设备切换；当前编辑器日志反复报告 WASAPI 设备丢失/空交换。
5. 清理前先由白天 Owner 核对活跃任务卡、临时脚本与 2329 文件 checkpoint 的归属；夜间不删除、不归档。

## 恢复工作注意

- 今夜两个 UE MCP 在线；Level0、L_Game、正式 GameMode/HUD、主菜单和相关 DA 均非 Dirty，主要父类与音效引用已回读。
- Level0 静态存在 NavMeshBoundsVolume/Recast Actor，但导航系统关闭；静态存在不等于运行可用。
- 参数问题先调现有 DataAsset/曲线；不新增任务框架或常驻 Tick。
