// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file CharacterImpactResponseComponent.cpp
 * 职责：实现非 Tick 的站立 Light 状态、水平停顿/减速、精确 Montage 与 Heavy Prepared 抢占。
 * 边界：只恢复自己写过的 MaxWalkSpeed，不恢复历史速度、不改 MovementMode，也不操作 Physics Control。
 */

#include "Components/Physics/CharacterImpactResponseComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/Physics/HeavyImpactResponseComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/Physics/CharacterImpactSourceProfile.h"
#include "Data/Physics/CharacterImpactTuningData.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharacterImpact, Log, All);

namespace
{
	int32 GetImpactPriority(const EStandingImpactResult Result)
	{
		switch (Result)
		{
		case EStandingImpactResult::Stop:
			return 2;
		case EStandingImpactResult::Slow:
			return 1;
		default:
			return 0;
		}
	}
}

UCharacterImpactResponseComponent::UCharacterImpactResponseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	RecentImpactIds.Reserve(Demo::CharacterImpact::RecentImpactHistorySize);
}

void UCharacterImpactResponseComponent::Configure(
	ACharacter* InCharacter,
	USkeletalMeshComponent* InMesh,
	UCharacterMovementComponent* InMovement,
	UPhysicalAnimationComponent* InPhysicalAnimation,
	const EImpactReceiverCategory InReceiverCategory,
	UCharacterImpactTuningData* InTuning,
	UHeavyImpactResponseComponent* InHeavyImpact)
{
	StopPhysicalReaction();
	ReleasePhysicalAnimationConfiguration();
	ClearActiveImpact(true, true);
	if (IsValid(HeavyImpact))
	{
		HeavyImpact->OnPreContactCaptureRequested.RemoveAll(this);
		HeavyImpact->OnStateChanged.RemoveAll(this);
	}

	Character = InCharacter;
	Mesh = InMesh;
	Movement = InMovement;
	PhysicalAnimation = InPhysicalAnimation;
	ReceiverCategory = InReceiverCategory;
	Tuning = InTuning;
	HeavyImpact = InHeavyImpact;
	bConfigurationReady = false;
	bPhysicalReactionReady = false;
	PhysicalConfigurationFrame = 0;

	FString ConfigurationError;
	if (!IsValid(Character)
		|| Character != GetOwner()
		|| !IsValid(Mesh)
		|| !IsValid(Movement)
		|| !IsValid(Tuning)
		|| !IsValid(HeavyImpact))
	{
		UE_LOG(LogCharacterImpact, Error,
			TEXT("%s CharacterImpact missing Character, Mesh, Movement, Tuning or HeavyImpact; Light is disabled."),
			*GetNameSafe(GetOwner()));
		return;
	}
	if (!Tuning->IsConfigured(Mesh, ConfigurationError))
	{
		UE_LOG(LogCharacterImpact, Error,
			TEXT("%s CharacterImpact tuning is invalid: %s"),
			*GetNameSafe(GetOwner()), *ConfigurationError);
		return;
	}

	if (Tuning->bEnablePhysicalReaction)
	{
		FString PhysicalError;
		if (!IsValid(PhysicalAnimation))
		{
			PhysicalError = TEXT("the character has no PhysicalAnimation component");
		}
		else if (Tuning->IsPhysicalReactionConfigured(Mesh, PhysicalError))
		{
			PhysicalAnimation->SetSkeletalMeshComponent(Mesh);
			PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(
				Tuning->UpperBodyRootBone,
				Tuning->PhysicalAnimationSettings,
				true);
			PhysicalAnimation->SetStrengthMultiplyer(0.0f);
			PhysicalAnimation->Activate(true);
			PhysicalAnimation->SetComponentTickEnabled(true);
			PhysicalConfigurationFrame = GFrameCounter;
			bPhysicalReactionReady = true;
		}

		if (!bPhysicalReactionReady)
		{
			UE_LOG(LogCharacterImpact, Warning,
				TEXT("%s Light physical presentation disabled: %s"),
				*GetNameSafe(GetOwner()),
				PhysicalError.IsEmpty() ? TEXT("invalid physical configuration") : *PhysicalError);
			ReleasePhysicalAnimationConfiguration();
		}
	}

	HeavyImpact->OnPreContactCaptureRequested.AddUObject(
		this, &UCharacterImpactResponseComponent::HandleHeavyPreContactCaptureRequested);
	HeavyImpact->OnStateChanged.AddUObject(
		this, &UCharacterImpactResponseComponent::HandleHeavyImpactStateChanged);
	bConfigurationReady = true;
}

