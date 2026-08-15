// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticObjectComponent.cpp
 * 职责：实现磁性资格，并管理正式投掷期间的唯一临时碰撞身份、Hit、Light 窗口与破碎监听窗口。
 * 边界：Chaos 产生运动和 NormalImpulse；Light 与破碎消费者只读取本组件广播的同一次真实 Hit。
 * 状态 Owner：本组件独占 ArmedPrimitive、快照、ImpactId、Tag、Delegate 绑定与全部投掷 Timer。
 */

#include "Components/Magnetism/MagneticObjectComponent.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/MeshComponent.h"
#include "Components/Physics/HeavyImpactResponseComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Data/Magnetism/MagneticGrabTuningData.h"
#include "Data/Physics/CharacterImpactSourceProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/CharacterImpactReceiver.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Physics/CharacterImpactTypes.h"
#include "Physics/DemoCollisionChannels.h"
#include "Physics/DemoHitTags.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMagneticObjectImpact, Log, All);

/** 初始化为纯事件驱动的道具配置组件，禁止无意义的常驻 Tick。 */
UMagneticObjectComponent::UMagneticObjectComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/** 只有启用磁性、正在模拟物理且质量不超过玩家能力上限的 PrimitiveComponent 才可抓取。 */
bool UMagneticObjectComponent::CanGrab(const UPrimitiveComponent* CandidateComponent, const float MaxAllowedMass) const
{
	return bMagnetizable
		&& IsValid(CandidateComponent)
		&& CandidateComponent->IsSimulatingPhysics()
		&& CandidateComponent->GetMass() <= MaxAllowedMass;
}

/** Overlay 只覆盖当前精确网格；切换目标前先恢复旧目标，避免红光残留。 */
void UMagneticObjectComponent::SetExplosionPresentationActive(
	UPrimitiveComponent* TargetPrimitive,
	UMaterialInterface* OverlayMaterial)
{
	if (!IsValid(OverlayMaterial))
	{
		RestoreExplosionPresentation();
		return;
	}

	UMeshComponent* Mesh = Cast<UMeshComponent>(TargetPrimitive);
	if (!IsValid(Mesh))
	{
		return;
	}

	if (ExplosionPresentationMesh.Get() != Mesh)
	{
		RestoreExplosionPresentation();
		ExplosionPresentationMesh = Mesh;
		PreviousExplosionOverlayMaterial = Mesh->GetOverlayMaterial();
	}
	Mesh->SetOverlayMaterial(OverlayMaterial);
}

/** 所有取消、放下、重抓、超时和爆炸收口都经过此处恢复原 Overlay。 */
void UMagneticObjectComponent::RestoreExplosionPresentation()
{
	if (UMeshComponent* Mesh = ExplosionPresentationMesh.Get())
	{
		Mesh->SetOverlayMaterial(PreviousExplosionOverlayMaterial.Get());
	}
	ExplosionPresentationMesh.Reset();
	PreviousExplosionOverlayMaterial = nullptr;
}

