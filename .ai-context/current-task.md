# Current Task

- 当前状态：两张 active 卡并存。HydroLab V3 积木、兼容链和完整组合已保存并通过重载/静态/短时 Simulate 检查，等待用户视觉、玩家连续实走与 AI/NavMesh 验收；SFCorridors 仍是只读筛选，未获删除授权。
- HydroLab 证据边界：Level0 可由本地 UE MCP 读取 V3 Outliner；日志明确缺少 RecastNavMesh，因此不能宣称玩家胶囊连续通行或 AI 可达，也未转入 Runtime WFC。
- 追猎者新改动：近战攻击/方向受击蒙太奇、受击停顿、AttackApproachRadius、限时 AttackProjectile Tag 和 Camera 通道忽略已落盘；相关 BP/AnimBP 为 UpToDate，但当前 C++ 未构建、未跑 PIE、未做连续命中或重复投掷验收。
- 代码风险：同一物体在旧 Tag Timer 到期前再次投掷时，旧 Timer 可能提前移除新一轮 AttackProjectile Tag；先白天复现，不自动修复。
- SFCorridors 边界：HydroLab 保持 PCG 主结构；首批候选仍为 `SM_LampWallL` 与 `SM_comp`。295 个 LFS 资产不删除，后续先做 Asset Registry 依赖闭包和精确清单，再请求授权。
- 当前最短可玩下一步：先为 Level0 建立 Recast 并让玩家/追猎者走完 V3 组合，再完成“生命归零 → GameFlow 失败 → 同 Seed 重开”；随后对追猎者新攻击/受击链做构建、PIE 与至少 10 次命中验收。