EStandingImpactSubmitResult UCharacterImpactResponseComponent::SubmitImpact(
	const FStandingImpactRequest& Request)
{
	FString FailureReason;
	if (!bConfigurationReady
		|| !Request.IsStructurallyValid(Character, FailureReason)
		|| !Request.SourceProfile->IsConfigured(FailureReason))
	{
		UE_LOG(LogCharacterImpact, Warning,
			TEXT("CharacterImpact rejected on %s: %s"),
			*GetNameSafe(GetOwner()),
			FailureReason.IsEmpty() ? TEXT("component is not configured") : *FailureReason);
		return EStandingImpactSubmitResult::Invalid;
	}

	if (HasSeenImpactId(Request.ImpactId))
	{
		return EStandingImpactSubmitResult::Duplicate;
	}

	if (HeavyImpact->IsBusy())
	{
		RecordRecentImpactId(Request.ImpactId);
		return EStandingImpactSubmitResult::HeavyBusy;
	}

	const FStandingImpactReactionSpec& Spec =
		Request.SourceProfile->GetReaction(ReceiverCategory);
	if (Spec.Result == EStandingImpactResult::None)
	{
		RecordRecentImpactId(Request.ImpactId);
		return EStandingImpactSubmitResult::Ignored;
	}
	if (Spec.bApplyPhysicalReaction)
	{
		TryApplyPhysicalReaction(Request);
	}

	const float Strength = FMath::Clamp(Request.NormalizedStrength, 0.0f, 1.0f);
	const int32 NewPriority = GetImpactPriority(Spec.Result);
	const int32 ActivePriority = GetImpactPriority(ActiveResult);
	if (IsLightActive())
	{
		if (NewPriority < ActivePriority
			|| (NewPriority == ActivePriority
				&& Strength <= ActiveStrength + UE_KINDA_SMALL_NUMBER))
		{
			RecordRecentImpactId(Request.ImpactId);
			return EStandingImpactSubmitResult::Ignored;
		}
		if (!FMath::IsNearlyEqual(Movement->MaxWalkSpeed, LastWrittenMaxWalkSpeed))
		{
			UE_LOG(LogCharacterImpact, Warning,
				TEXT("%s MaxWalkSpeed changed while Light was active; stronger request was ignored to avoid overwriting another system."),
				*GetNameSafe(GetOwner()));
			RecordRecentImpactId(Request.ImpactId);
			return EStandingImpactSubmitResult::Ignored;
		}
	}

	const bool bWasInactive = !IsLightActive();
	const float BaselineSpeed = bWasInactive ? Movement->MaxWalkSpeed : BaselineMaxWalkSpeed;
	const float EffectiveSpeedMultiplier =
		FMath::Lerp(1.0f, Spec.SpeedMultiplier, Strength);
	bool bMovementApplied = false;
	if (Spec.Result == EStandingImpactResult::Slow)
	{
		bMovementApplied = ApplySlowMovement(EffectiveSpeedMultiplier, Strength, BaselineSpeed);
	}
	else if (Spec.Result == EStandingImpactResult::Stop)
	{
		bMovementApplied = ApplyStopMovement(BaselineSpeed);
	}

	if (!bMovementApplied)
	{
		UE_LOG(LogCharacterImpact, Warning,
			TEXT("%s ignored Light Stop/Slow in unsupported MovementMode %d."),
			*GetNameSafe(GetOwner()), static_cast<int32>(Movement->MovementMode));
		RecordRecentImpactId(Request.ImpactId);
		return EStandingImpactSubmitResult::Ignored;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (bWasInactive)
	{
		BaselineMaxWalkSpeed = BaselineSpeed;
		LightWindowStartTimeSeconds = Now;
	}
	const float DurationScale = FMath::Lerp(
		Tuning->MinimumStrengthDurationScale, 1.0f, Strength);
	const float CandidateEnd = Now + Spec.DurationSeconds * DurationScale;
	const float MaximumEnd = LightWindowStartTimeSeconds + Tuning->MaxContinuousLightSeconds;
	// 当前请求只有在优先级或强度更高时才会走到这里，因此让胜出请求完整替换时长。
	// 这样同一组强弱请求交换到达顺序时，最终都由强请求决定，而不会遗留弱请求的较长截止时间。
	ActiveEndTimeSeconds = FMath::Min(CandidateEnd, MaximumEnd);

	const bool bShouldStartAnimation =
		Spec.bPlayReactionAnimation
		&& (bWasInactive || NewPriority > ActivePriority);
	ActiveResult = Spec.Result;
	ActiveStrength = Strength;
	ActiveImpactId = Request.ImpactId;
	ActiveSourceActor = Request.SourceActor;
	RecordRecentImpactId(Request.ImpactId);
	ScheduleEndTimer(ActiveEndTimeSeconds);
	if (bShouldStartAnimation)
	{
		PlayReactionAnimation(Request.WorldDirection);
	}

	return EStandingImpactSubmitResult::Applied;
}

void UCharacterImpactResponseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPhysicalReaction();
	ClearActiveImpact(true, true);
	if (IsValid(HeavyImpact))
	{
		HeavyImpact->OnPreContactCaptureRequested.RemoveAll(this);
		HeavyImpact->OnStateChanged.RemoveAll(this);
	}
	ReleasePhysicalAnimationConfiguration();
	Super::EndPlay(EndPlayReason);
}

void UCharacterImpactResponseComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bPhysicalReactionActive
		|| !IsValid(Mesh)
		|| !IsValid(Tuning)
		|| !IsValid(GetWorld()))
	{
		StopPhysicalReaction();
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const float NormalBlendStart =
		LastPhysicalImpactTimeSeconds + Tuning->PhysicalHoldSeconds;
	const float ForcedBlendStart =
		PhysicalSessionStartTimeSeconds + Tuning->MaxContinuousPhysicalSeconds;
	const float BlendStart = FMath::Min(NormalBlendStart, ForcedBlendStart);
	if (Now < BlendStart)
	{
		return;
	}

	const float Alpha = 1.0f - FMath::Clamp(
		(Now - BlendStart) / Tuning->PhysicalBlendOutSeconds,
		0.0f,
		1.0f);
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(
		Tuning->UpperBodyRootBone,
		Alpha,
		false,
		true);

	if (Alpha <= UE_KINDA_SMALL_NUMBER)
	{
		StopPhysicalReaction();
	}
}

void UCharacterImpactResponseComponent::HandleHeavyPreContactCaptureRequested(
	const FHeavyImpactPreparationRequest& /*Request*/)
{
	StopPhysicalReaction();
}

void UCharacterImpactResponseComponent::HandleHeavyImpactStateChanged(
	const EHeavyImpactState /*Previous*/,
	const EHeavyImpactState Current)
{
	if (Current == EHeavyImpactState::Prepared)
	{
		StopPhysicalReaction();
		ClearActiveImpact(true, true);
	}
}

