# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源。

## 当前焦点

HeavyImpact 倒地起身恢复桥已具备当前 DLL、自动化和真实恢复日志；下一步只收口玩家/追猎者画面、堵塞边界与追逐恢复，不继续扩展新机关或公共抽象。

## 已确认

- 真实 Chaos 接触仍决定角色位移，角色侧不增加 `AddImpulse`/`LaunchCharacter`；旧追猎者局部受击路径保留但运行停用。
- 玩家与追猎者项目 AnimBP 均继承 `UHeavyImpactAnimInstance`，当前加载状态 `UpToDate`；现有 HeavyImpact 自动化 5/5 成功。
- 现有日志已覆盖玩家与追猎者 `committed → Downed → recovery completed`，也记录了少量无可信 Capsule Sweep 起点的恢复阻塞。
- 当前磁盘 C++ 新增倒地二次接触最小 Chaos 冲量门槛，并调整恢复 Capsule 路径起点；DLL 时间晚于源码。

## 当前门槛

1. 玩家 AnimBP、追猎者 AnimBP 与 Level0 当前为编辑器内脏状态；先由用户决定保存或放弃，夜间任务不得代为保存。
2. 用户验收正躺/趴倒、墙边/墙角、完全堵塞与解除堵塞、倒地二次接触、Montage 混合和控制恢复。
3. 在有 Recast 的正式追逐环境确认追猎者起身后恢复追逐；Level0 当前 Recast 缺失日志不能替代该验证。
