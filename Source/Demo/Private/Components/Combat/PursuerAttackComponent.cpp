// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAttackComponent.cpp
 * 职责：实现近战 Sweep、一次性预判跑跳、抛物线位移、真实 Heavy 攻击刚体、伤害、击飞与命中后喘息。
 * 边界：不持续修正空中方向、不伪造 Heavy 接触、不依赖斧头组件名或动画长度决定玩法状态。
 */

#include "Components/Combat/PursuerAttackComponent.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Characters/PursuerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/Physics/HeavyImpactResponseComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Data/PursuerConfig.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interfaces/HeavyImpactReceiver.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPursuerAttack, Log, All);

/** 创建无 Tick 的攻击组件；所有更新来自 Timer 与落地 Delegate。 */
UPursuerAttackComponent::UPursuerAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/** 解除旧角色绑定后注入新角色与配置；配置完整性仍由角色统一校验。 */
void UPursuerAttackComponent::Configure(
	APursuerCharacter* InCharacter,
	const UPursuerConfig* InConfig)
{
	if (Character.IsValid())
	{
		Character->LandedDelegate.RemoveDynamic(this, &UPursuerAttackComponent::HandleCharacterLanded);
	}

	CancelAttack();
	Character = InCharacter;
	Config = InConfig;
	AttackImpactBody = IsValid(InCharacter) ? InCharacter->GetAttackImpactBody() : nullptr;

	if (!Character.IsValid() || !Config.IsValid() || !AttackImpactBody.IsValid())
	{
		UE_LOG(LogPursuerAttack, Error,
			TEXT("PursuerAttack Configure 缺少有效角色、Config 或 AttackImpactBody，攻击保持禁用。"));
		return;
	}

	Character->LandedDelegate.AddUniqueDynamic(this, &UPursuerAttackComponent::HandleCharacterLanded);
}

/** 先验证地面/冷却/受击，再播放完整近战 Montage 并创建命中 Timer。 */
bool UPursuerAttackComponent::TryStartCloseSwing(APawn* Target)
{
	if (!CanStartAgainst(Target)
		|| !PlayAttackMontage(Config->AttackMontage, Config->CloseAttackPlayRate))
	{
		return false;
	}

	BeginAttack(EPursuerAttackPhase::CloseSwing, Target, Target->GetActorLocation());
	GetWorld()->GetTimerManager().SetTimer(
		ActionTimerHandle,
		this,
		&UPursuerAttackComponent::ExecuteCloseHit,
		Config->CloseAttackHitDelay,
		false);
	return true;
}

/** 播放全身 Montage 并进入助跑；真正离地时才按玩家当时速度锁定一次落点。 */
bool UPursuerAttackComponent::TryStartJumpSmash(APawn* Target)
{
	if (!CanStartAgainst(Target)
		|| !PlayAttackMontage(Config->JumpAttackMontage, Config->JumpAttackPlayRate))
	{
		return false;
	}

	LockedImpactPoint = FVector::ZeroVector;
	BeginAttack(EPursuerAttackPhase::JumpWindup, Target, Target->GetActorLocation());
	if (IsValid(Config->JumpAttackStartSound))
	{
		// Sweep_02 没有自带衰减包；用起跳时与目标的二维距离做简单的近响远弱。
		const float DistanceToTarget = FVector::Dist2D(
			Character->GetActorLocation(), Target->GetActorLocation());
		const float DistanceAlpha = FMath::Clamp(
			DistanceToTarget / FMath::Max(1.0f, Config->JumpAttackMaxRange),
			0.0f,
			1.0f);
		const float DistanceVolumeScale = FMath::Lerp(1.0f, 0.25f, DistanceAlpha);
		UGameplayStatics::PlaySoundAtLocation(
			this,
			Config->JumpAttackStartSound.Get(),
			Character->GetActorLocation(),
			Config->JumpAttackStartVolume * DistanceVolumeScale);
	}

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.SetTimer(
		ActionTimerHandle,
		this,
		&UPursuerAttackComponent::LaunchJump,
		Config->JumpAttackLaunchDelay,
		false);
	TimerManager.SetTimer(
		TimeoutTimerHandle,
		this,
		&UPursuerAttackComponent::HandleAttackTimeout,
		Config->JumpAttackLaunchDelay
			+ Config->JumpAttackFlightSeconds
			+ Config->JumpAttackRecoverySeconds
			+ 1.0f,
		false);
	return true;
}