bool UCharacterImpactResponseComponent::ResolvePhysicalHit(
	const FStandingImpactRequest& Request,
	FName& OutImpulseBody,
	FVector& OutWorldPoint) const
{
	OutImpulseBody = NAME_None;
	OutWorldPoint = FVector::ZeroVector;
	if (!IsValid(Mesh) || !IsValid(Tuning))
	{
		return false;
	}

	const FName ClosestBone = Mesh->FindClosestBone(
		Request.ImpactPoint,
		nullptr,
		0.0f,
		true);
	if (ClosestBone.IsNone())
	{
		return false;
	}

	const auto IsBoneOrChild = [this, ClosestBone](const FName ParentBone)
	{
		return ClosestBone == ParentBone || Mesh->BoneIsChildOf(ClosestBone, ParentBone);
	};
	if (IsBoneOrChild(Tuning->HeadImpulseBone))
	{
		OutImpulseBody = Tuning->HeadImpulseBone;
	}
	else if (IsBoneOrChild(Tuning->LeftArmImpulseBone))
	{
		OutImpulseBody = Tuning->LeftArmImpulseBone;
	}
	else if (IsBoneOrChild(Tuning->RightArmImpulseBone))
	{
		OutImpulseBody = Tuning->RightArmImpulseBone;
	}
	else if (IsBoneOrChild(Tuning->UpperBodyRootBone))
	{
		OutImpulseBody = Tuning->TorsoImpulseBone;
	}
	else
	{
		return false;
	}

	const FVector BodyCenter = Mesh->GetBoneLocation(OutImpulseBody);
	if (BodyCenter.ContainsNaN())
	{
		return false;
	}

	const FVector RequestedOffset = Request.ImpactPoint - BodyCenter;
	OutWorldPoint = BodyCenter + RequestedOffset.GetClampedToMaxSize(
		Tuning->MaximumPhysicalImpulseLeverArm);
	return !OutWorldPoint.ContainsNaN();
}

void UCharacterImpactResponseComponent::TryApplyPhysicalReaction(
	const FStandingImpactRequest& Request)
{
	UWorld* World = GetWorld();
	if (!bPhysicalReactionReady
		|| GFrameCounter <= PhysicalConfigurationFrame
		|| !IsValid(World)
		|| !IsValid(Mesh)
		|| !IsValid(Movement)
		|| !IsValid(PhysicalAnimation)
		|| !IsValid(Tuning)
		|| !IsValid(HeavyImpact)
		|| HeavyImpact->IsBusy()
		|| (!Movement->IsMovingOnGround() && !Movement->IsFalling()))
	{
		return;
	}

	const FVector Direction = Request.WorldDirection.GetSafeNormal();
	const float Magnitude = Tuning->PhysicalImpulseAtFullStrength
		* FMath::Clamp(Request.NormalizedStrength, 0.0f, 1.0f);
	if (Direction.IsNearlyZero() || !FMath::IsFinite(Magnitude) || Magnitude <= 0.0f)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (bPhysicalReactionActive
		&& Now - PhysicalSessionStartTimeSeconds >= Tuning->MaxContinuousPhysicalSeconds)
	{
		return;
	}

	FName ImpulseBody = NAME_None;
	FVector AppliedPoint = FVector::ZeroVector;
	if (!ResolvePhysicalHit(Request, ImpulseBody, AppliedPoint))
	{
		return;
	}

	if (!bPhysicalReactionActive)
	{
		PhysicalBaselineMeshCollisionProfileName = Mesh->GetCollisionProfileName();
		PhysicalBaselineMeshCollisionEnabled = Mesh->GetCollisionEnabled();
		PhysicalBaselineMeshCollisionResponses = Mesh->GetCollisionResponseToChannels();
		bPhysicalMeshCollisionOverridden = false;
		bPhysicalMeshPhysicsBodyResponseOverridden = false;
		if (PhysicalBaselineMeshCollisionResponses.GetResponse(ECC_PhysicsBody) != ECR_Ignore)
		{
			// Light 的可见反馈由受限 AddImpulse 驱动；普通 PhysicsBody 不应在同一窗口再次挤压表现 Mesh。
			Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
			bPhysicalMeshPhysicsBodyResponseOverridden =
				Mesh->GetCollisionResponseToChannel(ECC_PhysicsBody) == ECR_Ignore;
		}
		if (PhysicalBaselineMeshCollisionEnabled == ECollisionEnabled::QueryOnly)
		{
			// SkeletalMesh 的 BodyInstance 使用组件级 CollisionEnabled；仅开启模拟还不能接收冲量。
			// QueryAndPhysics 保留既有命中查询，而 AttackProjectileBody 仍由角色基线响应显式 Ignore，
			// 因此不会把磁力物额外路由到 Mesh 产生第二次实体碰撞。
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			bPhysicalMeshCollisionOverridden =
				Mesh->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics;
		}

		if (!CollisionEnabledHasPhysics(Mesh->GetCollisionEnabled()))
		{
			RestorePhysicalMeshCollisionBaseline();
			return;
		}

		PhysicalSessionStartTimeSeconds = Now;
		PhysicalAnimation->SetStrengthMultiplyer(1.0f);
		Mesh->SetAllBodiesBelowSimulatePhysics(Tuning->UpperBodyRootBone, true, true);
		Mesh->SetAllBodiesBelowPhysicsBlendWeight(
			Tuning->UpperBodyRootBone,
			1.0f,
			false,
			true);
		bPhysicalReactionActive = true;
		SetComponentTickEnabled(true);
	}
	else
	{
		Mesh->SetAllBodiesBelowPhysicsBlendWeight(
			Tuning->UpperBodyRootBone,
			1.0f,
			false,
			true);
	}

	LastPhysicalImpactTimeSeconds = Now;
	Mesh->AddImpulseAtLocation(Direction * Magnitude, AppliedPoint, ImpulseBody);
	UE_LOG(LogCharacterImpact, Log,
		TEXT("%s applied Light physical reaction: Body=%s Strength=%.2f Impulse=%.1f Point=%s."),
		*GetNameSafe(GetOwner()),
		*ImpulseBody.ToString(),
		FMath::Clamp(Request.NormalizedStrength, 0.0f, 1.0f),
		Magnitude,
		*AppliedPoint.ToCompactString());
}