/** 保存一次精确碰撞基线并建立 Light/破碎共享的正式投掷命中事务。 */
bool UMagneticObjectComponent::ArmThrownImpact(
	UPrimitiveComponent* ThrownPrimitive,
	AActor* Thrower,
	const float LightActiveDurationSeconds,
	const float MaximumBreakMonitoringSeconds,
	UMagneticGrabTuningData* ExplosionTuning)
{
	DisarmThrownImpact();

	FString ConfigurationError;
	const bool bArmExplosion = IsValid(ExplosionTuning);
	const ECollisionEnabled::Type ExistingCollision = IsValid(ThrownPrimitive)
		? ThrownPrimitive->GetCollisionEnabled()
		: ECollisionEnabled::NoCollision;
	if (!IsValid(ThrownPrimitive)
		|| ThrownPrimitive->GetOwner() != GetOwner()
		|| !ThrownPrimitive->IsSimulatingPhysics()
		|| !CollisionEnabledHasPhysics(ExistingCollision)
		|| !IsValid(Thrower)
		|| !IsValid(StandingImpactSourceProfile)
		|| !StandingImpactSourceProfile->IsConfigured(ConfigurationError)
		|| (bArmExplosion && !ExplosionTuning->IsConfigured(ConfigurationError))
		|| !FMath::IsFinite(LightActiveDurationSeconds)
		|| LightActiveDurationSeconds <= 0.0f
		|| !IsValid(GetWorld()))
	{
		UE_LOG(LogMagneticObjectImpact, Warning,
			TEXT("%s could not arm thrown impact: %s"),
			*GetNameSafe(GetOwner()),
			ConfigurationError.IsEmpty()
				? TEXT("invalid primitive, thrower, duration, physics state or source profile")
				: *ConfigurationError);
		return false;
	}

	bool bEnableBreakMonitoring = false;
	if (!FMath::IsNearlyZero(MaximumBreakMonitoringSeconds))
	{
		bEnableBreakMonitoring = FMath::IsFinite(MaximumBreakMonitoringSeconds)
			&& MaximumBreakMonitoringSeconds > LightActiveDurationSeconds;
		if (!bEnableBreakMonitoring)
		{
			UE_LOG(LogMagneticObjectImpact, Warning,
				TEXT("%s ignored invalid break monitoring duration %.3f; it must be finite and greater than Light duration %.3f."),
				*GetNameSafe(GetOwner()),
				MaximumBreakMonitoringSeconds,
				LightActiveDurationSeconds);
		}
	}

	CollisionSnapshot.CollisionProfileName = ThrownPrimitive->GetCollisionProfileName();
	CollisionSnapshot.CollisionEnabled = ExistingCollision;
	CollisionSnapshot.ObjectType = ThrownPrimitive->GetCollisionObjectType();
	CollisionSnapshot.Responses = ThrownPrimitive->GetCollisionResponseToChannels();
	CollisionSnapshot.bNotifyRigidBodyCollision = ThrownPrimitive->BodyInstance.bNotifyRigidBodyCollision;
	CollisionSnapshot.bUseCCD = ThrownPrimitive->BodyInstance.bUseCCD;
	CollisionSnapshot.bOwnerHadAttackProjectileTag = GetOwner()->ActorHasTag(DemoHitTags::AttackProjectile());
	CollisionSnapshot.bValid = true;

	ArmedPrimitive = ThrownPrimitive;
	ActiveThrower = Thrower;
	ActiveImpactId = FGuid::NewGuid();
	ActiveExplosionTuning = bArmExplosion ? ExplosionTuning : nullptr;
	bLightImpactWindowActive = !bArmExplosion;
	bExplosionImpactWindowActive = bArmExplosion;
	bKeepMonitoringForBreak = bEnableBreakMonitoring;
	bImpactConsumed = false;
	if (bArmExplosion)
	{
		SetExplosionPresentationActive(
			ThrownPrimitive,
			ExplosionTuning->ExplosionArmedOverlayMaterial.Get());
	}

	ThrownPrimitive->SetCollisionObjectType(Demo::CollisionChannels::AttackProjectileBody);
	ThrownPrimitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	ThrownPrimitive->SetNotifyRigidBodyCollision(true);
	ThrownPrimitive->SetUseCCD(true);
	ThrownPrimitive->OnComponentHit.AddUniqueDynamic(
		this, &UMagneticObjectComponent::HandleArmedPrimitiveHit);
	GetOwner()->Tags.AddUnique(DemoHitTags::AttackProjectile());

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.SetTimer(
		ActiveDurationTimerHandle,
		this,
		&UMagneticObjectComponent::HandleLightWindowExpired,
		LightActiveDurationSeconds,
		false);
	if (bKeepMonitoringForBreak)
	{
		TimerManager.SetTimer(
			MaximumMonitoringTimerHandle,
			this,
			&UMagneticObjectComponent::HandleMaximumMonitoringExpired,
			MaximumBreakMonitoringSeconds,
			false);
	}

	UE_LOG(LogMagneticObjectImpact, Verbose,
		TEXT("%s armed thrown impact (%s %.2fs, break monitor %.2fs)."),
		*GetNameSafe(GetOwner()),
		bArmExplosion ? TEXT("Explosion") : TEXT("Light"),
		LightActiveDurationSeconds,
		bKeepMonitoringForBreak ? MaximumBreakMonitoringSeconds : 0.0f);
	return true;
}