/** Idle、冷却结束、配置有效、未受击且角色站在地面时才允许新攻击。 */
bool UPursuerAttackComponent::CanStartAttack() const
{
	if (IsBusy() || !Character.IsValid() || !Config.IsValid() || !IsValid(GetWorld()))
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	return IsValid(Movement)
		&& Movement->IsMovingOnGround()
		&& !Character->IsImpactAttackSuppressed()
		&& GetWorld()->GetTimeSeconds() >= NextAttackAllowedWorldTime;
}

/** 清掉组件独占的事务状态和 Timer；只停止 ActiveMontage，不触碰其他系统 Montage。 */
void UPursuerAttackComponent::CancelAttack(const float BlendOutSeconds)
{
	ClearAttackTimers();
	if (Character.IsValid() && IsValid(ActiveMontage))
	{
		if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(FMath::Max(0.0f, BlendOutSeconds), ActiveMontage);
		}
	}

	ActiveMontage = nullptr;
	UnbindTargetHeavyImpact();
	DeactivateAttackImpactBody();
	ActiveTarget.Reset();
	LockedImpactPoint = FVector::ZeroVector;
	ActiveImpactId.Invalidate();
	PendingDamage = 0.0f;
	PendingKnockbackVelocity = FVector::ZeroVector;
	PendingMissRecoverySeconds = 0.0f;
	bHeavyCommitQueued = false;
	Phase = EPursuerAttackPhase::Idle;
}

/** 预测只使用水平速度，随后按最大可达水平距离截断，避免高速目标产生不可达落点。 */
FVector UPursuerAttackComponent::ComputePredictedTargetPoint(
	const FVector& Origin,
	const FVector& TargetLocation,
	const FVector& TargetVelocity,
	const float LeadSeconds,
	const float MaximumHorizontalDistance)
{
	const float SafeLeadSeconds = FMath::Max(0.0f, LeadSeconds);
	FVector HorizontalVelocity(TargetVelocity.X, TargetVelocity.Y, 0.0f);
	FVector Predicted = TargetLocation + HorizontalVelocity * SafeLeadSeconds;

	FVector HorizontalDelta = Predicted - Origin;
	HorizontalDelta.Z = 0.0f;
	const float SafeMaximumDistance = FMath::Max(0.0f, MaximumHorizontalDistance);
	if (SafeMaximumDistance > 0.0f && HorizontalDelta.SizeSquared() > FMath::Square(SafeMaximumDistance))
	{
		HorizontalDelta = HorizontalDelta.GetSafeNormal() * SafeMaximumDistance;
		Predicted.X = Origin.X + HorizontalDelta.X;
		Predicted.Y = Origin.Y + HorizontalDelta.Y;
	}
	Predicted.Z = TargetLocation.Z;
	return Predicted;
}

/** 用固定时间抛体公式求初速度；不使用 SuggestProjectileVelocity 的多解随机性。 */
bool UPursuerAttackComponent::CalculateBallisticLaunchVelocity(
	const FVector& Start,
	const FVector& Target,
	const float FlightSeconds,
	const float GravityZ,
	FVector& OutVelocity)
{
	OutVelocity = FVector::ZeroVector;
	if (!FMath::IsFinite(FlightSeconds)
		|| FlightSeconds <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(GravityZ)
		|| Start.ContainsNaN()
		|| Target.ContainsNaN())
	{
		return false;
	}

	const FVector Delta = Target - Start;
	OutVelocity = Delta / FlightSeconds;
	OutVelocity.Z = (Delta.Z - 0.5f * GravityZ * FMath::Square(FlightSeconds)) / FlightSeconds;
	return !OutVelocity.ContainsNaN();
}

