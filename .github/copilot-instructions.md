# Demo Copilot Instructions

- 首先遵循根目录 `AGENTS.md`；详细架构见 `DOC/PROJECT_ARCHITECTURE_RULES.md`。
- UE 5.7.4 单机 Demo，C++ 优先；默认不用 GAS，不设计联机。
- C++ 实现玩法逻辑、组件、GameMode/GameState、Controller 和 Subsystem。
- 蓝图只做资源装配、UI、关卡配置和 AnimBP 连线；配置使用 DataAsset/DataTable。
- 涉及 UE 资产时，先通过 UE Editor MCP 读取真实结构和引用，再修改并验证。
- 非简单任务先在 `claude/tasks/active/` 建立任务卡；避免与其他 AI 的文件范围重叠。
- 记忆文件必须通过 project-memory-mcp 读写。开始读取当前任务/上下文，结束更新进度、当前上下文和当日日志。
- 默认关闭 Tick；不要使用字符串名称查找代替稳定引用、Interface 或 Delegate。
- 小步修改，不改无关文件；完成后报告改动、验证证据、风险和下一步。

按需阅读：

- C++：`.github/skills/unreal-cpp/SKILL.md`
- UE 资产/蓝图：`.github/skills/unreal-blueprint/SKILL.md`
- 材质：`.github/skills/unreal-material/SKILL.md`
- Editor Python：`.github/skills/unreal-python/SKILL.md`
- MCP 索引：`DOC/AI_Coding_Guide.md`
