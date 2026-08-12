// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardLauncher.h
 * 职责：拥有壁挂机关的首个角色锁定、真实弹体预装、预警期移动目标预测、机械炮管瞄准和一次性释放。
 * 边界：不模拟弹体、不在离膛后追踪、不判断受击，不依赖关卡名、Actor 名或组件名查找。
 * 状态 Owner：本 Actor 唯一写入 Armed/Warning/Spent/Disabled、目标快照、弹道解、弹体生命周期和两个 Timer。
 * 轴约定：Muzzle/ProjectileSpawnPoint 局部 +X 是炮管方向；弹体局部 +Z 对齐最终世界方向。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"

#include "ThrustGuidedHazardLauncher.generated.h"

class ACharacter;
class AThrustGuidedHazardProjectile;
class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UThrustGuidedHazardTuningData;

/** 项目内部的壁挂发射器阶段，不是 UE 官方状态机类型。 */
enum class EThrustGuidedHazardLauncherPhase : uint8
{
	Armed,
	Warning,
	Spent,
	Disabled
};

/** 项目内部的弹道候选来源，用于日志区分精确解、最近解和机械偏差弹。 */
enum class EThrustGuidedHazardAimSource : uint8
{
	PredictedIntercept,
	CurrentTarget,
	ClosestReachable,
	MechanicalForward,
	MechanicallyLimited
};

/** 一次预警采样得到的候选；只有收敛且未限角时才可标记 PredictedIntercept。 */
struct FThrustGuidedHazardAimSolution
{
	FVector DesiredLaunchVelocity = FVector::ZeroVector;
	FVector AimPoint = FVector::ZeroVector;
	FVector HypotheticalSpawnPoint = FVector::ZeroVector;
	float EstimatedFlightTime = 0.0f;
	float TimeResidualSeconds = BIG_NUMBER;
	float AimPointResidualCentimeters = BIG_NUMBER;
	float SpawnPointResidualCentimeters = BIG_NUMBER;
	float DirectionResidualDegrees = BIG_NUMBER;
	int32 IterationCount = 0;
	EThrustGuidedHazardAimSource Source =
		EThrustGuidedHazardAimSource::MechanicalForward;
	bool bYawLimited = false;
	bool bPitchLimited = false;
	bool bSlewLimited = false;
};

/** 出生检查结果把静态装配错误与运行时堵膛分开。 */
enum class EThrustGuidedHazardSpawnCheck : uint8
{
	Clear,
	StaticAssemblyFault,
	RuntimeObstruction,
	InvalidQuery
};

/** 出生检查的结构化诊断；接触点仅用于表现和日志，不伪造物理冲量。 */
struct FThrustGuidedHazardSpawnCheckResult
{
	EThrustGuidedHazardSpawnCheck Result =
		EThrustGuidedHazardSpawnCheck::InvalidQuery;
	TWeakObjectPtr<UPrimitiveComponent> BlockingComponent;
	FVector ApproximateContactPoint = FVector::ZeroVector;
	FString Reason;
};

