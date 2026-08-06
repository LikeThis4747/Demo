// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PendulumHazard.cpp
 * 职责：建立 World-to-Body 物理摆、从侧边自然释放，并以每半周期至多一次的有限冲量补回阻尼损耗。
 * 边界：预测球只经共享接口发准备请求；真实受击动量仍只来自 Bob 与角色 Mesh 的 Chaos 接触。
 */

#include "Actors/Hazards/PendulumHazard.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Data/Hazards/PendulumHazardTuningData.h"
#include "Engine/World.h"
#include "Interfaces/HeavyImpactReceiver.h"
#include "Physics/HeavyImpactTypes.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPendulumHazard, Log, All);

namespace PendulumHazard
{
	/** Timer 只采样跨越事件；真实物理仍由 Chaos 自身步进。 */
	constexpr float AssistSampleIntervalSeconds = 1.0f / 60.0f;

	/** 忽略中线附近的小角度数值抖动，防止同一次穿越被重复计数。 */
	constexpr float CenterCrossingDeadZoneDegrees = 1.0f;

	/** 低于该切向速度时无法可靠判断运动方向，本次不补能。 */
	constexpr float MinimumDirectionalSpeed = 1.0f;

	/** 小于该速度缺口不施加冲量，避免浮点噪声导致无意义微调。 */
	constexpr float MinimumUsefulSpeedDeficit = 1.0f;

	/** Low-FPS safety window shared semantically with the receiver; normal 30/60 FPS keeps authored timing. */
	constexpr float MaximumPreparationFrameMultiplier = 2.5f;
	constexpr float AbsoluteMaximumPreparationSeconds = 0.5f;

	/** 返回从盒体中心沿指定世界方向射线抵达当前朝向盒体表面的距离。 */
	float CalculateBoxSurfaceDistance(
		const UBoxComponent& Box,
		const FVector& WorldDirection)
	{
		const FVector NormalizedDirection = WorldDirection.GetSafeNormal();
		if (NormalizedDirection.IsNearlyZero())
		{
			return 0.0f;
		}

		const FVector LocalDirection =
			Box.GetComponentQuat().UnrotateVector(NormalizedDirection).GetAbs();
		const FVector ScaledExtent = Box.GetScaledBoxExtent();
		float SurfaceDistance = BIG_NUMBER;

		if (LocalDirection.X > KINDA_SMALL_NUMBER)
		{
			SurfaceDistance = FMath::Min(SurfaceDistance, ScaledExtent.X / LocalDirection.X);
		}
		if (LocalDirection.Y > KINDA_SMALL_NUMBER)
		{
			SurfaceDistance = FMath::Min(SurfaceDistance, ScaledExtent.Y / LocalDirection.Y);
		}
		if (LocalDirection.Z > KINDA_SMALL_NUMBER)
		{
			SurfaceDistance = FMath::Min(SurfaceDistance, ScaledExtent.Z / LocalDirection.Z);
		}

		return FMath::IsFinite(SurfaceDistance) && SurfaceDistance < BIG_NUMBER
			? SurfaceDistance
			: 0.0f;
	}

}

/** 装配物理组件和美术挂点，配置明确的碰撞矩阵，并关闭 Actor Tick。 */
APendulumHazard::APendulumHazard()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	AnchorVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AnchorVisualRoot"));
	AnchorVisualRoot->SetMobility(EComponentMobility::Movable);
	AnchorVisualRoot->SetupAttachment(SceneRoot);

	PhysicsConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("PhysicsConstraint"));
	PhysicsConstraint->SetMobility(EComponentMobility::Movable);
	PhysicsConstraint->SetupAttachment(SceneRoot);

	BobBody = CreateDefaultSubobject<UBoxComponent>(TEXT("BobBody"));
	BobBody->SetMobility(EComponentMobility::Movable);
	BobBody->SetupAttachment(SceneRoot);
	BobBody->SetCanEverAffectNavigation(false);
	BobBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BobBody->SetCollisionObjectType(ECC_PhysicsBody);
	BobBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	BobBody->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	BobBody->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	BobBody->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	BobBody->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BobBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BobBody->SetGenerateOverlapEvents(true);
	BobBody->SetSimulatePhysics(false);
	BobBody->SetEnableGravity(true);

	PreparationVolume = CreateDefaultSubobject<USphereComponent>(TEXT("PreparationVolume"));
	PreparationVolume->SetMobility(EComponentMobility::Movable);
	PreparationVolume->SetupAttachment(BobBody);
	PreparationVolume->SetCanEverAffectNavigation(false);
	PreparationVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PreparationVolume->SetCollisionObjectType(ECC_WorldDynamic);
	PreparationVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	PreparationVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PreparationVolume->SetGenerateOverlapEvents(true);

	BobVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BobVisualRoot"));
	BobVisualRoot->SetMobility(EComponentMobility::Movable);
	BobVisualRoot->SetupAttachment(BobBody);

	RodVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RodVisualRoot"));
	RodVisualRoot->SetMobility(EComponentMobility::Movable);
	RodVisualRoot->SetupAttachment(BobBody);

	// 构造阶段使用类默认值提供可见的组件布局；运行时仍强制要求显式 DataAsset。
	ApplyGeometry(*GetDefault<UPendulumHazardTuningData>(), true);
}