void UCharacterImpactResponseComponent::RestorePhysicalMeshCollisionBaseline()
{
	if (IsValid(Mesh)
		&& (bPhysicalMeshCollisionOverridden || bPhysicalMeshPhysicsBodyResponseOverridden))
	{
		const ECollisionEnabled::Type ExpectedCollisionEnabled =
			bPhysicalMeshCollisionOverridden
				? ECollisionEnabled::QueryAndPhysics
				: PhysicalBaselineMeshCollisionEnabled;
		FCollisionResponseContainer ExpectedCollisionResponses =
			PhysicalBaselineMeshCollisionResponses;
		if (bPhysicalMeshPhysicsBodyResponseOverridden)
		{
			ExpectedCollisionResponses.SetResponse(ECC_PhysicsBody, ECR_Ignore);
		}

		if (Mesh->GetCollisionEnabled() == ExpectedCollisionEnabled
			&& Mesh->GetCollisionResponseToChannels() == ExpectedCollisionResponses)
		{
			Mesh->SetCollisionResponseToChannels(PhysicalBaselineMeshCollisionResponses);
			Mesh->SetCollisionEnabled(PhysicalBaselineMeshCollisionEnabled);
			if (!PhysicalBaselineMeshCollisionProfileName.IsNone()
				&& PhysicalBaselineMeshCollisionProfileName != UCollisionProfile::CustomCollisionProfileName)
			{
				// Response/Enabled setters mark the BodyInstance as Custom; restore the registered baseline last.
				Mesh->SetCollisionProfileName(PhysicalBaselineMeshCollisionProfileName);
			}
		}
		else
		{
			UE_LOG(LogCharacterImpact, Warning,
				TEXT("%s did not restore Mesh collision baseline because another system changed it during Light."),
				*GetNameSafe(GetOwner()));
		}
	}

	PhysicalBaselineMeshCollisionProfileName = NAME_None;
	PhysicalBaselineMeshCollisionEnabled = ECollisionEnabled::NoCollision;
	PhysicalBaselineMeshCollisionResponses = FCollisionResponseContainer::GetDefaultResponseContainer();
	bPhysicalMeshCollisionOverridden = false;
	bPhysicalMeshPhysicsBodyResponseOverridden = false;
}

