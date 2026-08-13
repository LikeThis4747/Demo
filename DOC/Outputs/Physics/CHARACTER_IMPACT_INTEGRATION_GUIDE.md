# 角色受击系统新机关接入指南

> 状态：当前权威接入说明。最后核对：2026-08-13，代码基线 `ae404c21ce64ef30dbe062ddcbe9a7578d6d30c8`。

## 1. 结论

新机关接入现有受击系统时，先由机关选择一条明确通道：

- `None / Slow / Stop`：命中或 Overlap 已确认后，调用 `ICharacterImpactReceiver::SubmitStandingImpact`。
- `HeavyImpact`：真实接触发生前，调用 `IHeavyImpactReceiver::PrepareForHeavyImpact`；随后必须由同一个真实刚体完成 Chaos 接触。

玩家与追猎者已经实现这两个接口。只要新机关仍使用现有四种结果，就不应修改：

- `UCharacterImpactResponseComponent`
- `UHeavyImpactResponseComponent`
- 玩家或追猎者的受击转发代码
- Heavy 的 Physics Control Asset、倒地或起身参数

机关只负责描述“这一次事件是什么”，角色侧负责把结果转换成玩家或 AI 的移动、动画和身体表现。

## 2. 当前架构与职责

| 层 | 唯一职责 | 不应承担 |
|---|---|---|
| 机关/来源 | 生成稳定事件 ID；确认命中；提供方向、点和强度；选择 Light 或 Heavy 通道；管理自己的碰撞与销毁 | Cast 到具体玩家/追猎者并直接改速度、播放角色动画或操纵角色骨骼 |
| `UCharacterImpactSourceProfile` | 配置同一来源对玩家、追猎者分别产生 `None / Slow / Stop`，以及物理强度映射 | 保存角色动画、Heavy 参数或机关运行时状态 |
| `UCharacterImpactResponseComponent` | Light 的优先级、持续时间、速度恢复、可选 Stop 动画和短时局部物理 | 判断某个具体机关类型、管理弹体或陷阱生命周期 |
| 角色适配层 | 玩家输入门控；追猎者攻击中断与寻路停止 | 重新解释来源类型 |
| `UHeavyImpactResponseComponent` | Heavy 的准备、真实物理接触、击飞、倒地和起身 | 根据 Light 冲量自动猜测是否升级 Heavy |

核心组件中没有磁力箱、制导投射物、地刺、摆锤或冲锤的类型判断。当前这些机关的差异都留在各自来源代码和 DataAsset 中。

## 3. 如何选择结果

| 目标效果 | 选择 | 新机关需要做什么 | 是否修改角色受击核心 |
|---|---|---|---|
| 无玩法反应 | `None` | Profile 配置即可 | 否 |
| 仍可移动但减速 | `Slow` | 创建来源 Profile；命中后提交一次请求 | 否 |
| 短暂停住 | `Stop` | 创建来源 Profile；命中后提交一次请求；可选择 Stop 动画 | 否 |
| 击飞、倒地、起身 | `HeavyImpact` | 在接触前预测目标、接触点和 ETA；随后让真实动态刚体撞到角色 Mesh | 否，但机关必须写自己的预测适配 |

这里的“映射”只是来源 Profile 中 `PlayerReaction` 和 `PursuerReaction` 两栏选择表，不是隐藏的自动判级算法。一个机关可以让玩家 Slow、追猎者 Stop，也可以反过来；修改这两栏不需要修改接收组件。

## 4. Light：新机关最小接入流程

### 4.1 创建来源 Profile

创建一份 `UCharacterImpactSourceProfile` DataAsset，并分别填写：

- `PlayerReaction`
- `PursuerReaction`
- 真实刚体来源使用的 `MinimumPhysicalImpulse / FullStrengthPhysicalImpulse`
- 为了保证观感而设置的 `MinimumResponseStrength`

规则：

- `None`：持续时间为 0、速度倍率为 1、动画和局部物理关闭。
- `Slow`：持续时间大于 0，速度倍率在 0 到 1 之间；当前合同不播放全身受击动画，但可启用局部物理。
- `Stop`：持续时间大于 0，速度倍率为 0；可分别选择动画和局部物理。

### 4.2 每个攻击事件只生成一个稳定 ID

同一次攻击的所有回调必须共用一个 `ImpactId`：

- 一次投掷：一枚 ID。
- 一次地刺伸出相位：一枚 ID。
- 一次摆动或一次发射：一枚 ID。

不要在每次 Overlap、每个子步或每帧生成新 ID。接收端有有界去重，但来源仍必须保证事件语义正确。

### 4.3 在来源确认命中后提交

