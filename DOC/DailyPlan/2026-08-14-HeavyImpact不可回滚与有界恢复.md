# 2026-08-14 HeavyImpact 不可回滚与有界恢复

## 目标与边界

- 摆锤、冲锤只有在真实锤头盒体短时 Sweep 确认将撞到角色 Mesh 后才请求 Heavy。
- 接收端返回 `Accepted` 后不得恢复受击前动画；真实 Hit 或准备期限到达都进入同一 Heavy 物理流程，不补人工冲量。
- 保留自然飞行、落地和滚动；低线速、低角速连续 0.35 秒可提前进入 Downed，但持续 Chaos 模拟统一以 1.5 秒为上限，地面旋转、空中卡墙和贴墙抖动均不能绕过。
- Downed 睡眠后立即尝试起身；安全站位最多阻塞 1.5 秒。Heavy 飞行由 Physics Asset 刚体负责碰撞和位移，QueryOnly Capsule 只跟随骨盆供相机与角色外壳定位，不参与阻挡。起身以最终骨盆附近做现有 0/20/40/60 cm 有界搜索；仅当短线检测证明骨盆刚跨过近处静态墙体时，优先向受击前所在墙面一侧做不超过 60 cm 的小幅纠偏。受击前位置只用于判断墙的内侧，绝不作为恢复目的地。
- Snapshot 到起身动画的淡入保持 0.30 秒。本轮不修改 Montage 播放时序、AnimBP 或起身动画；物理终姿与正躺/趴躺两条固定动画首姿的匹配质量单独验收。
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
4. 删除墙体重叠、无支撑墙卡等现象特判；保留 0.35 秒自然稳定路径，并为 Simulating/Settling 增加统一 1.5 秒上限。
5. Downed 立即尝试起身，0.20 秒重试、1.5 秒截止；以最终骨盆附近最多 60 cm 的既有安全搜索为主。若骨盆刚跨过近处静态墙体，则只将候选偏向墙内并继续执行可行走地面与完整站立 Capsule 校验；不回到受击前位置。
6. 更新 DataAsset、自动化测试、构建并通过官方 MCP 回读保存结果。

## 验证与回退

- 自动化：更新 Heavy 调参合同并重跑全部 `Demo.Physics.HeavyImpact.*`。
- 构建：`DemoEditor Win64 Development`。
- PIE：摆锤/冲锤正撞与擦边、奔跑/跳跃玩家、长滚动、小幅抖动、墙角阻塞、面朝上/下面起身、Downed 二次命中，覆盖 30/60/120 FPS。
- 任一 Accepted 后出现 `Prepared -> Inactive`、持续物理超过 1.5 秒仍未进入 Downed、起身空间阻塞超过 1.5 秒仍不恢复操作、跨越阻挡墙体到墙外起身、大范围回飞或既有 Heavy 画面明显退化，均视为阻塞。
- C++ 与四份 DataAsset 独立提交；失败时整体回退该提交，不保留兼容开关或旧路径。

## 用户复测后的最小增量修复

本节覆盖本文较早“相机/Light 不在范围”的旧边界，但不改变 Heavy 的唯一状态 Owner：

1. 恢复重试 Timer 只记录待处理序号并启用 Heavy Tick；真正的起身位置放置在下一次 `TG_PostPhysics` 的 Heavy Tick 中完成，使既有 Heavy→CameraBoom prerequisite 在同帧生效。
2. Heavy 飞行期间保持原有 QueryOnly Capsule 跟随，不用站立 Capsule Sweep 干预 Physics Asset 的真实击飞。恢复时才在最终骨盆附近寻找站立空间；如果骨盆刚越过近处静态墙体，只沿墙面法线向受击前一侧做小幅纠偏，并继续要求可行走地面和完整 Capsule 无重叠。若真实 Mesh 频繁穿墙，再以固定复现点单独判断 Physics Asset、CCD、子步或墙体厚度，不在本轮增加逐 Body 纠偏。
3. 让追猎者斧头、冲锤视觉件与磁力箱忽略 `ECC_Camera`；摆锤和制导投射物现状已经忽略。墙、顶、地继续阻挡相机。
4. 将 FollowCamera 的 `(30,10,0)` 子组件偏移等价折入 CameraBoom 终点并把子偏移归零，使实际镜头位置参与 SpringArm Sweep；FOV、位置 Lag 与旋转控制暂时不变。
5. 地刺玩家 Stop 时长从 `0.25s` 调为 `0.70s`，追猎者仍为 Slow。地刺三个碰撞组件均不影响导航生成；升降刺运行时 Ignore Pawn，伤害区只 Overlap Pawn，因此不取消或物理阻断追猎者 PathFollowing。

增量修改文件：

- `Source/Demo/Public/Components/Physics/HeavyImpactResponseComponent.h`
- `Source/Demo/Private/Components/Physics/HeavyImpactResponseComponent.cpp`
- `/Game/ZeroEscape/Characters/BP_ZeroEscapeCharacter`
- `/Game/ZeroEscape/Enemies/BP_Pursuer`
- `/Game/ZeroEscape/Hazards/Physics/BP_BatteringRamHazard`
- `/Game/ZeroEscape/Interaction/Magnetism/BP_MagneticProp`
- `/Game/ZeroEscape/Physics/Impact/DA_SpikeStandingImpact`

增量验收先在 60 FPS 完成：开阔/贴墙 Heavy、冲锤起身一帧闪、玩家/追猎者墙体外壳、斧头/冲锤/箱子经过镜头、地刺玩家 0.70 秒 Stop 与追猎者持续寻路。通过后再做 30/120 FPS 冒烟验证；若只剩真实物理 Mesh 穿墙或墙角原生 SpringArm 伸展跳动，作为独立证据再决定是否进入下一阶段，不在本轮预埋新系统。

## 冲锤来源响应方案 A（用户确认）

- 在通用 `FHeavyImpactPreparationRequest` 增加 `PhysicalResponseScale`，默认 `1.0`；来源不设置时 Heavy 行为不变。
- 冲锤 DataAsset 增加同名参数，首轮默认 `0.60`。冲锤构建准备请求时复制该值；不通过 Actor 类型或名称在接收端识别冲锤。
- 真实 Chaos 接触仍先发生。接收端比较接触前后骨盆速度，只缩放沿来源移动方向新增加的全身共同平移；不重放冲量、不改角速度、不抹除各刚体之间的相对运动。
- 首次真实接触提交后立即让角色 Mesh Ignore `ECC_PhysicsBody`，避免同一运动学来源继续顶推；世界静态/动态几何继续参与碰撞。没有精确 Hit 的 Heavy 仍使用既有短延迟兜底。
- 不新增状态、组件、接口、PCA、角色分支或机关特判。验证重点是冲锤侧撞位移、局部身体反馈、同一锤头不连续推送，以及摆锤默认 `1.0` 的回归。
