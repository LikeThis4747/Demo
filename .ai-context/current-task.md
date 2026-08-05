# Current Task

- 当前任务：完成玩家/追猎者共享的重冲击物理受击原型，并在 Editor 可安全重启后完成 PCA/DA/Blueprint 装配、自动化、PIE 与用户画面验收。
- 当前阶段：C++、摆锤预测接入和自动化测试源码已落盘；UHT 与 Demo 模块多轮 no-link 编译通过，最终一轮 4 个动作成功。完整 DLL 链接、资产作者化和运行验证尚未完成。
- 实施前可回退基线：`1c50616c8f19cfa5daa62d39fd626c1c11ff7310`，已推送到内部工蜂并核验远端一致。
- 当前阻塞：PID 13728 的 UnrealEditor 正在运行且 Live Coding 占用 `UnrealEditor-Demo.dll`；不得杀进程、热更或写临时 DLL。用户需先安全关闭 Editor。
- Editor 重启前后的人工门槛：用户创建并最终 Compile/Save 两份 PCA（`PCA_PlayerHeavyImpact`、`PCA_PursuerHeavyImpact`）；双 MCP 无 PCA Factory/Compile 入口。其余 DA/CDO/Blueprint 与摆锤 Tuning 更新优先用官方 MCP。
- 关键合同：预接管后由锤体与模拟 Mesh 的真实 Chaos 接触产生击飞；角色侧不补人工冲量；PCA 只提供有限 Parent-space 角向姿态约束；角色停在真实倒地点。
- 旧追猎者局部受击源码/调参/回调完整保留，只暂停三处运行装配；新共享组件是重冲击 Physics Control 权威。
- 详细人工步骤与未验证边界：`claude/docs/2026-08-05-重冲击物理受击实施记录与人工待办.md`。
