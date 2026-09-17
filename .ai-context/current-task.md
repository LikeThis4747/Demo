# 当前任务

- 2026-09-16：已授权追猎者AI最小行为树迁移；保持攻击参数、动画、组件和恢复时序，三项手感问题之后逐项最小修复，不做绕过。
- 五个源码文件已实现，15:47标准DemoEditor构建成功。用户已创建BT_Pursuer/BB_Pursuer；20:06前官方MCP核对树、修正TargetActor基类为Actor并装配DA_Pursuer.BehaviorTree，三资产保存且无Dirty。
- 已验证：BP_Pursuer编译成功；已有攻击数学自动化1/1通过；Level0 PIE中真实执行追击→大跳→普攻，条件false时攻击任务继续，黑板玩家正确，BT检查周期0.1s。证据claude/artifacts/2026-09-16-pursuer-bt-final-validation.json、final-branch-0..3.png等。
- 当前PIE已停止，LogBehaviorTree恢复Display；临时位置仅改PIE副本，关卡未改。官方ActorTools.set_actor_transform省略scale/rotation会重置，复测已显式保持scale1.2，后续需完整Transform。
- 待办：用户体验和精确220cm/上下层坡面/受击中断/低帧率时序联合验收。运行发现Heavy准备超时/恢复兜底，临时拉开位置后还有两次攻击事务超时清理；未证明根因或与迁移因果，不宣称手感已修复。
- 低帧率下BT服务固定ThinkInterval累加脱困时间的节奏尚未与原Timer对照，未额外改代码。
- 用户确认单会话、旧占用失效。本次GitHub origin覆盖旧工蜂规则，基线dcf624852b4394c787856bf476cdaf43a0390d4b已push并核验；新代码和资产未提交。等联合验收时不占用实现权。
- 恢复：TASK/HANDOFF-20260916-001-AI行为树迁移与攻击手感诊断；正式方案DOC/DailyPlan/2026-09-16-追猎者行为树最小迁移.md。未验收，不归档。