/** 方向只取水平面；目标与来源重合时退回角色前向，避免产生零向量击飞。 */
FVector UPursuerAttackComponent::ComputeKnockbackVelocity(
	const FVector& SourceLocation,
	const FVector& TargetLocation,
	const FVector& SourceForward,
	const float HorizontalVelocity,
	const float UpwardVelocity)
{
	FVector HorizontalDirection = TargetLocation - SourceLocation;
	HorizontalDirection.Z = 0.0f;
	if (!HorizontalDirection.Normalize())
	{
		HorizontalDirection = SourceForward;
		HorizontalDirection.Z = 0.0f;
		HorizontalDirection = HorizontalDirection.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
	}

	return HorizontalDirection * FMath::Max(0.0f, HorizontalVelocity)
		+ FVector::UpVector * FMath::Max(0.0f, UpwardVelocity);
}

/** 保留约 45 cm 的目标身体余量，再叠加球半径和速度乘 ETA，避免同帧提前接触。 */
FVector UPursuerAttackComponent::ComputeImpactBodyStart(
	const FVector& TargetPoint,
	const FVector& AttackDirection,
	const float BodyRadius,
	const float BodySpeed,
	const float ContactEtaSeconds)
{
	const FVector SafeDirection = AttackDirection.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
	const float StartDistance = FMath::Max(0.0f, BodyRadius)
		+ 45.0f
		+ FMath::Max(0.0f, BodySpeed) * FMath::Max(0.0f, ContactEtaSeconds);
	return TargetPoint - SafeDirection * StartDistance;
}

/** 世界销毁前解除动态委托，并停止组件自己的 Montage/Timer。 */
void UPursuerAttackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Character.IsValid())
	{
		Character->LandedDelegate.RemoveDynamic(this, &UPursuerAttackComponent::HandleCharacterLanded);
	}
	CancelAttack(0.0f);
	Super::EndPlay(EndPlayReason);
}

/** 目标必须有效、不是自己，且共享攻击前置条件通过。 */
bool UPursuerAttackComponent::CanStartAgainst(const APawn* Target) const
{
	return CanStartAttack()
		&& IsValid(Target)
		&& Target != Character.Get();
}

/** 资源缺失或播放失败时不建立玩法状态，避免无动画的隐形攻击。 */
bool UPursuerAttackComponent::PlayAttackMontage(
	const TSoftObjectPtr<UAnimMontage>& MontageAsset,
	const float PlayRate)
{
	if (!Character.IsValid() || MontageAsset.IsNull())
	{
		return false;
	}

	UAnimMontage* Montage = MontageAsset.LoadSynchronous();
	if (!IsValid(Montage))
	{
		UE_LOG(LogPursuerAttack, Error, TEXT("%s 无法加载攻击 Montage %s。"),
			*GetNameSafe(Character.Get()), *MontageAsset.ToString());
		return false;
	}

	if (Character->PlayAnimMontage(Montage, FMath::Max(0.01f, PlayRate)) <= 0.0f)
	{
		UE_LOG(LogPursuerAttack, Warning, TEXT("%s 攻击 Montage %s 播放失败。"),
			*GetNameSafe(Character.Get()), *GetNameSafe(Montage));
		return false;
	}

	ActiveMontage = Montage;
	return true;
}

/** 写入一次攻击的共同状态；冷却从起手开始，冷却期间组件空闲后 AI 可继续追击。 */
void UPursuerAttackComponent::BeginAttack(
	const EPursuerAttackPhase NewPhase,
	APawn* Target,
	const FVector& FacingPoint)
{
	Phase = NewPhase;
	ActiveTarget = Target;
	ActiveImpactId = FGuid::NewGuid();
	NextAttackAllowedWorldTime = GetWorld()->GetTimeSeconds() + Config->AttackCooldown;
	FacePoint(FacingPoint);

	// 近战必须立即停步；跑跳攻击的前段是可见助跑，保留既有 MoveToActor，
	// 直到真正离地时才停止 PathFollowing 并交给 CharacterMovement 抛物线。
	if (NewPhase == EPursuerAttackPhase::CloseSwing)
	{
		if (AAIController* Controller = Cast<AAIController>(Character->GetController()))
		{
			Controller->StopMovement();
		}
	}
}

/** 只改水平 Yaw，让动画和 Launch 初始方向一致。 */
void UPursuerAttackComponent::FacePoint(const FVector& WorldPoint) const
{
	if (!Character.IsValid())
	{
		return;
	}

	FVector Direction = WorldPoint - Character->GetActorLocation();
	Direction.Z = 0.0f;
	if (!Direction.IsNearlyZero())
	{
		Character->SetActorRotation(Direction.Rotation());
	}
}

