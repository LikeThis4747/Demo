# 2026-08-14 HeavyImpact 不可回滚与有界恢复

## 目标与边界

- 摆锤、冲锤只有在真实锤头盒体短时 Sweep 确认将撞到角色 Mesh 后才请求 Heavy。
- 接收端返回 `Accepted` 后不得恢复受击前动画；真实 Hit 或准备期限到达都进入同一 Heavy 物理流程，不补人工冲量。
- 保留自然飞行、落地和滚动，不设置物理阶段总时长上限；身体获得地面支撑并持续降到低能量后才进入 Downed。
- Downed 睡眠后立即尝试起身；安全站位最多阻塞 3 秒。届时先以受击前 Capsule 位置为种子做一次有界安全搜索；仍无解时回到 Heavy 准备前记录的有效角色外壳变换，跳过起身动画并恢复操作，禁止继续卡在 Downed。
- Snapshot 到起身动画的淡入改为 0.30 秒。
- 不修改 Light、追猎者攻击、Heavy Physics Control Asset、AnimBP、起身动画、关卡、制导投射物或其他机关；不新增组件、接口、状态或资产类型。

## 文件与资产范围

- `Source/Demo/Public/Components/Physics/HeavyImpactResponseComponent.h`
- `Source/Demo/Private/Components/Physics/HeavyImpactResponseComponent.cpp`
- `Source/Demo/Public/Data/Physics/HeavyImpactTuningData.h`
- `Source/Demo/Private/Data/Physics/HeavyImpactTuningData.cpp`
- `Source/Demo/Private/Actors/Hazards/BatteringRamHazard.cpp`
- `Source/Demo/Private/Actors/Hazards/PendulumHazard.cpp`
- `Source/Demo/Public/Data/Hazards/BatteringRamHazardTuningData.h`
- `Source/Demo/Public/Data/Hazards/PendulumHazardTuningData.h`
- `Source/Demo/Private/Data/Hazards/PendulumHazardTuningData.cpp`
- `Source/Demo/Private/Physics/Tests/HeavyImpactResponseTests.cpp`
- `/Game/ZeroEscape/Physics/HeavyImpact/DA_PlayerHeavyImpact`
- `/Game/ZeroEscape/Physics/HeavyImpact/DA_PursuerHeavyImpact`
- `/Game/ZeroEscape/Hazards/Physics/Data/DA_BatteringRamHazard_Default`
- `/Game/ZeroEscape/Hazards/Physics/Data/DA_PendulumHazard_Default`

## 实施顺序

1. 完整基线 commit/push 并核对远端与干净工作区。
2. 收紧摆锤、冲锤预测为真实盒体对接收 Mesh 的短时 Sweep；候选体积不再直接决定 Heavy。
3. 将 Prepared 改为 `Accepted` 后不可回滚；删除正常误预测回滚，只保留返回 `Accepted` 前的原子转换失败恢复。
4. 删除 Settling 抢跑起身及 5/10 秒正常硬切；稳定进度对轻微噪声衰减，对离地或明显滚动清零。
5. Downed 立即尝试起身，0.20 秒重试、3.0 秒截止；截止后以受击前 Capsule 位置为种子做最后一次有界搜索。仍无解时回到 Heavy 准备前已验证有效的角色外壳变换，跳过起身动画恢复操作，不再进入无界重试或永久 Downed。
6. 更新 DataAsset、自动化测试、构建并通过官方 MCP 回读保存结果。

## 验证与回退

- 自动化：更新 Heavy 调参合同并重跑全部 `Demo.Physics.HeavyImpact.*`。
- 构建：`DemoEditor Win64 Development`。
- PIE：摆锤/冲锤正撞与擦边、奔跑/跳跃玩家、长滚动、小幅抖动、墙角阻塞、面朝上/下面起身、Downed 二次命中，覆盖 30/60/120 FPS。
- 任一 Accepted 后出现 `Prepared -> Inactive`、正撞漏触发、擦边误触发、3 秒后仍不起身、起身穿入静态几何或既有 Heavy 画面明显退化，均视为阻塞。
- C++ 与四份 DataAsset 独立提交；失败时整体回退该提交，不保留兼容开关或旧路径。
