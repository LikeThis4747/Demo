# Demo - AI 快速入口

## 项目身份

- UE 5.8（由 5.7.4 升级），预计工期三周的单机 Demo，C++ 优先；默认不使用 GAS、不设计联机。
- 工程根：`D:\UE5projects\Demo`；主模块：`Source/Demo`；UE 资产根：`Content`（`/Game`）。
- 蓝图只负责资源装配、UI、关卡配置和 AnimBP 连线；数据使用 DataAsset/DataTable。
- 编辑器操作有**两个 MCP 可同时使用**：本地 `ue-editor-mcp`（socket 55558，擅长蓝图微操/重构、PIE、日志断言、审计）与 UE5.8 官方 `ue58-official-mcp`（HTTP 8000 `/mcp`，擅长语义搜索、自动化测试、物理资产、各类资产 CRUD、属性/类发现、Python 编排）。**选型与协同规则必须遵循 `MCP/capability-notes/dual-mcp-usage-guide.md`**。官方 MCP 随编辑器自动启动（`bAutoStartServer=True`），走 `list_toolsets`→`describe_toolset`→`call_tool` 三步；两者端口不冲突。

## 每次开始

1. 通过 Memory MCP 读取 `.ai-context/current-task.md`、`.ai-context/latest-error.md` 与 `memory-bank/activeContext.md`；若昨夜 Git 提交或推送失败，开始工作时先提醒用户。不要直接读写记忆文件。
2. 查看 `claude/tasks/active/`；开始非简单任务前创建任务卡并声明 Owner、范围和可能修改的文件。
3. 阅读 `DOC/AI_WORK_GUIDELINES/PROJECT_ARCHITECTURE_RULES.md`；仅在任务需要时再读对应 Skill 或 MCP 手册。
4. 被要求查看审计意见时，只读 `claude/reviews/` 根目录里未加 `Done-` 前缀的最近一份报告；自主判断哪些建议值得采纳，不必全部落实。处理完后由写代码的 AI 把该报告改名加 `Done-` 前缀、开头标注已完成，并移入 `claude/reviews/archive/`。不主动通读 `archive/` 里的历史审计。
5. 涉及 UE 资产时，先用 UE Editor MCP 检查真实蓝图、引用和配置，禁止只凭 C++ 猜测。

## 工作与交接

- 计划和检查点写入自己的任务卡；不要让多个 AI 同时修改重叠文件。
- 交接时按 `claude/templates/HANDOFF_TEMPLATE.md` 写明已完成、未完成、验证、风险和下一步。
- 完成功能后更新 `memory-bank/progress.md`；形成稳定决策/模式时更新对应长期记忆。
- 每个有实质工作的日期，在 `memory-bank/daily.md` 对应日期下追加完成、验证和遗留事项。
- 会话结束前刷新 `activeContext.md` 和当前任务；只保留恢复工作真正需要的信息。

## 硬规则

- 功能实现遵循"讨论方案 → 确定方案 → 对话中展示拟实现代码 → 用户明确允许后落盘 → 联合验证与用户验收"；未经验收不得标记完成，细则见 `DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md`。
- 对外交流和正式文档不得把 AI 临时创造的简称或项目内部命名当作 UE 官方概念、标准算法或行业通用术语。术语首次出现时必须说明其来源类别并用直白中文解释；无法确认来源时不用该术语，也不得默认用户已经理解。细则见 `DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md` 的“术语与表达规范”。
- 优先采用满足目标的最直接、最小方案。若项目规则或当前授权与该方案冲突，必须先向用户说明冲突、直接方案（包括所需人工操作或许可）、影响与替代方案并询问选择；不得默认用户不愿操作，也不得自行增加非必要的代码、资产副本、映射、脚本或流程来规避。只有用户明确选择复杂替代方案后，才可实施该方案。
- **Git 推送唯一目标为内部工蜂 `git@git.woa.com:shiqiqiwang/Demo.git`；禁止推送到 GitHub 或任何外部平台。执行 push 前必须核查 remote，详见 `DOC/AI_WORK_GUIDELINES/GIT_INTERNAL.md`。**
- 默认关闭 Tick；优先事件、Delegate、Timer、AnimNotify、碰撞、感知和行为树。
- 不依赖组件名、Actor 名、Widget 函数名或关卡名格式实现逻辑。
- 不修改无关文件；未经用户明确许可不修改第三方素材包；不把 Editor-only 依赖加入 Runtime 模块。
- `claude/` 是 AI 沙盒；可清理 AI 自己创建的临时文件。删除用户文件或非本任务创建的文件前必须征得许可。
- 完成必须包含适当的 C++ 构建、蓝图编译/保存、实际运行与边界验证；仅生成代码不算完成。
- 夜间无人值守任务必须遵循 `DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md` 的夜间红线，禁止修改或删除项目代码、资产、配置和规范。
- 新文档必须放入 `DOC/` 下用途明确的分类目录；没有合适目录时创建新目录，并同步更新 `DOC/README.md`。禁止把业务文档直接堆在 `DOC/` 根目录。
- 用户确认后的当日实施方案写入 `DOC/DailyPlan/`；讨论稿和中间方案留在 `claude/`；夜间再将实际完成、验证和遗留写入 `DOC/DailyReport/`。细则见 `DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md`。
- 新增 C++ 前必须按稳定职责设计 `Source/Demo/Public` 与 `Private` 的镜像子目录，并在方案中列出每个新增/修改文件的完整路径；细则见 `DOC/AI_WORK_GUIDELINES/PROJECT_ARCHITECTURE_RULES.md`。

## 按需导航

- 文档目录：`DOC/README.md`
- AI 规范目录：`DOC/AI_WORK_GUIDELINES/README.md`
- 架构边界：`DOC/AI_WORK_GUIDELINES/PROJECT_ARCHITECTURE_RULES.md`
- AI 记忆、并行与交接：`DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md`
- MCP/工具索引：`DOC/AI_WORK_GUIDELINES/AI_Coding_Guide.md`
- **Git 仓库与推送规范：`DOC/AI_WORK_GUIDELINES/GIT_INTERNAL.md`（所有 git push 必须先查阅）**
- 待整理规则：`DOC/AI_WORK_GUIDELINES/RULES_INBOX.md`
- AI 代码审计结论（实现前必查）：`claude/reviews/`（索引见 `claude/reviews/README.md`）
- 当前正式策划：`DOC/Design/GAME_DESIGN.md`
- 当前开发排期：`DOC/Design/DEVELOPMENT_SCHEDULE.md`
- 历史创意与评审：`DOC/Ideas/`
- MCP 搜索换词：`MCP-SEARCH-GUIDE.md`
- MCP 能力边界：`MCP/capability-notes/README.md`
- **双 MCP 协同使用规范（同时用 ue-editor-mcp 与官方 MCP 前必读）：`MCP/capability-notes/dual-mcp-usage-guide.md`**
- C++/蓝图/材质/Python 工作流：`.github/skills/`