/** 使用已指定且合法的 DataAsset 预览释放姿态；非法配置留给 BeginPlay 明确报错。 */
void APendulumHazard::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const UPendulumHazardTuningData* PreviewData = IsValid(TuningData)
		? TuningData.Get()
		: GetDefault<UPendulumHazardTuningData>();

	FString ConfigurationError;
	if (IsValid(PreviewData) && PreviewData->IsConfigured(ConfigurationError))
	{
		ApplyGeometry(*PreviewData, true);
	}
}

/** 在最低点绑定世界约束，再设置一次释放姿态；之后不创建任何目标轨迹。 */
void APendulumHazard::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(TuningData))
	{
		DisableHazard(TEXT("未指定 PendulumHazardTuningData。"));
		return;
	}

	FString ConfigurationError;
	if (!TuningData->IsConfigured(ConfigurationError))
	{
		DisableHazard(ConfigurationError);
		return;
	}

	if (!GetActorScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		DisableHazard(TEXT("Actor Scale 必须保持 (1,1,1)；摆长与锤头半径已经使用 cm 参数定义。"));
		return;
	}

	const UWorld* World = GetWorld();
	if (!IsValid(World) || !FMath::IsFinite(World->GetGravityZ()) || FMath::IsNearlyZero(World->GetGravityZ()))
	{
		DisableHazard(TEXT("当前 World 没有有效重力，无法建立自由摆和能量目标。"));
		return;
	}

	// 必须先回到最低点，再建立约束参考帧；编辑器预览的侧边姿态不能成为约束零点。
	ApplyGeometry(*TuningData, false);

	BobBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BobBody->SetSimulatePhysics(true);
	ApplyPhysicsProperties(*TuningData);

	// 清掉 Blueprint 可能残留的 Frame1 组件引用；SceneRoot 不是刚体，因此 Frame1 明确落到世界。
	PhysicsConstraint->ConstraintActor1 = nullptr;
	PhysicsConstraint->ComponentName1.ComponentName = NAME_None;
	PhysicsConstraint->OverrideComponent1.Reset();
	PhysicsConstraint->ConstraintInstance.ConstraintBone1 = NAME_None;
	// 先绑定再配置，避免初始化过程覆盖约束 Profile。
	PhysicsConstraint->SetConstrainedComponents(nullptr, NAME_None, BobBody, NAME_None);
	ConfigureConstraint(*TuningData);

	SetInitialReleasePose(*TuningData);
	LastObservedSide = 0;
	PreparationCandidates.Reset();
	BeginNewSwingPass();

	PreparationVolume->OnComponentBeginOverlap.AddUniqueDynamic(
		this,
		&APendulumHazard::HandlePreparationVolumeBeginOverlap);
	PreparationVolume->OnComponentEndOverlap.AddUniqueDynamic(
		this,
		&APendulumHazard::HandlePreparationVolumeEndOverlap);

	// 组件可能在 BeginPlay 绑定 Delegate 前已经产生初始重叠，因此显式补齐当前候选。
	TArray<AActor*> InitiallyOverlappingActors;
	PreparationVolume->GetOverlappingActors(InitiallyOverlappingActors);
	for (AActor* OverlappingActor : InitiallyOverlappingActors)
	{
		if (IsValid(OverlappingActor)
			&& OverlappingActor != this
			&& OverlappingActor->GetClass()->ImplementsInterface(
				UHeavyImpactReceiver::StaticClass()))
		{
			PreparationCandidates.Add(OverlappingActor);
		}
	}

	GetWorldTimerManager().SetTimer(
		EnergyAssistTimerHandle,
		this,
		&APendulumHazard::EvaluateEnergyAssist,
		PendulumHazard::AssistSampleIntervalSeconds,
		true);
}

