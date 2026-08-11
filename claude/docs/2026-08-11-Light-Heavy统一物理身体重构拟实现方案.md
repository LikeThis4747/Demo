# Light / Heavy 物理身体路线比较研究（已暂停）

- Owner：Codex `/root`
- 日期：2026-08-11
- 状态：**已暂停的比较研究稿；非权威方案；不得据此修改 Source / Config / Blueprint / DataAsset**
- 文档关系：本稿不取代 `claude/docs/2026-08-10-统一轻受击响应第一版拟实现方案.md`，也不代表新的 Light 实现已经定案
- 当前边界：只保留问题分析、候选架构、风险与验收维度；正式方案需在全身常驻、接触前按需预激活、接触前局部预激活等路线完成对比并由用户重新确认后另行形成

> **阅读规则：** `CharacterPhysicalBodyComponent`、`StandingControlled` 等名称都是本稿曾提出的项目候选，不是 UE 官方系统名，也不是已批准接口。下文保留的“必须”“第一版”“新增”“删除”“阶段 A/B/C”等措辞只记录原候选在其自身假设下需要满足的条件，不构成实施指令、文件授权或当前决策。当前 Heavy 的已验收行为与误预测精确回滚继续保持原样。

## 1. 已证实问题与已撤回结论

上一版 Light 实现只做了：

1. 箱子撞角色 Capsule；
2. Hit 回调读取冲量；
3. 角色随后播放动画、停顿或减速。

它没有让箱子撞到角色 Physics Asset 的动态刚体，因此角色身体没有承受同一次物理接触，箱子也不会因为胸、肩、手臂的让位而产生相应的失速或偏转。用户看到“只有动画，没有物理反馈”是实现模型错误，不是参数没调好。

原稿曾直接选择以下路线，**该选择现已撤回，仅作为全身常驻候选的示意**：

```text
角色平时就处于受动画目标驱动的全身受控物理状态
                    │
磁力箱子 ──真实 Chaos 接触──► Physics Asset 刚体
                    │             │
                    │             ├─ 命中部位与相邻关节真实偏转
                    │             ├─ 箱子真实失速、旋转或偏转
                    │             └─ 有限骨盆/腿部控制把角色拉回站姿
                    │
                    └─ Hit 事实只附加 Stop / Slow / 攻击打断等玩法后果
```

原候选曾假设：

- 做受控全身物理，不采用只模拟胸口或手臂的局部方案；
- 不播放 Light 受击 Montage；
- 不在 Hit 后给身体补 `AddImpulse`；
- 不用 `LaunchCharacter`；
- 不实现主动跨步找平衡；
- 胶囊继续决定角色实际位置、CharacterMovement 和 AI 导航；
- Light 与 Heavy 共用一套物理身体权威，但保留不同的来源协议和高层状态机。

上述“全身常驻 + 新共享组件”并不是由“真实接触必须提前参与求解”必然推出的结论。当前至少还需平等比较：全身常驻受控物理、接触前短时开启全身物理、接触前局部开启物理，以及作为反例的 Hit 后补冲量。尤其不能用旧局部方案的失败，直接证明所有接触前局部方案都无效。

### 1.1 当前待比较路线

| 路线 | 是否能让第一次物体接触真实影响人体刚体 | 主要优点 | 主要风险 / 未证实点 |
|---|---|---|---|
| 当前 Capsule Hit 后动画 / Stop / Slow | 否 | 稳定、便宜、玩法结果明确 | 没有身体物理反馈，已被现场否决 |
| Hit 后再切局部物理并补冲量 | 否；只能在下一帧重演 | 改动小 | 仍是假反应，旧局部方案不能证明接触前局部方案也失败 |
| 接触前短时开启局部身体 | 是，但仅限已模拟区域 | 根部、移动和性能风险较低 | 传力链可能太短，容易只表现为手臂或胸口松一下 |
| 接触前短时开启全身身体 | 是 | 可保留全身传力，同时避免长期模拟 | 需要可靠预激活、误预测恢复和 Light→Heavy 交接 |
| 全身持续受控物理 | 是，且不依赖碰撞预测 | 意外接触也可即时响应，物理连续性最好 | 对 CharacterMovement、脚底、ABP、性能和现有 Heavy 的爆炸半径最大；控制弱时可能接近软体人，控制强时又可能像石像 |

因此，全身持续开启不是默认推荐，更不等于《Human: Fall Flat》式完全无力布娃娃；它仍可由动画目标和关节马达保持姿势，但软硬两端都存在明显风险。当前没有证据证明它优于“接触前短时开启”，也没有证据证明接触前局部一定不够。

## 2. 不随技术路线变化的目标画面

### 箱子撞肩膀

- 命中侧肩膀和上臂先被推开；
- 胸腔随后产生有限扭转，另一侧身体滞后；
- 骨盆只小幅偏移，不跟着箱子整体飞走；
- 箱子因撞到可让位的肩/胸刚体而减速并改变旋转；
- 控制器在碰撞后把身体逐渐带回当前 locomotion 姿势。

### 箱子正中胸口

- 胸、脊柱和双肩一起向后让位，反应比擦肩更集中；
- 腿仍维持站立，人物不突然跪倒；
- 箱子的正向速度损失更明显。

### 箱子擦过手臂

- 手臂摆开，胸和骨盆只受较弱牵连；
- 箱子可能偏转或滚转，但人物不会播放一整套正面受击动画。

这三个画面必须来自碰撞位置、质量、速度、形状和关节控制的共同结果，而不是从 Front / Left / Right 动画中选一个。

## 3. 外部资料能证明什么、不能证明什么