/** 只在 CloseSwing 阶段结算；查询失败即视为可读的落空而非自动吸附命中。 */
void UPursuerAttackComponent::ExecuteCloseHit()
{
	if (Phase != EPursuerAttackPhase::CloseSwing || !Character.IsValid() || !Config.IsValid())
	{
		return;
	}
	if (Character->IsImpactAttackSuppressed())
	{
		CancelAttack();
		return;
	}

	FVector ImpactOrigin = FVector::ZeroVector;
	if (ActiveTarget.IsValid()
		&& IsActiveTargetInCloseSweep(ImpactOrigin)
		&& HasClearLineToTarget(ImpactOrigin, ActiveTarget.Get()))
	{
		if (ArmHeavyStrike(
			ActiveTarget.Get(),
			ImpactOrigin,
			Config->CloseAttackDamage,
			Config->CloseHeavyImpactBodyRadius,
			Config->CloseKnockbackHorizontalVelocity,
			Config->CloseKnockbackUpwardVelocity,
			Config->CloseAttackRecoverySeconds))
		{
			return;
		}
	}
	BeginRecovery(Config->CloseAttackRecoverySeconds);
}

/** 固定时间抛体起跳；失败时取消事务，不允许在地面以插值方式补位。 */
void UPursuerAttackComponent::LaunchJump()
{
	if (Phase != EPursuerAttackPhase::JumpWindup || !Character.IsValid() || !Config.IsValid())
	{
		return;
	}
	if (Character->IsImpactAttackSuppressed())
	{
		CancelAttack();
		return;
	}
	if (!ActiveTarget.IsValid())
	{
		CancelAttack();
		return;
	}

	// 锁定发生在脚真正离地时，而不是起手时。这样前段助跑仍可追近玩家，
	// 同时落点只计算一次，空中不会变成不可躲避的持续追踪。
	LockedImpactPoint = ComputePredictedTargetPoint(
		Character->GetActorLocation(),
		ActiveTarget->GetActorLocation(),
		ActiveTarget->GetVelocity(),
		Config->JumpAttackLeadSeconds,
		Config->JumpAttackMaxRange);

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	FVector LaunchVelocity = FVector::ZeroVector;
	if (!IsValid(Movement)
		|| !Movement->IsMovingOnGround()
		|| !CalculateBallisticLaunchVelocity(
			Character->GetActorLocation(),
			LockedImpactPoint,
			Config->JumpAttackFlightSeconds,
			Movement->GetGravityZ(),
			LaunchVelocity))
	{
		UE_LOG(LogPursuerAttack, Warning, TEXT("%s 跑跳攻击无法计算合法起跳速度，已取消。"),
			*GetNameSafe(Character.Get()));
		CancelAttack();
		return;
	}

	FacePoint(LockedImpactPoint);
	if (AAIController* Controller = Cast<AAIController>(Character->GetController()))
	{
		Controller->StopMovement();
	}
	Phase = EPursuerAttackPhase::JumpAirborne;
	Character->LaunchCharacter(LaunchVelocity, true, true);
}

/** 只有已离地的跑跳事务才在真实落地时结算范围攻击。 */
void UPursuerAttackComponent::HandleCharacterLanded(const FHitResult& Hit)
{
	if (Phase != EPursuerAttackPhase::JumpAirborne || !Character.IsValid() || !Config.IsValid())
	{
		return;
	}

	// 落地瞬间把抛物线剩余的水平速度清掉，让下砸动作真正“钉”在落点上，
	// 避免进入恢复段后角色继续沿地面滑行。
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	const FVector ImpactOrigin = Character->GetActorLocation();
	if (ActiveTarget.IsValid()
		&& IsActiveTargetInLandingRadius(ImpactOrigin)
		&& HasClearLineToTarget(ImpactOrigin, ActiveTarget.Get()))
	{
		if (ArmHeavyStrike(
			ActiveTarget.Get(),
			ImpactOrigin,
			Config->JumpAttackDamage,
			Config->JumpHeavyImpactBodyRadius,
			Config->JumpKnockbackHorizontalVelocity,
			Config->JumpKnockbackUpwardVelocity,
			Config->JumpAttackRecoverySeconds))
		{
			return;
		}
	}
	BeginRecovery(Config->JumpAttackRecoverySeconds);
}

