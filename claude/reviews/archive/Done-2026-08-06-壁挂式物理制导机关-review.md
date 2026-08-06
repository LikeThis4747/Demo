> 已处理完成，仅存档，无需继续阅读。处理结论：V1 按用户决定独立落盘；采纳 `IsStructurallyValid` 与三套轴约定；HeavyImpact 公共层延后到功能稳定后统一评估；制导力矩反算保留并补充恒定推力幅值下的用途说明。

# 壁挂式物理制导一次性机关 拟实现稿 —— 代码审阅

- 审阅对象：`claude/docs/2026-08-06-壁挂式物理制导一次性机关拟实现代码.md`（拟实现稿，未落盘）
- 审阅日期：2026-08-06
- 审阅性质：只读审阅，未修改任何功能 C++/Blueprint/资产/关卡/配置，未构建、未 PIE、未提交。
- 对照现状：`Source/Demo` 中已有 `IHeavyImpactReceiver` / `FHeavyImpactPreparationRequest` 接口，以及 `PendulumHazard`、`BatteringRamHazard` 两个已实现机关。

---

## 1. 依赖一致性核对

拟实现稿引用的现有合同与项目一致：

- `IHeavyImpactReceiver::GetHeavyImpactPredictionPrimitive() const`、`PrepareForHeavyImpact(const FHeavyImpactPreparationRequest&)` 签名匹配（见 `Source/Demo/Public/Interfaces/HeavyImpactReceiver.h`）。
- `FHeavyImpactPreparationRequest` 六个字段（`ImpactId` / `SourceActor` / `SourceComponent` / `PredictedImpactPoint` / `SourceLinearVelocity` / `EstimatedTimeToContactSeconds`）与拟实现稿填充一致（见 `Source/Demo/Public/Physics/HeavyImpactTypes.h`）。
- `EHeavyImpactPrepareResult`（Accepted/Duplicate/Busy/Invalid）用法一致。

一致性结论：拟实现稿只读现有 HeavyImpact 合同，未改签名，符合"并行任务接口保持稳定"的约束。

---

## 2. 代码规模

拟实现稿第 4 节共 6 个 C++ 文件（含 Doxygen 注释、UPROPERTY meta、空行）：

| 文件 | 含注释行数 | 性质 |
|---|---|---|
| `ThrustGuidedHazardTuningData.h` | ~178 | 纯数据 + 18 个调参字段 |
| `ThrustGuidedHazardTuningData.cpp` | ~122 | 一个 `IsConfigured` 范围校验 |
| `ThrustGuidedHazardLauncher.h` | ~139 | 触发器声明 |
| `ThrustGuidedHazardLauncher.cpp` | ~335 | 锁定/预警/锥体瞄准/延迟生成 |
| `ThrustGuidedHazardProjectile.h` | ~217 | 弹体声明 |
| `ThrustGuidedHazardProjectile.cpp` | ~1011 | 制导数学 + 预测 + 碰撞回调 |
| **合计** | **~2000 行** | 有效逻辑约占 55–60% |

规模判断：对一个"一次性机关"偏重。复杂度集中在 `Projectile.cpp` 两块——`TryCalculateGuidedForceDirection`（PD→扭矩→横向力反算，~90 行）与 `BuildPreparationRequest` + `CalculateCapsuleRaySurfaceDistance`（重冲击预测，~150 行）。`TuningData` 的 18 个字段 + 逐字段范围校验也是行数大户。

---

## 3. 明显冗余（最高优先级）

**三个机关在重复同一套 HeavyImpact 预测管线。** 与 `PendulumHazard.cpp`、`BatteringRamHazard.cpp` 并排对照，以下几乎是复制粘贴：

1. **帧感知窗口常量三处各写一遍**：`MaximumPreparationFrameMultiplier = 2.5f`、`AbsoluteMaximumPreparationSeconds = 0.5f`。
   - `BatteringRamHazard.cpp:27-31`、`PendulumHazard.cpp:40-41`、拟实现稿 `1096-1099`，一字不差。
2. **`EvaluatePreparationCandidates` 结构相同**：快照候选 → 检查 overlap/接口 → `NotifiedReceivers` 去重 → `Execute_PrepareForHeavyImpact` → 仅在 Accepted/Duplicate 记录。
3. **`BuildPreparationRequest` 骨架相同**：取 `GetHeavyImpactPredictionPrimitive` → 校验 owner/collision → 算 SurfaceGap → closing speed 下限过滤 → `FrameAwareMaximumSeconds = Min(Absolute, Delta*Multiplier)` → `AllowedMaximumSeconds = Max(MaximumPreparationLeadTime, FrameAware)` → 填 `OutRequest`。三份仅"表面距离怎么算"不同。
4. **"射线到自身表面距离"工具族**：Pendulum 有 box 版，拟实现稿新写 capsule 版 `CalculateCapsuleRaySurfaceDistance`，同族几何。
5. **`DisableHazard` / `OnConstruction` 预览 / `PreparationVolume` 的 Query-only Pawn 碰撞配置 / `bGenerateOverlapEvents`** 三处样板重复。