/** 锁定首个进入角色、机械预判并发射一次真实 Chaos 弹体的壁挂机关。 */
UCLASS()
class DEMO_API AThrustGuidedHazardLauncher final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建固定外壳、机械转轴、出口、质心出生点、独立触发锚点和预警挂点；本 Actor 永不 Tick。 */
	AThrustGuidedHazardLauncher();

	/** 预览 TriggerVolume 尺寸和质心相对偏移，但不覆盖 TriggerAnchor/Muzzle 的作者摆位。 */
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	/** 校验配置/世界重力/装配，缓存固定中性轴并允许第一个 Character 触发。 */
	virtual void BeginPlay() override;

	/** 清理两个 Timer、重叠委托、目标和预警表现。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Blueprint 可在此播放预警灯、蓄能声或机械前摇；C++ 调用不依赖实现。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "机关|预判抛射|表现")
	void ReceiveWarningStarted(ACharacter* TargetCharacter);

	/** Blueprint 可在真实弹体成功离膛后播放炮口闪光、发射声和后坐；C++ 调用不依赖实现。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "机关|预判抛射|表现")
	void ReceiveProjectileFired(AThrustGuidedHazardProjectile* Projectile);

	/** Blueprint 可在出生体积被动态物体堵塞时播放明确卡膛反馈；预装弹体不会离膛或伪造冲量。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "机关|预判抛射|表现")
	void ReceiveBlockedDischarge(
		UPrimitiveComponent* BlockingComponent,
		FVector ApproximateContactPoint);

private:
	/** 只设置 TriggerVolume 半尺寸；空间位置和旋转由 TriggerAnchor 唯一决定。 */
	void ApplyTriggerGeometry(const UThrustGuidedHazardTuningData& Tuning);

	/** 按当前胶囊半高和 Margin 设置 Muzzle 子级质心点的局部 +X 偏移。 */
	void ApplyProjectileSpawnOffset(const UThrustGuidedHazardTuningData& Tuning);

	/** 缓存不可漂移的 AimPivot/Muzzle/SpawnPoint 初始装配，作为所有机械限制的固定基准。 */
	bool CacheNeutralAssembly(FString& OutError);

	/** 锁定首个 ACharacter、关闭后续触发并启动预警与机械瞄准 Timer。 */
	void EnterWarning(ACharacter& TargetCharacter);

	/** 读取有效角色胶囊中心和 Actor 速度；目标失效时保留最后一次合法快照。 */
	bool CaptureLastValidTargetState();

	/** 计算候选弹道并按真实经过时间推进 AimPivot；只在 Warning 阶段运行。 */
	void UpdateWarningAim();

	/** 预警结束同步采样，沿实际炮管方向检查净空并释放预装的同一个弹体。 */
	void FireLockedTarget();

	/** BeginPlay 延迟生成真实弹体并以 KeepWorld 方式挂到质心点，使其随 AimPivot 一起转。 */
	bool SpawnAndAttachLoadedProjectile(
		const FTransform& SpawnTransform,
		FString& OutError);

	/** 回收本 Launcher 创建的真实弹体，无论它仍在预装还是已经离膛。 */
	void DestroyOwnedProjectile();

	/** 清理 Warning/Aim Timer、目标和预警显隐；不隐式改写当前阶段。 */
	void ClearWarningState();

	/** 读取 World 重力并由参考射程/角推导唯一设计初速，失败返回 0。 */
	float CalculateDerivedLaunchSpeed() const;

	/** 从显式质心起点到目标点求固定初速低弧；可选 Closest，但不做路径许可 Trace。 */
	bool TrySolveLowArcToPoint(
		const FVector& StartPoint,
		const FVector& TargetPoint,
		bool bAcceptClosest,
		FVector& OutLaunchVelocity) const;

	/** 对固定目标点迭代炮管方向与质心起点；机械限角时按调用约定拒绝或返回偏差弹。 */
	bool TrySolveAimToPoint(
		const FVector& TargetPoint,
		bool bAcceptClosest,
		EThrustGuidedHazardAimSource ExactSource,
		FThrustGuidedHazardAimSolution& OutSolution) const;

	/** 用最多八次外层迭代求目标匀速移动交点；未收敛或机械限角时返回 false。 */
	bool TrySolvePredictedIntercept(
		FThrustGuidedHazardAimSolution& OutSolution) const;

	/** 数学解全部失败时生成固定中性前向低角度候选，不依赖目标。 */
	FThrustGuidedHazardAimSolution BuildMechanicalFallback() const;

	/** 相对缓存中性轴分别限制 Yaw/Pitch，并返回世界单位方向。 */
	FVector ClampToMechanicalLimits(
		const FVector& DesiredWorldDirection,
		bool& bOutYawLimited,
		bool& bOutPitchLimited) const;

	/** 由候选方向组合缓存装配，得到该炮向下真实质心位置与胶囊旋转。 */
	FTransform BuildHypotheticalSpawnTransform(
		const FVector& LimitedWorldDirection) const;

	/** 由固定中性装配得到候选方向对应的 AimPivot 世界旋转。 */
	FQuat BuildAimPivotWorldRotation(
		const FVector& LimitedWorldDirection) const;

	/** 以常角速度把 AimPivot 推向候选；返回实际炮管是否仍落后。 */
	bool AdvanceAimPivot(
		const FVector& DesiredWorldDirection,
		float DeltaSeconds);

	/** 从实际 ProjectileSpawnPoint 和炮管方向构造最终 SpawnTransform 与初速度。 */
	bool BuildActualSpawnTransform(
		FTransform& OutSpawnTransform,
		FVector& OutLaunchVelocity,
		FString& OutError) const;

	/** 在最终质心做完整胶囊 Overlap，并可选检查 Muzzle 后沿到 Margin 的短 Sweep。 */
	FThrustGuidedHazardSpawnCheckResult CheckFinalSpawnClearance(
		const FTransform& SpawnTransform) const;

	/** 消费被动态物体堵塞的一发并调用表现事件；不生成重叠刚体、不直接施力。 */
	void CompleteBlockedDischarge(
		const FThrustGuidedHazardSpawnCheckResult& CheckResult);

	/** 清理触发和预警状态并记录明确原因；不会生成隐藏兜底弹体。 */
	void DisableHazard(const FString& Reason);

	/** 第一个 Character 进入 Query-only TriggerVolume 时锁定；其他 Actor 和后续角色被忽略。 */
	UFUNCTION()
	void HandleTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** 固定壁挂基准；Blueprint 在其下装配支架、管线和墙柜。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 固定外壳纯美术挂点；不随瞄准旋转。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> HousingVisualRoot;

	/** 机械炮管转轴；C++ 只旋转该节点，固定外壳与 Trigger 不受影响。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> AimPivot;

	/** 炮管出口平面；局部 +X 是中性炮轴，只服务机械外观和出口表现。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Muzzle;

	/** 弹体胶囊质心的唯一出生点；C++ 按半高 + Margin 设置其 Muzzle 局部 +X。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ProjectileSpawnPoint;

	/** 可独立于炮管摆放的触发区空间基准。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> TriggerAnchor;

	/** TriggerAnchor 原点上的 Query-only Pawn 触发盒；不是压力板，不阻挡任何对象。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> TriggerVolume;

	/** 纯美术预警挂点；C++ 控制基础显隐，Blueprint 事件可补充声光。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|预判抛射",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> WarningVisualRoot;

	/** 唯一配置来源；缺失或非法时发射器明确进入 Disabled。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "机关|预判抛射|配置",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UThrustGuidedHazardTuningData> TuningData;

	/** BeginPlay 预装的真实弹体 Blueprint/C++ 类。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "机关|预判抛射|配置",
		meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AThrustGuidedHazardProjectile> ProjectileClass;

	/** 发射前可见并随 AimPivot 转动的真实弹体；成功离膛后立即清空，避免 Launcher 误销毁它。 */
	UPROPERTY(Transient)
	TObjectPtr<AThrustGuidedHazardProjectile> LoadedProjectile;

	/** 本 Launcher 创建的唯一真实弹体；离膛后仍保留，确保关卡重置可立即回收。 */
	UPROPERTY(Transient)
	TObjectPtr<AThrustGuidedHazardProjectile> OwnedProjectile;

	/** 当前一次性阶段，只由本 Actor 的阶段函数写入。 */
	EThrustGuidedHazardLauncherPhase Phase =
		EThrustGuidedHazardLauncherPhase::Disabled;

	/** 预警期锁定的首个角色；失效后仍使用最后一次合法位置/速度。 */
	TWeakObjectPtr<ACharacter> LockedTargetActor;

	/** 最近一次合法目标胶囊中心，单位 cm。 */
	FVector LastValidTargetLocation = FVector::ZeroVector;

	/** 最近一次合法目标 Actor 速度，单位 cm/s。 */
	FVector LastValidTargetVelocity = FVector::ZeroVector;

	/** 是否拥有可供预测/回退使用的目标快照。 */
	bool bHasLastValidTargetState = false;

	/** 最近一次候选；发射日志记录其来源和限制，弹体不保存目标。 */
	FThrustGuidedHazardAimSolution LastAimSolution;

	/** 固定中性 Muzzle 世界旋转；其局部 +X 定义 Yaw/Pitch 机械坐标。 */
	FQuat NeutralAimRotation = FQuat::Identity;

	/** 固定中性 AimPivot 世界旋转，用于从零点构造每个候选而不是累积漂移。 */
	FQuat NeutralAimPivotWorldRotation = FQuat::Identity;

	/** 固定中性炮轴世界单位向量。 */
	FVector NeutralAimDirection = FVector::ForwardVector;

	/** Muzzle 相对 AimPivot 的作者装配快照。 */
	FTransform NeutralMuzzleRelativeTransform = FTransform::Identity;

	/** ProjectileSpawnPoint 相对 Muzzle 的权威质心装配快照。 */
	FTransform NeutralSpawnPointRelativeTransform = FTransform::Identity;

	/** 是否已经成功缓存固定装配。 */
	bool bNeutralAssemblyCached = false;

	/** 上一次机械瞄准更新的 World Game Time，GetTimeSeconds 返回 double。 */
	double LastAimUpdateWorldSeconds = 0.0;

	/** 预警结束的一次性 Timer。 */
	FTimerHandle WarningTimerHandle;

	/** 预警期间按固定间隔更新弹道与机械转轴的 Timer。 */
	FTimerHandle AimTimerHandle;
};
