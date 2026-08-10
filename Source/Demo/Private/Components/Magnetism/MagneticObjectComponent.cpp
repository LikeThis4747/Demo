// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticObjectComponent.cpp
 * 职责：实现磁性资格，并管理正式投掷期间的临时碰撞身份与一次真实角色命中。
 * 边界：Chaos 产生运动和 NormalImpulse；本组件只路由 Light 请求并精确恢复自身改写。
 * 状态 Owner：本组件独占 ArmedPrimitive、快照、ImpactId、Tag、Delegate 与两个 Timer。
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

bool UMagneticObjectComponent::ArmThrownImpact(
	UPrimitiveComponent* ThrownPrimitive,
	AActor* Thrower,
	const float ActiveDurationSeconds)
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
		|| !FMath::IsFinite(ActiveDurationSeconds)
		|| ActiveDurationSeconds <= 0.0f
		|| !IsValid(GetWorld()))
	{
		UE_LOG(LogMagneticObjectImpact, Warning,
			TEXT("%s could not arm thrown Light impact: %s"),
			*GetNameSafe(GetOwner()),
			ConfigurationError.IsEmpty() ? TEXT("invalid primitive, thrower, duration, physics state or source profile") : *ConfigurationError);
		return false;
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
	bImpactConsumed = false;

	ThrownPrimitive->SetCollisionObjectType(Demo::CollisionChannels::AttackProjectileBody);
	ThrownPrimitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	ThrownPrimitive->SetNotifyRigidBodyCollision(true);
	ThrownPrimitive->SetUseCCD(true);
	ThrownPrimitive->OnComponentHit.AddUniqueDynamic(
		this, &UMagneticObjectComponent::HandleArmedPrimitiveHit);
	GetOwner()->Tags.AddUnique(DemoHitTags::AttackProjectile());

	GetWorld()->GetTimerManager().SetTimer(
		ActiveDurationTimerHandle,
		this,
		&UMagneticObjectComponent::DisarmThrownImpact,
		ActiveDurationSeconds,
		false);
	return true;
}

void UMagneticObjectComponent::DisarmThrownImpact()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ActiveDurationTimerHandle);
		World->GetTimerManager().ClearTimer(DeferredDisarmTimerHandle);
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
	bImpactConsumed = false;
}

void UMagneticObjectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DisarmThrownImpact();
	Super::EndPlay(EndPlayReason);
}

void UMagneticObjectComponent::HandleArmedPrimitiveHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (bImpactConsumed
		|| HitComponent != ArmedPrimitive.Get()
		|| !IsValid(OtherActor)
		|| OtherActor == ActiveThrower.Get()
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
	const FVector TowardTarget = (OtherActor->GetActorLocation() - HitComponent->GetComponentLocation()).GetSafeNormal();
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
		TEXT("%s thrown impact consumed by %s with result %d (impulse %.1f)."),
		*GetNameSafe(GetOwner()), *GetNameSafe(OtherActor), static_cast<int32>(Result), ImpulseMagnitude);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ActiveDurationTimerHandle);
		DeferredDisarmTimerHandle = World->GetTimerManager().SetTimerForNextTick(
			this, &UMagneticObjectComponent::HandleDeferredDisarm);
	}
}

void UMagneticObjectComponent::HandleDeferredDisarm()
{
	DisarmThrownImpact();
}
