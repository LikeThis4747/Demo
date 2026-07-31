# Current Task

- 当前目标：完成最小一局的阶段一——主菜单选择 Seed/难度，经 GameInstance 进入正式游戏关卡，PCG 生成后玩家与追猎者正确就位。
- 已落盘但未验证：8 个新增 GameFlow/UI C++ 文件与 UMG 模块依赖；包含 UZeroEscapeGameInstance、AMainMenuGameMode、UMainMenuWidget、AZeroEscapeGameMode。
- 当前证据边界：新源码时间为 2026-07-30，Demo 模块二进制仍为 2026-07-29；没有当前构建、UHT、自动化或 PIE 证据。
- UE 装配边界：本轮本地 UE Editor MCP 尚未复测；UE5.8 官方 MCP 的项目级 Codex/CodeBuddy 配置已补齐，直连握手与 23 个 toolset 验证通过，但当前 Codex 任务工具表不会热刷新，仍需客户端首次重载及 UE 重启复测后才能执行蓝图审计。
- 静态风险：正式 GameMode 原生 DefaultPawnClass 为 AZeroEscapeCharacter，若蓝图不覆盖为已装配 InputConfig/磁力资源的玩家蓝图，角色会缺少正常输入；开局流程先移动玩家再校验/生成追猎者，失败时可能形成半初始化状态。
- 场景并行遗留：Level0 三层楼梯塔任务卡记录 211 Actor / 13 文件夹与四个 600cm 接口，但本轮未实时资产复核；玩家、Recast 与真实追猎者连续上下楼仍未执行。
- 明日最短下一步：先确认官方 MCP 在新任务中正式暴露，再构建新 C++；随后通过 UE 核对并装配阶段一所需资产/项目设置，并 PIE 跑通选参→生成→玩家/追猎者就位。通过后再验收 V5/楼梯塔、18 项 Demo.PCG 与至少 10 Seed。