/** 清理中线采样 Timer。 */
void APendulumHazard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(EnergyAssistTimerHandle);

	if (IsValid(PreparationVolume))
	{
		PreparationVolume->OnComponentBeginOverlap.RemoveDynamic(
			this,
			&APendulumHazard::HandlePreparationVolumeBeginOverlap);
		PreparationVolume->OnComponentEndOverlap.RemoveDynamic(
			this,
			&APendulumHazard::HandlePreparationVolumeEndOverlap);
	}

	PreparationCandidates.Reset();
	NotifiedReceiversThisSwing.Reset();
	CurrentSwingImpactId = FGuid();
	Super::EndPlay(EndPlayReason);
}

/** 设置支点、盒形碰撞尺寸和美术挂点位置；未知网格的尺寸与轴向由 Blueprint 装配直接同步。 */
void APendulumHazard::ApplyGeometry(
	const UPendulumHazardTuningData& Tuning,
	const bool bPreviewReleasePose)
{
	const FVector PivotLocation(0.0f, 0.0f, Tuning.PivotHeight);
	const FVector RestRadius(0.0f, 0.0f, -Tuning.PendulumLength);
	const float PreviewAngleRadians = bPreviewReleasePose
		? FMath::DegreesToRadians(Tuning.TargetAmplitudeDegrees)
		: 0.0f;
	const FQuat PreviewRotation(FVector::ForwardVector, PreviewAngleRadians);

	PhysicsConstraint->SetRelativeLocationAndRotation(PivotLocation, FRotator::ZeroRotator);
	AnchorVisualRoot->SetRelativeLocationAndRotation(PivotLocation, FRotator::ZeroRotator);

	BobBody->SetBoxExtent(Tuning.BobHalfExtents, false);
	BobBody->SetRelativeLocationAndRotation(
		PivotLocation + PreviewRotation.RotateVector(RestRadius),
		PreviewRotation);
	PreparationVolume->SetSphereRadius(
		Tuning.BobHalfExtents.Size() + Tuning.PreparationLookAheadDistance,
		false);
	PreparationVolume->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

	BobVisualRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	RodVisualRoot->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, Tuning.PendulumLength * 0.5f),
		FRotator::ZeroRotator);
}

/** 写入真实刚体质量与低阻尼；CCD 只改善高速接触，不改变碰撞动量规则。 */
void APendulumHazard::ApplyPhysicsProperties(const UPendulumHazardTuningData& Tuning)
{
	BobBody->SetMassOverrideInKg(NAME_None, Tuning.BobMassKilograms, true);
	BobBody->SetLinearDamping(Tuning.LinearDamping);
	BobBody->SetAngularDamping(Tuning.AngularDamping);
	BobBody->SetUseCCD(true);
}