```cpp
if (IsValid(Target)
	&& Target->GetClass()->ImplementsInterface(UCharacterImpactReceiver::StaticClass()))
{
	FStandingImpactRequest Request;
	Request.ImpactId = ActiveAttackId;
	Request.SourceActor = this;
	Request.SourceComponent = ImpactPrimitive;
	Request.SourceProfile = StandingImpactSourceProfile;
	Request.WorldDirection = PushDirection.GetSafeNormal();
	Request.ImpactPoint = HitPoint;
	Request.NormalizedStrength = ResponseStrength;
	Request.RawNormalImpulse = NormalImpulse;

	const EStandingImpactSubmitResult Result =
		ICharacterImpactReceiver::Execute_SubmitStandingImpact(Target, Request);
}
```

请求字段合同：

- `SourceComponent` 必须属于 `SourceActor`，并且是这次事件的真实来源组件。
- `WorldDirection` 表示目标身体被推向的方向，不是未经转换的命中法线。
- `ImpactPoint` 使用真实命中点；触发型机关可使用目标与机关关系推导的合理点。
- 真实刚体来源优先用 `SourceProfile->NormalizePhysicalImpulse(NormalImpulse.Size())`。
- Trigger/Overlap 来源没有真实冲量时直接提交策划强度 `0..1`，`RawNormalImpulse` 保持零。

### 4.4 返回结果

| 返回值 | 含义 | 来源应做什么 |
|---|---|---|
| `Applied` | 新的 Light 玩法状态被接受 | 一次性来源按已命中处理 |
| `Ignored` | 结果为 None、或玩法优先级/强度没有胜出 | 不排队、不重试；一次性命中仍应消费 |
| `Duplicate` | 该 ID 已处理 | 不重试 |
| `HeavyBusy` | 角色正在 Heavy | 不排队，也不要在 Heavy 结束后补交 |
| `Invalid` | 请求、Profile 或装配错误 | 记录明确日志并修配置 |

当前局部物理表现与玩法状态分层：非 None 请求即使没有升级正在生效的 Slow/Stop，也可能产生一次新的短时局部物理偏转。因此来源不能把 `Ignored` 解释成“绝对没有任何画面”，只能解释成“没有建立或升级玩法状态”。

## 5. Heavy：新机关最小接入流程

Heavy 不是命中后提交的枚举，而是接触前协议。新机关需要：

1. 用 `GetHeavyImpactPredictionPrimitive` 取得角色真实预测表面。
2. 在机关有效攻击阶段内，根据自身几何与运动计算预计接触点和 ETA。
3. 使用本次摆动/冲撞的稳定 ID 构造 `FHeavyImpactPreparationRequest`。
4. 调用 `PrepareForHeavyImpact`。
5. 只有 `Accepted` 或同事件 `Duplicate` 才视为该接收者已准备；随后仍由 `SourceComponent` 的真实 Chaos 接触完成击飞。

```cpp
FHeavyImpactPreparationRequest Request;
Request.ImpactId = ActiveAttackId;
Request.SourceActor = this;
Request.SourceComponent = MovingImpactBody;
Request.PredictedImpactPoint = PredictedPoint;
Request.SourceLinearVelocity = MovingImpactBody->GetPhysicsLinearVelocity();
Request.EstimatedTimeToContactSeconds = EstimatedTimeToContact;

const EHeavyImpactPrepareResult Result =
	IHeavyImpactReceiver::Execute_PrepareForHeavyImpact(Target, Request);
```

约束：

- `SourceComponent` 必须是随后真正撞到角色的动态刚体，不能传预测 Trigger。
- 机关必须有有效攻击相位、最小接近速度和可解释 ETA；静止贴住角色不能触发 Heavy。
- 不允许在角色上再调用 `AddImpulse` 或 `LaunchCharacter` 冒充 Heavy。
- Overlap-only 的地刺、持续气流等来源不能只改一个枚举就获得现有 Heavy；它们若要击飞，需要先具备真实接触与预测能力，或另行设计明确的脚本击退执行器。

不同机关的摆动轴、形状和运动方式不同，所以预测代码会留在机关侧。这是来源适配，不是受击系统与机关耦合；接收端接口和 Heavy 状态机不需要随机关增加而修改。

## 6. Light 与 Heavy 不能在接触后互相降级

一个攻击事件必须在接触前选择通道：

- `LightOnly`：只提交 StandingImpact。
- `HeavyOnly`：只走 Heavy 准备与真实接触。
- 同一机关支持两种攻击等级时：由攻击阶段、速度或策划条件在接触前选定其中一条。

Heavy 请求返回 `Busy / Invalid / Duplicate` 后，不得在同一次真实接触里偷偷补交 Light。这会绕过 Heavy 的保护和去重，并制造双重反馈。

## 7. 机关自己的生命周期仍由机关负责

受击接口不会替新机关处理：

