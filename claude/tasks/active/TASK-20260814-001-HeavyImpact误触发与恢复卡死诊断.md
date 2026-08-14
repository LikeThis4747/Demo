# TASK-20260814-001 HeavyImpact 误触发与恢复卡死诊断

- Owner：Codex `/root`
- 任务类型：实现
- 实现文件写入权：Codex `/root` 独占（用户 2026-08-14 明确授权本轮 Heavy/相机与地刺时长修复）
- Status：active
- Stage：增量实现、构建与自动化已完成；等待用户在原复现点验收 Heavy 墙体/相机与地刺时长
- Created：2026-08-14

## 用户反馈

1. Heavy 即将触发后经常失败并返回动画状态，视觉上出戏。
2. 倒地后偶尔长时间无法起身，不符合玩法节奏。
3. 尚未发生可信接触时角色已经开始击飞，存在“碰瓷”感。

## 只读范围

- `UHeavyImpactResponseComponent` 的 Prepare、Commit、超时、Landing、Downed、Recovering 与回滚时序。
- 摆锤、冲锤、追猎者攻击和制导机关的 Heavy 预测、真实接触与 Commit 入口。
- 玩家/追猎者 Heavy DataAsset、Physics Control Asset、Blueprint 装配和当前 PIE 日志。
- Heavy 自动化仅作合同证据；视觉与 Chaos 时序必须由真实 PIE 复现补证。

## 排除范围

- 本轮不修改 C++、Blueprint、DataAsset、Physics Asset、AnimBP、关卡或配置。
- 不触碰当前并行的刺轮修改。
- 未完成根因排序前不延长超时、不缩短起身时间、不放宽预测距离，也不增加强制动画回退。

## 输出

- 将三个症状分别对应到真实状态转换、触发源和日志证据。
- 区分确定根因、强嫌疑与待复现项。
- 给出最小修复顺序、风险和需要用户决定的玩法取舍；获得明确授权后再实施。

## 2026-08-14 已确认约束与方案默认

1. 机关候选预测可以静默消失，但不得让玩家看到 `Prepared -> Inactive` 的回正。
2. 接收端一旦返回 `Accepted`，本次 Heavy 即进入不可回滚阶段；正常超时仍继续 Heavy，只有进入 `Accepted` 之前的组件/资产基础设施失败允许内部恢复并写 Error 日志。
3. 物理飞行、落地和滚动没有总时长硬上限；不再使用 5 秒自由回退与 10 秒强制 Downed 作为正常玩法。
4. 不在第一次落地时抢跑起身，也不要求绝对静止。身体持续降到低能量后进入 Downed；正常落地仍利用可行走支撑，挂墙/夹缝中无支撑的持续低能量姿势也必须收口。
5. Downed 睡眠完成后立即尝试安全起身，不再固定等待 0.5 秒。正常站位受阻时每 0.2 秒重试，最多 2.0 秒。
6. 2 秒到期仍无本地安全位置时，以受击前 Capsule 位置为种子做最后一次有界安全搜索。若仍无解，回到 Heavy 准备前已校验有效的角色外壳变换，跳过起身动画并恢复操作；允许罕见的可见位置回退，不允许永久 Downed。
7. Snapshot 到起身动画的显式淡入保持 0.30 秒；本轮不修改 Montage 播放时序、AnimBP 或起身动画。

## 2026-08-14 用户复测后增量根因

- 玩家与追猎者确实使用同一 `UHeavyImpactResponseComponent`；差异只在各自的 Heavy DataAsset、骨架/起身动画及 AI 恢复后的导航接管。
- AI 挂墙不起不是 Controller 漏了恢复回调，而是 Heavy 前半段只在有可行走地面支撑时累计稳定时间。挂墙/夹缝姿势虽已低能量，仍会每帧清零进度，根本到不了 Downed 和后续起身截止计时。
- 共享修正为：骨盆低线速+低角速连续 0.35 秒即视为物理运动结束；有支撑走正常落地，无支撑走墙边卡死收口。自由飞行顶点的低速窗口小于 0.35 秒，不会因此抢跑起身。
- 起身交接的代码与 AnimGraph 顺序经复核成立：Snapshot 在同帧被求值，四条 GetUp Sequence 的前 0.75 动画秒也基本静止。当前闪感更符合“任意物理终姿只映射到正躺/趴躺两种固定首姿”的姿势差异；暂停 Montage 或继续延长 Blend 都不是有证据的修复，因此本轮不加入该补丁。

