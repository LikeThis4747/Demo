// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardLauncher.cpp
 * 职责：实现壁挂机关的首个角色触发、Muzzle 锥体瞄准和一次延迟生成。
 * 边界：不做隔墙搜索、导航追踪、循环发射、对象池或弹体物理控制。
 */

#include "Actors/Hazards/ThrustGuidedHazardLauncher.h"

#include "Actors/Hazards/ThrustGuidedHazardProjectile.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Data/Hazards/ThrustGuidedHazardTuningData.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogThrustGuidedHazardLauncher, Log, All);

namespace ThrustGuidedHazardLauncher
{
	/** 把任意期望方向限制在 Forward 周围的圆锥内；反向退化时保持 Forward。 */
	FVector ClampDirectionToCone(
		const FVector& Forward,
		const FVector& Desired,
		const float MaximumAngleRadians)
	{
		const FVector NormalizedForward = Forward.GetSafeNormal();
		const FVector NormalizedDesired = Desired.GetSafeNormal();
		if (NormalizedForward.IsNearlyZero() || NormalizedDesired.IsNearlyZero())
		{
			return NormalizedForward;
		}

		const float Dot = FMath::Clamp(
			FVector::DotProduct(NormalizedForward, NormalizedDesired),
			-1.0f,
			1.0f);
		const float Angle = FMath::Acos(Dot);
		if (Angle <= MaximumAngleRadians)
		{
			return NormalizedDesired;
		}

		const FVector Axis =
			FVector::CrossProduct(NormalizedForward, NormalizedDesired).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			return NormalizedForward;
		}

		return FQuat(Axis, MaximumAngleRadians)
			.RotateVector(NormalizedForward)
			.GetSafeNormal();
	}

	/** 镜像 ProjectileBody 对场景对象类型的阻挡响应，避免纯查询体造成出生假失败。 */
	bool ProjectileBlocksObjectType(const ECollisionChannel ObjectType)
	{
		switch (ObjectType)
		{
		case ECC_WorldStatic:
		case ECC_WorldDynamic:
		case ECC_PhysicsBody:
		case ECC_Pawn:
		case ECC_Visibility:
			return true;
		default:
			return false;
		}
	}
}

/** 装配固定壁挂外壳、独立发射口、独立触发锚点和预警挂点。 */
AThrustGuidedHazardLauncher::AThrustGuidedHazardLauncher()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	HousingVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HousingVisualRoot"));
	HousingVisualRoot->SetupAttachment(SceneRoot);

	Muzzle = CreateDefaultSubobject<USceneComponent>(TEXT("Muzzle"));
	Muzzle->SetupAttachment(SceneRoot);

	TriggerAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("TriggerAnchor"));
	TriggerAnchor->SetupAttachment(SceneRoot);
	TriggerAnchor->SetRelativeLocation(FVector(600.0f, 0.0f, 0.0f));

	TriggerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerVolume"));
	TriggerVolume->SetupAttachment(TriggerAnchor);
	TriggerVolume->SetMobility(EComponentMobility::Movable);
	TriggerVolume->SetCanEverAffectNavigation(false);
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TriggerVolume->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerVolume->SetGenerateOverlapEvents(true);

	WarningVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WarningVisualRoot"));
	WarningVisualRoot->SetupAttachment(HousingVisualRoot);
	WarningVisualRoot->SetVisibility(false, true);

	ApplyTriggerGeometry(*GetDefault<UThrustGuidedHazardTuningData>());
}

/** 预览配置只改变盒体尺寸，避免 Construction 覆盖设计师为拐角摆位设置的锚点变换。 */
void AThrustGuidedHazardLauncher::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const UThrustGuidedHazardTuningData* PreviewData = IsValid(TuningData)
		? TuningData.Get()
		: GetDefault<UThrustGuidedHazardTuningData>();

	FString Error;
	if (IsValid(PreviewData) && PreviewData->IsConfigured(Error))
	{
		ApplyTriggerGeometry(*PreviewData);
	}
}

