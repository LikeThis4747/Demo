# HeavyImpact 预测公共层抽取草案

- 状态：讨论稿，未落盘。本文只规划重构方向，不修改任何功能 C++/Blueprint/资产。
- 背景：`PendulumHazard`、`BatteringRamHazard` 与拟实现的 `ThrustGuidedHazard*` 三个机关重复了同一套 HeavyImpact 预测管线（详见 `claude/reviews/2026-08-06-壁挂式物理制导机关-review.md` 第 3 节）。
- 目标：把重复的预测流程抽成可插拔公共层，改语义只改一处，三个机关各减约 100–150 行。

---

## 1. 设计取舍

**用组件，不用基类。** 三个机关运动学本质不同（自动周期 / 摆动 / 自由物理弹体），强行塞进 `AHeavyImpactHazardBase` 会把生命周期也绑死；且项目既有决策是"组件 > 继承"。故抽成可插拔的 `UHeavyImpactPredictionComponent`。

**抽什么、留什么**：

| 能力 | 归属 | 理由 |
|---|---|---|
| 帧感知窗口常量、ETA 上限过滤、去重集合、Accepted/Duplicate 判定、候选遍历 + 接口检查 + `Execute_PrepareForHeavyImpact` | 抽进组件 | 三份逐字相同 |
| box / capsule 射线表面距离、接收者最近点 / Bounds 回退 | 抽进几何工具库 | 形状不同但同族，集中可测试 |
| 源速度来源、当前 ImpactId、源碰撞体、"接近参数怎么算" | 留宿主，经接口注入 | 运动学本质差异，无法统一 |

**顺带收益**：`HeavyImpactTypes.h` 中 `PredictedImpactPoint` 与 `SourceLinearVelocity` 均注明"仅用于诊断"。故 ThrustGuided 原稿 `BuildPreparationRequest` 后半段 ~30 行"双向预测位置 + 二次 capsule 前表面"是给诊断字段做的过度精算，统一到基础预测点后可直接砍掉。

---

## 2. 几何工具库 `HeavyImpactGeometry.h`（无状态静态函数）

```cpp
// 职责：集中机关侧"射线到自身表面距离"与接收者最近间隙估算，供预测组件与各机关复用。
// 边界：纯几何，不读世界状态、不施力、不依赖具体机关类型。

/** 一次接近估算的结果；预测点字段仅用于诊断。 */
struct DEMO_API FHeavyImpactApproach
{
    bool bValid = false;
    float SurfaceGap = 0.0f;  // 源前表面到接收者最近表面的间隙(cm)
    FVector ApproachDirWorld = FVector::ZeroVector;   // 源→接收者单位方向
 FVector SourceSurfacePoint = FVector::ZeroVector; // 源前表面点(世界)
};

namespace Demo::HeavyImpactGeometry
{
    /** 盒体中心沿世界方向到当前朝向盒面的距离（Pendulum/BatteringRam 用）。 */
    DEMO_API float RaySurfaceDistanceForBox(
        const UBoxComponent& Box, const FVector& WorldDirection);

    /** 胶囊中心沿世界方向到圆柱段/端球表面的精确距离（ThrustGuided 用，原稿逻辑照搬）。 */
    DEMO_API float RaySurfaceDistanceForCapsule(
const UCapsuleComponent& Capsule, const FVector& WorldDirection);

    /**
     * 通用接收者间隙：优先 GetSquaredDistanceToCollision 最近点，失败回退 Bounds 轴向投影。
     * 返回 ApproachDirWorld / SurfaceGap(仅接收者侧) / 接收者最近表面点。
     * 源前表面距离由调用方按源形状另算并扣减。
     */
    DEMO_API FHeavyImpactApproach ComputeApproach(
        const FVector& SourceCenter,
    const UPrimitiveComponent& ReceiverPrimitive);
}
```

> `RaySurfaceDistanceForCapsule` 即原稿 `CalculateCapsuleRaySurfaceDistance` 原样搬入；box 版从 Pendulum 搬。三种形状集中一处、可单测。

