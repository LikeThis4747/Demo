# TASK-20260814-001 HeavyImpact 误触发与恢复卡死诊断

- Owner：Codex `/root`
- 任务类型：实现
- 实现文件写入权：Codex `/root` 独占（用户 2026-08-14 明确授权本轮 Heavy/相机与地刺时长修复）
- Status：active
- Stage：冲锤 Heavy 方案 A 已完成代码、构建、装配回读与自动化；等待用户在原复现点验收力度和持续顶推
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
3. 保留自然飞行、落地和滚动；低线速、低角速连续 0.35 秒仍可提前收口，但持续 Chaos 模拟统一以 1.5 秒为上限。
4. 不把落地或可行走支撑当成进入 Downed 的必要前提；地面旋转、空中卡墙和贴墙抖动均不能绕过统一上限。
5. Downed 睡眠完成后立即尝试安全起身，不再固定等待 0.5 秒。正常站位受阻时每 0.2 秒重试，最多 1.5 秒。
6. Heavy 飞行继续由 Physics Asset 刚体掌权，QueryOnly Capsule 只跟随、不参与阻挡。起身使用最终骨盆附近现有 0/20/40/60 cm 有界搜索；若骨盆刚跨过近处静态墙体，只把候选向受击前所在墙面一侧小幅纠偏并继续完整安全校验。受击前位置不作为目的地，禁止大范围回飞。
7. Snapshot 到起身动画的显式淡入保持 0.30 秒；本轮不修改 Montage 播放时序、AnimBP 或起身动画。

## 2026-08-14 用户复测后增量根因

- 玩家与追猎者确实使用同一 `UHeavyImpactResponseComponent`；差异只在各自的 Heavy DataAsset、骨架/起身动画及 AI 恢复后的导航接管。
- AI 挂墙不起不是 Controller 漏了恢复回调，而是共享 Heavy 前半段可能长期达不到速度稳定阈值；原有 Downed 后截止计时覆盖不到这段持续模拟。
- 共享修正为：骨盆低线速+低角速连续 0.35 秒仍可自然提前收口；无论是否有支撑，持续模拟达到 1.5 秒都进入同一 Downed/起身流程。自由飞行顶点的低速窗口小于 0.35 秒，不会因此抢跑起身。
- 起身交接的代码与 AnimGraph 顺序经复核成立：Snapshot 在同帧被求值，四条 GetUp Sequence 的前 0.75 动画秒也基本静止。当前闪感更符合“任意物理终姿只映射到正躺/趴躺两种固定首姿”的姿势差异；暂停 Montage 或继续延长 Blend 都不是有证据的修复，因此本轮不加入该补丁。

## 2026-08-14 玩家墙体穿入与相机增量反馈

- 用户复测发现玩家被冲锤击入走廊墙体；该问题与追猎者挂墙同属共享 Heavy 边界问题，不做玩家或 AI 特判。两者均使用 `UHeavyImpactResponseComponent`，差异只来自各自骨架、Physics Asset 与 Heavy 调参资产。
- Heavy 将 Mesh 脱离角色外壳并全身模拟，同时用无 Sweep 的 `TeleportPhysics` 让 QueryOnly Actor/Capsule 追随物理骨盆。这是全身 Ragdoll 的既有权威划分：Physics Asset 负责飞行碰撞，Capsule 不得反向拦住身体；需要分别验收真实骨骼穿墙和恢复位置，不能用相机修复掩盖真实骨骼穿模。
- 墙体穿入列为本轮 P0 验收阻断：修复必须落在共享 Heavy 或真实碰撞配置，不得在玩家类、AIController 或具体冲锤里增加位置特判。
- 最小处理顺序：保留 Physics Asset 身体主导击飞与 QueryOnly Capsule 跟随；起身仍从最终骨盆附近搜索。只有短线检测证明最终骨盆刚跨过近处 `WorldStatic` 墙体时，才沿墙法线向受击前所在一侧寻找不超过 60 cm 的站立点；该点仍必须通过可行走地面与完整站立 Capsule 无重叠校验。
- 起身 1.5 秒兜底也纳入同一墙体验收：受击前位置只用于判断墙的内侧，不作为恢复位置；不得大范围拉回。若真实 Mesh 仍频繁穿墙，后续单独验证 Physics Asset、CCD、物理子步与墙体厚度，不增加玩家/AI/机关特判。
- 相机修复继续保持独立职责：动态玩法物忽略 Camera、起身位移改在 Heavy PostPhysics 执行、真实镜头终点参与 SpringArm Sweep；这些只能解决画面闪动和相机入墙，不能替代角色身体的墙体约束。