/** Sweep 只查询 Pawn 对象并精确匹配起手目标；不依赖 AxeMesh 的蓝图组件名。 */
bool UPursuerAttackComponent::IsActiveTargetInCloseSweep(FVector& OutImpactOrigin) const
{
	OutImpactOrigin = FVector::ZeroVector;
	if (!Character.IsValid() || !Config.IsValid() || !ActiveTarget.IsValid() || !IsValid(GetWorld()))
	{
		return false;
	}

	const FVector Start = Character->GetActorLocation() + FVector::UpVector * 70.0f;
	const FVector End = Start + Character->GetActorForwardVector() * Config->CloseAttackReach;
	OutImpactOrigin = End;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PursuerCloseAttack), false, Character.Get());
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByObjectType(
		Hits,
		Start,
		End,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(Config->CloseAttackSweepRadius),
		QueryParams);

	for (const FHitResult& Hit : Hits)
	{
		if (Hit.GetActor() == ActiveTarget.Get())
		{
			OutImpactOrigin = Hit.ImpactPoint.IsNearlyZero() ? End : Hit.ImpactPoint;
			return true;
		}
	}
	return false;
}

/** 落地查询同样只接收 Pawn，并要求命中起手目标，避免伤到无关 AI。 */
bool UPursuerAttackComponent::IsActiveTargetInLandingRadius(const FVector& ImpactOrigin) const
{
	if (!Config.IsValid() || !ActiveTarget.IsValid() || !IsValid(GetWorld()))
	{
		return false;
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PursuerJumpImpact), false, Character.Get());
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		ImpactOrigin,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(Config->JumpAttackImpactRadius),
		QueryParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Overlap.GetActor() == ActiveTarget.Get())
		{
			return true;
		}
	}
	return false;
}

/** Visibility 首个阻挡是目标或没有阻挡才允许命中，世界几何会挡住范围伤害。 */
bool UPursuerAttackComponent::HasClearLineToTarget(
	const FVector& ImpactOrigin,
	const APawn* Target) const
{
	if (!Character.IsValid() || !IsValid(Target) || !IsValid(GetWorld()))
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PursuerAttackVisibility), false, Character.Get());
	FHitResult Hit;
	const FVector Start = ImpactOrigin + FVector::UpVector * 45.0f;
	const FVector End = Target->GetActorLocation() + FVector::UpVector * 45.0f;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
		Hit, Start, End, ECC_Visibility, QueryParams);
	return !bBlocked || Hit.GetActor() == Target;
}

