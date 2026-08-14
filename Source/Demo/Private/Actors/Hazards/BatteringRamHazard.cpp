// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file BatteringRamHazard.cpp
 * 职责：执行固定自动周期，以运动学锤头产生真实 Chaos 接触，并复用共享重冲击准备接口。
 * 边界：不按角色类型分支，不补冲量，不包含 PCG、压板、过载冷却或多机关同步。
 */

#include "Actors/Hazards/BatteringRamHazard.h"

#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Data/Hazards/BatteringRamHazardTuningData.h"
#include "Engine/World.h"
#include "Interfaces/HeavyImpactReceiver.h"
#include "Physics/HeavyImpactTypes.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBatteringRamHazard, Log, All);

namespace BatteringRamHazard
{
	/** 拒绝无法形成稳定相对接近速度的数值噪声。 */
	constexpr float MinimumClosingSpeedCmPerSecond = 1.0f;

}

/** 装配固定外壳挂点、运动学锤头、预测体积和预警挂点。 */
ABatteringRamHazard::ABatteringRamHazard()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	HousingVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HousingVisualRoot"));
	HousingVisualRoot->SetupAttachment(SceneRoot);

	RamBody = CreateDefaultSubobject<UBoxComponent>(TEXT("RamBody"));
	RamBody->SetupAttachment(SceneRoot);
	RamBody->SetMobility(EComponentMobility::Movable);
	RamBody->SetCanEverAffectNavigation(false);
	RamBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RamBody->SetCollisionObjectType(ECC_PhysicsBody);
	RamBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	RamBody->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	RamBody->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	RamBody->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	RamBody->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	RamBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	RamBody->SetGenerateOverlapEvents(true);
	RamBody->SetSimulatePhysics(false);
	RamBody->SetEnableGravity(false);
	RamBody->SetUseCCD(true);

	RamVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RamVisualRoot"));
	RamVisualRoot->SetupAttachment(RamBody);

	PreparationVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("PreparationVolume"));
	PreparationVolume->SetupAttachment(RamBody);
	PreparationVolume->SetMobility(EComponentMobility::Movable);
	PreparationVolume->SetCanEverAffectNavigation(false);
	PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreparationVolume->SetCollisionObjectType(ECC_WorldDynamic);
	PreparationVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	PreparationVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PreparationVolume->SetGenerateOverlapEvents(true);

	WarningVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WarningVisualRoot"));
	WarningVisualRoot->SetupAttachment(HousingVisualRoot);
	WarningVisualRoot->SetVisibility(false, true);

	ApplyGeometry(*GetDefault<UBatteringRamHazardTuningData>());
}

/** 使用配置资产或 CDO 默认值预览锤头和预测盒；运行时仍要求显式资产。 */
void ABatteringRamHazard::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const UBatteringRamHazardTuningData* PreviewData = IsValid(TuningData)
		? TuningData.Get()
		: GetDefault<UBatteringRamHazardTuningData>();

	FString Error;
	if (IsValid(PreviewData) && PreviewData->IsConfigured(Error))
	{
		ApplyGeometry(*PreviewData);
	}
}

/** 建立运行时碰撞和重叠候选，然后从完全缩回的安全期开始。 */
void ABatteringRamHazard::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(false);

	if (!IsValid(TuningData))
	{
		DisableHazard(TEXT("未指定 BatteringRamHazardTuningData。"));
		return;
	}

	FString Error;
	if (!TuningData->IsConfigured(Error))
	{
		DisableHazard(Error);
		return;
	}

	if (!GetActorScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		DisableHazard(TEXT("Actor Scale 必须保持 (1,1,1)；冲程和盒体已经使用 cm 定义。"));
		return;
	}

	ApplyGeometry(*TuningData);
	RamBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PreparationVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	EnterWaiting();
}

/** 只推进当前运动阶段；等待和预警由一次性 Timer 驱动。 */
void ABatteringRamHazard::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Phase == EBatteringRamPhase::Extending)
	{
		const FVector PlannedWorldVelocity =
			GetActorForwardVector() * (TuningData->StrokeDistance / TuningData->ExtensionSeconds);
		EvaluatePreparationCandidates(PlannedWorldVelocity);

		if (AdvanceLinearPhase(DeltaSeconds, TuningData->ExtensionSeconds, true))
		{
			BeginRetraction();
		}
	}
	else if (Phase == EBatteringRamPhase::Retracting
		&& AdvanceLinearPhase(DeltaSeconds, TuningData->RetractionSeconds, false))
	{
		FinishRetraction();
	}
}