## 已授权最小实施范围

- 收紧 `ABatteringRamHazard` 与 `APendulumHazard` 的预测：PreparationVolume 只收集候选，最终请求必须通过锤头真实盒体对接收 Mesh 的短时几何 Sweep。
- `UHeavyImpactResponseComponent` 保持唯一 Heavy/Physics Control Owner，不新增组件、接口或状态；删除正常 Prepared 回滚、Settling 抢跑起身与 5/10 秒正常硬切。
- 清理已失效的 `FalsePositive` 命名、旧调参字段与对应测试，不保留双路径或兼容开关。
- 不修改 Light、追猎者攻击、Heavy PCA、AnimBP、起身动画、关卡或其他机关。

## 2026-08-14 相机、墙体与地刺增量实施范围

- Heavy 恢复 Timer 只排队，不再在 SpringArm 已完成 PostPhysics 后直接移动 Capsule；下一次 Heavy PostPhysics 消费请求，让 CameraBoom 在同帧随后重算。
- Heavy 飞行保持 Physics Asset 身体权威与 QueryOnly Capsule 无阻挡跟随；恢复阶段仅在最终骨盆刚跨过近处静态墙体时，对玩家和追猎者统一使用不超过 60 cm 的墙内候选偏置，不添加角色分类分支。
- 修正相机碰撞职责：`BP_Pursuer` 斧头、`BP_BatteringRamHazard` 视觉件、`BP_MagneticProp` 对 Camera Ignore；保留各自专用玩法碰撞体。
- `BP_ZeroEscapeCharacter` 将 FollowCamera 的额外相对偏移折入 SpringArm 有效终点，确保真实镜头位置参与墙体 Sweep；不修改 FOV、控制旋转或建立新 CameraManager。
- `/Game/ZeroEscape/Physics/Impact/DA_SpikeStandingImpact` 仅把玩家 Stop `DurationSeconds` 从 `0.25` 改为 `0.70`；追猎者仍为 `Slow 0.60 / 0.45`。
- 地刺导航只读结论：GrateMesh、SpikeMesh、HurtZone 均 `bCanEverAffectNavigation=false`；运行时 SpikeMesh 对 Pawn Ignore，HurtZone 仅 Pawn Overlap，AI Slow 不取消 PathFollowing。格栅保留地板碰撞，但不导出 Recast 障碍。
- 不新增碰撞通道、SpringArm 子类、PlayerCameraManager、逐 Body 纠偏、兼容开关或机关特判；不修改 Level0、AnimBP、Heavy PCA、追猎者攻击和地刺 C++。
- 并行纯文档改动已按用户授权纳入检查点；本次增量实现实际基线为远端 `47adb45d3d338a54d67cb55bef0088aa793901e9`，写入前工作区与远端均已核对干净。

## 2026-08-14 增量实施与验证结果

- Heavy 恢复 Timer 现在只排队序号；真正的 `TryBeginRecovery` 在下一次组件 `TG_PostPhysics` Tick 执行。已有 Heavy→CameraBoom prerequisite 因此能在同帧让弹簧臂使用最终 Capsule 位置更新，消除 Timer 位于 SpringArm 之后造成的一帧旧缓存。
- 已撤销“物理期用站立 Capsule 阻挡 Sweep 跟随”的实验：它会让相机留在受击点，并使远距离正常击飞在恢复时被拉回。当前恢复原有 QueryOnly Capsule 无阻挡跟随，起身搜索继续以最终物理骨盆为中心；新增的唯一墙体处理是近墙跨越时最多 60 cm 的墙内候选偏置，且最终候选仍执行可行走地面与完整站立 Capsule 校验。
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
- [ ] 完成摆锤/冲锤真实 PIE 边界验证：正撞/擦边、奔跑/跳跃、长滚动、挂墙低能量收口、1.5 秒模拟上限与 1.5 秒阻塞兜底、墙内起身、面朝上/下、Downed 二次命中、30/60/120 FPS。
- [ ] 等待用户最终视觉与手感验收。