/** 建立硬杆式世界约束，显式关闭 UE5.8 默认的 Soft Angular Limit，并关闭全部位置/速度驱动。 */
void APendulumHazard::ConfigureConstraint(const UPendulumHazardTuningData& Tuning)
{
	PhysicsConstraint->SetLinearXLimit(LCM_Locked, 0.0f);
	PhysicsConstraint->SetLinearYLimit(LCM_Locked, 0.0f);
	PhysicsConstraint->SetLinearZLimit(LCM_Locked, 0.0f);

	PhysicsConstraint->SetAngularTwistLimit(ACM_Limited, Tuning.MainAxisLimitDegrees);

	if (Tuning.SecondaryAxisLimitDegrees <= KINDA_SMALL_NUMBER)
	{
		PhysicsConstraint->SetAngularSwing1Limit(ACM_Locked, 0.0f);
		PhysicsConstraint->SetAngularSwing2Limit(ACM_Locked, 0.0f);
	}
	else
	{
		PhysicsConstraint->SetAngularSwing1Limit(ACM_Limited, Tuning.SecondaryAxisLimitDegrees);
		PhysicsConstraint->SetAngularSwing2Limit(ACM_Limited, Tuning.SecondaryAxisLimitDegrees);
	}

	// UE5.8 的 Twist/Cone Limit 默认是 Soft Constraint；ACM_Limited 本身只设置运动类型与角度。
	// 显式关闭软边界后，限位只在接近边界时阻止继续转动，不会在允许范围内追踪角度或相位。
	FConstraintInstance& Instance = PhysicsConstraint->ConstraintInstance;
	Instance.SetSoftTwistLimitParams(false, 0.0f, 0.0f, 0.0f, 1.0f);
	Instance.SetSoftSwingLimitParams(false, 0.0f, 0.0f, 0.0f, 1.0f);

	PhysicsConstraint->SetLinearPositionDrive(false, false, false);
	PhysicsConstraint->SetLinearVelocityDrive(false, false, false);
	PhysicsConstraint->SetOrientationDriveTwistAndSwing(false, false);
	PhysicsConstraint->SetOrientationDriveSLERP(false);
	PhysicsConstraint->SetAngularVelocityDriveTwistAndSwing(false, false);
	PhysicsConstraint->SetAngularVelocityDriveSLERP(false);
	PhysicsConstraint->SetProjectionEnabled(false);
	PhysicsConstraint->SetLinearBreakable(false, 0.0f);
	PhysicsConstraint->SetAngularBreakable(false, 0.0f);
	// 世界锚点没有自身碰撞体；保持约束碰撞开关为 false，避免压掉锤头与环境的接触。
	PhysicsConstraint->SetDisableCollision(false);
}

