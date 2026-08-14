# 2026-08-14 HeavyImpact 不可回滚与有界恢复实现记录

## 结果

- 实现基线：`d819a1c2bc9540a9222585681cf3ee452484f083`，已在功能落盘前推送并核对内部 `origin/main`。
- 摆锤与冲锤只把 PreparationVolume 当作候选收集器；最终 Heavy 请求改由锤头真实盒体沿短时间运动轨迹 Sweep 接收者 PhysicsAsset。
- 接收端返回 `Accepted` 后不再走正常回滚。预期接触超时会继续 Heavy 物理流程，但不会伪造 `OnImpactCommitted`；同一来源稍后到达的真实 Chaos 接触仍只提交一次真实接触事件。
- 删除 Settling 抢跑起身与 5/10 秒正常硬切。身体持续处于低能量后进入 Downed；正常落地依然利用可行走支撑，但挂墙、夹缝中无支撑的低能量姿势也使用同一 0.35 秒窗口收口。
- Downed 睡眠完成后立即尝试起身；正常安全站位每 0.20 秒重试，最多 2.0 秒。截止时先以受击前位置为种子进行 200 cm 有界安全搜索；仍无解则按已确认玩法取舍回到受击前记录的角色变换、跳过起身动画并恢复控制，避免永久 Downed。
- Snapshot 到起身动画的淡入保持 0.30 秒。源码与动画曲线复核确认现有同帧 Snapshot 求值成立，四条起身动画的开头也基本静止，因此未保留“暂停 Montage”这一无证据补丁。当前仍只有正躺/趴躺两种固定起身首姿，任意物理终姿的姿势匹配观感继续由 PIE 验收。

## 调参与资产回读

- 玩家与追猎者 Heavy：`MaximumPreparationSeconds=0.18`、`RecoveryRetrySeconds=0.20`、`MaximumRecoveryBlockedSeconds=2.0`、`RecoverySnapshotBlendSeconds=0.30`。
- 冲锤与摆锤：`MaximumPreparationLeadTime=0.08`。
- 保留实际机关覆盖值：冲锤盒体 `60/140/140`、LookAhead `350`、伸出 `0.30s`、回收 `0.60s`；摆锤盒体 `110/40/75`、LookAhead `850`、摆幅 `79°`、长度 `520`。
- 重启编辑器后使用官方 UE5.8 MCP 回读四份 DataAsset，数值正确且均为非 dirty。

## 技术验证

- `DemoEditor Win64 Development` 在本轮修正后再次完整构建成功，11 个 Action，正式链接通过。
- 官方自动化发现并执行 7 项：HeavyImpact 5 项、CharacterImpact 2 项；结果 `7/7 Success`，无警告、无错误。
- Level0 进行 5 秒 Simulate-In-Editor 启动冒烟检查，未产生 `LogHeavyImpact` 初始化错误；该检查不替代真实机关命中验收。
- 静态清理确认：旧 FalsePositive 回滚命名、0.5 秒恢复延迟、Settling 提前交接、5/10 秒硬切字段和旧接收者速度外推均无残余引用。

## 待用户 PIE 验收

- 摆锤与冲锤分别验证正撞和擦边，重点观察 Accepted 后不再出现提前倒下又瞬间回正。
- 覆盖玩家奔跑、跳跃、长距离飞行与自然滚动；确认没有物理阶段总时长硬切。
- 覆盖静止身体、墙角/障碍阻塞、面朝上/下面起身、Downed 二次命中。
- 人为阻塞起身超过 2 秒，确认正常搜索或最终位置回退后恢复操作，不永久倒地。
- 在 30/60/120 FPS 下回归预测与恢复观感。Chaos 画面和手感仍以用户现场验收为准，构建与自动化不替代该结论。

## 用户复测后的相机、墙体与地刺增量

- 增量实现基线为 `47adb45d3d338a54d67cb55bef0088aa793901e9`，写入前已推送并确认工作区干净、远端 `main` 一致。
- 恢复重试 Timer 不再直接移动 Actor；只排队事务序号，下一次 Heavy `TG_PostPhysics` Tick 才执行安全位置查询和起身交接。由此 CameraBoom 能通过既有 prerequisite 在 CameraManager 采样前按最终 Capsule 位置重算。
- Heavy 物理期的 Actor/Capsule 骨盆跟随增加仅针对 `WorldStatic` 的胶囊 Sweep，玩家与追猎者共用；动态玩法物不会参与外壳 Sweep，物理 Mesh 也没有新增逐 Body Teleport 或纠偏。
- `BP_ZeroEscapeCharacter` 将相机子偏移折入 SpringArm 有效终点：`ArmLength 220→190`、`SocketOffset (0,55,65)→(0,65,65)`、`FollowCamera RelativeLocation (30,10,0)→0`。FOV 90、Camera Lag 12/60、ECC_Camera 与 Probe 12 均保持。
- `BP_Pursuer` 斧头、`BP_BatteringRamHazard` 四个视觉件、`BP_MagneticProp` 的物理箱新增 Camera Ignore；原碰撞 Profile、ObjectType、CollisionEnabled 与模拟状态均保持。
- `DA_SpikeStandingImpact` 的玩家 Stop 改为 `0.70s`；追猎者保持 `Slow 0.60s / 0.45`。官方回读和 C++ 均确认 SpikeTrap 不导出导航障碍，升降刺运行时 Ignore Pawn，Slow 不取消 AI PathFollowing。
- Demo 模块正式构建、链接成功；冷启动后四个 Blueprint warning-as-error 编译通过，相关资产均非 dirty；Heavy 5 项 + CharacterImpact 2 项自动化 `7/7 Success`、0 warning、0 error。短 PIE 无 Heavy/Light 初始化错误。
- 待用户在原墙边复现点确认：Capsule/相机锚点不再进墙、起身一帧闪是否消失、真实物理 Mesh 是否仍穿墙、墙角原生 SpringArm 碰撞伸展是否仍有跳变，以及地刺 0.70 秒 Stop 与 AI Slow 寻路体感。

## 冲锤 Heavy 方案 A 实现

- Heavy 准备请求新增来源参数 `PhysicalResponseScale`，默认 `1.0`。冲锤调参默认 `0.60`；摆锤、追猎者攻击及其他既有来源未赋值，继续保留完整响应。
- 真实 Hit 前保存骨盆线速度；Hit 后只削减沿来源速度方向新增的全身共同平移。实现对全部模拟 Body 施加同一个速度修正，因此不改变 Body 间相对线速度与角速度，也没有生成第二份人工受击冲量。
- 首次真实接触提交后立即结束 Mesh 对 `ECC_PhysicsBody` 的阻挡，墙与地面碰撞保持不变，避免运动学冲锤在后续帧持续向对墙推送。原 `PhysicsBodyReleaseDelaySeconds=0.15` 仅作为没有精确 Hit 的超时/径向 Heavy 兜底。
- 官方 UE5.8 MCP 回读 `DA_BatteringRamHazard_Default.PhysicalResponseScale=0.60`，原有盒体、行程和时序数值未改；资产和三个相关 Blueprint 未产生新二进制差异。
- `DemoEditor Win64 Development -Module=Demo -NoEngineChanges` 编译链接通过；`BP_BatteringRamHazard`、`BP_ZeroEscapeCharacter`、`BP_Pursuer` warning-as-error 编译通过；Heavy 5 项 + CharacterImpact 2 项自动化 `7/7 Success`、0 warning、0 error。
- 仍待用户真实 PIE 判定冲锤 `0.60` 的画面力度，以及首次接触后是否不再持续顶推；该手感验收不由构建或自动化代替。
