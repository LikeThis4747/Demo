# Demo AI 工具与文档索引

> 这里只维护导航和稳定事实。具体步骤放在对应文档中，避免多个入口重复规则。

## MCP

| 服务 | 用途 | 入口 |
|---|---|---|
| `project-memory-mcp` | 长短期记忆、搜索、容量检查和安全写入 | `MCP/memory/README.md` |
| `ue-editor-mcp` | UE 资产、蓝图、Widget、材质和编辑器操作 | `Plugins/UEEditorMCP/README.md` |
| `ue-editor-mcp-logs` | 日志、缩略图和资产差异 | `Plugins/UEEditorMCP/README.md` |

UE Editor MCP 使用 `search → schema → run`。连接前提：Demo Editor 已打开并加载 UEEditorMCP 插件。

## 文档路由

| 问题 | 阅读 |
|---|---|
| Git 仓库地址、推送规范、AI 操作须知 | `DOC/GIT_INTERNAL.md` |
| 项目职责、C++/蓝图边界、性能基线 | `PROJECT_ARCHITECTURE_RULES.md` |
| 记忆、任务卡、并行 AI、交接、每日记录 | `AI_WORKFLOW.md` |
| 找不到 UE MCP 动作 | `../MCP-SEARCH-GUIDE.md` |
| 判断 MCP 能否完成某资产操作 | `../MCP/capability-notes/README.md` |
| Memory MCP 参数、备份与压缩 | `../MCP/memory/README.md` |
| C++/蓝图/材质/Python 实施流程 | `../.github/skills/` 中对应 Skill |

## 稳定路径

- 项目：`D:/UE5projects/Demo`
- `.uproject`：`Demo.uproject`
- Runtime 模块：`Source/Demo`
- UE 资产：`Content`（编辑器内 `/Game`）
- AI 沙盒：`claude`
- 热上下文：`.ai-context`
- 长期记忆：`memory-bank`
- MCP 配置：`.vscode/mcp.json`

工具数量和底层 Action 会随插件版本变化。AI 应实时查询，不要依赖文档中的旧数量。
