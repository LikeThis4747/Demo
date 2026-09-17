# 当前错误与待定位现象（2026-09-16）

- 无当前C++构建或本轮BP编译错误；DLL占用LNK1104已解决，攻击数学自动化1/1通过。
- 本轮Level0 PIE出现多次HeavyImpact accepted preparation timed out及恢复兜底警告；临时拉开追猎者位置后出现两次LogPursuerAttack攻击事务超时安全清理。尚未定位根因/与行为树迁移的因果，不声称命中或手感已修复。
- 日志claude/artifacts/2026-09-16-pursuer-bt-runtime-relevant.log；分支切换已用调试器截图验证，不能把这些警告直接解释为树卡死。
- 08:02黑板自循环警告早于本次PIE，当前Parent=None，本次未新增同类警告。工具在PIE临时改Transform时输出GetCurrentLevel不支持PIE错误但变换成功；后续优先完整Transform并复核作用域。
- PIE已停止，LogBehaviorTree恢复Display，未修改关卡。仍需联合边界和手感验收。
