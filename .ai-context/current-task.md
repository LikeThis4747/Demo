# Current Task

- ID：`TASK-20260723-002`
- 目标：修正《零号逃亡》实时 PCG 的路线质量，再进入追猎者和玩法闭环。
- 已实现基线：构造性 Progression、固定中央主干、16 OpeningMask 无回溯 Grid-WFC、600→300 cm HydroLab 结构展开与 Runtime HISM；UE 5.8 构建、`Demo.PCG` 13/13、288 Seed Sweep 和 Runtime PIE 烟测通过。
- 2026-07-25 玩家验收：未通过。固定中央长直路、路线过少且生成区域没有运行时室内灯。
- 审计结论：`claude/reviews/2026-07-25-pcg-route-generation-revision-review.md` 判定方案可行并允许进入 DailyPlan；采纳删除固定主干/Optional Envelope、旧构造性见证、重复热路径逐边复核，并要求预算先测 P50/P95/Max 再冻结。
- 正式计划：`DOC/DailyPlan/2026-07-25-PCG-WFC路线重构实施方案.md`。
- 拟实现代码：`claude/docs/2026-07-25-pcg-wfc-route-refactor-code-proposal.md`。已覆盖 Types/Assets/Core、三项约束、Trail、chronological backtracking、Grid 接线、Runtime 日志和测试拆分；它只是评审稿，未写入 Source。
- 新补正确性边界：完整折叠候选若在 Grid 的通关总长或额外折返验收中超限，必须返回 Reject 并继续同一 WFC 决策栈回溯；其他最终不变量返回 Fatal。该单一完成态验收不是通用约束框架，WFC 不认识 K-of-N/房间/Plan，Grid 不接触 Domain/Trail。
- 难度边界：WFC 权重移入 Difficulty，但三个难度必须保持相同 EmptyWeight 与非空 Variant 总权重，只重新分配非空形态比例；共享 Count 与路线长度上限不随难度扩大。
- 约束边界：保留 16-bit Domain、最小熵、权重、局部传播、Progression、2×2 Objective 房内约束、K-of-N DP、最终 BFS 和结构展开；不做 Backjump、LoopConstraint、通用约束插件框架、隐藏重启或第二套房间图生成器。
- 预算口径：`MaxWfcCandidateAttempts` 每次把决策 Cell 收窄为 singleton 计一次；`MaxWfcBacktrackCount` 每次恢复一个决策帧计一次。首轮数值只是 Seed Sweep 的宽安全上限，拿到 P50/P95/Max 后才冻结。
- 测试边界：`WITH_DEV_AUTOMATION_TESTS` 纯算法回归长期保留，不并入 GameFlow；增长后拆成 WFC Solver 与 Generation Pipeline 两个 Private CPP。Runtime Harness 暂时只服务独立测试关卡，正式 GameFlow 接管后再单独申请删除。
- PIE 修正：本机 `LastExecutedPlayModeType` 已从 `PlayMode_InEditorFloating` 改为 `PlayMode_InViewPort`；以后 MCP 玩家验收显式使用 `SelectedViewport`，不向 Runtime 代码增加窗口逻辑。
- 并行边界：素材迁移正在修改 Core、Tests、Presentation 与 HydroLab 资产路径；真正实现前必须等待交接并保留 `FailCore` 修复和五项新资产路径。WFC 不触碰 Level0、Demo.Build.cs、追猎者文件或素材移动。
- 授权边界：用户本轮只要求规划并贴出拟实现代码；尚未授权修改 PCG C++、DataAsset、Blueprint、关卡或第三方素材。下一步等待用户或独立 AI 审查，再申请落盘授权。