/** 清除全部异步入口、解绑唯一 Hit，并严格恢复正式投掷前的碰撞和 Tag。 */
void UMagneticObjectComponent::DisarmThrownImpact()
{
	RestoreExplosionPresentation();

	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(ActiveDurationTimerHandle);
		TimerManager.ClearTimer(DeferredLightResolutionTimerHandle);
		TimerManager.ClearTimer(MaximumMonitoringTimerHandle);
	}

	UPrimitiveComponent* Primitive = ArmedPrimitive.Get();
	if (IsValid(Primitive))
	{
		Primitive->OnComponentHit.RemoveDynamic(
			this, &UMagneticObjectComponent::HandleArmedPrimitiveHit);
		if (CollisionSnapshot.bValid)
		{
			Primitive->SetCollisionProfileName(CollisionSnapshot.CollisionProfileName);
			Primitive->SetCollisionEnabled(CollisionSnapshot.CollisionEnabled);
			Primitive->SetCollisionObjectType(CollisionSnapshot.ObjectType);
			Primitive->SetCollisionResponseToChannels(CollisionSnapshot.Responses);
			Primitive->SetNotifyRigidBodyCollision(CollisionSnapshot.bNotifyRigidBodyCollision);
			Primitive->SetUseCCD(CollisionSnapshot.bUseCCD);
		}
	}

	if (IsValid(GetOwner())
		&& CollisionSnapshot.bValid
		&& !CollisionSnapshot.bOwnerHadAttackProjectileTag)
	{
		GetOwner()->Tags.Remove(DemoHitTags::AttackProjectile());
	}

	ArmedPrimitive.Reset();
	ActiveThrower.Reset();
	ActiveExplosionTuning.Reset();
	ActiveImpactId.Invalidate();
	CollisionSnapshot.Reset();
	bLightImpactWindowActive = false;
	bExplosionImpactWindowActive = false;
	bKeepMonitoringForBreak = false;
	bImpactConsumed = false;
}

/** Owner 生命周期结束时先撤销所有外部回调，再交给父类完成销毁。 */
void UMagneticObjectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DisarmThrownImpact();
	Super::EndPlay(EndPlayReason);
}

/** 先广播已过滤的真实阻挡命中，再在 Light 窗口内尝试构造一次角色请求。 */
void UMagneticObjectComponent::HandleArmedPrimitiveHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (HitComponent != ArmedPrimitive.Get()
		|| !Hit.bBlockingHit
		|| !IsValid(OtherActor)
		|| OtherActor == ActiveThrower.Get())
	{
		return;
	}


	if (bExplosionImpactWindowActive && ActiveExplosionTuning.IsValid())
	{
		const float FragmentMultiplier =
			ActiveExplosionTuning->ExplosionFragmentSeparationMultiplier;
		bExplosionImpactWindowActive = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ActiveDurationTimerHandle);
		}

		const FVector ExplosionOrigin = Hit.ImpactPoint.ContainsNaN()
			? HitComponent->GetComponentLocation()
			: FVector(Hit.ImpactPoint);
		TriggerExplosion(ExplosionOrigin);
		ThrownBlockingHit.Broadcast(
			HitComponent,
			OtherActor,
			OtherComponent,
			NormalImpulse,
			Hit,
			true,
			FragmentMultiplier);
		DisarmThrownImpact();
		return;
	}

	ThrownBlockingHit.Broadcast(
		HitComponent,
		OtherActor,
		OtherComponent,
		NormalImpulse,
		Hit,
		false,
		1.0f);

	if (!bLightImpactWindowActive
		|| bImpactConsumed
		|| !OtherActor->GetClass()->ImplementsInterface(UCharacterImpactReceiver::StaticClass()))
	{
		return;
	}

	const float ImpulseMagnitude = NormalImpulse.Size();
	if (!FMath::IsFinite(ImpulseMagnitude)
		|| ImpulseMagnitude < StandingImpactSourceProfile->MinimumPhysicalImpulse)
	{
		return;
	}

	FVector PushDirection = (-Hit.ImpactNormal).GetSafeNormal();
	const FVector TowardTarget =
		(OtherActor->GetActorLocation() - HitComponent->GetComponentLocation()).GetSafeNormal();
	if (PushDirection.IsNearlyZero())
	{
		PushDirection = (-NormalImpulse).GetSafeNormal();
	}
	if (!TowardTarget.IsNearlyZero() && FVector::DotProduct(PushDirection, TowardTarget) < 0.0f)
	{
		PushDirection *= -1.0f;
	}
	if (PushDirection.IsNearlyZero())
	{
		return;
	}

	FStandingImpactRequest Request;
	Request.ImpactId = ActiveImpactId;
	Request.SourceActor = GetOwner();
	Request.SourceComponent = HitComponent;
	Request.SourceProfile = StandingImpactSourceProfile;
	Request.WorldDirection = PushDirection;
	Request.ImpactPoint = Hit.ImpactPoint;
	Request.NormalizedStrength = StandingImpactSourceProfile->NormalizePhysicalImpulse(ImpulseMagnitude);
	Request.RawNormalImpulse = NormalImpulse;

	const EStandingImpactSubmitResult Result =
		ICharacterImpactReceiver::Execute_SubmitStandingImpact(OtherActor, Request);
	bImpactConsumed = true;
	UE_LOG(LogMagneticObjectImpact, Verbose,
		TEXT("%s thrown Light consumed by %s with result %d (impulse %.1f)."),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(OtherActor),
		static_cast<int32>(Result),
		ImpulseMagnitude);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ActiveDurationTimerHandle);
		DeferredLightResolutionTimerHandle = World->GetTimerManager().SetTimerForNextTick(
			this, &UMagneticObjectComponent::HandleDeferredLightResolved);
	}
}