/** 宽容查询只授予准备资格；伤害仍等待同一隐藏刚体的真实 Chaos 接触提交。 */
bool UPursuerAttackComponent::ArmHeavyStrike(
	APawn* Target,
	const FVector& ImpactOrigin,
	const float Damage,
	const float BodyRadius,
	const float HorizontalVelocity,
	const float UpwardVelocity,
	const float MissRecoverySeconds)
{
	if (!Character.IsValid()
		|| !Config.IsValid()
		|| !AttackImpactBody.IsValid()
		|| !IsValid(Target)
		|| !ActiveImpactId.IsValid()
		|| !Target->GetClass()->ImplementsInterface(UHeavyImpactReceiver::StaticClass())
		|| !IsValid(GetWorld()))
	{
		UE_LOG(LogPursuerAttack, Warning,
			TEXT("%s 无法为 %s 建立 Heavy 攻击事务。"),
			*GetNameSafe(Character.Get()), *GetNameSafe(Target));
		return false;
	}

	UHeavyImpactResponseComponent* HeavyImpact =
		Target->FindComponentByClass<UHeavyImpactResponseComponent>();
	UPrimitiveComponent* TargetPrimitive =
		IHeavyImpactReceiver::Execute_GetHeavyImpactPredictionPrimitive(Target);
	if (!IsValid(HeavyImpact) || !IsValid(TargetPrimitive))
	{
		UE_LOG(LogPursuerAttack, Warning,
			TEXT("%s 命中查询选中 %s，但目标缺少 HeavyImpactResponse 或预测 Primitive。"),
			*GetNameSafe(Character.Get()), *GetNameSafe(Target));
		return false;
	}

	FVector AttackDirection = TargetPrimitive->Bounds.Origin - ImpactOrigin;
	AttackDirection.Z = 0.0f;
	if (!AttackDirection.Normalize())
	{
		AttackDirection = Character->GetActorForwardVector().GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
	}

	const float FrameAwareLeadSeconds = GetWorld()->GetDeltaSeconds() > 0.0f
		? GetWorld()->GetDeltaSeconds() * 1.5f
		: 0.0f;
	const float ContactEtaSeconds = FMath::Clamp(
		FMath::Max(Config->HeavyImpactLeadSeconds, FrameAwareLeadSeconds),
		0.03f,
		0.45f);
	// Heavy 接收端在 Prepare 成功时会同步停掉 CharacterMovement；因此瞄准当前身体中心，
	// 不再把准备前的奔跑速度外推到一个目标已不会抵达的位置。
	const FVector PredictedTargetPoint = TargetPrimitive->Bounds.Origin;
	const FVector Start = ComputeImpactBodyStart(
		PredictedTargetPoint,
		AttackDirection,
		BodyRadius,
		Config->HeavyImpactBodySpeed,
		ContactEtaSeconds);

	USphereComponent* Body = AttackImpactBody.Get();
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Body->SetSphereRadius(BodyRadius, true);
	Body->SetWorldLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetMassOverrideInKg(NAME_None, Config->HeavyImpactBodyMassKg, true);
	Body->SetEnableGravity(false);
	Body->SetSimulatePhysics(true);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector, false, NAME_None);
	Body->WakeAllRigidBodies();

	FHeavyImpactPreparationRequest Request;
	Request.ImpactId = ActiveImpactId;
	Request.SourceActor = Character.Get();
	Request.SourceComponent = Body;
	Request.PredictedImpactPoint = PredictedTargetPoint;
	Request.SourceLinearVelocity = AttackDirection * Config->HeavyImpactBodySpeed;
	Request.EstimatedTimeToContactSeconds = ContactEtaSeconds;

	const EHeavyImpactPrepareResult PrepareResult =
		IHeavyImpactReceiver::Execute_PrepareForHeavyImpact(Target, Request);
	if (PrepareResult != EHeavyImpactPrepareResult::Accepted)
	{
		UE_LOG(LogPursuerAttack, Verbose,
			TEXT("%s Heavy attack prepare rejected by %s (Result=%d)."),
			*GetNameSafe(Character.Get()), *GetNameSafe(Target), static_cast<int32>(PrepareResult));
		DeactivateAttackImpactBody();
		return false;
	}

	TargetHeavyImpact = HeavyImpact;
	HeavyImpact->OnImpactCommitted.RemoveAll(this);
	HeavyImpact->OnStateChanged.RemoveAll(this);
	HeavyImpact->OnImpactCommitted.AddUObject(this, &UPursuerAttackComponent::HandleHeavyImpactCommitted);
	HeavyImpact->OnStateChanged.AddUObject(this, &UPursuerAttackComponent::HandleTargetHeavyStateChanged);
	PendingDamage = FMath::Max(0.0f, Damage);
	PendingKnockbackVelocity = ComputeKnockbackVelocity(
		Character->GetActorLocation(),
		Target->GetActorLocation(),
		Character->GetActorForwardVector(),
		HorizontalVelocity,
		UpwardVelocity);
	PendingMissRecoverySeconds = FMath::Max(0.01f, MissRecoverySeconds);
	bHeavyCommitQueued = false;
	Phase = EPursuerAttackPhase::ImpactPending;

	Body->SetPhysicsLinearVelocity(Request.SourceLinearVelocity, false, NAME_None);
	GetWorld()->GetTimerManager().ClearTimer(ActionTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(
		ActionTimerHandle,
		this,
		&UPursuerAttackComponent::HandleHeavyStrikeMiss,
		FMath::Min(0.5f, ContactEtaSeconds + 0.25f),
		false);
	return true;
}