/** 围绕已建立的世界约束主轴设置一次释放姿态；不更新约束参考帧。 */
void APendulumHazard::SetInitialReleasePose(const UPendulumHazardTuningData& Tuning)
{
	const FTransform ConstraintTransform = PhysicsConstraint->GetComponentTransform();
	const FVector PivotLocation = ConstraintTransform.GetLocation();
	const FVector MainAxis = ConstraintTransform.GetUnitAxis(EAxis::X);
	const FVector RestRadius = BobBody->GetComponentLocation() - PivotLocation;
	const FQuat ReleaseRotation(MainAxis, FMath::DegreesToRadians(Tuning.TargetAmplitudeDegrees));

	const FVector ReleasedLocation = PivotLocation + ReleaseRotation.RotateVector(RestRadius);
	const FQuat ReleasedBodyRotation = ReleaseRotation * BobBody->GetComponentQuat();

	BobBody->SetWorldLocationAndRotation(
		ReleasedLocation,
		ReleasedBodyRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	BobBody->SetPhysicsLinearVelocity(FVector::ZeroVector, false);
	BobBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
	BobBody->WakeAllRigidBodies();
}

/** 用带 1° 死区的侧别变化识别中线穿越，同时复用同一 60 Hz Timer 评估预测候选。 */
void APendulumHazard::EvaluateEnergyAssist()
{
	if (!IsValid(TuningData) || !BobBody->IsSimulatingPhysics())
	{
		return;
	}

	const float TwistDegrees = PhysicsConstraint->GetCurrentTwist();
	if (FMath::IsFinite(TwistDegrees))
	{
		const int8 CurrentSide = TwistDegrees > PendulumHazard::CenterCrossingDeadZoneDegrees
			? 1
			: (TwistDegrees < -PendulumHazard::CenterCrossingDeadZoneDegrees ? -1 : 0);

		if (CurrentSide != 0)
		{
			if (LastObservedSide == 0)
			{
				LastObservedSide = CurrentSide;
			}
			else if (CurrentSide != LastObservedSide)
			{
				LastObservedSide = CurrentSide;
				BeginNewSwingPass();
				AssistAtCenterCrossing();
			}
		}
	}

	// 即使当前处于 1° 中线死区也必须继续预测，因为这里正是最可能发生真实命中的区域。
	EvaluatePreparationCandidates();
}

/** 快照候选后调用接口，避免接收者切换碰撞状态时同步触发 EndOverlap 破坏当前迭代器。 */
void APendulumHazard::EvaluatePreparationCandidates()
{
	if (!IsValid(PreparationVolume) || !CurrentSwingImpactId.IsValid())
	{
		return;
	}

	TArray<TWeakObjectPtr<AActor>> CandidateSnapshot;
	CandidateSnapshot.Reserve(PreparationCandidates.Num());
	for (const TWeakObjectPtr<AActor>& Candidate : PreparationCandidates)
	{
		CandidateSnapshot.Add(Candidate);
	}

	for (const TWeakObjectPtr<AActor>& Candidate : CandidateSnapshot)
	{
		AActor* Receiver = Candidate.Get();
		if (!IsValid(Receiver))
		{
			PreparationCandidates.Remove(Candidate);
			continue;
		}

		if (!PreparationVolume->IsOverlappingActor(Receiver))
		{
			PreparationCandidates.Remove(Candidate);
			continue;
		}

		if (NotifiedReceiversThisSwing.Contains(Candidate))
		{
			continue;
		}

		FHeavyImpactPreparationRequest Request;
		if (!BuildPreparationRequest(*Receiver, Request))
		{
			continue;
		}

		const EHeavyImpactPrepareResult Result =
			IHeavyImpactReceiver::Execute_PrepareForHeavyImpact(Receiver, Request);
		if (Result == EHeavyImpactPrepareResult::Accepted
			|| Result == EHeavyImpactPrepareResult::Duplicate)
		{
			// 接收结果按 Actor 独立记录，因此同一 ImpactId 可同时通知玩家和多个 AI。
			NotifiedReceiversThisSwing.Add(Candidate);
		}
	}
}

/** 用接收者声明的真实受击组件估算径向间隙；预测只决定准备时机，不写入任何物理速度。 */
bool APendulumHazard::BuildPreparationRequest(
	const AActor& Receiver,
	FHeavyImpactPreparationRequest& OutRequest)
{
	if (!IsValid(BobBody) || !CurrentSwingImpactId.IsValid())
	{
		return false;
	}

	const FVector BobCenter = BobBody->GetComponentLocation();
	const FVector BobVelocity = BobBody->GetPhysicsLinearVelocity();
	const FVector ReceiverVelocity = Receiver.GetVelocity();
	if (BobCenter.ContainsNaN()
		|| BobVelocity.ContainsNaN()
		|| ReceiverVelocity.ContainsNaN())
	{
		return false;
	}

	UPrimitiveComponent* PredictionPrimitive =
		IHeavyImpactReceiver::Execute_GetHeavyImpactPredictionPrimitive(&Receiver);
	if (!IsValid(PredictionPrimitive)
		|| PredictionPrimitive->GetOwner() != &Receiver
		|| PredictionPrimitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		return false;
	}

	FVector ClosestSurfacePoint = FVector::ZeroVector;
	float ClosestSurfaceDistance = BIG_NUMBER;
	float ClosestSurfaceDistanceSquared = BIG_NUMBER;
	if (PredictionPrimitive->GetSquaredDistanceToCollision(
			BobCenter,
			ClosestSurfaceDistanceSquared,
			ClosestSurfacePoint)
		&& FMath::IsFinite(ClosestSurfaceDistanceSquared)
		&& ClosestSurfaceDistanceSquared >= 0.0f
		&& !ClosestSurfacePoint.ContainsNaN())
	{
		ClosestSurfaceDistance = FMath::Sqrt(ClosestSurfaceDistanceSquared);
	}
	else
	{
		ClosestSurfacePoint = FVector::ZeroVector;
	}

	// 复杂 Physics Asset 查询无法返回最近点时，只回退到同一个权威组件的包围盒，禁止改用外层 Capsule。
	if (ClosestSurfaceDistance == BIG_NUMBER)
	{
		const FVector BoundsOrigin = PredictionPrimitive->Bounds.Origin;
		const FVector BoundsExtent = PredictionPrimitive->Bounds.BoxExtent;

		const FVector ToBoundsCenter = BoundsOrigin - BobCenter;
		const float CenterDistance = ToBoundsCenter.Size();
		const FVector BoundsDirection = ToBoundsCenter.GetSafeNormal();
		if (!FMath::IsFinite(CenterDistance) || BoundsDirection.IsNearlyZero())
		{
			return false;
		}

		const FVector AbsoluteDirection = BoundsDirection.GetAbs();
		const float ProjectedExtent = FVector::DotProduct(BoundsExtent, AbsoluteDirection);
		ClosestSurfaceDistance = FMath::Max(0.0f, CenterDistance - ProjectedExtent);
		ClosestSurfacePoint = BobCenter + BoundsDirection * ClosestSurfaceDistance;
	}

	FVector ApproachDirection = (ClosestSurfacePoint - BobCenter).GetSafeNormal();
	if (ApproachDirection.IsNearlyZero())
	{
		ApproachDirection = (Receiver.GetActorLocation() - BobCenter).GetSafeNormal();
	}
	if (ApproachDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector RelativeVelocity = BobVelocity - ReceiverVelocity;
	const float ClosingSpeed = FVector::DotProduct(RelativeVelocity, ApproachDirection);
	if (!FMath::IsFinite(ClosingSpeed)
		|| ClosingSpeed < TuningData->MinimumHeavyImpactClosingSpeed)
	{
		return false;
	}

	const float BobSurfaceDistance =
		PendulumHazard::CalculateBoxSurfaceDistance(*BobBody, ApproachDirection);
	if (BobSurfaceDistance <= 0.0f)
	{
		return false;
	}

	const float SurfaceGap = FMath::Max(0.0f, ClosestSurfaceDistance - BobSurfaceDistance);
	const float EstimatedTimeToContact = SurfaceGap / ClosingSpeed;
	const UWorld* World = GetWorld();
	const float DeltaSeconds = IsValid(World) ? World->GetDeltaSeconds() : 0.0f;
	const float FrameAwareMaximumSeconds = FMath::Min(
		PendulumHazard::AbsoluteMaximumPreparationSeconds,
		DeltaSeconds * PendulumHazard::MaximumPreparationFrameMultiplier);
	const float AllowedMaximumSeconds = FMath::Max(
		TuningData->MaximumPreparationLeadTime,
		FrameAwareMaximumSeconds);
	if (!FMath::IsFinite(EstimatedTimeToContact)
		|| EstimatedTimeToContact > AllowedMaximumSeconds)
	{
		return false;
	}

	const FVector PredictedBobCenter = BobCenter + BobVelocity * EstimatedTimeToContact;
	const FVector PredictedReceiverSurface =
		ClosestSurfacePoint + ReceiverVelocity * EstimatedTimeToContact;
	FVector PredictedContactDirection =
		(PredictedReceiverSurface - PredictedBobCenter).GetSafeNormal();
	if (PredictedContactDirection.IsNearlyZero())
	{
		PredictedContactDirection = ApproachDirection;
	}
	const float PredictedBobSurfaceDistance =
		PendulumHazard::CalculateBoxSurfaceDistance(*BobBody, PredictedContactDirection);
	if (PredictedBobSurfaceDistance <= 0.0f)
	{
		return false;
	}

	OutRequest = FHeavyImpactPreparationRequest();
	OutRequest.ImpactId = CurrentSwingImpactId;
	OutRequest.SourceActor = this;
	OutRequest.SourceComponent = BobBody;
	OutRequest.PredictedImpactPoint =
		PredictedBobCenter + PredictedContactDirection * PredictedBobSurfaceDistance;
	OutRequest.SourceLinearVelocity = BobVelocity;
	OutRequest.EstimatedTimeToContactSeconds = EstimatedTimeToContact;
	return !OutRequest.PredictedImpactPoint.ContainsNaN();
}

/** 每次确认穿过中线后更新一次 ID；候选本身保留，以便仍在体积内的接收者参与下一半摆。 */
void APendulumHazard::BeginNewSwingPass()
{
	CurrentSwingImpactId = FGuid::NewGuid();
	NotifiedReceiversThisSwing.Reset();
}

/** 只登记共享接口接收者；不在 Overlap 回调中提前做一次性预测。 */
void APendulumHazard::HandlePreparationVolumeBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (IsValid(OtherActor)
		&& OtherActor != this
		&& OtherActor->GetClass()->ImplementsInterface(UHeavyImpactReceiver::StaticClass()))
	{
		PreparationCandidates.Add(OtherActor);
	}
}

/** 多组件 Actor 仅在最后一个组件离开预测球后才移除，避免 Capsule/Mesh 交替造成漏通知。 */
void APendulumHazard::HandlePreparationVolumeEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/)
{
	if (IsValid(OtherActor)
		&& IsValid(PreparationVolume)
		&& !PreparationVolume->IsOverlappingActor(OtherActor))
	{
		PreparationCandidates.Remove(OtherActor);
	}
}