/** Light 时间自然结束；破碎消费者存在时只降级身份，否则完整恢复。 */
void UMagneticObjectComponent::HandleLightWindowExpired()
{
	bLightImpactWindowActive = false;
	bExplosionImpactWindowActive = false;
	if (!bKeepMonitoringForBreak)
	{
		DisarmThrownImpact();
		return;
	}

	RestoreAttackIdentityButKeepHitMonitoring();
}

/** 爆炸由同一投掷 ImpactId 对每个 Pawn Actor 只结算一次；碎片不参与该查询。 */
void UMagneticObjectComponent::TriggerExplosion(const FVector& ExplosionOrigin)
{
	UWorld* World = GetWorld();
	UMagneticGrabTuningData* ExplosionTuning = ActiveExplosionTuning.Get();
	if (!IsValid(World) || !IsValid(ExplosionTuning) || !ActiveImpactId.IsValid())
	{
		return;
	}
	SpawnExplosionPresentation(ExplosionOrigin, *ExplosionTuning);

	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MagneticExplosion), false, GetOwner());
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		ExplosionOrigin,
		FQuat::Identity,
		ObjectQuery,
		FCollisionShape::MakeSphere(ExplosionTuning->ExplosionRadius),
		QueryParams);

	AController* InstigatorController = nullptr;
	if (const APawn* ThrowerPawn = Cast<APawn>(ActiveThrower.Get()))
	{
		InstigatorController = ThrowerPawn->GetController();
	}

	TSet<AActor*> AffectedActors;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* TargetActor = Overlap.GetActor();
		if (!IsValid(TargetActor) || TargetActor == GetOwner() || AffectedActors.Contains(TargetActor))
		{
			continue;
		}
		AffectedActors.Add(TargetActor);

		const FVector ToTarget = TargetActor->GetActorLocation() - ExplosionOrigin;
		const float Distance = ToTarget.Size();
		if (!FMath::IsFinite(Distance) || Distance > ExplosionTuning->ExplosionRadius)
		{
			continue;
		}

		const float DistanceAlpha = FMath::Clamp(
			Distance / ExplosionTuning->ExplosionRadius,
			0.0f,
			1.0f);
		const float DistanceScale = FMath::Lerp(
			1.0f,
			ExplosionTuning->ExplosionEdgeEffectScale,
			DistanceAlpha);
		const uint32 Seed = HashCombine(
			GetTypeHash(ActiveImpactId),
			GetTypeHash(TargetActor->GetUniqueID()));
		FRandomStream RandomStream(static_cast<int32>(Seed));

		FVector HorizontalDirection(ToTarget.X, ToTarget.Y, 0.0f);
		HorizontalDirection = HorizontalDirection.GetSafeNormal();
		if (HorizontalDirection.IsNearlyZero())
		{
			const float RandomYaw = RandomStream.FRandRange(-180.0f, 180.0f);
			HorizontalDirection = FRotator(0.0f, RandomYaw, 0.0f).Vector();
		}
		else
		{
			const float JitterYaw = RandomStream.FRandRange(
				-ExplosionTuning->ExplosionDirectionJitterDegrees,
				ExplosionTuning->ExplosionDirectionJitterDegrees);
			HorizontalDirection = FRotator(0.0f, JitterYaw, 0.0f)
				.RotateVector(HorizontalDirection);
		}

		const float StrengthScale = DistanceScale * RandomStream.FRandRange(
			1.0f - ExplosionTuning->ExplosionStrengthJitterRatio,
			1.0f + ExplosionTuning->ExplosionStrengthJitterRatio);
		const FVector VelocityChange =
			HorizontalDirection * ExplosionTuning->ExplosionHorizontalVelocityChange * StrengthScale
			+ FVector::UpVector * ExplosionTuning->ExplosionUpwardVelocityChange * StrengthScale;

		if (UHeavyImpactResponseComponent* HeavyImpact =
			TargetActor->FindComponentByClass<UHeavyImpactResponseComponent>())
		{
			HeavyImpact->RequestRadialImpact(
				ActiveImpactId,
				GetOwner(),
				VelocityChange);
		}

		UGameplayStatics::ApplyDamage(
			TargetActor,
			ExplosionTuning->ExplosionDamage * DistanceScale,
			InstigatorController,
			GetOwner(),
			UDamageType::StaticClass());
	}

	UE_LOG(LogMagneticObjectImpact, Log,
		TEXT("%s exploded at %s and affected %d Pawn actors."),
		*GetNameSafe(GetOwner()),
		*ExplosionOrigin.ToCompactString(),
		AffectedActors.Num());
}

