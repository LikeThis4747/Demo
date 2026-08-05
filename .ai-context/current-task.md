# Current Task

- 当前任务：完成玩家/追猎者共享重冲击物理受击原型的完整链接、资产装配、自动化、PIE 与用户画面验收。
- 当前阶段：C++、摆锤 ETA 预测和三组自动化测试源码已落盘；UHT/Demo 模块无链接编译通过，功能尚未形成可运行闭环。
- 实施前可回退基线：1c50616c8f19cfa5daa62d39fd626c1c11ff7310，已推送内部工蜂并核验。
- 当前阻塞：PID 13728 的 UnrealEditor 正在运行并占用旧 UnrealEditor-Demo.dll；用户需安全关闭 Editor 后再做完整链接，禁止杀进程、热更或触碰 D:\UE5_8 用户 OIT 改动。
- 当前资产证据：两份 PCA 与 HeavyImpact DA 不存在；Editor 加载的旧 CDO 没有新 HeavyImpact 字段，追猎者仍显示旧局部受击组件。
- 下一步：完整链接 → 重启 Editor → 用户创建并 Compile 两份 PCA → 官方 MCP 装配 DA/CDO/Blueprint → HeavyImpact 自动化/回归 → 玩家与追猎者 PIE 边界矩阵 → 用户验收。
- 关键合同：锤体与模拟 Mesh 的真实 Chaos 接触产生击飞；角色侧不补人工冲量；PCA 只提供有限 Parent-space 角向姿态约束；角色保留真实倒地点。
- PCG/机关边界：当前摆锤仅是 Level0 独立原型，未投放 PCG Population；机关房型与“一次迟滞后 AI 必通”规则待运行验收后讨论。