/** 校验唯一配置、弹体类和单位缩放，再允许第一个 Character 进入触发。 */
void AThrustGuidedHazardLauncher::BeginPlay()
{
	Super::BeginPlay();
	Phase = EThrustGuidedHazardLauncherPhase::Disabled;
	WarningVisualRoot->SetVisibility(false, true);

	if (!IsValid(TuningData))
	{
		DisableHazard(TEXT("未指定 ThrustGuidedHazardTuningData。"));
		return;
	}

	FString Error;
	if (!TuningData->IsConfigured(Error))
	{
		DisableHazard(Error);
		return;
	}

	if (!ProjectileClass)
	{
		DisableHazard(TEXT("未指定 AThrustGuidedHazardProjectile 子类。"));
		return;
	}

	if (!GetActorScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		DisableHazard(TEXT("Launcher Actor Scale 必须保持 (1,1,1)；触发尺寸和摆位使用 cm。"));
		return;
	}

	if (!IsValid(Muzzle) || Muzzle->GetForwardVector().ContainsNaN())
	{
		DisableHazard(TEXT("Muzzle 变换无效。"));
		return;
	}

	ApplyTriggerGeometry(*TuningData);
	TriggerVolume->OnComponentBeginOverlap.AddDynamic(
		this,
		&AThrustGuidedHazardLauncher::HandleTriggerBeginOverlap);
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Phase = EThrustGuidedHazardLauncherPhase::Armed;
}

/** 清理本 Actor 拥有的 Timer、委托和目标弱引用。 */
void AThrustGuidedHazardLauncher::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(WarningTimerHandle);
	if (IsValid(TriggerVolume))
	{
		TriggerVolume->OnComponentBeginOverlap.RemoveDynamic(
			this,
			&AThrustGuidedHazardLauncher::HandleTriggerBeginOverlap);
	}
	LockedTargetActor.Reset();
	LockedTargetComponent.Reset();

	Super::EndPlay(EndPlayReason);
}

/** DataAsset 只拥有尺寸；TriggerAnchor 变换属于 Blueprint/关卡摆位数据。 */
void AThrustGuidedHazardLauncher::ApplyTriggerGeometry(
	const UThrustGuidedHazardTuningData& Tuning)
{
	TriggerVolume->SetBoxExtent(Tuning.TriggerHalfExtent, true);
	TriggerVolume->SetRelativeLocationAndRotation(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		false,
		nullptr,
		ETeleportType::None);
}

/** 关闭触发盒确保“首个进入者”唯一，并把根组件作为稳定追踪点。 */
void AThrustGuidedHazardLauncher::EnterWarning(ACharacter& TargetCharacter)
{
	if (Phase != EThrustGuidedHazardLauncherPhase::Armed)
	{
		return;
	}

	USceneComponent* TargetComponent = TargetCharacter.GetRootComponent();
	if (!IsValid(TargetComponent))
	{
		return;
	}

	Phase = EThrustGuidedHazardLauncherPhase::Warning;
	LockedTargetActor = &TargetCharacter;
	LockedTargetComponent = TargetComponent;
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WarningVisualRoot->SetVisibility(true, true);

	GetWorldTimerManager().SetTimer(
		WarningTimerHandle,
		this,
		&AThrustGuidedHazardLauncher::FireLockedTarget,
		TuningData->WarningSeconds,
		false);
}

