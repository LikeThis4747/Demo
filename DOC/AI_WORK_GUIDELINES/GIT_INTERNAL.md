# 内部 Git 仓库使用指南

本项目使用腾讯内部工蜂（TGit）作为远程仓库。

## 仓库信息

| 字段 | 值 |
|---|---|
| 平台 | 工蜂（git.woa.com） |
| 仓库地址（SSH） | `git@git.woa.com:shiqiqiwang/Demo.git` |
| 仓库地址（HTTPS） | `https://git.woa.com/shiqiqiwang/Demo.git` |
| 仓库主页 | https://git.woa.com/shiqiqiwang/Demo |
| 用户名 | shiqiqiwang |
| 可见性 | Internal（公司内可见） |
| 主分支 | main |

## 首次克隆

```bash
git clone git@git.woa.com:shiqiqiwang/Demo.git
cd Demo
```

> 需要提前在工蜂账号中配置本机 SSH 公钥：https://git.woa.com/profile/keys

## 日常推送

```bash
git status --short
git add -- path/to/owned-file
git diff --cached --name-only
git commit -m "你的提交信息"
git push origin main
```

并行任务存在时禁止用 `git add .` 兜底；必须按任务拥有的明确路径暂存，并在 commit 前核对暂存区文件清单。

## AI 执行 Git 操作须知

- 所有 commit/push 操作默认以 `git@git.woa.com:shiqiqiwang/Demo.git` 为目标。
- 执行推送前，检查 `git remote get-url origin`，确认指向工蜂。
- 若 remote 不正确，先执行 `git remote set-url origin git@git.woa.com:shiqiqiwang/Demo.git` 再推送。
- 如需创建新的远程分支，通过工蜂网页或 UGit 客户端操作。

### 代码实现前的基线门禁

- 同一时刻只允许一个会话持有实现文件写入权。用户授权实现后、首次修改任何会影响构建、运行或编辑器行为的代码、资产、配置、构建/测试文件前，先记录完整 `git status --short` 与当前 HEAD，并确认没有其他会话正在写实现文件。
- 全部已确认的既有实现改动必须形成完整 commit。该基线 commit 必须成功 push 到当前获准的内部远端分支；push 后核对远端分支已包含记录的提交哈希，不能只以命令返回成功代替远端核验。
- 若实现范围和其他实现文件均无既有改动，且当前 HEAD 已在获准远端，可直接记录该 HEAD 为实现基线，不要求为了清空无关文档改动再创建空提交。
- 已知 Owner、路径明确且不与实现范围重叠的纯文档改动可以保持未暂存，不阻塞实现，也不要求全局 `git status --short` 为空。实现 Owner 必须把这些路径记入任务卡，且不得修改、暂存或提交它们；commit 前使用 `git diff --cached --name-only` 与按路径状态核对边界。
- 若存在来源不明的改动、其他会话持有的实现改动、当前分支没有获准远端，或 commit/push/远端核验失败，不得开始或继续功能实现；先由对应 Owner 与用户协调。
- 该门禁只授权为即将开始的已批准实现建立基线，不授权提交或推送无关、未确认的修改。基线后新增的无关纯文档改动不触发重建基线；任何实现文件出现非当前 Owner 的新改动则立即停止写入。

## 夜间自动任务

夜间 Git 快照（commit + push）遵循 `DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md` 夜间红线，推送目标同样为工蜂仓库。
