# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源。

## 当前焦点

HeavyImpact “物理姿势 → 起身动画开头”连续过渡已完成代码、AnimBP、参数与技术验证；下一步只做用户真实命中画面验收和必要的小范围参数修正，不扩展新受击或机关框架。

## 已确认

- 真实 Chaos 接触仍决定角色位移、翻滚与最终倒地点；角色侧没有 `AddImpulse`、`LaunchCharacter`、WorldSpace 拉拽或骨盆线性动画驱动。
- 玩家与 AI AnimBP 都在现有 Slot/Downed Snapshot 前接入准备目标分支，官方 MCP warnings-as-errors 编译保存成功。
- 准备阶段默认 0.40 秒，ParentSpace 角向强度/最大扭矩从 Landing 配置的 30% 平滑升到 100%，阻尼保持不变。
- Demo 模块最终构建成功；`Demo.Physics.HeavyImpact.*` 5/5 通过。
- Level0、四条起身动画、玩家 BP、官方 `ABP_Unarmed`、Physics Asset/PCA 与所有机关未改。

## 当前门槛

1. 用户验收玩家/AI 正躺、趴倒、开阔地、墙边、墙角、堵塞解除、准备中再次受撞，以及起身后控制/追逐恢复。
2. 重点比较 `demo.HeavyImpact.RecoveryPosePreparation 1/0`：是否能看到肢体逐渐收拢，是否消除 Montage 开头闪切，且不明显缩短击飞距离。
3. 如果画面有问题，首轮只调准备时间、初始控制比例和正/反面采样时间；不同时改机关速度、冲击力、PCA 拓扑或动画资源。
4. 当前工作区还有另一任务的壁挂式制导机关 Review 文件；本任务不得读取、修改或并入提交，最终全工作区干净门禁需等其 Owner 收口。