/** 只接受当前事务与同一真实攻击刚体，避免目标其他 Heavy 来源触发追猎者伤害。 */
void UPursuerAttackComponent::HandleHeavyImpactCommitted(
	const FHeavyImpactPreparationRequest& Request)
{
	if (Phase != EPursuerAttackPhase::ImpactPending
		|| bHeavyCommitQueued
		|| Request.ImpactId != ActiveImpactId
		|| Request.SourceActor != Character.Get()
		|| Request.SourceComponent != AttackImpactBody.Get()
		|| !IsValid(GetWorld()))
	{
		return;
	}

	bHeavyCommitQueued = true;
	GetWorld()->GetTimerManager().ClearTimer(ActionTimerHandle);
	CommitFinalizeTimerHandle = GetWorld()->GetTimerManager().SetTimerForNextTick(
		this,
		&UPursuerAttackComponent::FinalizeCommittedHeavyHit);
}

/** 真实提交后的下一帧再关源刚体，确保接收端同一求解事件已完成状态转换。 */
void UPursuerAttackComponent::FinalizeCommittedHeavyHit()
{
	if (Phase != EPursuerAttackPhase::ImpactPending
		|| !bHeavyCommitQueued
		|| !Character.IsValid()
		|| !ActiveTarget.IsValid())
	{
		CancelAttack();
		return;
	}

	APawn* Target = ActiveTarget.Get();
	UPrimitiveComponent* TargetPrimitive =
		Target->GetClass()->ImplementsInterface(UHeavyImpactReceiver::StaticClass())
		? IHeavyImpactReceiver::Execute_GetHeavyImpactPredictionPrimitive(Target)
		: nullptr;
	if (IsValid(TargetPrimitive) && TargetPrimitive->IsAnySimulatingPhysics())
	{
		TargetPrimitive->AddImpulse(PendingKnockbackVelocity, NAME_None, true);
	}

	if (IsValid(Config->AttackHitSound))
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			Config->AttackHitSound.Get(),
			Target->GetActorLocation(),
			Config->AttackHitVolume);
	}

	UGameplayStatics::ApplyDamage(
		Target,
		PendingDamage,
		Character->GetController(),
		Character.Get(),
		UDamageType::StaticClass());

	DeactivateAttackImpactBody();
	BeginPostHitRespite();

	UE_LOG(LogPursuerAttack, Display,
		TEXT("%s Heavy attack committed on %s (Damage=%.1f, VelocityChange=%s, PostRecoveryCooldown=%.2fs)."),
		*GetNameSafe(Character.Get()),
		*GetNameSafe(Target),
		PendingDamage,
		*PendingKnockbackVelocity.ToCompactString(),
		Config->SuccessfulHitCooldownSeconds);
}

/** Prepared 超时恢复由目标 Heavy 自己处理；攻击端只关源刚体并走原落空恢复。 */
void UPursuerAttackComponent::HandleHeavyStrikeMiss()
{
	if (Phase != EPursuerAttackPhase::ImpactPending || bHeavyCommitQueued)
	{
		return;
	}

	const float RecoverySeconds = PendingMissRecoverySeconds;
	UnbindTargetHeavyImpact();
	DeactivateAttackImpactBody();
	BeginRecovery(RecoverySeconds);
}

