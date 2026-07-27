# System Patterns — Demo

- Character/Pawn 是组合入口，不是玩法逻辑仓库。
- 独立变化或复用的职责进入 ActorComponent。
- 每个关键状态只有一个明确 Owner。
- 查询能力使用 Interface 或 const 查询；变化通知使用 Delegate；稳定语义可使用 GameplayTag。
- UI 只展示状态和发送意图，不持有玩法权威状态。
- Actor/Component 默认关闭 Tick。
- 蓝图只负责资源装配、UI、关卡配置和 AnimBP 连线。
- DataAsset/DataTable 保存配置。
- 修改 UE 资产前先通过 MCP 读取真实结构，修改后编译、保存和运行验证。
- PCG 消费链采用单向分层：Generator 输出只读空间结果；Presentation/Population/GameFlow 分别消费，不把表现、批量玩法对象或局状态写回 WFC。
- 测试脚手架只服务尚未接入真实流程的阶段；GameFlow 接管并验收后删除旧脚手架，不保留并行兼容链。
