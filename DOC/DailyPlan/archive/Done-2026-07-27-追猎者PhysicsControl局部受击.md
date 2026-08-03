# 2026-07-27 追猎者 Physics Control 局部受击实施计划

> 状态：已完成并归档；实现、构建、资产装配和用户 PIE 验收均已完成。

## 目标

让现有 `BP_Pursuer` 在保持 Capsule、CharacterMovement 和 AI 权威的前提下，对真实模拟物理道具命中产生短时局部 Physics Control 反馈，并可靠恢复动画。

## 已授权范围

- 接入交接包的六个源码文件和一份调参 DataAsset。
- 按本机 UE5.8 API 修正返回值、字段与构造函数差异。
- 仅在 `BP_Pursuer` 设置 `PhysicsControlHitTuning`，编译并保存。
- 不修改 `DA_Pursuer` 参数，不修改 Manny、Physics Asset、AnimBP、磁力蓝图逻辑、伤害或 AI 逻辑。
- 用户追加授权：在 `/Game/Levels/Level0` 新增 3 个小型 `BP_MagneticProp` 测试 Actor，方便观察局部受击；不改既有 Actor。

## 完整文件清单

- 修改 `Source/Demo/Demo.Build.cs`
- 修改 `Source/Demo/Public/Characters/PursuerCharacter.h`
- 修改 `Source/Demo/Private/Characters/PursuerCharacter.cpp`
- 新增 `Source/Demo/Public/Components/Physics/PhysicsControlHitResponseComponent.h`
- 新增 `Source/Demo/Private/Components/Physics/PhysicsControlHitResponseComponent.cpp`
- 新增 `Source/Demo/Public/Data/Physics/PhysicsControlHitTuningData.h`
- 新增 `Content/ZeroEscape/Enemies/Physics/DA_PursuerPhysicsControlHit.uasset`
- 修改 `Content/ZeroEscape/Enemies/BP_Pursuer.uasset`
- 修改 `Content/Levels/Level0.umap`（实际资产 `/Game/Levels/Level0`，仅新增 3 个小型测试 Actor）

## 步骤与检查点

1. 以交接源码为基线落盘，保留事件/Delegate/Timer 方案和独立状态 Owner。
2. 适配 UE5.8：删除不存在字段，使用当前 Modifier 构造签名，将无返回值 Set API 改为顺序调用。
3. 构建 `DemoEditor Win64 Development`；任何新修正限制在已授权文件内。
4. 复制并加载调参资产；设置 `BP_Pursuer.PhysicsControlHitTuning`，编译、保存并回读。
5. PIE 验证 ready 日志、LeftArm/RightArm/Torso、冲量方向、至少 10 次连续命中、约 0.55 秒恢复、Capsule 稳定及追击/攻击连续性。
6. 在 Level0 的玩家—追猎者测试区新增 3 个小型磁力道具并保存，作为用户可直接抓取/投掷的验收物体。

## 回退点

- 源码回退：仅移除新增三文件，并还原 Build.cs 与 PursuerCharacter 的局部改动。
- 资产回退：清除 `BP_Pursuer.PhysicsControlHitTuning` 后删除本任务新增的调参资产；不触碰其他现有资产。
- 用户视觉验收前状态保持“待用户验收”，不标记完成。