## 2026-08-14 玩家墙体穿入与相机增量反馈

- 用户复测发现玩家被冲锤击入走廊墙体；该问题与追猎者挂墙同属共享 Heavy 边界问题，不做玩家或 AI 特判。两者均使用 `UHeavyImpactResponseComponent`，差异只来自各自骨架、Physics Asset 与 Heavy 调参资产。
- 当前 Heavy 会将 Mesh 脱离角色外壳并全身模拟，同时用无 Sweep 的 `TeleportPhysics` 让 Actor/Capsule 追随物理骨盆。由此需要分别确认两件事：模拟骨骼是否真的穿入墙体，以及 Capsule/相机锚点是否在骨骼贴墙时被无 Sweep 放进墙体；不能用相机修复掩盖真实骨骼穿模。
- 墙体穿入列为本轮 P0 验收阻断：修复必须落在共享 Heavy 或真实碰撞配置，不得在玩家类、AIController 或具体冲锤里增加位置特判。
- 最小处理顺序：先在固定复现点回读墙体碰撞与玩家/追猎者 Physics Asset 接触，确认 Mesh 与 Capsule 各自位置；再让 Heavy 的 Actor/Capsule 追随使用 WorldStatic Sweep 并保留命中结果；若 Mesh 仍真实穿墙，才针对 Heavy 模拟 Body 的 CCD/碰撞几何做最小修正，不预先引入逐 Body 纠偏、持续 Teleport 或新状态机。
- 起身 2 秒兜底也纳入同一墙体验收：最终恢复位置必须经过当前站立 Capsule 的 Sweep/Overlap 验证；不得把历史上安全但当前已被墙体或机关占据的位置无 Sweep 强塞给角色。
- 相机修复继续保持独立职责：动态玩法物忽略 Camera、起身位移改在 Heavy PostPhysics 执行、真实镜头终点参与 SpringArm Sweep；这些只能解决画面闪动和相机入墙，不能替代角色身体的墙体约束。

## 已授权最小实施范围

- 收紧 `ABatteringRamHazard` 与 `APendulumHazard` 的预测：PreparationVolume 只收集候选，最终请求必须通过锤头真实盒体对接收 Mesh 的短时几何 Sweep。
- `UHeavyImpactResponseComponent` 保持唯一 Heavy/Physics Control Owner，不新增组件、接口或状态；删除正常 Prepared 回滚、Settling 抢跑起身与 5/10 秒正常硬切。
- 清理已失效的 `FalsePositive` 命名、旧调参字段与对应测试，不保留双路径或兼容开关。
- 不修改 Light、追猎者攻击、Heavy PCA、AnimBP、起身动画、关卡或其他机关。

## 2026-08-14 相机、墙体与地刺增量实施范围

