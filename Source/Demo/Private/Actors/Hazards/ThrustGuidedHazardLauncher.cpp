// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardLauncher.cpp
 * 职责：实现壁挂机关的首个角色触发、Muzzle 锥体瞄准和一次延迟生成。
 * 边界：不做隔墙搜索、导航追踪、循环发射、对象池或弹体物理控制。
 */

#include "Actors/Hazards/ThrustGuidedHazardLauncher.h"

#include "Actors/Hazards/ThrustGuidedHazardProjectile.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Data/Hazards/ThrustGuidedHazardTuningData.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogThrustGuidedHazardLauncher, Log, All);

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

/** 用 Muzzle 位置和 +X 生成初始姿态；延迟生成保证 BeginPlay 前注入配置和目标。 */
void AThrustGuidedHazardLauncher::FireLockedTarget()
{
	if (Phase != EThrustGuidedHazardLauncherPhase::Warning)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(WarningTimerHandle);
	Phase = EThrustGuidedHazardLauncherPhase::Spent;
	WarningVisualRoot->SetVisibility(false, true);

	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(TuningData) || !ProjectileClass)
	{
		DisableHazard(TEXT("发射时 World、TuningData 或 ProjectileClass 已失效。"));
		return;
	}

	USceneComponent* TargetComponent = LockedTargetComponent.Get();
	const FVector InitialDirection = CalculateInitialLaunchDirection(TargetComponent);
	if (InitialDirection.IsNearlyZero() || InitialDirection.ContainsNaN())
	{
		DisableHazard(TEXT("无法得到有限的初始发射方向。"));
		return;
	}

	// 弹体胶囊局部 +Z 是推进轴；这里只设置出生姿态，不在生成后改 Transform。
	const FQuat LaunchRotation =
		FQuat::FindBetweenNormals(FVector::UpVector, InitialDirection);
	const FTransform SpawnTransform(
		LaunchRotation,
		Muzzle->GetComponentLocation(),
		FVector::OneVector);

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

/** 在 Muzzle 前方锥体内瞄向目标；目标在背后或方向退化时保持直射。 */
FVector AThrustGuidedHazardLauncher::CalculateInitialLaunchDirection(
	const USceneComponent* TargetComponent) const
{
	const FVector MuzzleForward = Muzzle->GetForwardVector().GetSafeNormal();
	if (MuzzleForward.IsNearlyZero() || !IsValid(TargetComponent))
	{
		return MuzzleForward;
	}

	const FVector DesiredDirection =
		(TargetComponent->GetComponentLocation() - Muzzle->GetComponentLocation()).GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		return MuzzleForward;
	}

	const float Dot = FMath::Clamp(
		FVector::DotProduct(MuzzleForward, DesiredDirection),
		-1.0f,
		1.0f);
	const float DesiredAngleRadians = FMath::Acos(Dot);
	const float MaximumAngleRadians =
		FMath::DegreesToRadians(TuningData->MaximumInitialAimAngleDegrees);
	if (DesiredAngleRadians <= MaximumAngleRadians)
	{
		return DesiredDirection;
	}

	const FVector RotationAxis =
		FVector::CrossProduct(MuzzleForward, DesiredDirection).GetSafeNormal();
	if (RotationAxis.IsNearlyZero())
	{
		return MuzzleForward;
	}

	return FQuat(RotationAxis, MaximumAngleRadians)
		.RotateVector(MuzzleForward)
		.GetSafeNormal();
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
	if (Phase != EThrustGuidedHazardLauncherPhase::Armed)
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