/** 先检查完整出生胶囊，再延迟生成；只有 FinishSpawning 成功才消耗一次性机关。 */
void AThrustGuidedHazardLauncher::FireLockedTarget()
{
	if (Phase != EThrustGuidedHazardLauncherPhase::Warning)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(WarningTimerHandle);

	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(TuningData) || !ProjectileClass)
	{
		DisableHazard(TEXT("发射时 World、TuningData 或 ProjectileClass 已失效。"));
		return;
	}

	USceneComponent* TargetComponent = LockedTargetComponent.Get();
	ACharacter* TargetActor = LockedTargetActor.Get();
	if (!IsValid(TargetComponent)
		|| !IsValid(TargetActor)
		|| !TargetComponent->IsRegistered()
		|| TargetComponent->GetOwner() != TargetActor)
	{
		ReturnToArmedAfterCancelledWarning(TEXT("locked target became invalid"));
		return;
	}

	FTransform SpawnTransform;
	FString FailureReason;
	if (!TryBuildSpawnTransform(*TargetComponent, SpawnTransform, FailureReason))
	{
		DisableHazard(FailureReason);
		return;
	}

	const FGuid LaunchId = FGuid::NewGuid();
	AThrustGuidedHazardProjectile* Projectile =
		World->SpawnActorDeferred<AThrustGuidedHazardProjectile>(
			ProjectileClass,
			SpawnTransform,
			this,
			GetInstigator(),
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Projectile))
	{
		DisableHazard(TEXT("延迟生成物理制导弹体失败。"));
		return;
	}

	Projectile->ConfigureLaunch(TuningData, TargetComponent, LaunchId);
	AActor* FinishedActor = UGameplayStatics::FinishSpawningActor(
		Projectile,
		SpawnTransform);
	if (!IsValid(FinishedActor))
	{
		DisableHazard(TEXT("完成物理制导弹体生成失败。"));
		return;
	}

	Phase = EThrustGuidedHazardLauncherPhase::Spent;
	WarningVisualRoot->SetVisibility(false, true);

	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Display,
		TEXT("Launcher %s fired LaunchId=%s Target=%s."),
		*GetNameSafe(this),
		*LaunchId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*GetNameSafe(LockedTargetActor.Get()));

	LockedTargetActor.Reset();
	LockedTargetComponent.Reset();
}

/** 目标在预警期间消失时恢复 Armed；本次预警不自动改锁其他角色。 */
void AThrustGuidedHazardLauncher::ReturnToArmedAfterCancelledWarning(
	const TCHAR* Reason)
{
	GetWorldTimerManager().ClearTimer(WarningTimerHandle);
	LockedTargetActor.Reset();
	LockedTargetComponent.Reset();

	if (IsValid(WarningVisualRoot))
	{
		WarningVisualRoot->SetVisibility(false, true);
	}

	Phase = EThrustGuidedHazardLauncherPhase::Armed;
	if (IsValid(TriggerVolume))
	{
		bSuppressTriggerOverlap = true;
		TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		bSuppressTriggerOverlap = false;
	}

	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Display,
		TEXT("Launcher %s cancelled warning and returned to Armed: %s."),
		*GetNameSafe(this),
		Reason);
}

/** 按距离计算短时目标前置，并在 Muzzle 前方锥体内限制初始瞄准。 */
FVector AThrustGuidedHazardLauncher::CalculateInitialLaunchDirection(
	const USceneComponent* TargetComponent) const
{
	const FVector MuzzleForward = Muzzle->GetForwardVector().GetSafeNormal();
	if (MuzzleForward.IsNearlyZero()
		|| MuzzleForward.ContainsNaN()
		|| !IsValid(TargetComponent)
		|| !IsValid(TuningData))
	{
		return MuzzleForward;
	}

	const FVector MuzzleLocation = Muzzle->GetComponentLocation();
	const FVector TargetLocation = TargetComponent->GetComponentLocation();
	const FVector TargetVelocity = TargetComponent->GetComponentVelocity();
	if (MuzzleLocation.ContainsNaN()
		|| TargetLocation.ContainsNaN()
		|| TargetVelocity.ContainsNaN())
	{
		return MuzzleForward;
	}

	const float Distance = FVector::Distance(MuzzleLocation, TargetLocation);
	if (!FMath::IsFinite(Distance))
	{
		return MuzzleForward;
	}

	const float LeadSeconds = FMath::Clamp(
		Distance / FMath::Max(TuningData->TargetPoweredSpeed, 1.0f),
		0.0f,
		TuningData->MaximumTargetLeadTimeSeconds);
	const FVector PredictedTargetLocation =
		TargetLocation + TargetVelocity * LeadSeconds;
	const FVector DesiredDirection =
		(PredictedTargetLocation - MuzzleLocation).GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		return MuzzleForward;
	}

	return ThrustGuidedHazardLauncher::ClampDirectionToCone(
		MuzzleForward,
		DesiredDirection,
		FMath::DegreesToRadians(TuningData->MaximumInitialAimAngleDegrees));
}