- EA / Frostbite 的 driven ragdoll 分享展示了“身体刚体真实参与碰撞、关节马达追随动画、根部有有限驱动力”的结构；轻微接触只使身体偏离并恢复，强接触才进入跌倒。它是本方案最直接的行业参考：[GDC 2018 — Physics Driven Ragdolls and Animation at EA](https://www.gdcvault.com/play/1025210/Physics-Driven-Ragdolls-and-Animation)。
- EA 对 FIFA 12 Impact Engine 的公开说明强调每个身体接触点实时产生结果，角色能从轻微碰撞中恢复，而不是每次播放固定反应：[EA — FIFA 12 Player Impact Engine](https://www.ea.com/en-gb/news/fifa12-player-impact-engine-02)。
- UE 5.8 官方 `UPhysicsControlComponent` 明确把 Control 定义为物理弹簧/阻尼驱动，把 Body Modifier 定义为模拟/运动学、重力等身体属性的管理手段；它证明 UE 有实现候选路线所需的底层能力，但不能证明本项目必须新建共享组件或必须常驻全身模拟：[UE 5.8 UPhysicsControlComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/PhysicsControl/UPhysicsControlComponent?lang=en-US)。
- UE 官方 API 支持 WorldSpace 与 ParentSpace 控制集合，也支持以 Skeletal Animation 作为目标并使用动画目标速度；原全身常驻候选曾设想用有限骨盆控制维持角色位置、用 ParentSpace 控制保持关节姿态，但具体效果尚未在本项目验证：[Create Controls and Body Modifiers from Limb Bones](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/PhysicsControl/CreateControlsandBodyModifiersfr-)。

这些资料不等于本项目已经选定 Frostbite 式常驻 driven ragdoll，也不能替代玩家/追猎者 Physics Asset、ABP、CharacterMovement 和 HeavyImpact 的现场验证。它们只支持继续研究“真实刚体接触 + 受控恢复”，不支持直接定案具体常驻方式。

## 4. Heavy 与 Light 的所有权问题（架构尚未定案）

当前 `UHeavyImpactResponseComponent` 同时拥有四类职责：

1. Heavy 接触前预测与 Expected Source 校验；
2. 唯一创建和销毁 Physics Control 记录；
3. 修改每个身体的 Simulated / Kinematic、碰撞、CCD、Physics Blend；
4. 飞行、落地、倒地、安全起身和 Montage 交接。

现有代码还有两条硬冲突：

- Physics Control 中已有任何运行记录时，Heavy 初始化直接失败；
- Heavy 准备前发现任何身体已经在模拟时，准备直接失败。

真实物理 Light 需要在碰撞求解前让相应身体参与模拟。因此不能：

- 给 Light 再建第二套 Controls / BodyModifiers；
- 在 Hit 回调后才把局部骨骼切成物理；
- 让旧 `PhysicsControlHitResponseComponent` 与 Heavy 轮流 `DestroyAllControlsAndBodyModifiers()`。

可以确定的是，同一 Mesh / Physics Control / Body 状态只能有一个运行时写入者；**不能据此直接推出“必须先抽出新的共享组件”**。候选还包括由现有 Heavy Owner 增加受限 Light 模式、先在隔离测试角色中验证效果后再决定是否抽取，以及其他不改变生产 Heavy 的原型方式。

## 5. 原稿候选模块拆分（未定案）

### 5.1 `UCharacterPhysicalBodyComponent`：唯一物理身体权威

原稿设想新增该项目组件，负责“身体怎样被物理控制”，候选职责包括：

- `UPhysicsControlComponent` 的 Controls / BodyModifiers 创建、命名、校验和销毁；
- Physics Control Profile 与控制倍率应用；
- Runtime Pelvis Anchor 的创建、启停和销毁；
- 每个 Physics Body 的模拟、重力、CCD、Blend、速度、唤醒和睡眠；
- Mesh 的物理碰撞模式，以及 Capsule / Mesh 对 Light 专用通道的 Response；
- 身体与 Physics Control 的原始基线快照；
- Standing / Heavy / Recovery 之间不闪切的身体模式转换。

如果以后选择该架构，才需要由它统一创建、销毁和切换角色 Physics Control 记录；当前不得按此段迁移 Heavy。

它**不拥有** Actor / Capsule 的空间位置，也不拥有 Mesh 的 AttachParent、Socket 和 RelativeTransform。共享核心只回答“身体当前怎样被物理解算”，不接管 Heavy 已经成熟的角色外壳与起身放置算法。

### 5.2 `UHeavyImpactResponseComponent`：Heavy 高层状态机

继续唯一拥有：

- `IHeavyImpactReceiver` 接触前准备协议；
- Heavy ImpactId、Expected Source 和预测时序；
- `EHeavyImpactState`；
- CharacterMovement 禁用与飞行期间 Actor / Capsule 跟随骨盆；
- 真实接触提交、飞行、落地判稳、Downed、二次命中；
- 同机关保护；
- Actor / Capsule Transform、Capsule `CollisionEnabled`；
- Mesh AttachParent / Socket / RelativeTransform 的快照、脱离和重新挂接；
- 安全起身空间、正反面判断、Pose Snapshot、起身 Montage，以及失败后恢复 Downed 的完整事务。

迁出：

- Physics Control 初始化/销毁；
- Profile 直接调用；
- BodyModifier 与逐 Body 基线；
- Mesh 身体模拟与碰撞直接写入。

Heavy 通过共享身体组件请求 `Prepared / Flight / Landing / Free / Recovery` 的物理表现，不再自己持有第二份身体状态。

现有 `FHeavyImpactRecoveryBaseline` 中与 Body 模拟、Mesh 物理碰撞和逐 Body 状态重复的字段必须迁出；Heavy 可以保存安全 Sweep 所需的 Capsule Response 只读快照，但恢复时只能请求共享核心应用，不得再写第二遍。Heavy 仍独占 Capsule 的 `CollisionEnabled`，共享核心只独占其 Light 通道 Response，二者不得用整份 ResponseContainer 互相覆盖。

### 5.3 `UCharacterImpactResponseComponent`：仅保留玩法后果

继续拥有：

- 来源 Profile 的 Player / Pursuer 对应栏；
- `None / Slow / Stop`；
- Stop 高于 Slow 的覆盖规则；
- ImpactId 去重；
- MaxWalkSpeed 安全快照和 Timer；
- 空中 Stop 只清 XY、保留 Z 和重力；
- 玩家输入门控；
- AI 攻击打断、Stop 取消路径、Slow 继续追击；
- Heavy 接管时清理未结束的玩法状态。

删除：

- Front / Left / Right 动画选择；
- Dynamic Montage；
- Montage 结束回调；
- `ActiveLightMontage`；
- 对 Mesh / AnimInstance 的依赖。

`Stop / Slow` 以后只表示玩法限制，不再冒充轻受击画面。即使来源配置为 `None`，只要它是合法的物理 Light 刚体，身体仍会产生真实物理反馈。

### 5.4 来源组件：只拥有自己的物体事务

磁力物、摆锤、冲锤等来源继续唯一拥有自己的 Actor / Primitive、速度、碰撞生命周期和来源 ID。角色组件不能反向管理机关。

### 5.5 属性级所有权

| 属性 | 唯一写入者 |
|---|---|
| Controls / BodyModifiers / Pelvis Anchor | `UCharacterPhysicalBodyComponent` |
| Body 模拟、重力、Blend、速度、睡眠、Mesh 物理碰撞 | `UCharacterPhysicalBodyComponent` |
| Capsule / Mesh 对 `AttackProjectileBody` 的 Response | `UCharacterPhysicalBodyComponent` |
| Actor / Capsule Transform、CharacterMovement、Capsule `CollisionEnabled` | `UHeavyImpactResponseComponent` |
| Mesh AttachParent / Socket / RelativeTransform | `UHeavyImpactResponseComponent` |
| Heavy 预测、飞行、落地、Downed、安全放置、Snapshot、Montage | `UHeavyImpactResponseComponent` |
| Light 的 None / Slow / Stop、输入与 AI 限制 | `UCharacterImpactResponseComponent` |

共享核心提供有限的原子操作给 Heavy 使用；Heavy 不能因接口缺失而回头直接写 Body，反过来共享核心也不能把起身空间和 Mesh 挂接逻辑吞进去变成新的上帝组件。

## 6. 原稿候选身体模式（未定案、不可实施）

共享组件只保存物理表示模式，不复制 Heavy 的完整状态机：

```cpp
/** 本项目内部的身体物理表示，不是 UE 官方枚举。 */
enum class ECharacterPhysicalBodyMode : uint8
{
    Uninitialized,
    StandingControlled,
    HeavyPhysical,
    AnimationRecovery
};

/** 进入 StandingControlled 时怎样处理当前物理姿势与速度。 */
enum class EStandingPhysicalEntry : uint8
{
    InitializeFromAnimation,
    ResumeAfterHeavyAbort,
    ResumeAfterAnimationRecovery
};

/** 共享核心允许 Heavy 请求的白名单阶段，不接受任意 Profile 名。 */
enum class ECharacterPhysicalHeavyStage : uint8
{
    Inactive,
    Prepared,
    Flight,
    Landing,
    Free
};
```

含义：

- `StandingControlled`：全身受控模拟，随时可接受 Light 真实接触；
- `HeavyPhysical`：骨盆站立锚关闭，Heavy 决定 Prepared / Flight / Landing / Free Profile；
- `AnimationRecovery`：起身 Snapshot / Montage 期间身体回到动画权威；
- Heavy 的 `Prepared / Simulating / Settling / Downed / Recovering` 仍只存在于 `EHeavyImpactState`，不会在共享组件中复制一份。

Light 命中不会切换 BodyMode。角色在接触前已经是 `StandingControlled`，Chaos 直接处理碰撞。

三个 Standing 入口不能混用：

- `InitializeFromAnimation`：仅用于初始启动，从当前动画姿势建立身体并清除残留速度；
- `ResumeAfterHeavyAbort`：原稿曾设想从当前物理姿势柔性回到动画目标；**该设想已撤回**。当前 Heavy 在误预测时精确恢复预测前 Actor Transform、MovementMode、Velocity、Mesh 挂接和碰撞，这一已验收合同不得被本研究稿改变；
- `ResumeAfterAnimationRecovery`：起身 Montage 完成后，从当前动画姿势重建 Standing 身体并清掉 Heavy 残留速度。

## 7. 全身常驻 `StandingControlled` 候选的设计与风险（未定案）

### 7.1 全身刚体

- 玩家和追猎者的 Physics Asset 都已经覆盖 pelvis、spine、head、双臂、双腿和脚，可作为第一版全身受控模拟基础；
- 所有受控身体通过 Physics Control Asset 的 BodyModifiers / Body Sets 进入 `Simulated + PhysicsBlendWeight 1`；禁止用 `SetAllBodiesSimulatePhysics(true)` 粗暴切换，以免触发不可控的 SkeletalMesh 组件脱离或根部语义变化；
- Standing Profile 必须显式设置 `QueryAndPhysics`、Gravity 1、CCD false，并启用使用 Skeletal Animation 目标的 ParentSpace 控制；
- Standing 时 `bPauseAnims=false`，ABP 每帧继续计算 locomotion；Physics Control 必须在有效的 PrePhysics 更新里取得当帧动画姿势与目标速度；
- 第一版不修改 AnimBP，不增加 Light Slot，也不重定向轻受击动画。

`StandingControlled` 首先是一道技术硬门槛，而不是调参结束后的附带测试：角色必须保持 Mesh 原有 AttachParent，Mesh Component RelativeTransform 不被 Chaos 接管，CharacterMovement Capsule 持续产生目标，pelvis 与 Capsule 的误差被有限锚控制在可解释范围内。玩家和追猎者都要各自完成原地、行走、转向各 30 秒采样，记录 AttachParent、RelativeTransform、骨盆误差和刚体速度。任一项不稳定就停止阶段 B；不得静默退成 kinematic pelvis、局部上半身或 C++ 假姿势。

ABP 也是硬门槛：Standing 时不能读取 Heavy Recovery 的 PoseSnapshot 分支；AnimInstance 重建后 Controls 仍需追随新的有效动画目标。如果现有 AnimGraph 无法提供正确目标，就明确修改 ABP 并让用户验收，不得在 C++ 复制 locomotion 姿势绕过。

现有 Heavy 的 Prepared / Flight / Landing / FreeFallback Profile 语义保持不变；`StandingPelvisAnchor` 是共享组件运行时创建的单一额外控制，登记在独立 `StandingPelvisAnchor` Set 与 OwnedControlNames 中，只由共享组件显式启停。PCA Profile 禁止包含或改写这个 Set；进入和应用任何 Heavy 阶段前后都断言 Anchor 已关闭，避免 `All` Set 或任意 Profile 把它重新打开并压住击飞。

### 7.2 有限骨盆锚

共享组件运行时创建唯一的 `StandingPelvisAnchor`：

- Parent Component：角色 Capsule；
- Child Component：角色 Skeletal Mesh；
- Child Bone：各角色调参资产里的 pelvis；
- `bUseSkeletalAnimation = true`；
-使用动画目标线速度和角速度；
- Linear / Angular Strength、Damping、MaxForce、MaxTorque 都必须为有限值；
- `bOnlyControlChildObject = true`，不能让控制器反向接管胶囊。

有限 MaxForce 是关键：无限或过强会让人物像石像，箱子也几乎不受影响；过弱会变成布娃娃。它应允许骨盆在胶囊附近小幅让位，再回到目标。

### 7.3 分区强度

- 双腿与脚：最强，负责保持站立轮廓；
- 脊柱：中等，允许胸腔传递冲击；
- 双臂：更软，命中时有明显滞后；
- 头：中等偏强，避免颈部过度甩动。

玩家与追猎者各用一份 `UCharacterPhysicalBodyTuningData`，共享字段结构但不强行共享数值。AI“更硬”、玩家“更软”应体现在身体控制强度与质量/约束结果上，而不是让每个机关硬编码角色类型。

### 7.4 碰撞路由

共享核心必须按 BodyMode 确定性应用路由，不能“恢复旧快照后碰巧得到正确结果”：

| BodyMode | Capsule 对 `AttackProjectileBody` | Mesh 对 `AttackProjectileBody` | Mesh 对 WorldStatic / WorldDynamic / PhysicsBody |
|---|---:|---:|---:|
| `StandingControlled` | Ignore | Block | Ignore |
| `HeavyPhysical` | Ignore | Ignore | 由 Heavy 白名单阶段决定 |
| `AnimationRecovery` | Ignore | Ignore | Ignore / 由起身事务关闭物理 |

已武装磁力物临时使用 `AttackProjectileBody` ObjectType，并且必须同时把自身对 `Mesh->GetCollisionObjectType()` 的 Response 设为 Block。角色侧 Mesh Block、物体侧也 Block，才构成真实双向阻挡；不能只验证一边。这样箱子只撞物理身体，不先被 Capsule 吃掉；角色走路时肢体也不会挂墙、碰地面或干扰导航。

Heavy 完成后重新进入 Standing 时直接应用 Standing 的确定路由；Heavy 的旧 Recovery Snapshot 不得把 Mesh 的 Light 通道改回旧版 Ignore/Block 组合。只有组件 `EndPlay` 才恢复角色进入本系统之前的原始基线。

Standing 身体不需要全身 CCD；由高速投掷物事务开启自身 CCD。Heavy Prepared / Flight 是否开启身体 CCD 继续由 Heavy Profile 决定。

## 8. 原稿候选的 Light / Heavy 交接（未定案）

### 8.1 Light 真实碰撞

```text
StandingControlled
    ├─ 箱子直接撞 Physics Asset，Chaos 已完成动量交换
    └─ 来源 Hit 事实提交玩法请求
          ├─ None：只有物理反应
          ├─ Slow：物理反应 + 减速/攻击打断
          └─ Stop：物理反应 + 停顿/攻击打断
```

Light 不会根据 Hit 回调再给角色身体施力。

### 8.2 Standing → Heavy

Heavy Prepare 采用显式的 `Begin → Commit / Rollback` 双组件事务：

1. Heavy 完整校验请求，保存 CharacterMovement、Actor / Capsule 和 Mesh 挂接状态；
2. `BeginHeavyTakeover(Prepared, Scale)` 在共享核心内关闭 `StandingPelvisAnchor`、保留当前物理姿势和速度、应用 Prepared Body/Profile/碰撞，并保存可回滚事务；
3. Heavy 切换 Capsule `CollisionEnabled`、Mesh 脱离和 Expected Source 外壳；
4. 任一外壳步骤失败：共享核心保持 Pending、Anchor 关闭；Heavy 先恢复 Actor / Capsule Transform、Capsule `CollisionEnabled`、Mesh AttachParent / Socket / RelativeTransform；确认外壳恢复后，才调用 `RollbackHeavyTakeover(ResumeAfterHeavyAbort)` 进入 Standing；
5. 两边都成功后调用 `CommitHeavyTakeover()`，共享模式才正式成为 `HeavyPhysical`；
6. Heavy 最后 `SetState(Prepared)`；现有 `OnStateChanged(..., Prepared)` 监听者此时才清理 Light 玩法 Stop / Slow。

`BeginHeavyTakeover` 必须内部原子成功，或者保持无副作用；进入 Pending 后的失败必须能完整 Rollback。任何失败都不得留下半套模拟或半套 Capsule 状态，也不得提前清掉仍有效的 Light 玩法状态。

外壳恢复若无法验证成功，绝不能在错误的 Capsule / Mesh 空间关系下重新启用 Standing Anchor；共享核心应通过 `AbortPendingToSafeHeavyFree()` 保持 Anchor 关闭并进入安全 Free 身体，Heavy 保持 Busy、记录硬错误并走现有安全恢复，不得伪装成 Prepare 被正常拒绝。

上述准备不假定调用点本身就是 PrePhysics。重构必须完整保留现有 `MinimumPreparationLead`、低帧率准备窗口和 `PreparedEntryFrame` 检查，保证在下一次有效 Chaos 求解前完成，并至少跨过一次可靠的 Physics Control 更新。

### 8.3 Heavy 误预测

没有发生预期真实接触时：

- Heavy 恢复 Actor / CharacterMovement / Capsule 事务；
- 共享身体通过 `ResumeAfterHeavyAbort` 回到 `StandingControlled`；
- 身体从当前姿势由驱动平滑回动画目标，不切 Kinematic 后瞬移；
- 已被 Heavy 抢占的旧 Light Timer 不复活。

### 8.4 Heavy 正常结束

- 飞行、落地、Downed 保持 `HeavyPhysical`；
- Heavy 在 `HeavyPhysical` 中验证空间并移动 Capsule，再捕获重定位后的物理 Pose；
- Snapshot 保存成功且 Montage 确认能够启动后，才请求共享核心进入 `AnimationRecovery`、关闭身体物理，并由 Heavy 重新挂接 Mesh；
- `BeginAnimationRecovery()` 在产生任何副作用前捕获共享层回滚快照：每个 Body 的世界 Transform、线/角速度、Simulated/Kinematic、Blend、Collision、Sleep/Wake，以及当前 Heavy Stage、共享核心唯一维护的动态刚体接触开关；捕获失败必须无副作用返回；
- 进入 `AnimationRecovery` 后若 Snapshot、Montage 或挂接的后续步骤失败，固定顺序是：Heavy 停止 Montage/清 Recovery Snapshot → 恢复 Downed Actor/Capsule Transform 与 Mesh 脱离状态 → 共享核心恢复逐 Body 快照、原 Heavy Downed/Free Profile、碰撞释放状态和原 Sleep 状态 → `EHeavyImpactState` 继续保持 `Downed` 并按正常重试间隔再次尝试；
- 起身期间 Light 专用物体不得与 Mesh 阻挡；
- Montage 完成、Mesh/胶囊恢复完成后，通过 `ResumeAfterAnimationRecovery` 进入 `StandingControlled`，此时才清共享层的 AnimationRecovery 回滚快照；
- `CompleteAnimationRecoveryToStanding()` 若失败，Heavy 不得设置 `Inactive`、不得恢复 CharacterMovement，必须保持 `Recovering + AnimationRecovery` 的安全状态并记录错误，等待明确重试/故障处理。

### 8.5 第一版不做的自动升级

第一版不实现“箱子先按 Light 撞上，再按冲量自动升级 Heavy”。来源仍需在接触前选择协议：

- 摆锤、冲锤：HeavyOnly，接触前 Prepare；
- 磁力箱子：StandingPhysical，角色常驻准备；
- 地刺：Overlap Gameplay Only，本轮没有物理身体接触。

共享身体核心为未来的物理升级保留条件，但本轮不新增 `LightToHeavy` 自动阈值、脚本击飞或失败降级。

## 9. 磁力投掷与破碎共享事务

另一个任务提出“一个投掷事务、两个 Hit 后消费者”是合理且必要的。

唯一事务 Owner：`UMagneticObjectComponent`，负责：

- 精确被投掷 Primitive；
- 碰撞完整快照；
- `AttackProjectileBody` 身份；
- CCD；
- Hit Delegate；
- ThrowId / ImpactId；
- 有效期、接触尾段和幂等恢复。

事务向消费者发送同一份不可变命中事实，消费者不得在回调中再读取随时可能被重新抓取/销毁改变的 Active Transaction：

```cpp
struct FMagneticThrownHitContext
{
    FGuid ThrowId;
    TObjectPtr<UPrimitiveComponent> ThrownPrimitive;
    TObjectPtr<AActor> OtherActor;
    TObjectPtr<UPrimitiveComponent> OtherComponent;
    FVector NormalImpulse;
    FHitResult Hit;
};
```

Hit 后消费者：

1. 角色玩法层：Stop / Slow / 攻击打断；
2. 破碎层：决定是否生成破碎结果。

真实物理 Light **不是第三个 Hit 后消费者**。在回调执行前，Chaos 已经让箱子和角色 Physics Asset 完成了碰撞。共享身体组件只负责让角色在那一刻已经处于正确物理状态。

约束：

- 两个消费者都不能再次绑定同一 Primitive 的 Hit；
- 两个消费者都不能自行开启/恢复 CCD 或 Object Channel；
- 没有配置玩法 Profile 时，物理投掷与破碎事务仍可 Arm；
- 玩法请求被消费后不能立刻在下一 Tick 恢复碰撞；由 `UMagneticObjectComponent` 唯一持有一个 `PostCharacterContactTailSeconds` Timer（首版建议 0.15 秒、放在 `MagneticGrabTuningData`）；
- 尾段到期只结束“角色物理接触阶段”：恢复普通 ObjectType / 碰撞路由，释放 `AttackProjectileBody` 身份；若破碎消费者仍有效，统一 Hit/CCD 监听继续保留到它的硬上限；
- 只有“接触尾段结束且没有剩余消费者”，或破碎成功、重新抓取、EndPlay、投掷总硬超时，才完整 `Disarm`；所有计时与恢复仍只有 `UMagneticObjectComponent` 一个 Owner；
- 破碎替换/销毁必须延迟到同帧消费者完成之后。

当前磁力破碎代码落盘后，本方案实施前必须重新只读审计它的最终 API，不能根据未提交稿硬编码适配。

## 10. 原稿候选的 DataAsset 拆分（未定案）

### 10.1 新增 `UCharacterPhysicalBodyTuningData`

玩家和追猎者各一份，至少保存：

- Physics Control Asset；
- Pelvis Bone；
- Standing Pelvis Anchor 的有限线性/角向控制参数；
- Legs / Spine / Arms / Head 的控制倍率；
- Standing Profile、Heavy 阶段白名单映射与资产合同版本；
- 安全数值范围和配置校验。

`Validate()` 必须在 BeginPlay 一次性确认 Standing，以及 Heavy 原有 Inactive、Prepared、Flight、LandingRecovery、FreeFallback 所需 Profile / Limb Set 均存在，全身 Body 覆盖完整，且 Runtime Anchor 没有被任何 PCA Profile 或 `All` 更新隐式接管。不能等打到某个阶段才发现资产缺项。

### 10.2 `UHeavyImpactTuningData`

保留：

- Prepared / Flight / Landing 倍率；
- Heavy 预测时序；
- 稳定、跟随、Downed、保护和起身参数；
- 起身动画与恢复骨骼。

迁出：

- Physics Control Asset 的所有权；
- 共享 Pelvis Bone 的物理拓扑定义；
- 通用 PCA 完整覆盖校验。

Heavy 通过共享身体组件读取骨盆和 PCA 能力，不再把“身体拓扑”当作 Heavy 私有配置。

### 10.3 `UCharacterImpactSourceProfile`

继续保存：

- Player / Pursuer 的 `None / Slow / Stop`；
- Duration 与 SpeedMultiplier；
- 该来源的玩法冲量阈值/归一化。

必须改注释：这些冲量字段只决定是否追加玩法后果及其强度，不决定身体是否产生物理反应，也不制造物理运动。

### 10.4 `UCharacterImpactTuningData`

删除整个类型与两份资产：

- 最低玩法持续时间缩放若仍需保留，移入 `FStandingImpactReactionSpec`，和来源的 Duration 放在同一个权威数据结构；
- 单个连续玩法效果窗口上限改为组件内部的安全常量，不再伪装成玩家/AI视觉调参；
- 删除 `DA_PlayerStandingImpact` / `DA_PursuerStandingImpact`，角色不再装配这个空壳 DataAsset。

这样角色差异只来自 PhysicalBody Tuning（身体软硬）和 SourceProfile 的 Player / Pursuer 栏（玩法后果），不会出现第三张没有清晰职责的角色 Light 调参表。

## 11. 原稿接口草图（仅供比较，不得落盘）

以下只表达职责，不是可直接落盘的完整头文件：

```cpp
UCLASS(ClassGroup=(Physics))
class DEMO_API UCharacterPhysicalBodyComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    bool Configure(
        ACharacter* Character,
        USkeletalMeshComponent* Mesh,
        UCapsuleComponent* Capsule,
        UPhysicsControlComponent* PhysicsControl,
        UCharacterPhysicalBodyTuningData* Tuning);

    // 幂等初始化；Heavy BeginPlay 等消费者必须显式确认成功，不能依赖组件 BeginPlay 顺序。
    bool EnsurePhysicalAuthorityInitialized();
    bool IsInitialized() const;

    bool EnterStandingControlled(EStandingPhysicalEntry Entry);

    // Heavy 接管是显式事务；Begin 失败无副作用，Begin 成功后必须 Commit 或 Rollback。
    bool BeginHeavyTakeover(
        ECharacterPhysicalHeavyStage InitialStage,
        const FCharacterPhysicalControlScale& Scale);
    void CommitHeavyTakeover(); // Begin 成功后只提交内部状态，不再执行可失败操作。
    bool RollbackHeavyTakeover(EStandingPhysicalEntry StandingEntry);
    void AbortPendingToSafeHeavyFree();

    bool ApplyHeavyStage(
        ECharacterPhysicalHeavyStage Stage,
        const FCharacterPhysicalControlScale& Scale);
    bool SetHeavyDynamicBodyContactEnabled(bool bEnabled);
    bool IsHeavyDynamicBodyContactEnabled() const;
    bool WakeHeavyBodies();
    bool SleepHeavyBodies();
    bool ZeroHeavyBodyVelocities();

    // 起身交接也必须可回滚；Heavy 仍拥有空间验证、Snapshot、Montage 与 Mesh 挂接。
    bool BeginAnimationRecovery();
    bool RollbackAnimationRecoveryToHeavyDowned();
    bool CompleteAnimationRecoveryToStanding();

    ECharacterPhysicalBodyMode GetMode() const;
    FName GetPelvisBone() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:
    bool CreateOwnedRuntimeRecords();
    bool CreateStandingPelvisAnchor();
    bool ApplyStandingProfile();
    bool ApplyCollisionRoute(ECharacterPhysicalBodyMode Mode);
    void RestoreOriginalPhysicalBaseline();
    void DestroyOwnedRuntimeRecords();

    // 项目内部事务标志，不扩充公开 BodyMode。
    bool bPendingHeavyTakeover = false;
    bool bPendingAnimationRecovery = false;
};
```

`FCharacterPhysicalControlScale` 应是共享物理层的普通倍率结构。现有 `FHeavyImpactControlStageTuning` 可迁移/替换为它，避免共享核心反向依赖 `HeavyImpactTuningData`。

`Configure()` 只注入依赖；`BeginPlay()` 校验两份角色资产并创建运行时 Records / Anchor。由于 UE 组件 BeginPlay 顺序不作为合同，Heavy 的 BeginPlay 和每次 Prepare 都必须显式调用幂等的 `EnsurePhysicalAuthorityInitialized()`；初始化失败时 Heavy 和 Standing Physical 都明确禁用并记录原因。`EndPlay()` 幂等恢复进入系统前的原始 Body 基线并只销毁自己登记的 Records。

两个 Pending 事务都拒绝第二次 Begin；Begin 成功后必须 Commit/Complete 或 Rollback，`EndPlay()` 能识别并安全撤销 Pending。`BeginAnimationRecovery()` 的回滚快照在成功进入 Standing 前一直有效，不能因 Montage 已启动就提前清除。

Heavy 的关键调用形态：

```cpp
EHeavyImpactPrepareResult UHeavyImpactResponseComponent::PrepareForImpact(...)
{
    ValidateHeavyRequest();
    CaptureHeavyGameplaySnapshot();

    if (!PhysicalBody->EnsurePhysicalAuthorityInitialized()
        || !PhysicalBody->BeginHeavyTakeover(
            ECharacterPhysicalHeavyStage::Prepared,
            Tuning->PreparedControl))
    {
        RollBackHeavyGameplaySnapshot();
        return EHeavyImpactPrepareResult::Invalid;
    }

    if (!EnterPreparedShellWithoutOwningBodyRecords())
    {
        if (!RestorePreparedShellSnapshot())
        {
            PhysicalBody->AbortPendingToSafeHeavyFree();
            EnterSafeBusyRecoveryAfterShellFailure();
            return EHeavyImpactPrepareResult::Invalid;
        }

        PhysicalBody->RollbackHeavyTakeover(
            EStandingPhysicalEntry::ResumeAfterHeavyAbort);
        FinishHeavyGameplayRollbackAfterShellRestore();
        return EHeavyImpactPrepareResult::Invalid;
    }

    PhysicalBody->CommitHeavyTakeover();
    SetState(EHeavyImpactState::Prepared); // 此时 Light 监听者才清理玩法状态。
    return EHeavyImpactPrepareResult::Accepted;
}
```

当前 Heavy 直接完成的以下能力也必须逐一改成上面的受限共享接口，不能因为示例未展开就继续直接写 Body：

- 首次接触后延迟关闭/重新开放 Mesh 对 `PhysicsBody` 的阻挡；
- Downed 二次命中的 Wake、落稳后的 Sleep；
- 硬超时的全身安全停速；
- 未提交真实命中的 `ResumeAfterHeavyAbort`；
- `HeavyPhysical → AnimationRecovery`；
- 起身交接失败返回原 `HeavyPhysical Downed`；
- 起身成功后的 `ResumeAfterAnimationRecovery`。

现有 Heavy 私有的 `bPhysicsBodyCollisionReleased` 必须随碰撞写入权一起迁入共享核心；Heavy 只决定何时调用 `SetHeavyDynamicBodyContactEnabled()`，需要判断时读取 `IsHeavyDynamicBodyContactEnabled()`，不得保留同义布尔副本。AnimationRecovery 回滚也只保存这一份共享状态。

Light 玩法请求不再播放动画：

```cpp
EStandingImpactSubmitResult UCharacterImpactResponseComponent::SubmitImpact(...)
{
    ValidateAndDeduplicate();
    RejectIfHeavyBusy();
    const FStandingImpactReactionSpec& GameplayEffect = SelectReceiverColumn();
    return ApplyNoneSlowOrStopOnly(GameplayEffect);
}
```

## 12. 原稿估算的代码与资产影响面（不是授权范围）

### 12.1 新增 C++

- `Source/Demo/Public/Physics/CharacterPhysicalBodyTypes.h`
- `Source/Demo/Private/Physics/CharacterPhysicalBodyTypes.cpp`
- `Source/Demo/Public/Components/Physics/CharacterPhysicalBodyComponent.h`
- `Source/Demo/Private/Components/Physics/CharacterPhysicalBodyComponent.cpp`
- `Source/Demo/Public/Data/Physics/CharacterPhysicalBodyTuningData.h`
- `Source/Demo/Private/Data/Physics/CharacterPhysicalBodyTuningData.cpp`
- `Source/Demo/Private/Physics/Tests/CharacterPhysicalBodyTests.cpp`
- `Source/Demo/Private/Magnetism/Tests/MagneticThrownImpactTests.cpp`

### 12.2 重构 C++

- `Source/Demo/Public/Components/Physics/HeavyImpactResponseComponent.h`
- `Source/Demo/Private/Components/Physics/HeavyImpactResponseComponent.cpp`
- `Source/Demo/Public/Data/Physics/HeavyImpactTuningData.h`
- `Source/Demo/Private/Data/Physics/HeavyImpactTuningData.cpp`
- `Source/Demo/Public/Physics/HeavyImpactTypes.h`
- `Source/Demo/Public/Components/Physics/CharacterImpactResponseComponent.h`
- `Source/Demo/Private/Components/Physics/CharacterImpactResponseComponent.cpp`
- `Source/Demo/Public/Physics/CharacterImpactTypes.h`
- `Source/Demo/Private/Physics/CharacterImpactTypes.cpp`
- `Source/Demo/Public/Data/Physics/CharacterImpactSourceProfile.h`
- `Source/Demo/Private/Data/Physics/CharacterImpactSourceProfile.cpp`
- `Source/Demo/Public/Characters/ZeroEscapeCharacter.h`
- `Source/Demo/Private/Characters/ZeroEscapeCharacter.cpp`
- `Source/Demo/Public/Characters/PursuerCharacter.h`
- `Source/Demo/Private/Characters/PursuerCharacter.cpp`
- `Source/Demo/Public/AI/PursuerAIController.h`
- `Source/Demo/Private/AI/PursuerAIController.cpp`
- `Source/Demo/Public/Data/PursuerConfig.h`
- `Source/Demo/Private/Physics/Tests/HeavyImpactResponseTests.cpp`
- `Source/Demo/Private/Physics/Tests/CharacterImpactResponseTests.cpp`

### 12.3 串行接入磁力任务后再修改

- `Source/Demo/Public/Components/Magnetism/MagneticObjectComponent.h`
- `Source/Demo/Private/Components/Magnetism/MagneticObjectComponent.cpp`
- `Source/Demo/Public/Data/Magnetism/MagneticGrabTuningData.h`
- `Source/Demo/Private/Data/Magnetism/MagneticGrabTuningData.cpp`
- `Source/Demo/Private/Components/Magnetism/ElectromagneticGrabComponent.cpp`
- `Source/Demo/Public/Components/Magnetism/MagneticThrowBreakComponent.h`（磁力破碎任务最终 API 需要共享 HitContext 时）
- `Source/Demo/Private/Components/Magnetism/MagneticThrowBreakComponent.cpp`（同上）

不得在磁力破碎任务未提交时并行修改这些文件。

### 12.4 建议删除的旧 C++

- `Source/Demo/Public/Components/Physics/PhysicsControlHitResponseComponent.h`
- `Source/Demo/Private/Components/Physics/PhysicsControlHitResponseComponent.cpp`
- `Source/Demo/Public/Data/Physics/PhysicsControlHitTuningData.h`
- `Source/Demo/Public/Data/Physics/CharacterImpactTuningData.h`
- `Source/Demo/Private/Data/Physics/CharacterImpactTuningData.cpp`
- `Source/Demo/Public/Physics/DemoHitTags.h`（最终确认磁力破碎实现不再使用 Tag 后）
- Pursuer 中的 `PhysicsControlHitResponse`、`PhysicsControlHitTuning`、`bIsReacting`、`HandleHitReact`、`OnHitReactMontageEnded`
- `UPursuerConfig` 的 `HitReactFromFront / Left / Right` 运行字段

这些代码的技术模型是“命中后下一帧补冲量并临时松一个区域”，与新系统冲突。回滚依靠 Git 阶段提交，不在运行项目里永久保留两套受击实现。

### 12.5 资产

新增：

- `/Game/ZeroEscape/Physics/CharacterBody/DA_PlayerPhysicalBody`
- `/Game/ZeroEscape/Physics/CharacterBody/DA_PursuerPhysicalBody`

修改：

- `/Game/ZeroEscape/Physics/HeavyImpact/PCA_PlayerHeavyImpact`
- `/Game/ZeroEscape/Physics/HeavyImpact/PCA_PursuerHeavyImpact`
- `/Game/ZeroEscape/Characters/BP_ZeroEscapeCharacter`
- `/Game/ZeroEscape/Enemies/BP_Pursuer`
- `/Game/ZeroEscape/Physics/HeavyImpact/DA_PlayerHeavyImpact`
- `/Game/ZeroEscape/Physics/HeavyImpact/DA_PursuerHeavyImpact`
- `/Game/ZeroEscape/Physics/Impact/DA_MagneticThrownImpact`
- `/Game/ZeroEscape/Physics/Impact/DA_SpikeStandingImpact`
- `/Game/ZeroEscape/Data/Magnetism/DA_MagneticGrabTuning`
- `/Game/ZeroEscape/Interaction/Magnetism/BP_MagneticProp`
- `/Game/ZeroEscape/Enemies/DA_Pursuer`（解除旧 HitReact 运行字段）
- 磁力破碎任务最终资产（只做共享事务接线，不重复加 Hit Owner）

建议删除：

- `/Game/ZeroEscape/Enemies/Physics/DA_PursuerPhysicsControlHit`
- `/Game/ZeroEscape/Physics/Impact/DA_PlayerStandingImpact`
- `/Game/ZeroEscape/Physics/Impact/DA_PursuerStandingImpact`

不删除：

- 追猎者三条 Front / Left / Right 原始动画和 Montage；只解除受击系统运行引用，保留用户资产。

ABP：第一版不修改。如果现场证明 Physics Control 无法从现有 ABP 获得正确动画目标，必须停下来给用户列出真实 AnimGraph 修改，而不是在 C++ 中补一套假姿势绕过。

## 13. 原稿阶段设想（已暂停，不得执行）

### 原候选阶段 A：纯重构共享身体权威

1. 等磁力破碎任务完整提交、推送并恢复 clean；
2. 按项目门禁重新做完整 baseline commit/push/远端核验；
3. 新建共享身体组件和两份 PhysicalBody DataAsset，在玩家/追猎者 BP 装配；迁移两份 PCA 引用；阶段 A 就创建并完整校验 Standing Profile 与 Runtime Anchor，但保持两者禁用；
4. 从 Heavy 迁出 Physics Control、BodyModifier、逐 Body 基线和 Mesh 物理碰撞所有权；
5. 第一阶段仍让共享身体以 Heavy 原有 Inactive 行为启动；原 Heavy PCA Controls / Modifiers 的名字、数量和原有五个 Profile 语义必须保持一致；共享总数会因新增但禁用的 `StandingPelvisAnchor` 多一个，不拿总数做错误等价比较；
6. 验证 Heavy 误预测、飞行、Downed、二次命中、起身成功与起身交接失败回滚；
7. 构建、现有 Heavy 自动化、摆锤/冲锤、倒地起身回归。

阶段 A 不通过，整阶段回退，不开始 Standing Light。

### 原候选阶段 B：启用 `StandingControlled`

1. 启用阶段 A 已创建并校验的 Standing Profile 与 Runtime Anchor，开始现场调参；
2. 删除 Light Montage、动画 Tuning 和角色旧 HitReact 运行字段，所有 SourceProfile 暂设 `None`；
3. 创建独立 Set 的有限 Pelvis Anchor；
4. 通过 BodyModifiers 启用全身受控模拟和分区强度；
5. 先完成原地、行走、转向各 30 秒的 AttachParent / 相对变换 / 骨盆误差 / ABP 目标硬门槛；
6. 反转 Capsule / Mesh 的 `AttackProjectileBody` 路由并验证双向 Block；
7. 用磁力箱子只验证纯物理画面。

纯物理验收通过后再打开 AI Stop / Player Slow 等玩法配置，避免玩法停顿掩盖物理效果。

### 原候选阶段 C：磁力共享事务与旧代码清理

1. 只读复核磁力破碎最终实现；
2. 在唯一投掷事务中以同一个不可变 HitContext 接玩法与破碎两个消费者；
3. 由唯一事务 Owner 保留 0.15 秒默认角色接触尾段；尾段结束只恢复普通碰撞，不能提前关闭仍在等待撞墙的破碎消费者；
4. 删除旧局部 Physics Control 与剩余死代码/资产；Light Montage 和旧 HitReact 已在阶段 B 删除，不在这里保留第二条路径；
5. 做 Light / Heavy / 破碎交叉回归；
6. 完整构建、文档、提交、推送，等待用户现场验收。

不使用永久 `bUseLegacyLightImpact` 开关。阶段提交就是双重保险和回退边界。

## 14. 可复用的比较与验收维度

### 14.1 纯物理 Light 硬门槛

必须同时满足：

1. 禁用所有 Light 动画，画面仍明显成立；
2. 箱子命中后明显失速、偏转或旋转；
3. 胸、肩、手臂、腿的命中结果不同；
4. 角色不离地、不倒地、不主动迈步；
5. 碰撞后身体逐渐回到当前动画姿势，没有突然弹回；
6. Mesh 始终保持正确 AttachParent / RelativeTransform，pelvis 与 Capsule 误差有界；
7. Standing 时 ABP 持续更新、`bPauseAnims=false`，当前 locomotion 而非 Heavy Recovery Snapshot 成为物理目标；
8. 无命中时，走路、转向、追击和攻击没有持续抖动或漂移。

任一项失败都不能用 Montage 或手工冲量补成“看起来像成功”。

### 14.2 Heavy 回归

- 摆锤、冲锤击飞距离不退化；
- Heavy 仍只由真实接触提交；
- Standing → Heavy 不经过动画姿势闪切；
- Heavy 误预测继续执行当前精确回滚；任何替代恢复语义必须另行讨论和验收；
- Heavy Prepare 失败不清理仍有效的 Stop / Slow；
- Heavy Profile 应用前后 `StandingPelvisAnchor` 始终关闭；
- Downed、墙角挪动、安全起身和同机关保护不退化；
- 起身交接任一步失败能回到原 Heavy Downed 物理，而不是站立或卡死；
- 起身完成后重新可接受物理 Light。

### 14.3 磁力/破碎

- 同一 Primitive 只有一个 Hit/CCD/碰撞快照 Owner；
- 同帧玩法与破碎都得到同一份不可变 HitContext / ThrowId；
- 破碎不会提前销毁导致玩法丢回调；
- 玩法消费不会立即取消尚在进行的物理接触；
- 接触尾段只有一个 Timer Owner；尾段结束后 `AttackProjectileBody` 已恢复，但仍活动的破碎监听继续存在；
- 无玩法 Profile 仍能 Arm；重抓、破碎、总超时、EndPlay 均可幂等完整 Disarm；
- 重新抓取与超时恢复精确回到原碰撞状态。

### 14.4 玩家 / AI / PCG

- 同一物体真实物理反馈共享；角色身体软硬由各自 PhysicalBody Tuning 决定；
- 同一来源的 Stop / Slow 仍可在 SourceProfile 中分别配置玩家与追猎者；
- AI Slow / Stop 均打断攻击，只有 Stop 取消路径；
- 玩家空中 Stop 只清 XY；
- PCG 只放来源 Actor 和引用共享 Profile，不在 PCG 图里写玩家/AI分支，不新增机关 Tick。

## 15. 已识别风险与止损依据

### P0：共享权威迁移造成 Heavy 退化

用阶段 A 隔离。Body 接管与 Heavy 外壳接管必须是显式可回滚事务；Heavy 等价回归不通过，禁止继续 Light。

### P0：胶囊与骨盆争抢造成抖动/根漂移

必须先通过 Mesh 挂接、相对变换、骨盆误差和 ABP 目标硬门槛，再调整 Pelvis Anchor 与 Physics Control 目标关系；不能降级为局部手臂反应。若现有骨架/PCA 确实无法稳定，停下来记录真实失败证据，再决定是否需要用户修改 Physics Asset 或 AnimBP。

### P0：碰撞只配置一侧，画面仍像假受击

Standing 必须同时验证 Mesh 对 `AttackProjectileBody` Block、投掷物对 Mesh 当前 ObjectType Block、Capsule Ignore。Heavy / Recovery 必须确定性 Ignore Light 通道；不得依赖旧 Snapshot 恰好恢复正确。

### P1：控制过强像石像

降低有限 MaxForce/MaxTorque 和上身控制；不增加假动画。

### P1：控制过弱像布娃娃

提高腿/骨盆稳定，校正质量与约束；不把碰撞改回 Capsule。

### P1：正常 locomotion 持续物理开销与抖动

当前没有 PCG 最大并发角色数和目标硬件性能数据，不能假定常驻全身模拟成本可接受。常驻、距离休眠和接触前预激活应作为不同路线比较，而不是先实施常驻再补优化。

### P1：PCA/ABP 资产编辑工具不足

实施时优先官方 UE5.8 MCP。若官方 MCP 无法安全修改 Physics Control Asset 或必要 AnimGraph，明确给出人工操作步骤并等待用户完成；不写 C++ 绕过资产本应承担的配置。

## 16. 原稿复杂度估算（仅说明爆炸半径）

- 架构复杂度：中高；主要难点是共享权威迁移和 Standing / Heavy 无闪切交接，不是 Stop / Slow 本身。
- 新共享核心与调参/校验：约 700～1100 行；
- Heavy 迁移：预计从现有约 2200 行中迁出/删除 350～600 行，新增调用与回滚约 150～250 行；
- 删除旧局部受击、旧 Light 动画与死代码：约 800～1100 行；
- 测试新增/调整：约 300～500 行；
- 最终净增量预计明显小于“保留三套系统并叠加第四套”，而且物理身体只有一个 Owner。

这些是范围估算，不是工期承诺。阶段 A、B、C 必须分别有独立提交和可回退点。

## 17. 当前结论状态

1. 尚未选择全身常驻、接触前短时全身、接触前局部或其他物理 Light 路线。
2. 尚未决定是否抽取新的共享身体组件；唯一运行时写入者是约束，不等于组件形式已经确定。
3. 尚未授权删除 12.4、12.5 或任何旧代码/资产；旧路径先作为回退和问题证据保留。
4. 当前只保留已确认的目标：磁力物优先验证真实物理接触；Stop / Slow 是独立玩法后果；地刺本轮仍是 Gameplay Only；现有 Heavy 效果和精确误预测回滚不得退化。
5. 在完成路线对比、项目资产回读和用户重新确认前，本稿不得升级为正式方案，也不得触发实现门禁。
