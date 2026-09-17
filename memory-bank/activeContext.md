# Active Context

## 当前工作（2026-09-16）

追猎者行为树最小迁移已完成代码和资产接入，标准C++构建、BP编译、攻击数学自动化1/1通过。用户创建BT/BB，官方MCP修正TargetActor基类为Actor并装配DA；三个资产无Dirty。仍待联合边界与用户体验验收，不标记任务完成。

- 根Selector挂服务，依次普攻/大跳/追击。黑板TargetActor、CanCloseAttack、CanJumpAttack及SelfActor均未InstanceSynced，攻击装饰器IsSet/ResultChange/LowerPriority。服务实际读取DA周期0.1s。
- PIE调试器观察到玩家目标、攻击条件false仍保持任务、追击→大跳→普攻。保持220cm重叠边界、共用冷却3.5s、原动画/恢复/受击/脱困规则；原三项攻击问题尚未修复。
- PIE已停止，日志级别恢复Display，关卡无修改；临时Actor位置只在PIE副本测试。官方set_actor_transform缺省字段实测重置scale/rotation，之后显式保持scale1.2复测，后续必须传完整Transform。
- 未验证完：精确220cm、楼层/坡面、受击取消、低帧率下脱困累计与原Timer节奏。运行出现Heavy准备超时/恢复兜底，临时位置测试后出现攻击事务超时，因果未定位。
- 用户已确认仅本会话实现、旧占用失效；本次GitHub远端覆盖旧工蜂规则，基线dcf6248已推送并核验。新增实现未提交，当前等待联合验收不占用实现权。
- 恢复入口：本任务卡与handoff；DOC/DailyPlan/2026-09-16-追猎者行为树最小迁移.md；claude/artifacts/2026-09-16-pursuer-bt-final-validation.json与final-branch截图。
