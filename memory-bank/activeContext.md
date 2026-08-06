# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源。

## 当前焦点

停止继续扩展孤立物理原型，先把 HeavyImpact、相机、冲锤和一次性制导机关合并为同版本的玩家/追猎者可玩验收；起身桥只在动画兼容性通过后实施。

## 已确认

- HeavyImpact 共享输入、玩家/追猎者适配、PCA/DA/BP 装配和 4 项自动化已有完成记录；真实 Chaos 接触决定位移，角色侧不补 `AddImpulse`/`LaunchCharacter`。
- 摆锤保持无 Actor Tick；冲锤和制导弹体只在运动阶段开启 PrePhysics Tick，等待/预警使用 Timer。
- 自动冲锤已有完整循环与真实接触证据；玩家手感、追猎者通过和墙边二次碰撞仍待验收。
- 相机更新依赖和 SpringArm 位置延迟已落盘；玩家视觉对照仍待验收。
- 壁挂式制导机关已完成静态实现与低顶 L 形测试区，尚无 PIE 触发、命中、首碰失导和反弹证据。
- 起身动画已导入/重定向，但 `Recovering` 未实现；玩家实际 Mesh 与 `ABP_Unarmed` Skeleton 兼容性是首要运行门槛。

## 当前门槛

1. 用当前 DLL 在 Level0 统一复测迟到预测、FreeFallback、空中硬超时、贴墙/斜坡/角落和连续碰撞。
2. 完成制导机关与冲锤的玩家/追猎者运行验收，以及 HeavyImpact Physics Control/纯 ragdoll、相机 40/60/100 cm 对照。
3. 兼容性通过后实现真实倒地点起身；不得瞬间立正或恢复到受击前 Transform。
4. 三个机关稳定后只选择一个接入正式一局/PCG，再评估预测公共层抽取。