/** 清除本 Actor 拥有的 Timer、ID 和本次通知集合。 */
void ABatteringRamHazard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	NotifiedReceiversThisStroke.Reset();
	CurrentImpactId = FGuid();

	Super::EndPlay(EndPlayReason);
}

/** Actor 原点作为完全缩回中心，预测盒从锤头前表面沿局部 +X 延伸。 */
void ABatteringRamHazard::ApplyGeometry(const UBatteringRamHazardTuningData& Tuning)
{
	RamBody->SetBoxExtent(Tuning.RamBodyHalfExtent, true);
	RamBody->SetRelativeLocationAndRotation(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		false,
		nullptr,
		ETeleportType::None);

	const FVector PreparationHalfExtent(
		Tuning.PreparationLookAheadDistance * 0.5f,
		Tuning.RamBodyHalfExtent.Y,
		Tuning.RamBodyHalfExtent.Z);
	PreparationVolume->SetBoxExtent(PreparationHalfExtent, true);
	PreparationVolume->SetRelativeLocationAndRotation(
		FVector(Tuning.RamBodyHalfExtent.X + PreparationHalfExtent.X, 0.0f, 0.0f),
		FRotator::ZeroRotator,
		false,
		nullptr,
		ETeleportType::None);

	WarningVisualRoot->SetVisibility(false, true);
}

/** 关闭运动 Tick，回到原点并在安全期结束后进入预警。 */
void ABatteringRamHazard::EnterWaiting()
{
	Phase = EBatteringRamPhase::Waiting;
	MotionElapsedSeconds = 0.0f;
	CurrentImpactId = FGuid();
	NotifiedReceiversThisStroke.Reset();
	RamBody->SetRelativeLocation(FVector::ZeroVector, false, nullptr, ETeleportType::None);
	WarningVisualRoot->SetVisibility(false, true);
	SetActorTickEnabled(false);

	GetWorldTimerManager().SetTimer(
		PhaseTimerHandle,
		this,
		&ABatteringRamHazard::EnterWarning,
		TuningData->RetractedWaitSeconds,
		false);
}

/** 只打开美术预警；不移动锤头、不准备受击者。 */
void ABatteringRamHazard::EnterWarning()
{
	Phase = EBatteringRamPhase::Warning;
	WarningVisualRoot->SetVisibility(true, true);

	GetWorldTimerManager().SetTimer(
		PhaseTimerHandle,
		this,
		&ABatteringRamHazard::BeginExtension,
		TuningData->WarningSeconds,
		false);
}

/** 生成一次伸出 ID；先评估近距离候选，再让锤头从下一帧开始运动。 */
void ABatteringRamHazard::BeginExtension()
{
	Phase = EBatteringRamPhase::Extending;
	MotionElapsedSeconds = 0.0f;
	CurrentImpactId = FGuid::NewGuid();
	NotifiedReceiversThisStroke.Reset();
	WarningVisualRoot->SetVisibility(false, true);

	const FVector PlannedWorldVelocity =
		GetActorForwardVector() * (TuningData->StrokeDistance / TuningData->ExtensionSeconds);
	EvaluatePreparationCandidates(PlannedWorldVelocity);
	SetActorTickEnabled(true);
}

/** 失效 ImpactId，保证缩回阶段不触发角色重冲击准备。 */
void ABatteringRamHazard::BeginRetraction()
{
	Phase = EBatteringRamPhase::Retracting;
	MotionElapsedSeconds = 0.0f;
	CurrentImpactId = FGuid();
	NotifiedReceiversThisStroke.Reset();
}

/** 精确回到缩回原点并进入下一轮安全等待。 */
void ABatteringRamHazard::FinishRetraction()
{
	RamBody->SetRelativeLocation(FVector::ZeroVector, false, nullptr, ETeleportType::None);
	EnterWaiting();
}

/** 使用线性 alpha 写入确定性局部位置；不 Sweep，以保留机械驱动轨迹和 Chaos 接触。 */
bool ABatteringRamHazard::AdvanceLinearPhase(
	const float DeltaSeconds,
	const float DurationSeconds,
	const bool bExtending)
{
	MotionElapsedSeconds += DeltaSeconds;
	const float Alpha = FMath::Clamp(MotionElapsedSeconds / DurationSeconds, 0.0f, 1.0f);
	const float Distance = bExtending
		? TuningData->StrokeDistance * Alpha
		: TuningData->StrokeDistance * (1.0f - Alpha);

	RamBody->SetRelativeLocation(
		FVector::ForwardVector * Distance,
		false,
		nullptr,
		ETeleportType::None);
	return Alpha >= 1.0f;
}

