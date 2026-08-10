# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源。

## 当前焦点

HeavyImpact 提前 Snapshot 单一混合已完成技术实现；当前只做玩家/AI 真实摆锤命中画面、墙角/堵塞/二次受撞和 Recast 追逐恢复验收，不扩展新受击或机关框架。

## 已确认

- 正常空间在最短物理模拟后低速且有支撑 0.10 秒即交接；当前物理姿势以 0.22 秒混入已播放的动态起身 Montage。
- 旧 PreparingPose、Sequence Evaluator、准备时间/控制比例和物理蠕动分支已删除；Blocked Downed 重试不唤醒刚体。
- 最终起身位置仍要求完整站立 Capsule；墙角可在 60 cm 内沿不穿阻挡几何的路径退让，完全封闭时持续睡眠重试。
- 玩家/AI AnimBP 引脚回读一致，两个蓝图 warnings-as-errors 编译成功；两份 DataAsset 重启回读一致且相关资产/Level0 非脏。
- HeavyImpact 自动化 5/5、短 PIE 初始化和最终 Demo 模块构建通过；没有真实命中画面证据。

## Git

- `origin` 已恢复为 `git@git.woa.com:shiqiqiwang/Demo.git`；实施 baseline `3350b78c7708ca170ecb6641d32e240d57077f34` 已推送并远端核验。

## 当前门槛

1. 用户验收玩家/AI 正躺、趴倒、开阔地、墙边、墙角、堵塞解除、二次受撞，以及起身后控制/追逐恢复。
2. 在同一正式追逐场景记录 Recast 创建、追猎路径与 HeavyImpact 恢复，并复现历史无地面支撑硬超时。
3. HeavyImpact 与追逐通过后，只从摆锤、冲锤、制导机关中选一个已验收原型接入正式一局。