---

## 3. 宿主接口 `IHeavyImpactSource`（机关 Actor 实现，供组件回调）

```cpp
class DEMO_API IHeavyImpactSource
{
    GENERATED_BODY()
public:
 /** 当前有效的一次性 ImpactId；无效(未激活)时返回无效 GUID，组件据此跳过。 */
    virtual FGuid GetActiveImpactId() const = 0;

    /** 最终与角色阻挡接触的真实刚体组件（RamBody / bob / ProjectileBody）。 */
    virtual UPrimitiveComponent* GetHeavyImpactSourceComponent() const = 0;

    /** 本次预测使用的源世界线速度（Ram 用规划速度、弹体用真实 Chaos 线速度）。 */
    virtual FVector GetHeavyImpactSourceWorldVelocity() const = 0;

    /**
     * 给定接收者预测组件，算出接近参数（间隙/方向/源前表面点）。
     * 各机关用自己的源形状 + HeavyImpactGeometry 工具实现；这是唯一保留的差异点。
     */
    virtual FHeavyImpactApproach ComputeApproachToReceiver(
        const UPrimitiveComponent& ReceiverPrimitive) const = 0;
};
```

---

## 4. 预测组件 `UHeavyImpactPredictionComponent`

```cpp
UCLASS(ClassGroup = (HeavyImpact), meta = (BlueprintSpawnableComponent))
class DEMO_API UHeavyImpactPredictionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
  /** 宿主每次想通知时调用：遍历候选→过滤→去重→PrepareForHeavyImpact。 */
    void EvaluateAndNotify(float MaximumPreparationLeadTime);

    /** 事件驱动的宿主(摆锤/弹体)用：登记/移除仍在预测体积内的接收者。 */
    void RegisterCandidate(AActor* Actor);
    void UnregisterCandidateIfLeft(AActor* Actor, const UPrimitiveComponent* Volume);

    /** 即时查询的宿主(冲锤)用：每帧从体积刷新候选集合。 */
    void RefreshCandidatesFromVolume(const UPrimitiveComponent& Volume);

    /** 新的一次冲击周期开始时清空本轮去重集合(不清候选)。 */
    void BeginNewImpact();

private:
    // 三处曾各写一遍的常量，现在只此一份
    static constexpr float MaximumPreparationFrameMultiplier = 2.5f;
    static constexpr float AbsoluteMaximumPreparationSeconds = 0.5f;
    static constexpr float MinimumClosingSpeedCmPerSecond = 1.0f; // 或由宿主传阈值

    TSet<TWeakObjectPtr<AActor>> Candidates;
    TSet<TWeakObjectPtr<AActor>> NotifiedThisImpact;
};
```

核心实现（三个机关合并出来的唯一真身）：

