# TASK-20260718-001 — 优化 AI 工作流

- Owner：Codex 当前会话
- Status：done
- Created：2026-07-18
- Updated：2026-07-18

## 目标与验收

- 目标：建立精简入口、分层记忆、多 AI 并行/交接、每日日志和可清理 AI 沙盒。
- 结果：完成。

## 完成内容

- 将 AGENTS、Copilot 指令和 AI 工具索引压缩为短入口与按需路由。
- 建立 `claude/` 任务、交接、脚本、临时文件、产物和模板目录。
- 建立 task card 的 Owner/文件范围认领机制与交接模板。
- 重置错误带入的 Warrior 旧记忆，新增 decisions 和统一 daily 日志。
- 添加 Claude 入口和 Copilot 路径级 C++/资产规则。

## 验证

- AGENTS 39 行；Copilot 指令 19 行；AI 工具索引 37 行。
- AI 工作区目录和模板齐全。
- `.ai-memory/config.json`、`.vscode/mcp.json`、`Demo.uproject` JSON 有效。
- Memory MCP 新文件写入成功，旧 Warrior 项目状态已替换为 Demo 状态。
- Memory Guard 在变更前后均未超限；新 target 将在 MCP 重启后生效。

## 后续

- VSCode/Codex 重新加载工作区，使新的 Memory Guard targets 生效。
- 题目公布后创建新的 active task card。
