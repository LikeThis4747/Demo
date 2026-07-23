# Active Context — Demo

## 当前阶段

三周 Demo 当前以实时、非工具性 PCG 整关生成作为第一优先。用户已确认 V2.1 并授权实施；现在先完成实施前 Git 快照并推送到内部工蜂，成功后进入源码落盘。

## 活跃任务

- TASK-20260723-002：PCG 整关场景 V2.1，已授权；处于实施前快照门禁。
- TASK-20260723-003：Demo MCP 可用性与蓝图访问诊断，独立任务。
- TASK-20260723-004：SFCorridors 动态光照与性能验收，独立任务。

## 稳定决定

- Runtime 每局现场生成，一局内难度、Seed 和布局固定；困难不靠明显延长关键路线或长距离回头路。
- 宏观流程不交给 WFC：Progression/Spatial Graph 先定主路/支路/前向汇合，Special Socket 与 A* 保证必需结构，有限 WFC 只选择局部模块。
- 首版 Flow 暴露 EscapeOnly、CollectAll、CollectKOfN；K-of-N 的 N 上限 12。
- WFC 采用 Support Count + 删除事件，完整 Domain 快照作为首版正确性基线；Active 256、Variant 64、实时快照 16 MiB、单 Attempt 累计复制 64 MiB。
- SFCorridors 是当前素材输入，但通过项目 Module Catalog + Presentation Profile 隔离；第三方资产保持只读。
- Transform Test 0、WFC 慢速 Oracle、多层回溯 Oracle、实例化 rollback 与重复 Generate 都是实施门禁。
- 同步优先，先测分阶段 P50/P95；数据证明后才评审 Trail、工作线程或固定数量分批实例化。
- 源码集中在 Public/PCG 与 Private/PCG；职责真实扩大后才拆分子目录。

## 权限与验证边界

- 用户已授权 PCG Source 和项目自有适配资产实施。
- 实施前必须先把当前工作区快照提交并推送到 `git@git.woa.com:shiqiqiwang/Demo.git`。
- SFCorridors 第三方目录只读，不直接修改。
- 静态评审通过不等于 UHT、Build、Automation、Blueprint、PIE 或用户验收通过。

## 下一步

完成实施前内部 Git 快照；随后按 Transform Test 0/基础 Types → Profile/Catalog → Socket/A* → Support-count WFC → Runtime 生命周期 → Automation/PIE 实施，并只适配 PIE-A 所需的最小 SFCorridors 模块集。
