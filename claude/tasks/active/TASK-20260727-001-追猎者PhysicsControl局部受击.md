# TASK-20260727-001 — 追猎者 Physics Control 局部受击

- Owner：Codex 当前会话
- Status：active
- Stage：已授权实现
- Created：2026-07-27
- Updated：2026-07-27

## 目标与验收

- 目标：接入交接包中的 Physics Control 局部受击，让追猎者保持 Capsule、CharacterMovement 与 AI 权威，仅在真实物理道具命中 Manny Physics Asset 时短暂模拟 LeftArm、RightArm 或 Torso，并由 Timer 恢复动画。
- 验收：源码通过 `DemoEditor Win64 Development` 构建；`BP_Pursuer` 正确引用调参资产且编译保存；PIE 中三类区域响应方向正确；连续至少 10 次命中无永久模拟、爆炸、穿地或 AI/攻击时序丢失；停止命中后约 0.55 秒恢复；无新增 Error。
- 非目标：不修改 `DA_Pursuer` 追猎参数；不实现伤害、死亡、失衡、倒地或全身 Ragdoll；不修改 Manny、Physics Asset、AnimBP、磁力蓝图逻辑或攻击 AI。Level0 只允许新增本任务的小型磁力测试物体。

## 修改范围

- 允许修改：
  - `Source/Demo/Demo.Build.cs`
  - `Source/Demo/Public/Characters/PursuerCharacter.h`
  - `Source/Demo/Private/Characters/PursuerCharacter.cpp`
  - `Source/Demo/Public/Components/Physics/PhysicsControlHitResponseComponent.h`（新增）
  - `Source/Demo/Private/Components/Physics/PhysicsControlHitResponseComponent.cpp`（新增）
  - `Source/Demo/Public/Data/Physics/PhysicsControlHitTuningData.h`（新增）
  - `Content/ZeroEscape/Enemies/Physics/DA_PursuerPhysicsControlHit.uasset`（新增）
  - `Content/ZeroEscape/Enemies/BP_Pursuer.uasset`（仅设置 `PhysicsControlHitTuning`，编译并保存）
  - `Content/Levels/Level0.umap`（资产 `/Game/Levels/Level0`；仅新增 3 个小型 `BP_MagneticProp` 测试 Actor）
  - 本任务卡、当日计划及项目规定的完成/交接记录。
- 共享/潜在冲突：`PursuerCharacter.h/.cpp` 与既有追猎者第一步任务重叠；本任务只做已授权的第二步组件装配，不改追击/攻击逻辑。`BP_Pursuer` 仅修改继承 CDO 属性。
- 并行拆分/依赖：不并行修改重叠文件；依赖 PhysicsControl 插件、本机 UE5.8 Physics Control API、Manny Physics Asset 和现有磁力物理道具。
- 用户已授权范围：2026-07-27 用户明确允许“最小接入方案，包括 UE5.8 兼容修正；不修改 DA_Pursuer 参数”；随后明确要求直接在场景中添加几个小物体，以便观察局部受击效果。

## 计划与检查点

- [x] 只读核对交接包、当前源码、插件启用状态与 `BP_Pursuer` 基线。
- [x] 识别 UE5.8 API 差异：三个 Set API 返回 `void`、移除 `bUseAccelerationDriveMode`、Modifier 构造函数为 6 参数。
- [ ] 接入六个源码文件并复制调参 DataAsset。
- [ ] 构建 `DemoEditor Win64 Development`。
- [ ] 在 `BP_Pursuer` 设置 `PhysicsControlHitTuning`，编译并保存。
- [ ] PIE 验证 ready 日志、三类区域、连续命中、恢复、Capsule 与追击/攻击连续性。
- [ ] 在 Level0 新增并保存 3 个小型磁力测试物体，不修改磁力蓝图或既有 Actor。
- [ ] 更新验证记录并交给用户视觉验收。

## 验证

- 命令/场景：`DemoEditor Win64 Development`；当前追猎者测试关卡 PIE；真实模拟物理道具命中 Manny。
- 结果：实施中。
- 用户操作（时机、步骤、预期结果）：自动验证通过后由用户最终确认局部受击幅度、方向和恢复手感。
- 用户验收：待验收

## 阻塞、风险与下一步

- 阻塞：若新增反射类无法由当前编辑器热重载，需要先确认无未保存资产，再完整重启编辑器。
- 风险：开启 Mesh 的 QueryAndPhysics 后必须验证地面/环境碰撞；Physics Control 是 Experimental 插件；交接包的既有烟测不能替代本机 UE5.8 重建与玩家验收。
- 下一步：源码接入与构建。
