# AI 记忆、任务与交接工作流

## 1. 三层信息模型

| 层级 | 内容 | 位置 | 默认读取 |
|---|---|---|---|
| L0 启动 | 不可违反的规则、入口导航 | `AGENTS.md` | 是 |
| L1 工作 | 当前任务、任务卡、错误、交接 | `.ai-context/`、`claude/tasks/`、`claude/handoffs/` | 仅相关项 |
| L2 记忆 | 项目事实、架构、进度、决策、每日记录 | `memory-bank/` | 搜索后按需读取 |

不要把聊天流水写进长期记忆。长期记忆只保存未来仍有用、能改变后续决策的信息。

## 2. 记忆职责

| 文件 | 内容 |
|---|---|
| `.ai-context/current-task.md` | 主任务的一页恢复摘要：目标、阶段、阻塞、下一步 |
| `.ai-context/latest-error.md` | 当前仍有效的错误；解决后标记结论或清空 |
| `memory-bank/activeContext.md` | 当前迭代目标、活跃任务索引、近期决定 |
| `memory-bank/projectbrief.md` | 稳定项目定义、范围和非目标 |
| `memory-bank/progress.md` | 里程碑和功能完成状态 |
| `memory-bank/techContext.md` | 引擎、模块、插件、构建和性能配置 |
| `memory-bank/systemPatterns.md` | 已验证并应重复使用的架构模式 |
| `memory-bank/decisions.md` | 重要决策、理由、替代方案和影响 |
| `memory-bank/daily.md` | 按日期倒序记录完成、验证和遗留事项；每日只写一次或少量汇总 |

所有 `.ai-context/` 和 `memory-bank/` 内容必须通过 Memory MCP 读写，以获得原子写入、备份和容量限制。

## 3. 任务卡与并行 AI

非简单任务使用 `claude/templates/TASK_TEMPLATE.md` 创建：

`claude/tasks/active/TASK-YYYYMMDD-NNN-简短名.md`

开始编辑前填写：

- 唯一任务 ID、Owner（AI/会话标识）、状态。
- 目标、验收条件和明确非目标。
- 允许修改的文件/目录和预计共享依赖。
- 当前检查点、验证结果、阻塞与下一步。

并行规则：

1. 只并行彼此独立的任务。
2. 文件范围重叠时，后认领者不得直接修改；先拆分范围或完成交接。
3. 每个 AI 只更新自己的任务卡；共享长期事实由完成任务的 AI 汇总写入 Memory MCP。
4. 任务卡不是长期记忆。完成后将有价值结论写入 progress/decisions/systemPatterns/daily，再把卡移入 `claude/tasks/archive/`。

## 4. 交接

换 AI、换会话或主动暂停时，在 `claude/handoffs/` 创建交接文件。交接必须可执行，不复制整段聊天：

- 当前目标和完成度。
- 已修改文件与关键决定。
- 已运行的验证及结果。
- 未解决问题、风险、精确下一步。
- 接手前必须读取的最少文件。

接手者先核对文件和运行状态，再继续；不得假设交接内容仍然正确。

## 5. 每日记录与任务完成

当天发生实质工作时，在会话结束前向 `daily.md` 当天标题下追加一条简短记录：

- 完成了什么。
- 如何验证。
- 形成了什么决定。
- 留下什么问题及下一步。

功能完成后同步更新 `progress.md`；架构决定写 `decisions.md`；可复用做法写 `systemPatterns.md`；不要只写每日流水而漏掉权威状态。

`daily.md` 接近容量上限时，把较早月份压缩成月度摘要；热上下文不加载完整日报，只在追溯历史时搜索。

## 6. `claude/` 工作区与清理

| 目录 | 用途 | 保留策略 |
|---|---|---|
| `claude/tasks/active` | 活跃任务卡 | 任务期间 |
| `claude/tasks/archive` | 已完成任务卡 | 可定期清理 |
| `claude/handoffs` | 临时交接 | 接手确认后可归档/清理 |
| `claude/scripts` | AI 创建的辅助脚本 | 有复用价值才保留 |
| `claude/scratch` | 临时文本、试验和中间结果 | 默认可清理 |
| `claude/artifacts` | 临时报告、截图等产物 | 交付后可清理 |
| `claude/templates` | 任务与交接模板 | 长期保留 |

AI 可以清理自己在 `claude/` 内创建且已无用的文件。用户文件、项目源码、资产和来源不明的文件仍需先征得许可。

## 7. 上下文控制

- 启动时只读 `AGENTS.md`、当前任务和 activeContext。
- 按任务选择一个 Skill；不要预读全部 Skills、MCP 手册或历史日报。
- 长文档先搜索标题/关键词，再读取相关段落。
- activeContext 保持一页以内；完成或过期内容移出热上下文。
- 文档冲突时以真实代码/资产和最新验证为准，并修正文档。

## 8. 夜间无人值守模式

夜间自动任务只允许“只读审计 + 受控记忆维护 + AI 报告 + Git 快照”。

### 绝对红线

- 不得修改、删除、移动或重命名任何项目代码、蓝图资产、Content、Config、Source、Plugins、`.uproject`、Build.cs、项目规范和 AI 规则。
- 不得自动实施优化、修复编译错误、重构目录或调整资产配置。
- 不得执行清理工程、删除缓存以外的任何删除；即使文件看似无用也只报告，不处理。
- 只允许写入：Memory MCP 管理的记忆文件、`claude/artifacts/nightly/` 下的新报告，以及 Git 元数据。

### 每晚任务

1. 通过 Memory MCP 检查并精简热上下文；不删除历史，只压缩重复或失效内容。
2. 更新 `daily.md`：今日完成、验证、明日建议、未解决问题。
3. 将 AI 自己的经验压缩为最多三条；只有可复用且已验证的经验才进入 `systemPatterns.md`。
4. 只读检查代码和可通过 MCP 读取的蓝图/资产，输出按优先级排列的下一步建议。
5. 考虑三周工期：优先推荐新玩法内容、交付阻塞项和高回报改进；低价值重构放到“可延后”。
6. 对白天已有改动执行一次 Git commit 和 push；不得为了让提交成功而修改项目文件。
7. commit/push 失败时，在夜间报告和 daily 中记录准确原因，并在下一次用户会话首先提醒。

### 夜间报告

创建 `claude/artifacts/nightly/YYYY-MM-DD.md`，只包含：

- 今日完成与验证。
- Git commit/push 结果和提交哈希。
- 明日最优先任务。
- 新内容/玩法建议。
- 优化建议（阻塞交付、高回报、可延后）。
- 最多三条精炼 AI 经验。
