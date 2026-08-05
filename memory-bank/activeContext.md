# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源；此处只保留迭代焦点、决定与证据。

## 当前迭代焦点

完成玩家/追猎者共享重冲击原型的可运行闭环：完整 DLL 链接、PCA/DA/Blueprint 装配、自动化、PIE 与用户画面验收；未通过前不扩展 PCG 投放。

## 当前决定

- 保持 B 路线：预测 Prepare → 全身模拟 → 真实 Chaos 接触 → Flight/Settling → Downed；角色侧不制造第二份冲量。
- 玩家与追猎者各用一份骨架专属 PCA，共享 C++ 状态组件；旧追猎者局部受击保留为 dormant 回退路径。
- 首版不做起身动画；角色保留真实落点，Downed/恢复对正式一局的语义需在 PIE 中确认。
- 摆锤机关房型与 PCG 投放在重冲击验收后讨论；当前只保留独立 Level0 原型。

## 当前证据

- 当前 HEAD 前的白天实现提交为 29e900bd0e9ad3bd5eab0c02d8bb7a8914f915bd；工作区在夜报/记忆写入前干净。
- UHT 与 Demo 模块无链接编译通过；没有完整链接、HeavyImpact 自动化或重冲击 PIE 证据。
- 双 MCP 在线；Level0 打开、PIE 停止、相关资产非脏、蓝图 UpToDate。
- 当前 Editor 加载的 UnrealEditor-Demo.dll 时间为 20:25，早于 21:51 后的重冲击源码；PCA/HeavyImpact DA 不存在，当前 CDO 仍是旧模块字段。

## 当前阻塞与下一步

- PID 13728 的 Editor/Live Coding 锁住项目 DLL；用户需安全关闭 Editor，禁止杀进程、热更或触碰 D:\UE5_8 的 OIT 改动。
- 完整链接后重启 Editor；用户创建并 Compile PCA_PlayerHeavyImpact、PCA_PursuerHeavyImpact，再装配 DA/CDO/Blueprint。
- 依次运行 Demo.Physics.HeavyImpact.*、相关 GameFlow/Demo 回归和玩家/追猎者 PIE 边界矩阵。
