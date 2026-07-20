# Demo - AI 快速入口

## 项目身份

- UE 5.7.4，预计工期三周的单机 Demo，C++ 优先；默认不使用 GAS、不设计联机。
- 工程根：`D:\UE5projects\Demo`；主模块：`Source/Demo`；UE 资产根：`Content`（`/Game`）。
- 蓝图只负责资源装配、UI、关卡配置和 AnimBP 连线；数据使用 DataAsset/DataTable。

## 每次开始

1. 通过 Memory MCP 读取 `.ai-context/current-task.md`、`.ai-context/latest-error.md` 与 `memory-bank/activeContext.md`；若昨夜 Git 提交或推送失败，开始工作时先提醒用户。不要直接读写记忆文件。
2. 查看 `claude/tasks/active/`；开始非简单任务前创建任务卡并声明 Owner、范围和可能修改的文件。
3. 阅读 `DOC/PROJECT_ARCHITECTURE_RULES.md`；仅在任务需要时再读对应 Skill 或 MCP 手册。
4. 涉及 UE 资产时，先用 UE Editor MCP 检查真实蓝图、引用和配置，禁止只凭 C++ 猜测。

## 工作与交接

- 计划和检查点写入自己的任务卡；不要让多个 AI 同时修改重叠文件。
- 交接时按 `claude/templates/HANDOFF_TEMPLATE.md` 写明已完成、未完成、验证、风险和下一步。
- 完成功能后更新 `memory-bank/progress.md`；形成稳定决策/模式时更新对应长期记忆。
- 每个有实质工作的日期，在 `memory-bank/daily.md` 对应日期下追加完成、验证和遗留事项。
- 会话结束前刷新 `activeContext.md` 和当前任务；只保留恢复工作真正需要的信息。

## 硬规则

- 功能实现遵循"讨论方案 → 确定方案 → 对话中展示拟实现代码 → 用户明确允许后落盘 → 联合验证与用户验收"；未经验收不得标记完成，细则见 `DOC/AI_WORKFLOW.md`。
- **Git 推送唯一目标为内部工蜂 `git@git.woa.com:shiqiqiwang/Demo.git`；禁止推送到 GitHub 或任何外部平台。执行 push 前必须核查 remote，详见 `DOC/GIT_INTERNAL.md`。**
- 默认关闭 Tick；优先事件、Delegate、Timer、AnimNotify、碰撞、感知和行为树。
- 不依赖组件名、Actor 名、Widget 函数名或关卡名格式实现逻辑。
- 不修改无关文件或第三方素材包；不把 Editor-only 依赖加入 Runtime 模块。
- `claude/` 是 AI 沙盒；可清理 AI 自己创建的临时文件。删除用户文件或非本任务创建的文件前必须征得许可。
- 完成必须包含适当的 C++ 构建、蓝图编译/保存、实际运行与边界验证；仅生成代码不算完成。
- 夜间无人值守任务必须遵循 `DOC/AI_WORKFLOW.md` 的夜间红线，禁止修改或删除项目代码、资产、配置和规范。

## 按需导航

- 架构边界：`DOC/PROJECT_ARCHITECTURE_RULES.md`
- AI 记忆、并行与交接：`DOC/AI_WORKFLOW.md`
- MCP/工具索引：`DOC/AI_Coding_Guide.md`
- **Git 仓库与推送规范：`DOC/GIT_INTERNAL.md`（所有 git push 必须先查阅）**
- MCP 搜索换词：`MCP-SEARCH-GUIDE.md`
- MCP 能力边界：`MCP/capability-notes/README.md`
- C++/蓝图/材质/Python 工作流：`.github/skills/`
