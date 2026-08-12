// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAttackComponent.cpp
 * 职责：实现近战 Sweep、一次性预判跑跳、抛物线位移、落地范围命中、伤害/StandingImpact 与恢复。
 * 边界：不持续修正空中方向、不伪造 Heavy 接触、不依赖斧头组件名或动画长度决定玩法状态。
 */

#include "Components/Combat/PursuerAttackComponent.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Characters/PursuerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Data/Physics/CharacterImpactSourceProfile.h"
#include "Data/PursuerConfig.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interfaces/CharacterImpactReceiver.h"
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

	if (!Character.IsValid() || !Config.IsValid())
	{
		UE_LOG(LogPursuerAttack, Error, TEXT("PursuerAttack Configure 缺少有效角色或 Config，攻击保持禁用。"));
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
	ActiveTarget.Reset();
	LockedImpactPoint = FVector::ZeroVector;
	ActiveImpactId.Invalidate();
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
		ApplyAttackResult(ActiveTarget.Get(), ImpactOrigin, Config->CloseAttackDamage);
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
		ApplyAttackResult(ActiveTarget.Get(), ImpactOrigin, Config->JumpAttackDamage);
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

/** 一次命中同时走生命值和既有站立冲击合同；缺 Profile 时仍保留伤害并输出明确诊断。 */
void UPursuerAttackComponent::ApplyAttackResult(
	APawn* Target,
	const FVector& ImpactOrigin,
	const float Damage)
{
	if (!Character.IsValid() || !Config.IsValid() || !IsValid(Target) || !ActiveImpactId.IsValid())
	{
		return;
	}

	UGameplayStatics::ApplyDamage(
		Target,
		FMath::Max(0.0f, Damage),
		Character->GetController(),
		Character.Get(),
		UDamageType::StaticClass());

	if (!Target->GetClass()->ImplementsInterface(UCharacterImpactReceiver::StaticClass())
		|| !IsValid(Config->AttackImpactSourceProfile))
	{
		UE_LOG(LogPursuerAttack, Warning,
			TEXT("%s 命中 %s，但缺少站立冲击接收接口或 AttackImpactSourceProfile。"),
			*GetNameSafe(Character.Get()), *GetNameSafe(Target));
		return;
	}

	FVector WorldDirection = Target->GetActorLocation() - ImpactOrigin;
	WorldDirection.Z = FMath::Max(25.0f, WorldDirection.Size2D() * 0.2f);
	WorldDirection = WorldDirection.GetSafeNormal();
	if (WorldDirection.IsNearlyZero())
	{
		WorldDirection = Character->GetActorForwardVector();
	}

	FStandingImpactRequest Request;
	Request.ImpactId = ActiveImpactId;
	Request.SourceActor = Character.Get();
	Request.SourceComponent = Character->GetCapsuleComponent();
	Request.SourceProfile = Config->AttackImpactSourceProfile;
	Request.WorldDirection = WorldDirection;
	Request.ImpactPoint = Target->GetActorLocation() + FVector::UpVector * 60.0f;
	Request.NormalizedStrength = Config->AttackImpactStrength;

	const EStandingImpactSubmitResult Result =
		ICharacterImpactReceiver::Execute_SubmitStandingImpact(Target, Request);
	UE_LOG(LogPursuerAttack, Verbose,
		TEXT("%s attack hit %s (Damage=%.1f, StandingImpact=%d)."),
		*GetNameSafe(Character.Get()), *GetNameSafe(Target), Damage, static_cast<int32>(Result));
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
	}
}