结论：这是**第三次复制**，不是本稿独有问题。每加一个机关就多复制约 100–150 行预测代码，且改 HeavyImpact 语义要同时改三处，是明确维护隐患。

**处理建议**：落盘前先抽公共层——`UHeavyImpactPredictionComponent`（承载流程/去重/帧窗口/Execute）+ `IHeavyImpactSource` 接口（宿主注入速度/ImpactId/源组件/接近参数）+ `HeavyImpactGeometry` 静态工具库（box/capsule/bounds 表面距离）。详见配套草案 `claude/docs/2026-08-06-HeavyImpact预测公共层抽取草案.md`。

---

## 4. 迂回 / 过度设计（"绕过感"来源，非违规）

`TryCalculateGuidedForceDirection`（`1486-1574`）：

- 先用 PD 算 `NormalizedTorqueCommand`，再 `tau = r×F` 反算横向力，再 `ForwardForceMagnitude = sqrt(Thrust² - Lateral²)` 合成"期望世界力方向"；
- 该方向交给 `AimThrusterAtWorldForceDirection` 后**又被 `ClampDirectionToCone` 二次钳到 gimbal 锥体内**，最终只是把 Thruster 组件转过去。

问题：推力大小恒定为 `ThrustStrength`（Thruster 沿 -X 恒定施力），真正控制量只有"喷口方向"一个。前面整套扭矩/杠杆臂/`sqrt` 反算产出的其实只是一个被限幅的方向向量；既然最终又 clamp 到 cone，这套反算属于**冗余的间接层**，可用"期望转向方向 + 角速度阻尼"直接做锥内方向 PD 替代。

建议：要么简化，要么在注释显式声明"力幅值恒定，此推导仅用于确定锥内方向"，避免读者误以为在做真实矢量推力分配。

次要：`Tick` 里 `DesiredForceDirection` 先算 guided、失导后又整体覆盖为 `GetUpVector()`（`1345-1362`），逻辑对但绕，可合并。

---

## 5. 其他正确性 / 一致性点（非阻塞）

1. **未复用 `FHeavyImpactPreparationRequest::IsStructurallyValid`**：类型自带该校验（`HeavyImpactTypes.h:74`），但拟实现稿（和 BatteringRam）构造完只 check `ContainsNaN`。发送前调一次更稳、更一致。
2. **`GetUpVector()` 当"前进轴"**：弹体局部 +Z 为纵轴，全篇用 `GetUpVector()` 表前向，与 Launcher `FindBetweenNormals(UpVector, InitialDirection)` 自洽，但对维护者不直观，建议在 .h 顶部显式声明轴约定。
3. **Thruster 施力轴假设（-X）**：依赖 UE5.8 `UPhysicsThrusterComponent` 内部实现。`BeginPlay` 有 `GetAttachParent()==ProjectileBody` 直接附着校验（做得好），但"-X 施力"前提无运行期断言，建议保留注释指向引擎版本，避免升级后静默失效。
4. **接收者间隙 Bounds 回退**（`1776-1794`）逻辑正确，但与 Pendulum 又是一处平行实现，归入第 3 节一并抽取。

---

## 6. 有没有"绕过代码"

- **绕过项目规则 / Git / 落盘流程**：无。稿件开头明确标注"审阅稿、未落盘、未构建、未提交"，第 10 节列出实施门槛（先写 DailyPlan、核归属、基线 commit/push 到工蜂、再授权实现），克制。
- **绕过物理 / 职责边界的 hack**（直接写速度、AddImpulse、LaunchCharacter、把 NormalImpulse 反算补偿力、碰撞去抖）：刻意避免，第 7 节专门解释了"为什么不去抖""为什么首碰不结束推进""HeavyImpact 不施加冲量"。设计纪律良好。
- 唯一带"绕过味道"的是第 4 节的制导力反算迂回层——不违规，但用复杂路径实现本可直接表达的"锥内方向控制"。

---

## 7. 结论与建议顺序

- **规模**：~2000 行含注释、6 文件，偏重；重量集中在制导数学与预测。
- **明显冗余**：有，且是第三次复制 HeavyImpact 预测管线。落盘前应先抽公共层，最高优先级。
- **绕过代码**：无违规绕过；制导力反算是可简化的迂回层。

建议顺序（推进时序见配套草案第 7 节，2026-08-06 更新）：

1. ThrustGuided 先按独立实现落盘、在 Level0 验证跑通，**短暂容忍第三份预测复制**——因为它仍是纸面稿、且两个已有机关尚未稳定，此时不动公共层重构。
2. 落盘前顺手处理本稿自身问题：简化 / 注明制导方向控制（第 4 节）、接入 `IsStructurallyValid`（第 5 节）。
3. 待三个机关都验证稳定后，再统一抽 HeavyImpact 预测公共层（惠及全部三个机关）——见配套草案。
4. 全程遵守拟实现稿第 10 节的落盘门槛。