```cpp
void UHeavyImpactPredictionComponent::EvaluateAndNotify(float MaxLeadTime)
{
    IHeavyImpactSource* Source = Cast<IHeavyImpactSource>(GetOwner());
    if (!Source) { return; }

    const FGuid ImpactId = Source->GetActiveImpactId();
    if (!ImpactId.IsValid()) { return; }

    UPrimitiveComponent* SourceComp = Source->GetHeavyImpactSourceComponent();
    const FVector SourceVel = Source->GetHeavyImpactSourceWorldVelocity();
    if (!IsValid(SourceComp)) { return; }

    // 快照，避免 Execute 内 EndOverlap 改集合破坏迭代
    TArray<TWeakObjectPtr<AActor>> Snapshot(Candidates.Array());
    const float DeltaSeconds = GetWorld() ? FMath::Max(0.f, GetWorld()->GetDeltaSeconds()) : 0.f;
    const float AllowedMax = FMath::Max(
        MaxLeadTime,
        FMath::Min(AbsoluteMaximumPreparationSeconds, DeltaSeconds * MaximumPreparationFrameMultiplier));

    for (const TWeakObjectPtr<AActor>& Weak : Snapshot)
    {
        AActor* Receiver = Weak.Get();
        if (!IsValid(Receiver)) { Candidates.Remove(Weak); continue; }
      if (NotifiedThisImpact.Contains(Weak)) { continue; }
 if (!Receiver->GetClass()->ImplementsInterface(UHeavyImpactReceiver::StaticClass())) { continue; }

      UPrimitiveComponent* RecvPrim =
        IHeavyImpactReceiver::Execute_GetHeavyImpactPredictionPrimitive(Receiver);
        if (!IsValid(RecvPrim) || RecvPrim->GetOwner() != Receiver
            || RecvPrim->GetCollisionEnabled() == ECollisionEnabled::NoCollision) { continue; }

        const FHeavyImpactApproach A = Source->ComputeApproachToReceiver(*RecvPrim);
        if (!A.bValid) { continue; }

        const float ClosingSpeed =
        FVector::DotProduct(SourceVel - Receiver->GetVelocity(), A.ApproachDirWorld);
        if (!FMath::IsFinite(ClosingSpeed) || ClosingSpeed < MinimumClosingSpeedCmPerSecond) { continue; }

        const float ETA = A.SurfaceGap / ClosingSpeed;
        if (!FMath::IsFinite(ETA) || ETA > AllowedMax) { continue; }

    FHeavyImpactPreparationRequest Req;
        Req.ImpactId = ImpactId;
        Req.SourceActor = GetOwner();
  Req.SourceComponent = SourceComp;
        Req.SourceLinearVelocity = SourceVel;
     Req.EstimatedTimeToContactSeconds = ETA;
        Req.PredictedImpactPoint = A.SourceSurfacePoint + SourceVel * ETA; // 诊断用，够精度

        FString Why;
        if (!Req.IsStructurallyValid(Receiver, Why)) { continue; } // 顺手接上原本没用的官方校验

        const EHeavyImpactPrepareResult R =
            IHeavyImpactReceiver::Execute_PrepareForHeavyImpact(Receiver, Req);
        if (R == EHeavyImpactPrepareResult::Accepted || R == EHeavyImpactPrepareResult::Duplicate)
        {
            NotifiedThisImpact.Add(Weak);
   }
    }
}
```

---

## 5. 宿主改造示例（ThrustGuided 弹体，节选）

```cpp
// 头文件：多继承接口 + 持有组件
class AThrustGuidedHazardProjectile : public AActor, public IHeavyImpactSource { ... };
UPROPERTY() TObjectPtr<UHeavyImpactPredictionComponent> Prediction;

// overlap 事件只转发登记
void AThrustGuidedHazardProjectile::HandlePreparationVolumeBeginOverlap(...)
{ Prediction->RegisterCandidate(OtherActor); }
void AThrustGuidedHazardProjectile::HandlePreparationVolumeEndOverlap(...)
{ Prediction->UnregisterCandidateIfLeft(OtherActor, PreparationVolume); }

// 60Hz Timer 只剩一行
void AThrustGuidedHazardProjectile::EvaluatePreparationCandidates()
{ Prediction->EvaluateAndNotify(RuntimeTuningData->MaximumPreparationLeadTime); }

// 实现接口：只保留真正独有的几何/速度
FGuid  GetActiveImpactId() const { return LaunchId; }
UPrimitiveComponent* GetHeavyImpactSourceComponent() const { return ProjectileBody; }
FVector GetHeavyImpactSourceWorldVelocity() const { return ProjectileBody->GetPhysicsLinearVelocity(); }

FHeavyImpactApproach ComputeApproachToReceiver(const UPrimitiveComponent& RecvPrim) const
{
    const FVector Center = ProjectileBody->GetCenterOfMass();
    FHeavyImpactApproach A = Demo::HeavyImpactGeometry::ComputeApproach(Center, RecvPrim);
    if (!A.bValid) { return A; }
    const float SrcDist =
    Demo::HeavyImpactGeometry::RaySurfaceDistanceForCapsule(*ProjectileBody, A.ApproachDirWorld);
    A.SurfaceGap = FMath::Max(0.f, A.SurfaceGap - SrcDist);
    A.SourceSurfacePoint = Center + A.ApproachDirWorld * SrcDist;
    return A;
}
```