- Heavy 恢复 Timer 只排队，不再在 SpringArm 已完成 PostPhysics 后直接移动 Capsule；下一次 Heavy PostPhysics 消费请求，让 CameraBoom 在同帧随后重算。
- Heavy 的 Actor/Capsule 骨盆跟随改为带 Sweep 的共享外壳移动，阻止玩家和追猎者的相机锚点/角色外壳无条件进入 WorldStatic；不添加角色分类分支。
- 修正相机碰撞职责：`BP_Pursuer` 斧头、`BP_BatteringRamHazard` 视觉件、`BP_MagneticProp` 对 Camera Ignore；保留各自专用玩法碰撞体。
- `BP_ZeroEscapeCharacter` 将 FollowCamera 的额外相对偏移折入 SpringArm 有效终点，确保真实镜头位置参与墙体 Sweep；不修改 FOV、控制旋转或建立新 CameraManager。
- `/Game/ZeroEscape/Physics/Impact/DA_SpikeStandingImpact` 仅把玩家 Stop `DurationSeconds` 从 `0.25` 改为 `0.70`；追猎者仍为 `Slow 0.60 / 0.45`。
- 地刺导航只读结论：GrateMesh、SpikeMesh、HurtZone 均 `bCanEverAffectNavigation=false`；运行时 SpikeMesh 对 Pawn Ignore，HurtZone 仅 Pawn Overlap，AI Slow 不取消 PathFollowing。格栅保留地板碰撞，但不导出 Recast 障碍。
- 不新增碰撞通道、SpringArm 子类、PlayerCameraManager、逐 Body 纠偏、兼容开关或机关特判；不修改 Level0、AnimBP、Heavy PCA、追猎者攻击和地刺 C++。
- 并行纯文档改动已按用户授权纳入检查点；本次增量实现实际基线为远端 `47adb45d3d338a54d67cb55bef0088aa793901e9`，写入前工作区与远端均已核对干净。

## 2026-08-14 增量实施与验证结果

- Heavy 恢复 Timer 现在只排队序号；真正的 `TryBeginRecovery` 在下一次组件 `TG_PostPhysics` Tick 执行。已有 Heavy→CameraBoom prerequisite 因此能在同帧让弹簧臂使用最终 Capsule 位置更新，消除 Timer 位于 SpringArm 之后造成的一帧旧缓存。
- 物理期 Actor/Capsule 跟随增加仅查询 `WorldStatic` 的胶囊 Sweep。命中时停在 Sweep 安全位置；动态机关、箱子和 AI 不参与这次外壳约束。该修正同时作用于玩家与追猎者；已脱离外壳的物理 Mesh 不做逐 Body 纠偏，是否仍穿墙由用户固定复现点验收。
- 相机资产保持 `ECC_Camera` 对结构墙阻挡；追猎者斧头、冲锤四个视觉件和磁力箱新增 Camera Ignore。玩家实际镜头终点改为 `ArmLength=190 / SocketOffset=(0,65,65) / FollowCamera RelativeLocation=0`，与旧构图等价且真实镜头位置进入 SpringArm Sweep。
- 地刺玩家反应已回读为 `Stop 0.70s`；追猎者保持 `Slow 0.60s / 0.45`。SpikeTrap 三个组件均 `bCanEverAffectNavigation=false`；SpikeMesh 运行时 Ignore Pawn，HurtZone 仅 Overlap Pawn，Slow 不取消 PathFollowing。
- `DemoEditor Win64 Development -Module=Demo -NoEngineChanges` 正式构建、链接通过。冷启动后四个 Blueprint 以 warning-as-error 编译通过；官方自动化 Heavy 5 项 + CharacterImpact 2 项共 `7/7 Success`、0 warning、0 error。
- 短 PIE 启动/停止正常，没有 Heavy/Light 初始化错误；实际冲锤墙边、起身闪动与地刺持续寻路仍由用户现场验收，任务保持 active。

## 实施检查点

- [x] 根因审计、方案和拟实现代码经用户确认。
- [x] 创建 `DOC/DailyPlan/2026-08-14-HeavyImpact不可回滚与有界恢复.md`。
- [x] 完成全工作区基线 commit/push、远端哈希与干净状态核验：`d819a1c2bc9540a9222585681cf3ee452484f083`。
- [x] 完成 C++、DataAsset 与自动化修改。
- [x] 完成 `DemoEditor Win64 Development` 构建、官方 MCP 数值回读与 7/7 自动化。
- [ ] 完成摆锤/冲锤真实 PIE 边界验证：正撞/擦边、奔跑/跳跃、长滚动、挂墙低能量收口、2 秒阻塞兜底、面朝上/下、Downed 二次命中、30/60/120 FPS。
- [ ] 等待用户最终视觉与手感验收。
