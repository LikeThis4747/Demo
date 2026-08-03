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
git add .
git commit -m "你的提交信息"
git push origin main
```

## AI 执行 Git 操作须知

- 所有 commit/push 操作默认以 `git@git.woa.com:shiqiqiwang/Demo.git` 为目标。
- 执行推送前，检查 `git remote get-url origin`，确认指向工蜂。
- 若 remote 不正确，先执行 `git remote set-url origin git@git.woa.com:shiqiqiwang/Demo.git` 再推送。
- 如需创建新的远程分支，通过工蜂网页或 UGit 客户端操作。

## 夜间自动任务

夜间 Git 快照（commit + push）遵循 `DOC/AI_WORK_GUIDELINES/AI_WORKFLOW.md` 夜间红线，推送目标同样为工蜂仓库。