- 弹体命中后是否销毁、穿透、反弹或忽略 Pawn。
- 磁力物的抓取、投掷、CCD 与破碎。
- 地刺的危险相位和伤害结算。
- 高压气流的持续力与离开恢复。

例如制导投射物命中角色后忽略 Pawn，是为了防止同一枚 50 kg 弹体在后续帧持续顶住角色；这属于弹体碰撞生命周期，不是 StandingImpact 的特判。以后新弹体若也会持续挤压角色，需要在自己的命中生命周期中处理，而不是修改角色受击组件。

持续来源也不要每帧刷新 Light：推荐“进入时提交一次 Light 脉冲 + 独立持续移动/力效果 + 离开时恢复”。

伤害同样不属于受击状态。机关可以在同一事件中独立 ApplyDamage，但不能让伤害逻辑进入 CharacterImpact 组件。

## 8. 当前验收基线与已知缺口

2026-08-13 官方 UE5.8 MCP 对现有来源 Profile 的持久化回读：

| 来源 | 玩家 | 追猎者 |
|---|---|---|
| 制导投射物 | Slow 0.40s、速度 0.55、局部物理；最低表现强度 0.75 | None |
| 磁力投掷物 | Slow 0.40s、速度 0.55、局部物理 | Stop 0.60s、方向动画、局部物理；最低表现强度 0.70 |
| 地刺 | Stop 0.25s、当前无动画/局部物理 | Slow 0.60s、速度 0.45、无动画/局部物理 |

已由用户现场接受：

- 制导投射物命中玩家：`Slow 0.40s / SpeedMultiplier 0.55 / 无动画 / 局部物理`。
- 磁力箱命中玩家：`Slow 0.40s / SpeedMultiplier 0.55 / 无动画 / 局部物理`。
- 磁力箱命中追猎者：`Stop 0.60s / 三方向动画 / 局部物理`。
- 局部物理观感仍偏弱，但当前可用；不再为单个机关扩写新的物理架构。

当前唯一明确的完整度缺口：

- 玩家 `DA_PlayerStandingImpact` 的 Front/Left/Right 动画引用仍为空。
- 因此玩家 Stop 的移动状态已经存在，但全身方向受击动画尚未完成。
- 该资产补齐已列为独立最高优先级任务；它不要求修改新机关接入接口。

当前局部物理是角色侧按命中点、方向和归一化强度施加的受限表现冲量，不是外物与 Physics Asset 在第一次接触中的完整双向动量交换。它的目标是提供可调、可恢复的部位反馈；不要把它描述成完全物理驱动。

## 9. 新机关交付检查表

实现 AI 在提交新机关前应逐项确认：

- [ ] 已在接触前明确选定 Light 或 Heavy 通道。
- [ ] 没有 Cast 到 `AZeroEscapeCharacter` 或 `APursuerCharacter` 来决定受击结果。
- [ ] 来源 Profile 同时明确填写玩家与追猎者结果。
- [ ] 一个攻击事件只生成一个稳定 ID。
- [ ] 方向、点和强度都来自该机关自己的可靠数据。
- [ ] 一次性来源对所有返回结果都不会排队重试。
- [ ] 弹体/机关自己的碰撞、销毁、伤害和持续效果没有塞入角色受击组件。
- [ ] 若选择 Heavy，真实刚体、有效攻击相位、预测表面和 ETA 均成立。
- [ ] 没有因为新增机关而修改 CharacterImpact/HeavyImpact 核心；若确实需要修改，先说明新增的是哪一种此前不存在的玩法结果。
- [ ] 运行 Profile 合同测试，并在 PIE 验证玩家/追猎者映射、重复命中和 Light→Heavy 交叉行为。

## 10. 代码入口与现有范例

公共入口：

- `Source/Demo/Public/Interfaces/CharacterImpactReceiver.h`
- `Source/Demo/Public/Interfaces/HeavyImpactReceiver.h`
- `Source/Demo/Public/Physics/CharacterImpactTypes.h`
- `Source/Demo/Public/Physics/HeavyImpactTypes.h`
- `Source/Demo/Public/Data/Physics/CharacterImpactSourceProfile.h`

来源范例：

- 真实刚体 Light：`Source/Demo/Private/Components/Magnetism/MagneticObjectComponent.cpp`
- 带策划保底的弹体 Light：`Source/Demo/Private/Actors/Hazards/ThrustGuidedHazardProjectile.cpp`
- Trigger/Overlap Light：`Source/Demo/Private/Actors/Hazards/SpikeTrapHazard.cpp`
- 接触前 Heavy：`Source/Demo/Private/Actors/Hazards/BatteringRamHazard.cpp` 及现有摆锤实现

接收端只用于核对，不应为新机关修改：

- `Source/Demo/Private/Components/Physics/CharacterImpactResponseComponent.cpp`
- `Source/Demo/Private/Components/Physics/HeavyImpactResponseComponent.cpp`