void UCharacterImpactResponseComponent::StopPhysicalReaction()
{
	if (IsValid(PhysicalAnimation))
	{
		PhysicalAnimation->SetStrengthMultiplyer(0.0f);
	}

	// Prepared 状态回调会再次调用本函数；只有 Light 真正拥有模拟时才可改 Body，
	// 否则会把 Heavy 刚启用的全身模拟错误切回 Kinematic。
	if (bPhysicalReactionActive
		&& IsValid(Mesh)
		&& IsValid(Tuning)
		&& !Tuning->UpperBodyRootBone.IsNone())
	{
		Mesh->SetAllBodiesBelowPhysicsBlendWeight(
			Tuning->UpperBodyRootBone,
			0.0f,
			false,
			true);
		Mesh->SetAllBodiesBelowSimulatePhysics(
			Tuning->UpperBodyRootBone,
			false,
			true);
	}
	RestorePhysicalMeshCollisionBaseline();

	PhysicalSessionStartTimeSeconds = 0.0f;
	LastPhysicalImpactTimeSeconds = 0.0f;
	bPhysicalReactionActive = false;
	SetComponentTickEnabled(false);
}

void UCharacterImpactResponseComponent::ReleasePhysicalAnimationConfiguration()
{
	bPhysicalReactionReady = false;
	PhysicalConfigurationFrame = 0;
	if (!IsValid(PhysicalAnimation))
	{
		return;
	}

	PhysicalAnimation->SetStrengthMultiplyer(0.0f);
	PhysicalAnimation->SetSkeletalMeshComponent(nullptr);
	PhysicalAnimation->SetComponentTickEnabled(false);
	PhysicalAnimation->Deactivate();
}

void UCharacterImpactResponseComponent::FinishActiveImpact()
{
	ClearActiveImpact(true, false);
}

bool UCharacterImpactResponseComponent::ApplySlowMovement(
	const float SpeedMultiplier,
	const float /*Strength*/,
	const float BaselineSpeed)
{
	if ((!Movement->IsMovingOnGround() && !Movement->IsFalling())
		|| !FMath::IsFinite(BaselineSpeed)
		|| BaselineSpeed < 0.0f
		|| !FMath::IsFinite(SpeedMultiplier)
		|| SpeedMultiplier <= 0.0f
		|| SpeedMultiplier >= 1.0f)
	{
		return false;
	}

	LastWrittenMaxWalkSpeed = BaselineSpeed * SpeedMultiplier;
	Movement->MaxWalkSpeed = LastWrittenMaxWalkSpeed;
	return true;
}

bool UCharacterImpactResponseComponent::ApplyStopMovement(const float BaselineSpeed)
{
	if (!FMath::IsFinite(BaselineSpeed) || BaselineSpeed < 0.0f)
	{
		return false;
	}

	const bool bWasFalling = Movement->IsFalling();
	if (!bWasFalling && !Movement->IsMovingOnGround())
	{
		return false;
	}

	const float PreservedVerticalSpeed = Movement->Velocity.Z;
	Movement->StopMovementImmediately();
	if (bWasFalling && Movement->IsFalling())
	{
		Movement->Velocity.Z = PreservedVerticalSpeed;
		Movement->UpdateComponentVelocity();
	}

	LastWrittenMaxWalkSpeed = 0.0f;
	Movement->MaxWalkSpeed = LastWrittenMaxWalkSpeed;
	return true;
}

void UCharacterImpactResponseComponent::RestoreWalkSpeedIfUncontested()
{
	if (!IsLightActive() || !IsValid(Movement))
	{
		return;
	}

	if (FMath::IsNearlyEqual(Movement->MaxWalkSpeed, LastWrittenMaxWalkSpeed))
	{
		Movement->MaxWalkSpeed = BaselineMaxWalkSpeed;
	}
	else
	{
		UE_LOG(LogCharacterImpact, Warning,
			TEXT("%s did not restore MaxWalkSpeed because another system changed it during Light."),
			*GetNameSafe(GetOwner()));
	}
}