/** 成功 Heavy 后攻击事务保持 Busy，使 AI 停住而不是立刻贴脸重打。 */
void UPursuerAttackComponent::BeginPostHitRespite()
{
	if (!Character.IsValid() || !Config.IsValid() || !IsValid(GetWorld()))
	{
		CancelAttack();
		return;
	}

	Phase = EPursuerAttackPhase::PostHitRespite;
	if (AAIController* Controller = Cast<AAIController>(Character->GetController()))
	{
		Controller->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	GetWorld()->GetTimerManager().SetTimer(
		TimeoutTimerHandle,
		this,
		&UPursuerAttackComponent::BeginPostHitGrace,
		Config->PostHitMaximumHoldSeconds,
		false);

	if (!TargetHeavyImpact.IsValid() || !TargetHeavyImpact->IsBusy())
	{
		BeginPostHitGrace();
	}
}

/** 只关心完整回到 Inactive；中间 Simulating/Settling/Downed/Recovering 均继续给玩家喘息。 */
void UPursuerAttackComponent::HandleTargetHeavyStateChanged(
	const EHeavyImpactState Previous,
	const EHeavyImpactState Current)
{
	(void)Previous;
	if (Phase == EPursuerAttackPhase::PostHitRespite && Current == EHeavyImpactState::Inactive)
	{
		BeginPostHitGrace();
	}
}

/** 起身完成或保险超时后统一进入可调额外停顿；该路径可重复调用。 */
void UPursuerAttackComponent::BeginPostHitGrace()
{
	if (Phase != EPursuerAttackPhase::PostHitRespite || !Config.IsValid() || !IsValid(GetWorld()))
	{
		return;
	}

	UnbindTargetHeavyImpact();
	GetWorld()->GetTimerManager().ClearTimer(TimeoutTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(RecoveryTimerHandle);
	NextAttackAllowedWorldTime = FMath::Max(
		NextAttackAllowedWorldTime,
		static_cast<double>(GetWorld()->GetTimeSeconds() + Config->SuccessfulHitCooldownSeconds));
	if (Config->PostHitRecoveryGraceSeconds <= KINDA_SMALL_NUMBER)
	{
		FinishPostHitRespite();
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		RecoveryTimerHandle,
		this,
		&UPursuerAttackComponent::FinishPostHitRespite,
		Config->PostHitRecoveryGraceSeconds,
		false);
}

/** 喘息结束复用正常攻击清理，成功冷却截止时间保持不变。 */
void UPursuerAttackComponent::FinishPostHitRespite()
{
	FinishAttack();
}

/** 关闭物理前先清碰撞，避免在回挂角色时制造第二次 Heavy 接触。 */
void UPursuerAttackComponent::DeactivateAttackImpactBody()
{
	if (!AttackImpactBody.IsValid())
	{
		return;
	}

	USphereComponent* Body = AttackImpactBody.Get();
	if (Body->IsAnySimulatingPhysics())
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector, false, NAME_None);
	}
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetEnableGravity(false);
	if (Character.IsValid() && IsValid(Character->GetRootComponent()))
	{
		Body->AttachToComponent(
			Character->GetRootComponent(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

/** 原生委托按订阅者精确移除，不影响玩家或磁力系统的其他监听。 */
void UPursuerAttackComponent::UnbindTargetHeavyImpact()
{
	if (TargetHeavyImpact.IsValid())
	{
		TargetHeavyImpact->OnImpactCommitted.RemoveAll(this);
		TargetHeavyImpact->OnStateChanged.RemoveAll(this);
	}
	TargetHeavyImpact.Reset();
}

/** 攻击结算后只等待恢复 Timer；冷却和追击是否可用由各自独立条件决定。 */
void UPursuerAttackComponent::BeginRecovery(const float RecoverySeconds)
{
	if (!IsValid(GetWorld()))
	{
		CancelAttack();
		return;
	}

	Phase = EPursuerAttackPhase::Recovery;
	GetWorld()->GetTimerManager().ClearTimer(ActionTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(
		RecoveryTimerHandle,
		this,
		&UPursuerAttackComponent::FinishAttack,
		FMath::Max(0.01f, RecoverySeconds),
		false);
}

/** 正常恢复结束与取消使用同一清理路径，但保留已写入的冷却截止时间。 */
void UPursuerAttackComponent::FinishAttack()
{
	CancelAttack(0.15f);
}

/** 保险超时只结束事务，不在未知空中状态补伤害或传送。 */
void UPursuerAttackComponent::HandleAttackTimeout()
{
	UE_LOG(LogPursuerAttack, Warning, TEXT("%s 攻击事务超时，已安全清理。"),
		*GetNameSafe(Character.Get()));
	CancelAttack(0.15f);
}

/** 所有 Timer 都属于组件；清理可重复调用。 */
void UPursuerAttackComponent::ClearAttackTimers()
{
	if (IsValid(GetWorld()))
	{
		FTimerManager& TimerManager = GetWorld()->GetTimerManager();
		TimerManager.ClearTimer(ActionTimerHandle);
		TimerManager.ClearTimer(RecoveryTimerHandle);
		TimerManager.ClearTimer(TimeoutTimerHandle);
		TimerManager.ClearTimer(CommitFinalizeTimerHandle);
	}
}