/** 记录每次有效中线样本；仅在启用补能且速度低于目标时填补有限缺口。 */
void APendulumHazard::AssistAtCenterCrossing()
{
	const FTransform ConstraintTransform = PhysicsConstraint->GetComponentTransform();
	const FVector MainAxis = ConstraintTransform.GetUnitAxis(EAxis::X);
	const FVector Radius = BobBody->GetComponentLocation() - ConstraintTransform.GetLocation();
	const FVector PositiveTangent = FVector::CrossProduct(MainAxis, Radius).GetSafeNormal();

	if (PositiveTangent.IsNearlyZero())
	{
		return;
	}

	const float SignedTangentialSpeed =
		FVector::DotProduct(BobBody->GetPhysicsLinearVelocity(), PositiveTangent);
	const float CurrentSpeed = FMath::Abs(SignedTangentialSpeed);
	if (!FMath::IsFinite(CurrentSpeed) || CurrentSpeed < PendulumHazard::MinimumDirectionalSpeed)
	{
		return;
	}

	const float TargetSpeed = CalculateTargetCenterSpeed();
	const float SpeedDeficit = TargetSpeed - CurrentSpeed;
	if (!FMath::IsFinite(TargetSpeed) || !FMath::IsFinite(SpeedDeficit))
	{
		return;
	}

	if (TuningData->MaximumAssistSpeedDeltaPerPass <= 0.0f
		|| SpeedDeficit <= PendulumHazard::MinimumUsefulSpeedDeficit)
	{
		// 补能为 0 时，这条 Verbose 样本用于 3 分钟纯自由摆标定；Current >= Target 时也只记录、不刹车。
		UE_LOG(
			LogPendulumHazard,
			Verbose,
			TEXT("%s 中线样本：Current=%.2f cm/s, Target=%.2f cm/s, Deficit=%.2f cm/s, Added=0。"),
			*GetName(),
			CurrentSpeed,
			TargetSpeed,
			SpeedDeficit);
		return;
	}

	const float AddedSpeed = FMath::Min(SpeedDeficit, TuningData->MaximumAssistSpeedDeltaPerPass);
	const float BobMass = BobBody->GetMass();
	if (!FMath::IsFinite(BobMass) || BobMass <= 0.0f)
	{
		return;
	}

	const FVector Impulse =
		PositiveTangent * FMath::Sign(SignedTangentialSpeed) * BobMass * AddedSpeed;
	BobBody->AddImpulse(Impulse, NAME_None, false);

	UE_LOG(
		LogPendulumHazard,
		Verbose,
		TEXT("%s 中线补能：Current=%.2f cm/s, Target=%.2f cm/s, Added=%.2f cm/s。"),
		*GetName(),
		CurrentSpeed,
		TargetSpeed,
		AddedSpeed);
}

