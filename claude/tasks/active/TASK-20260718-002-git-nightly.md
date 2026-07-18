# TASK-20260718-002 — Git 与夜间自动任务

- Owner：Codex 当前会话
- Status：active
- Created：2026-07-18
- Updated：2026-07-18

## 目标与验收

- 初始化本地 Git 并建立首个可回滚提交。
- 尽可能连接用户远程 Git 仓库并首次推送。
- 记录三周期限。
- 建立夜间只读审计、记忆整理、日报、建议和 Git 快照规则。

## 修改范围

- Git 元数据与 `.gitignore`
- `AGENTS.md`、`DOC/AI_WORKFLOW.md`、项目简报/记忆
- 夜间自动任务配置

## 红线

- 夜间任务不得删除或修改项目代码、蓝图资产、Config、规范文件。
- 夜间只可更新 Memory MCP 记忆、AI 自建报告，并执行 Git commit/push。

## 当前计划

- [x] 检查 Git/GitHub 登录和仓库状态
- [x] 写入三周期限和夜间规则
- [x] 初始化并提交本地仓库
- [ ] 创建/连接远程仓库并推送
- [x] 创建夜间自动任务
