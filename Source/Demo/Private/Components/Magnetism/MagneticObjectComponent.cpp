// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticObjectComponent.cpp
 * 职责：实现磁性资格，并管理正式投掷期间的唯一临时碰撞身份、Hit、Light 窗口与破碎监听窗口。
 * 边界：Chaos 产生运动和 NormalImpulse；Light 与破碎消费者只读取本组件广播的同一次真实 Hit。
 * 状态 Owner：本组件独占 ArmedPrimitive、快照、ImpactId、Tag、Delegate 绑定与全部投掷 Timer。
 */

#include "Components/Magnetism/MagneticObjectComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Data/Physics/CharacterImpactSourceProfile.h"
#include "Engine/World.h"
#include "Interfaces/CharacterImpactReceiver.h"
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

/** 保存一次精确碰撞基线并建立 Light/破碎共享的正式投掷命中事务。 */
bool UMagneticObjectComponent::ArmThrownImpact(
	UPrimitiveComponent* ThrownPrimitive,
	AActor* Thrower,
	const float LightActiveDurationSeconds,
	const float MaximumBreakMonitoringSeconds)
{
	DisarmThrownImpact();

	FString ConfigurationError;
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
	bLightImpactWindowActive = true;
	bKeepMonitoringForBreak = bEnableBreakMonitoring;
	bImpactConsumed = false;

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
		TEXT("%s armed thrown impact (Light %.2fs, break monitor %.2fs)."),
		*GetNameSafe(GetOwner()),
		LightActiveDurationSeconds,
		bKeepMonitoringForBreak ? MaximumBreakMonitoringSeconds : 0.0f);
	return true;
}

/** 清除全部异步入口、解绑唯一 Hit，并严格恢复正式投掷前的碰撞和 Tag。 */
void UMagneticObjectComponent::DisarmThrownImpact()
{
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
	ActiveImpactId.Invalidate();
	CollisionSnapshot.Reset();
	bLightImpactWindowActive = false;
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

	ThrownBlockingHit.Broadcast(
		HitComponent,
		OtherActor,
		OtherComponent,
		NormalImpulse,
		Hit);

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
	if (!bKeepMonitoringForBreak)
	{
		DisarmThrownImpact();
		return;
	}

	RestoreAttackIdentityButKeepHitMonitoring();
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