/** 由势能换算中线速度，并计入盒形锤头绕本地 X 主摆轴的中心转动惯量。 */
float APendulumHazard::CalculateTargetCenterSpeed() const
{
	if (!IsValid(TuningData) || !IsValid(GetWorld()))
	{
		return 0.0f;
	}

	const float GravityMagnitude = FMath::Abs(GetWorld()->GetGravityZ());
	const float TargetAngleRadians = FMath::DegreesToRadians(TuningData->TargetAmplitudeDegrees);
	const float CenterInertiaPerMass =
		(FMath::Square(TuningData->BobHalfExtents.Y)
			+ FMath::Square(TuningData->BobHalfExtents.Z)) / 3.0f;
	const float RotationalFactor =
		1.0f + CenterInertiaPerMass / FMath::Square(TuningData->PendulumLength);
	const float AvailableEnergyPerMass =
		2.0f
		* GravityMagnitude
		* TuningData->PendulumLength
		* (1.0f - FMath::Cos(TargetAngleRadians));

	return FMath::Sqrt(FMath::Max(0.0f, AvailableEnergyPerMass / RotationalFactor));
}

/** 失败即明确停用，不以类默认值或隐藏钳制掩盖错误资产。 */
void APendulumHazard::DisableHazard(const FString& Reason)
{
	GetWorldTimerManager().ClearTimer(EnergyAssistTimerHandle);

	if (BobBody->IsSimulatingPhysics())
	{
		BobBody->SetPhysicsLinearVelocity(FVector::ZeroVector, false);
		BobBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
		BobBody->SetSimulatePhysics(false);
	}

	BobBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (IsValid(PreparationVolume))
	{
		PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	PreparationCandidates.Reset();
	NotifiedReceiversThisSwing.Reset();
	CurrentSwingImpactId = FGuid();
	UE_LOG(LogPendulumHazard, Error, TEXT("%s 已停用：%s"), *GetName(), *Reason);
}
