# 待整理规则收件箱（非权威）

只用这一份文件暂存用户随时提出、尚未归类的工作规则，不为每条规则创建文件。这里的内容不视为正式规范；确认稳定后合并进 `AI_WORKFLOW.md`、`PROJECT_ARCHITECTURE_RULES.md`、`AI_Coding_Guide.md` 或 `GIT_INTERNAL.md`，并删除已整理条目。玩法决定和代码方案不放这里。

## 2026-07-21

- AI 代码审计规范：评审结果统一放 `claude/reviews/`，每份 `YYYY-MM-DD-主题.md` 须注明审计日期与被审代码的 git 基线（提交哈希+时间，新代码通常未提交，取上一版本）。措辞面向 AI，简洁一针见血、不写客套，按"阻塞交付/高回报/可延后"分优先级；无问题就直说，不凑数。
  流程：写代码 AI 改完 → 评审 AI 出报告 → 用户让写代码 AI 去看 → 写代码 AI 自主抉择采纳（建议非必办）→ 处理完由写代码 AI 归档。写代码 AI 被要求查看时只读最近一份未加 `Done-` 前缀的报告，不通读历史。处理完后由写代码 AI（非评审 AI）把文件改名加 `Done-` 前缀并在开头标注"已完成，仅存档"；评审 AI 不得预先标记完成。
  参考优秀项目：`D:\UE5projects\LyraStarterGame`、`D:\UE5projects\ue5-warrior`，经验总结在 `claude/reviews/references/`，按三周单机无联机 Demo 规模裁剪，勿套大型工程冗余分层。（稳定后并入 `AI_WORKFLOW.md`。）