/** Muzzle 是出口平面；中心前移半高和净空，胶囊局部 +Z 对齐初始方向。 */
bool AThrustGuidedHazardLauncher::TryBuildSpawnTransform(
	const USceneComponent& TargetComponent,
	FTransform& OutSpawnTransform,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (!IsValid(Muzzle) || !IsValid(TuningData))
	{
		OutFailureReason = TEXT("Muzzle 或 TuningData 在出生检查前失效。");
		return false;
	}

	const FVector InitialDirection =
		CalculateInitialLaunchDirection(&TargetComponent);
	if (InitialDirection.IsNearlyZero() || InitialDirection.ContainsNaN())
	{
		OutFailureReason = TEXT("无法得到有限的初始发射方向。");
		return false;
	}

	const float CenterOffset =
		TuningData->ProjectileHalfHeight + TuningData->SpawnClearanceMargin;
	const FVector SpawnCenter =
		Muzzle->GetComponentLocation() + InitialDirection * CenterOffset;
	const FQuat SpawnRotation =
		FQuat::FindBetweenNormals(FVector::UpVector, InitialDirection);
	OutSpawnTransform = FTransform(
		SpawnRotation,
		SpawnCenter,
		FVector::OneVector);

	if (!OutSpawnTransform.IsValid())
	{
		OutFailureReason = TEXT("计算出的弹体出生 Transform 无效。");
		return false;
	}

	return IsSpawnPoseClear(OutSpawnTransform, OutFailureReason);
}

/** 故意不忽略 Launcher；若胶囊仍与炮管、外壳或墙体阻挡，摆位必须明确失败。 */
bool AThrustGuidedHazardLauncher::IsSpawnPoseClear(
	const FTransform& SpawnTransform,
	FString& OutFailureReason) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(TuningData))
	{
		OutFailureReason = TEXT("出生检查没有有效 World 或 TuningData。");
		return false;
	}

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		TuningData->ProjectileRadius,
		TuningData->ProjectileHalfHeight);
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(ThrustGuidedHazardSpawn),
		false);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(
		Overlaps,
		SpawnTransform.GetLocation(),
		SpawnTransform.GetRotation(),
		ECC_PhysicsBody,
		Shape,
		QueryParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* BlockingComponent = Overlap.Component.Get();
		if (!IsValid(BlockingComponent)
			|| (BlockingComponent->GetCollisionEnabled()
				!= ECollisionEnabled::QueryAndPhysics
				&& BlockingComponent->GetCollisionEnabled()
					!= ECollisionEnabled::PhysicsOnly)
			|| BlockingComponent->GetCollisionResponseToChannel(ECC_PhysicsBody)
				!= ECR_Block
			|| !ThrustGuidedHazardLauncher::ProjectileBlocksObjectType(
				BlockingComponent->GetCollisionObjectType()))
		{
			continue;
		}

		OutFailureReason = FString::Printf(
			TEXT("弹体出生胶囊被 %s.%s 阻挡。"),
			*GetNameSafe(BlockingComponent->GetOwner()),
			*GetNameSafe(BlockingComponent));
		return false;
	}

	return true;
}

/** 只接受 ACharacter；道具不会消耗机关，第二个角色也不能替换已锁定目标。 */
void AThrustGuidedHazardLauncher::HandleTriggerBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (bSuppressTriggerOverlap
		|| Phase != EThrustGuidedHazardLauncherPhase::Armed)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(OtherActor);
	if (IsValid(Character))
	{
		EnterWarning(*Character);
	}
}

/** 非法装配时永久关闭本次实例，避免半工作机关掩盖墙体穿插或配置错误。 */
void AThrustGuidedHazardLauncher::DisableHazard(const FString& Reason)
{
	GetWorldTimerManager().ClearTimer(WarningTimerHandle);
	Phase = EThrustGuidedHazardLauncherPhase::Disabled;
	LockedTargetActor.Reset();
	LockedTargetComponent.Reset();

	if (IsValid(TriggerVolume))
	{
		TriggerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (IsValid(WarningVisualRoot))
	{
		WarningVisualRoot->SetVisibility(false, true);
	}

	UE_LOG(
		LogThrustGuidedHazardLauncher,
		Error,
		TEXT("Launcher %s disabled: %s"),
		*GetNameSafe(this),
		*Reason);
}
