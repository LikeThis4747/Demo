// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticThrowBreakComponent.cpp
 * 职责：消费共享正式投掷 Hit，以冲量门槛去重，并在 next-tick 生成 Geometry Collection 替身。
 * 边界：正式投掷碰撞身份仍完全属于 UMagneticObjectComponent；本组件只读取 Hit 并管理替换事务。
 * 状态 Owner：本组件独占 Ready、BreakQueued、Consumed、BreakFailed，失败时保留原 Actor。
 */

#include "Components/Magnetism/MagneticThrowBreakComponent.h"

#include "Actors/Magnetism/MagneticFractureActor.h"
#include "Components/Magnetism/MagneticObjectComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMagneticThrowBreak, Log, All);

namespace UE::ZeroEscape::Magnetism
{
	/** 返回项目内部破碎状态的稳定日志名称。 */
	const TCHAR* BreakStateToString(const EMagneticThrowBreakState State)
	{
		switch (State)
		{
		case EMagneticThrowBreakState::Ready:
			return TEXT("Ready");
		case EMagneticThrowBreakState::BreakQueued:
			return TEXT("BreakQueued");
		case EMagneticThrowBreakState::Consumed:
			return TEXT("Consumed");
		case EMagneticThrowBreakState::BreakFailed:
			return TEXT("BreakFailed");
		default:
			return TEXT("Unknown");
		}
	}
}

/** 默认关闭 Tick；所有工作由共享 Hit 和一次 next-tick Timer 驱动。 */
UMagneticThrowBreakComponent::UMagneticThrowBreakComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/** 校验项目自有磁力原型装配，并只在完整配置时订阅共享命中。 */
void UMagneticThrowBreakComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* OwnerActor = GetOwner();
	UPrimitiveComponent* RootPrimitive = IsValid(OwnerActor)
		? Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent())
		: nullptr;
	UMagneticObjectComponent* FoundMagneticObject = IsValid(OwnerActor)
		? OwnerActor->FindComponentByClass<UMagneticObjectComponent>()
		: nullptr;

	MonitoredBody = RootPrimitive;
	MagneticObject = FoundMagneticObject;
	if (!IsValid(RootPrimitive)
		|| !RootPrimitive->IsSimulatingPhysics()
		|| !IsValid(FoundMagneticObject))
	{
		UE_LOG(LogMagneticThrowBreak, Error,
			TEXT("%s cannot configure magnetic fracture: owner needs a simulated primitive root and MagneticObjectComponent."),
			*GetNameSafe(OwnerActor));
		return;
	}

	if (!FractureActorClass)
	{
		UE_LOG(LogMagneticThrowBreak, Verbose,
			TEXT("%s has no fracture actor class and remains a regular magnetic prop."),
			*GetNameSafe(OwnerActor));
		return;
	}

	if (!FMath::IsFinite(MinimumBreakNormalImpulse)
		|| MinimumBreakNormalImpulse < 0.0f
		|| !FMath::IsFinite(MaximumMonitoringSeconds)
		|| MaximumMonitoringSeconds <= 0.0f)
	{
		UE_LOG(LogMagneticThrowBreak, Warning,
			TEXT("%s disabled magnetic fracture because impulse %.3f or duration %.3f is invalid."),
			*GetNameSafe(OwnerActor),
			MinimumBreakNormalImpulse,
			MaximumMonitoringSeconds);
		return;
	}

	ThrownHitDelegateHandle = FoundMagneticObject->OnThrownBlockingHit().AddUObject(
		this, &UMagneticThrowBreakComponent::HandleThrownBlockingHit);
	bConfigurationValid = true;
}

/** 移除原生委托和延迟替换，保证组件销毁后不会产生孤立碎片。 */
void UMagneticThrowBreakComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredBreakTimerHandle);
	}

	if (UMagneticObjectComponent* MagneticComponent = MagneticObject.Get();
		IsValid(MagneticComponent) && ThrownHitDelegateHandle.IsValid())
	{
		MagneticComponent->OnThrownBlockingHit().Remove(ThrownHitDelegateHandle);
	}
	ThrownHitDelegateHandle.Reset();
	PendingBody.Reset();

	Super::EndPlay(EndPlayReason);
}

/** 只有完整配置、精确根刚体和 Ready 状态才要求正式投掷延长破碎监听。 */
float UMagneticThrowBreakComponent::GetFormalThrowMonitoringSeconds(
	const UPrimitiveComponent* CandidateBody) const
{
	if (!bConfigurationValid)
	{
		return 0.0f;
	}

	if (State != EMagneticThrowBreakState::Ready)
	{
		UE_LOG(LogMagneticThrowBreak, Warning,
			TEXT("%s did not arm fracture monitoring because its state is %s."),
			*GetNameSafe(GetOwner()),
			UE::ZeroEscape::Magnetism::BreakStateToString(State));
		return 0.0f;
	}

	if (!IsValid(CandidateBody) || CandidateBody != MonitoredBody.Get())
	{
		UE_LOG(LogMagneticThrowBreak, Warning,
			TEXT("%s did not arm fracture monitoring because the thrown body %s does not match %s."),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(CandidateBody),
			*GetNameSafe(MonitoredBody.Get()));
		return 0.0f;
	}

	return MaximumMonitoringSeconds;
}

