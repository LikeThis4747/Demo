# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

最短可玩闭环已从“源码骨架”推进到已有白天构建、真实资产装配与单 Seed 开局日志证据；下一门槛是玩家完成菜单→游戏→Exit/死亡→结算或重开的整局验收。

## 当前索引

- GameFlow/UI：主菜单、GameInstance、正式 GameMode 与 WBP 已落盘；四个相关蓝图 UpToDate，L_Game 的 DefaultPawn、Generator 与 Populator 关键字段已通过官方 MCP 读取。
- PCG/Population：Seed 12345 的现有日志显示一次生成、4 个地刺、8 个磁性物体；不替代 18 项 Demo.PCG、至少 10 Seed、Recast 或真实追猎者验收。
- HydroLab：V5 静态原型与三层楼梯塔保留，继续等待玩家、Recast 与真实追猎者验收，不抢占最小一局关键路径。
- SFCorridors：仅保留只读筛选任务，资产退场仍需依赖闭包、精确清单和用户授权。

## 当前边界与风险

- PlayerStartSeparationCm=1200 cm，但最新成功日志实际 separation_cm=1138；需核对碰撞调整和实际出生公平性。
- 尚无当前整局胜负/重开验收，也未重建 18 项测试、至少 10 Seed 与导航回归基线。
- 软件 Lumen GI + SSR 配置保持 r.DynamicGlobalIlluminationMethod=1、r.ReflectionMethod=2；r.VirtualTextures=False 已进入白天提交，低配无 Lumen 室内补光仍待同机验证。