冲锤宿主差别仅三点：`GetHeavyImpactSourceWorldVelocity` 返回规划速度、`ComputeApproachToReceiver` 用 box 工具、Tick 里先 `RefreshCandidatesFromVolume` 再 `EvaluateAndNotify`。

---

## 6. 收益与需回归验证的行为变化

**收益**
- 三个机关各砍约 100–150 行；`EvaluateAndNotify` / 帧窗口 / 去重 / `Execute` 从三份变一份，改语义只动一处。
- 顺带接上原本未用的 `IsStructurallyValid`；ThrustGuided 砍掉过度精算的诊断预测。
- 组件流程可单测（喂假接收者），几何工具可单独单测。

**需诚实标注、落盘后回归**
1. **closing speed 基准统一**：BatteringRam 原用固定 `Axis`，现统一用 `ApproachDirWorld`。正前方命中几乎等价，斜向接近有细微差异，需在 Level0 冲锤场景回归一次。
2. **predicted point 统一成基础版**：ThrustGuided 丢弃双向精算（该字段仅诊断用）；若后续要做可视化调试再按需在宿主覆盖。
3. `MinimumClosingSpeed` 目前作组件常量；若三机关阈值想各自可调，改成 `EvaluateAndNotify` 入参更稳妥。

---

## 7. 推进顺序决策（2026-08-06 定）

**结论：现在不先抽公共层。** 先让 `ThrustGuidedHazard` 以独立实现落盘、在 Level0 验证跑通（短暂容忍第三份 ~150 行复制），待三个机关都成为已验证的真实案例后，再统一收编进公共层。

**决定性理由：**

1. **ThrustGuided 仍是纸面稿，未跑通。** 用未验证案例驱动抽象是"过早抽象"的变体；而它恰是三个里运动学差异最大的一个（真实 Chaos 刚体速度、capsule 射线、gimbal 制导）。`IHeavyImpactSource` 接口能否优雅承载弹体真实需求，只有等它在 Level0 跑起来才知道，否则很可能抽完接口不合身、返工，白改两个机关。
2. **两个已有机关尚未稳定（用户 2026-08-06 确认）。** `PendulumHazard`、`BatteringRamHazard` 仍在迭代，此时抽公共层要同时改动两个不稳定的机关，回归成本前置且不产出新的可玩内容；三周 Demo 节奏不划算。
3. **符合项目一贯原则**：抽象用"真实可跑案例反推"，不照纸面稿抽。当前只有两个已验证 + 一个稿子，应等"三个真实可跑案例"齐备再统一抽。

**触发收编的条件（同时满足）：**

- ThrustGuided 在 Level0 正面墙 + 拐角墙验证通过；
- Pendulum、BatteringRam 玩法冻结、基本不再频繁改动；
- 三个机关此时都有可回归的稳定基线。

**收编时的方式（备选，届时再定）：**

- 方案 A（推荐）：一次把三个机关收编进 `UHeavyImpactPredictionComponent` + `IHeavyImpactSource` + `HeavyImpactGeometry`，并补组件流程与几何工具单测，一次性回归三处（含第 6 节三条行为变化）。
- 方案 B：先收编 ThrustGuided，再逐个回填另两个；改动面小但公共层会短期只服务一个宿主，收益延后。

**明确不做**：不在 ThrustGuided 尚未验证、两个已有机关尚未稳定的当前时点动公共层重构。

> 落盘门槛同拟实现稿第 10 节：写入 DailyPlan、核并行改动归属、基线 commit/push 到工蜂、用户明确授权后再动功能代码。