void UCharacterImpactResponseComponent::ClearActiveImpact(
	const bool bRestoreWalkSpeed,
	const bool bImmediateMontageStop)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EndTimerHandle);
	}
	if (bRestoreWalkSpeed)
	{
		RestoreWalkSpeedIfUncontested();
	}
	StopReactionAnimation(bImmediateMontageStop);

	ActiveResult = EStandingImpactResult::None;
	ActiveStrength = 0.0f;
	ActiveImpactId.Invalidate();
	ActiveSourceActor.Reset();
	BaselineMaxWalkSpeed = 0.0f;
	LastWrittenMaxWalkSpeed = 0.0f;
	LightWindowStartTimeSeconds = 0.0f;
	ActiveEndTimeSeconds = 0.0f;
}

void UCharacterImpactResponseComponent::ScheduleEndTimer(const float EndTimeSeconds)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	const float Remaining = FMath::Max(EndTimeSeconds - World->GetTimeSeconds(), UE_SMALL_NUMBER);
	World->GetTimerManager().SetTimer(
		EndTimerHandle, this, &UCharacterImpactResponseComponent::FinishActiveImpact, Remaining, false);
}

void UCharacterImpactResponseComponent::RecordRecentImpactId(const FGuid& ImpactId)
{
	RecentImpactIds.Add(ImpactId);
	const int32 Overflow = RecentImpactIds.Num() - Demo::CharacterImpact::RecentImpactHistorySize;
	if (Overflow > 0)
	{
		RecentImpactIds.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

bool UCharacterImpactResponseComponent::HasSeenImpactId(const FGuid& ImpactId) const
{
	return RecentImpactIds.Contains(ImpactId);
}

UAnimSequenceBase* UCharacterImpactResponseComponent::SelectReactionAnimation(
	const FVector& WorldDirection) const
{
	if (!IsValid(Character) || !IsValid(Tuning))
	{
		return nullptr;
	}
	const FVector LocalDirection = Character->GetActorTransform()
		.InverseTransformVectorNoScale(WorldDirection.GetSafeNormal());
	if (FMath::Abs(LocalDirection.Y) > FMath::Abs(LocalDirection.X))
	{
		return LocalDirection.Y >= 0.0f ? Tuning->LeftReaction : Tuning->RightReaction;
	}
	return Tuning->FrontReaction;
}

void UCharacterImpactResponseComponent::PlayReactionAnimation(const FVector& WorldDirection)
{
	UAnimSequenceBase* Animation = SelectReactionAnimation(WorldDirection);
	UAnimInstance* AnimInstance = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (!IsValid(Animation) || !IsValid(AnimInstance))
	{
		return;
	}

	StopReactionAnimation(true);
	ActiveLightMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
		Animation,
		Demo::CharacterImpact::DefaultSlot,
		Tuning->MontageBlendInSeconds,
		Tuning->MontageBlendOutSeconds,
		Tuning->MontagePlayRate,
		1,
		-1.0f,
		0.0f);
	if (IsValid(ActiveLightMontage))
	{
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(
			this, &UCharacterImpactResponseComponent::HandleReactionMontageEnded);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, ActiveLightMontage);
	}
}

void UCharacterImpactResponseComponent::StopReactionAnimation(const bool bImmediate)
{
	UAnimInstance* AnimInstance = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (IsValid(AnimInstance) && IsValid(ActiveLightMontage))
	{
		AnimInstance->Montage_Stop(
			bImmediate ? 0.0f : Tuning->MontageBlendOutSeconds,
			ActiveLightMontage);
	}
	ActiveLightMontage = nullptr;
}

void UCharacterImpactResponseComponent::HandleReactionMontageEnded(
	UAnimMontage* Montage,
	bool /*bInterrupted*/)
{
	if (Montage == ActiveLightMontage)
	{
		ActiveLightMontage = nullptr;
	}
}
