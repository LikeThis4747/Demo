// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PendulumHazard.cpp
 * 职责：建立 World-to-Body 物理摆、从侧边自然释放，并以每半周期至多一次的有限冲量补回阻尼损耗。
 * 边界：Chaos 碰撞是外物影响摆锤的唯一来源；本文件不监听 OnHit、不识别撞击物、不追踪正弦目标。
 */

#include "Actors/Hazards/PendulumHazard.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Data/Hazards/PendulumHazardTuningData.h"
#include "Engine/World.h"
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

	BobBody = CreateDefaultSubobject<USphereComponent>(TEXT("BobBody"));
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
	Super::EndPlay(EndPlayReason);
}

/** 设置支点、碰撞半径和美术挂点位置；未知网格的尺寸与轴向由 Blueprint 装配直接同步。 */
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

	BobBody->SetSphereRadius(Tuning.BobRadius, false);
	BobBody->SetRelativeLocationAndRotation(
		PivotLocation + PreviewRotation.RotateVector(RestRadius),
		PreviewRotation);

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

/** 用带 1° 死区的侧别变化识别一次完整中线穿越，每半周期最多调用一次补能。 */
void APendulumHazard::EvaluateEnergyAssist()
{
	if (!IsValid(TuningData) || !BobBody->IsSimulatingPhysics())
	{
		return;
	}

	const float TwistDegrees = PhysicsConstraint->GetCurrentTwist();
	if (!FMath::IsFinite(TwistDegrees))
	{
		return;
	}

	const int8 CurrentSide = TwistDegrees > PendulumHazard::CenterCrossingDeadZoneDegrees
		? 1
		: (TwistDegrees < -PendulumHazard::CenterCrossingDeadZoneDegrees ? -1 : 0);

	if (CurrentSide == 0)
	{
		return;
	}

	if (LastObservedSide == 0)
	{
		LastObservedSide = CurrentSide;
		return;
	}

	if (CurrentSide == LastObservedSide)
	{
		return;
	}

	LastObservedSide = CurrentSide;
	AssistAtCenterCrossing();
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

/** 由势能换算中线速度，并近似计入实心球锤头绕自身中心的转动惯量。 */
float APendulumHazard::CalculateTargetCenterSpeed() const
{
	if (!IsValid(TuningData) || !IsValid(GetWorld()))
	{
		return 0.0f;
	}

	const float GravityMagnitude = FMath::Abs(GetWorld()->GetGravityZ());
	const float TargetAngleRadians = FMath::DegreesToRadians(TuningData->TargetAmplitudeDegrees);
	const float RadiusToLength = TuningData->BobRadius / TuningData->PendulumLength;
	const float RotationalFactor = 1.0f + 0.4f * FMath::Square(RadiusToLength);
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
	UE_LOG(LogPendulumHazard, Error, TEXT("%s 已停用：%s"), *GetName(), *Reason);
}