## 2026-08-14 冲锤 Heavy 方案 A

- 用户选择来源侧可调的最小方案：Heavy 请求新增 `PhysicalResponseScale`，默认 `1.0`，未配置的摆锤、追猎者攻击和其他来源保持既有效果；冲锤默认写入 `0.60`。
- 缩放不伪造第二份冲量：组件在真实 Chaos Hit 前缓存骨盆线速度，在同一 Hit 后只削减沿来源速度方向新增加的全身共同平移速度。对所有模拟 Body 加相同速度修正，因此保留各刚体相对线速度、局部姿态变化和角速度。
- 一个 Heavy 事务只消费首次真实接触。提交后 Mesh 立即 Ignore `ECC_PhysicsBody`，保留墙、地板等世界几何碰撞，避免运动学冲锤在随后数帧持续把角色顶向对墙。原 `0.15s` 仅保留为没有精确真实 Hit 的超时/径向事务兜底。
- 没有增加角色类型、机关类型判断、状态、组件或资产；调节入口归冲锤 DataAsset，接收组件只消费通用请求字段。
- `DemoEditor` 冷构建通过；`BP_BatteringRamHazard`、`BP_ZeroEscapeCharacter`、`BP_Pursuer` 均以 warning-as-error 编译通过；官方回读冲锤 `PhysicalResponseScale=0.60`，其余时序/几何参数未改；Heavy 5 项 + CharacterImpact 2 项自动化 `7/7 Success`。
- 待用户 PIE 验收：冲锤侧撞时位移是否明显收敛、局部翻滚是否仍可见、是否不再被同一锤头连续顶推；摆锤原效果不得变化。

## 2026-08-16 安全恢复终点提交修复

- 用户已授权继续实现；Codex `/root` 重新认领共享 Heavy 实现写入权。
- 当前根因证据：恢复候选终点已经通过完整站立 Capsule 与可行走地面校验，但真实 Capsule 可能已由 Heavy 的 QueryOnly 跟随进入墙体或地板；交接继续从该非法真实起点执行 Sweep，失败后截止分支又在当前非法位置恢复 Gameplay，最终造成 `CharacterMovement stuck`。
- 本轮只修共享 `UHeavyImpactResponseComponent` 的放置事务：起点可用时保留 Sweep；起点已阻挡重叠时，在 QueryOnly 阶段直接提交已验证终点，并在恢复碰撞前后再次校验完整 Capsule。
- 删除“安全终点提交失败后在当前 Heavy 位置恢复 Gameplay”的危险路径；失败不得伪装成成功恢复。
- 不新增安全锚点、状态、接口或配置，不修改摆锤、冲锤、角色类、Physics Asset、DataAsset、AnimBP、关卡和碰撞通道。
- 实现基线：`48c7b7422bc98e6d4096723712d323419327561f`，HEAD / `origin/main` / 远端 main 一致，写入前工作区干净。
- 已完成共享 Heavy 的最小放置事务修正：安全终点会在提交前、提交后以及恢复 Gameplay 碰撞前使用原始完整 Capsule 响应重新验证；仅当真实 QueryOnly Capsule 起点已处于阻挡重叠时，才跳过从非法起点离开的 Sweep，直接提交已验证的局部终点。
- 已删除截止分支“在当前非法 Heavy 位置恢复 Gameplay”的路径；放置失败继续保持 Downed，绝不把 CharacterMovement 交还给仍重叠的 Capsule。
- `Demo` 模块冷构建通过；官方自动化 HeavyImpact 5 项 + CharacterImpact 2 项 `7/7 Success`；玩家/追猎者 Blueprint warning-as-error 编译通过。真实墙边/地板/柱角 PIE 仍待用户验收，任务保持 active。