/** 火星和火焰烟雾生成在世界中；原投掷物随后破碎或 Disarm 不会截断表现。 */
void UMagneticObjectComponent::SpawnExplosionPresentation(
	const FVector& ExplosionOrigin,
	const UMagneticGrabTuningData& ExplosionTuning)
{
	UParticleSystemComponent* Sparks = nullptr;
	if (IsValid(ExplosionTuning.ExplosionSparkEffect.Get()))
	{
		const float SparkScale =
			ExplosionTuning.ExplosionRadius / ExplosionTuning.ExplosionSparkReferenceRadius;
		Sparks = UGameplayStatics::SpawnEmitterAtLocation(
			this,
			ExplosionTuning.ExplosionSparkEffect.Get(),
			ExplosionOrigin,
			FRotator::ZeroRotator,
			FVector(SparkScale),
			true,
			EPSCPoolMethod::AutoRelease,
			true);
	}
	if (IsValid(Sparks))
	{
		FTimerHandle DeactivateTimerHandle;
		GetWorld()->GetTimerManager().SetTimer(
			DeactivateTimerHandle,
			FTimerDelegate::CreateWeakLambda(Sparks, [Sparks]()
			{
				Sparks->DeactivateSystem();
			}),
			ExplosionTuning.ExplosionSparkEmissionSeconds,
			false);
	}

	UParticleSystemComponent* FireSmoke = nullptr;
	if (IsValid(ExplosionTuning.ExplosionFireSmokeEffect.Get()))
	{
		FireSmoke = UGameplayStatics::SpawnEmitterAtLocation(
			this,
			ExplosionTuning.ExplosionFireSmokeEffect.Get(),
			ExplosionOrigin,
			FRotator::ZeroRotator,
			FVector(ExplosionTuning.ExplosionFireSmokeVisualScale),
			true,
			EPSCPoolMethod::AutoRelease,
			true);
	}
	if (IsValid(FireSmoke))
	{
		FTimerHandle DeactivateTimerHandle;
		GetWorld()->GetTimerManager().SetTimer(
			DeactivateTimerHandle,
			FTimerDelegate::CreateWeakLambda(FireSmoke, [FireSmoke]()
			{
				FireSmoke->DeactivateSystem();
			}),
			ExplosionTuning.ExplosionFireSmokeEmissionSeconds,
			false);
	}
}

/** Light 请求同帧消费完成后，在 next-tick 安全恢复攻击身份或完整事务。 */
void UMagneticObjectComponent::HandleDeferredLightResolved()
{
	bLightImpactWindowActive = false;
	if (!bKeepMonitoringForBreak)
	{
		DisarmThrownImpact();
		return;
	}

	RestoreAttackIdentityButKeepHitMonitoring();
}

/** 达到破碎监听硬上限后不再保留 Hit/CCD，避免道具长期处于隐形武装状态。 */
void UMagneticObjectComponent::HandleMaximumMonitoringExpired()
{
	UE_LOG(LogMagneticObjectImpact, Verbose,
		TEXT("%s break monitoring expired; restoring the original collision state."),
		*GetNameSafe(GetOwner()));
	DisarmThrownImpact();
}

/** 恢复原始碰撞身份和 Tag，但由同一事务继续保留事件通知与 CCD 到硬上限。 */
void UMagneticObjectComponent::RestoreAttackIdentityButKeepHitMonitoring()
{
	UPrimitiveComponent* Primitive = ArmedPrimitive.Get();
	if (!IsValid(Primitive) || !CollisionSnapshot.bValid)
	{
		DisarmThrownImpact();
		return;
	}

	Primitive->SetCollisionProfileName(CollisionSnapshot.CollisionProfileName);
	Primitive->SetCollisionEnabled(CollisionSnapshot.CollisionEnabled);
	Primitive->SetCollisionObjectType(CollisionSnapshot.ObjectType);
	Primitive->SetCollisionResponseToChannels(CollisionSnapshot.Responses);
	Primitive->SetNotifyRigidBodyCollision(true);
	Primitive->SetUseCCD(true);

	if (IsValid(GetOwner()) && !CollisionSnapshot.bOwnerHadAttackProjectileTag)
	{
		GetOwner()->Tags.Remove(DemoHitTags::AttackProjectile());
	}
}
