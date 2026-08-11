# Current Task

## 当前任务

磁力投掷物碰撞破碎 P0 冲击感修正版已完成 C++ 技术落地，等待用户在 Level0 验收真实投掷命中 AI 的力量感。统一轻受击第一版仍待现场手感验收；Light/Heavy 统一物理身体重构属于独立暂停讨论，不由本任务扩展实施。

## 当前证据

- 本次修正从已推送且干净的 ba630a5 基线开始。
- 合格 Hit 当帧冻结并修正继承运动：门槛 5000 kg·cm/s、损失法向速度保留 0.6、最大继承线速度 5000 cm/s。
- Geometry Collection 解簇后获得 350 cm/s、包围球半径 1.25 倍的叶子分离速度。
- DemoEditor Win64 Development 构建成功；官方 UE5.8 MCP 属性回读和两个 Blueprint 编译通过。
- 隔离 PIE 确认原材质保持、碎片在空中散开并按 Remove On Break 清理；临时测试 Actor 与 Level0 测试写盘变化已清理。
- 旧的破碎侧独立 Hit/CCD、旧 DeferredDisarm 和 next-tick 重读速度路径均未保留。

## 下一步

用户重点验收“正式投掷命中 AI”：接触处立即碎裂，碎片继续沿撞击方向运动并有限外散，不再悬停后垂直下落。随后补验低冲量、薄墙/角落、角色 Light 与多物体竞态；通过后归档磁力 Review、DailyPlan 与任务卡。