/** 已进入不可逆替换队列时拒绝 Physics Handle；Ready/BreakFailed 均允许安全抓取。 */
bool UMagneticThrowBreakComponent::CanBeginGrab(const UPrimitiveComponent* CandidateBody) const
{
	if (!IsValid(CandidateBody) || CandidateBody != MonitoredBody.Get())
	{
		return true;
	}

	return State != EMagneticThrowBreakState::BreakQueued
		&& State != EMagneticThrowBreakState::Consumed;
}

/** 以真实 NormalImpulse 过滤轻擦，并在第一笔合格 Hit 时原子进入 BreakQueued。 */
void UMagneticThrowBreakComponent::HandleThrownBlockingHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	const FVector& NormalImpulse,
	const FHitResult& Hit)
{
	const float ImpulseMagnitude = NormalImpulse.Size();
	if (!bConfigurationValid
		|| State != EMagneticThrowBreakState::Ready
		|| HitComponent != MonitoredBody.Get()
		|| !Hit.bBlockingHit
		|| !IsValid(OtherActor)
		|| OtherActor == GetOwner()
		|| !FMath::IsFinite(ImpulseMagnitude)
		|| ImpulseMagnitude < MinimumBreakNormalImpulse
		|| !IsValid(GetWorld()))
	{
		return;
	}

	PendingBody = HitComponent;
	SetState(EMagneticThrowBreakState::BreakQueued, TEXT("first qualifying blocking hit"));
	UE_LOG(LogMagneticThrowBreak, Verbose,
		TEXT("%s queued fracture after hitting %s with impulse %.1f."),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(OtherActor),
		ImpulseMagnitude);

	DeferredBreakTimerHandle = GetWorld()->GetTimerManager().SetTimerForNextTick(
		this, &UMagneticThrowBreakComponent::ProcessQueuedBreak);
}

/** 读取碰撞后的运动，deferred spawn 替身，并严格保持“替身成功后才销毁原物体”。 */
void UMagneticThrowBreakComponent::ProcessQueuedBreak()
{
	if (State != EMagneticThrowBreakState::BreakQueued)
	{
		return;
	}

	UPrimitiveComponent* Body = PendingBody.Get();
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!IsValid(Body)
		|| !IsValid(OwnerActor)
		|| !IsValid(World)
		|| !FractureActorClass)
	{
		FailAndPreserveOriginal(TEXT("invalid body, owner, world or fracture class"));
		return;
	}

	const FTransform SpawnTransform = Body->GetComponentTransform();
	const FVector LinearVelocity = Body->GetPhysicsLinearVelocity();
	const FVector AngularVelocityRadians = Body->GetPhysicsAngularVelocityInRadians();

	AMagneticFractureActor* Fracture = World->SpawnActorDeferred<AMagneticFractureActor>(
		FractureActorClass,
		SpawnTransform,
		nullptr,
		OwnerActor->GetInstigator(),
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
		ESpawnActorScaleMethod::OverrideRootScale);
	if (!IsValid(Fracture))
	{
		FailAndPreserveOriginal(TEXT("SpawnActorDeferred returned null"));
		return;
	}

	Fracture->SetInheritedMotion(LinearVelocity, AngularVelocityRadians);
	AMagneticFractureActor* FinishedFracture = Cast<AMagneticFractureActor>(
		UGameplayStatics::FinishSpawningActor(
			Fracture,
			SpawnTransform,
			ESpawnActorScaleMethod::OverrideRootScale));
	if (!IsValid(FinishedFracture))
	{
		FailAndPreserveOriginal(TEXT("FinishSpawningActor did not produce a valid fracture actor"));
		return;
	}

	PendingBody.Reset();
	SetState(EMagneticThrowBreakState::Consumed, TEXT("fracture actor spawned"));
	if (!OwnerActor->Destroy())
	{
		FinishedFracture->Destroy();
		FailAndPreserveOriginal(TEXT("original actor could not be destroyed"));
	}
}

/** 替换失败时只降级本实例，不让配置错误删除玩家资源或每次碰撞循环报错。 */
void UMagneticThrowBreakComponent::FailAndPreserveOriginal(const TCHAR* Reason)
{
	PendingBody.Reset();
	SetState(EMagneticThrowBreakState::BreakFailed, Reason);
	if (UMagneticObjectComponent* MagneticComponent = MagneticObject.Get(); IsValid(MagneticComponent))
	{
		MagneticComponent->DisarmThrownImpact();
	}

	UE_LOG(LogMagneticThrowBreak, Error,
		TEXT("%s preserved the original magnetic prop because fracture replacement failed: %s."),
		*GetNameSafe(GetOwner()),
		Reason);
}

/** 单点写状态并保留前后状态与原因，便于 PIE 归因而不刷普通级别日志。 */
void UMagneticThrowBreakComponent::SetState(
	const EMagneticThrowBreakState NewState,
	const TCHAR* Context)
{
	const EMagneticThrowBreakState PreviousState = State;
	State = NewState;
	UE_LOG(LogMagneticThrowBreak, Verbose,
		TEXT("%s magnetic fracture state %s -> %s (%s)."),
		*GetNameSafe(GetOwner()),
		UE::ZeroEscape::Magnetism::BreakStateToString(PreviousState),
		UE::ZeroEscape::Magnetism::BreakStateToString(NewState),
		Context);
}
