# Current Task

- 当前目标：完成最小一局——主菜单选择 Seed/难度，进入正式关卡，PCG/Population 生成，玩家与追猎者就位，再接胜负与重开。
- 当前已验证：2026-07-31 白天构建产物晚于相关 GameFlow/UI 源码且被编辑器加载；四个 GameFlow/UI 蓝图为 UpToDate。官方 MCP 读取确认 L_Game 使用 BP_ZeroEscapeGameMode，DefaultPawn 为 BP_ZeroEscapeCharacter，Generator=ExplicitOnly，Populator 已绑定 Generator 与 DA_Population_Default。
- 运行证据：现有白天日志确认从主菜单以 Seed 12345 进入 L_Game，只生成一次该 Seed，随后放置 4 个地刺、8 个磁性物体，并生成玩家与追猎者。
- 验证边界：夜间未新跑构建、自动化或 PIE；玩家实际操控、Exit/生命归零、结算/同 Seed 重开、18 项 Demo.PCG、至少 10 Seed、Recast 与真实追猎者连续移动仍未验收。
- 当前风险：PlayerStartSeparationCm 配置为 1200 cm，但最新成功日志实际 separation_cm=1138，需白天检查碰撞调整与开局公平性。
- 场景遗留：HydroLab V5 与三层楼梯塔仍只具备静态装配证据，玩家/Recast/真实追猎者验收未完成；SFCorridors 退场未授权。
- 最短下一步：先完成一局实玩并处理 1138 cm 出生距离，再接 Exit/死亡结算与同 Seed 重开，之后恢复 18 测试/10 Seed/导航基线。
