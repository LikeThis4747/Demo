# Decisions — Demo

## ADR-001：C++ 优先
核心玩法使用 C++；蓝图只做配置与表现，以降低 AI 蓝图调试成本。

## ADR-002：三层 AI 上下文
AGENTS 为短入口；任务卡/交接为工作层；稳定事实、决策和日报进入 Memory MCP。

## ADR-003：文件级并行认领
非简单任务声明 Owner 和修改范围；重叠范围不得并行。

## ADR-004：三周交付优先
优先玩法闭环、新内容和高回报改进；低价值重构延后。

## ADR-005：夜间只读安全模式
无人值守时禁止修改/删除项目代码、资产、配置和规范，只允许记忆、报告与 Git 快照。

<!-- written by shiqiqiwang at 2026-07-23 08:25 UTC -->

## ADR-006：运行时 PCG 与难度不变量
PCG 必须“实时、非工具”：最终打包游戏在每局开始时根据 Seed 和设置中选择的难度现场生成完整关卡，不以 Editor 预烘焙或固定地图随机摆放替代。难度在一局内固定；困难不明显延长目标单局时间；所有难度都限制长距离回头路。具体混合 WFC/Socket 架构仍以用户确认 V1 后为准。

<!-- written by shiqiqiwang at 2026-07-24 02:42 UTC -->

<!-- written by Codex at 2026-07-24 UTC+8 -->

## ADR-007：项目关卡统一归档到 Levels
可运行关卡与 PIE 测试关卡统一放在 `/Game/Levels`。功能目录如 `/Game/ZeroEscape/Generation` 只保存生成器、DataAsset、Presentation 和可复用包装资产，不在其中建立 Maps 子目录。当前 PCG 测试关卡为 `/Game/Levels/L_PCG_RuntimeTest`。