/** 运动期只有约十余帧，直接查询当前重叠者，避免维护长期候选状态。 */
void ABatteringRamHazard::EvaluatePreparationCandidates(
	const FVector& PlannedWorldVelocity)
{
	if (Phase != EBatteringRamPhase::Extending || !CurrentImpactId.IsValid())
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	PreparationVolume->GetOverlappingActors(OverlappingActors);
	for (AActor* Receiver : OverlappingActors)
	{
		if (!IsValid(Receiver)
			|| Receiver == this
			|| !Receiver->GetClass()->ImplementsInterface(UHeavyImpactReceiver::StaticClass()))
		{
			continue;
		}

		const TWeakObjectPtr<AActor> Candidate(Receiver);
		if (NotifiedReceiversThisStroke.Contains(Candidate))
		{
			continue;
		}

		FHeavyImpactPreparationRequest Request;
		if (!BuildPreparationRequest(*Receiver, PlannedWorldVelocity, Request))
		{
			continue;
		}

		const EHeavyImpactPrepareResult Result =
			IHeavyImpactReceiver::Execute_PrepareForHeavyImpact(Receiver, Request);
		if (Result == EHeavyImpactPrepareResult::Accepted
			|| Result == EHeavyImpactPrepareResult::Duplicate)
		{
			NotifiedReceiversThisStroke.Add(Candidate);
		}
	}
}

/** 用真实锤头盒体沿剩余伸出轨迹 Sweep 接收者 Mesh，只有几何命中才请求 Heavy。 */
bool ABatteringRamHazard::BuildPreparationRequest(
	const AActor& Receiver,
	const FVector& PlannedWorldVelocity,
	FHeavyImpactPreparationRequest& OutRequest)
{
	if (!CurrentImpactId.IsValid()
		|| PlannedWorldVelocity.ContainsNaN()
		|| PlannedWorldVelocity.IsNearlyZero())
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

	const FVector Axis = GetActorForwardVector();
	const float ClosingSpeed = FVector::DotProduct(PlannedWorldVelocity, Axis);
	if (!FMath::IsFinite(ClosingSpeed)
		|| ClosingSpeed < BatteringRamHazard::MinimumClosingSpeedCmPerSecond)
	{
		return false;
	}

	const float RemainingExtensionSeconds = FMath::Max(
		0.0f,
		TuningData->ExtensionSeconds - MotionElapsedSeconds);
	const float SweepSeconds = FMath::Min(
		TuningData->MaximumPreparationLeadTime,
		RemainingExtensionSeconds);
	if (!FMath::IsFinite(SweepSeconds) || SweepSeconds <= 0.0f)
	{
		return false;
	}

	const FVector SweepStart = RamBody->GetComponentLocation();
	const FVector SweepEnd = SweepStart + PlannedWorldVelocity * SweepSeconds;
	FHitResult PredictedHit;
	const bool bWillHitReceiver = PredictionPrimitive->SweepComponent(
		PredictedHit,
		SweepStart,
		SweepEnd,
		RamBody->GetComponentQuat(),
		FCollisionShape::MakeBox(RamBody->GetScaledBoxExtent()),
		false);
	if (!bWillHitReceiver || PredictedHit.bStartPenetrating)
	{
		return false;
	}

	const float EstimatedTimeToContact = PredictedHit.Time * SweepSeconds;
	if (!FMath::IsFinite(EstimatedTimeToContact)
		|| EstimatedTimeToContact <= 0.0f)
	{
		return false;
	}

	OutRequest = FHeavyImpactPreparationRequest();
	OutRequest.ImpactId = CurrentImpactId;
	OutRequest.SourceActor = this;
	OutRequest.SourceComponent = RamBody;
	OutRequest.PredictedImpactPoint = PredictedHit.ImpactPoint;
	OutRequest.SourceLinearVelocity = PlannedWorldVelocity;
	OutRequest.EstimatedTimeToContactSeconds = EstimatedTimeToContact;
	return !OutRequest.PredictedImpactPoint.ContainsNaN();
}

/** 非法配置时停止全部周期和碰撞，避免半工作状态掩盖错误。 */
void ABatteringRamHazard::DisableHazard(const FString& Reason)
{
	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	SetActorTickEnabled(false);
	Phase = EBatteringRamPhase::Disabled;
	MotionElapsedSeconds = 0.0f;
	CurrentImpactId = FGuid();
	NotifiedReceiversThisStroke.Reset();
	WarningVisualRoot->SetVisibility(false, true);

	if (IsValid(RamBody))
	{
		RamBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (IsValid(PreparationVolume))
	{
		PreparationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	UE_LOG(
		LogBatteringRamHazard,
		Error,
		TEXT("冲锤 %s 已停用：%s"),
		*GetName(),
		*Reason);
}
