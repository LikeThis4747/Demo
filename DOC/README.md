# DOC 目录

| 目录 | 用途 |
|---|---|
| `AI_WORK_GUIDELINES/` | AI 工作规范；入口见该目录 `README.md` |
| `Design/` | 唯一现行策划案（`GAME_DESIGN.md`）、专题设定、开发排期，以及 `PCG/` 下的正式生成规则和素材契约 |
| `Ideas/` | 前期创意、候选方案与评审记录；按需读取，不加入启动上下文 |
| `DailyPlan/` | 用户确认后的当日实施方案；根目录只放正在推进的，完成后移入 `DailyPlan/archive/` |
| `DailyReport/` | 按日期保存的阶段或日报 |
| `Bugs/` | 尚未解决、暂停调查或等待验收的 Bug 记录与索引 |
| `Outputs/` | 可复用的工具与阶段产出说明；MCP 工具记录放在 `Outputs/MCP/` |

新增文档必须放入用途匹配的子目录；没有合适分类时创建名称明确的新目录，并在此表增加一行。`DOC/` 根目录只保留本目录文件。

## DailyPlan 归档约定

DailyPlan 完成或被后续主线取代后，**移入 `DailyPlan/archive/` 子目录并加 `Done-` 前缀**（如 `archive/Done-2026-07-22-xxx.md`），文件顶部保留一行归档 banner：

`> 状态：已完成并归档，仅作历史留痕，AI 无需主动阅读（当前主线为 …）。`

- `DailyPlan/` 根目录只保留正在推进的计划；AI 启动或检索时只看根目录，不主动阅读 `archive/` 内的历史计划，只有追溯特定历史决策时才按需打开。
- 移动文件时同步更新其它文档（任务卡、交接、Bug 记录等）里的路径引用，避免断链。
- 该约定与 `claude/reviews/`（完成报告移入 `reviews/archive/` 并加 `Done-` 前缀）保持一致。
