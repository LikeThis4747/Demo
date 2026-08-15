# 当前任务

- 任务：摆锤纯 Cube A/B 基线验证。
- Owner：Codex /root；本次实现写入权已释放，等待用户手感验收。
- 当前资产：Level0 单一纯 Engine Cube 摆锤；220x80x150 cm；PivotHeight 610 cm；MaximumPreparationLeadTime 0.08 s；BobBody 对 Pawn 仍为 Overlap。
- 本次代码：未使用 Git 恢复文件，手工恢复 d819a1c2 的发送端 Prepare 表面间距、相对闭合速度和帧自适应预测；接收端 Heavy 状态机与 PCG 初始相位功能未改。
- 验证：DemoEditor Win64 Development 完整构建成功，正式链接 UnrealEditor-Demo.dll 并写入 DemoEditor.target。
- 基线提交：b320f057d50f7587d1baf347b42cb96ab239f4bf。
- 代码提交：3e3ee56846b4b5597f441b01a4cf4f3684909c2c；已推送 origin/main 并核验远端同哈希。
- 待办：用户在原复现点确认是否仍有“穿过身体后再击飞”；未验收前不实施新的分层碰撞方案。
- 排除：两份 StandingImpact 资产、DOC/README.md、已删除 PPT 与既有 memory-bank 改动未纳入提交。
