// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeEnergyOrb.cpp
 * 职责：实现一次性玩家收集和约 0.3 秒的加速吸取、缩小消失表现。
 * 边界：收集是否有效由正式 GameMode/GameState 裁决；任何表现异常都不得重复结算。
 */

#include "Actors/Items/ZeroEscapeEnergyOrb.h"

#include "Engine/World.h"
#include "GameFlow/ZeroEscapeGameMode.h"
#include "GameFramework/Pawn.h"

AZeroEscapeEnergyOrb::AZeroEscapeEnergyOrb()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AZeroEscapeEnergyOrb::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);

	APawn* PlayerPawn = Cast<APawn>(OtherActor);
	AZeroEscapeGameMode* GameMode = IsValid(GetWorld())
		? Cast<AZeroEscapeGameMode>(GetWorld()->GetAuthGameMode())
		: nullptr;
	if (bCollectionCommitted
		|| !IsValid(PlayerPawn)
		|| !PlayerPawn->IsPlayerControlled()
		|| !IsValid(GameMode)
		|| !GameMode->TryCollectEnergyOrb(*PlayerPawn))
	{
		return;
	}

	BeginCollection(*PlayerPawn);
}

void AZeroEscapeEnergyOrb::BeginCollection(AActor& Collector)
{
	bCollectionCommitted = true;
	SetActorEnableCollision(false);
	CollectionTarget = &Collector;
	CollectionStartLocation = GetActorLocation();
	CollectionStartScale = GetActorScale3D();

	UWorld* World = GetWorld();
	if (!IsValid(World)
		|| !FMath::IsFinite(CollectionAnimationDurationSeconds)
		|| CollectionAnimationDurationSeconds <= 0.0f)
	{
		Destroy();
		return;
	}

	CollectionStartWorldTimeSeconds = static_cast<double>(World->GetTimeSeconds());
	World->GetTimerManager().SetTimer(
		CollectionAnimationTimer,
		this,
		&AZeroEscapeEnergyOrb::UpdateCollectionAnimation,
		1.0f / 60.0f,
		true,
		0.0f);
}

void AZeroEscapeEnergyOrb::UpdateCollectionAnimation()
{
	UWorld* World = GetWorld();
	AActor* Target = CollectionTarget.Get();
	if (!IsValid(World) || !IsValid(Target))
	{
		Destroy();
		return;
	}

	const double ElapsedSeconds =
		static_cast<double>(World->GetTimeSeconds()) - CollectionStartWorldTimeSeconds;
	const float Alpha = FMath::Clamp(
		static_cast<float>(ElapsedSeconds / CollectionAnimationDurationSeconds),
		0.0f,
		1.0f);
	const float AcceleratingAlpha = Alpha * Alpha;
	const FVector TargetLocation = Target->GetActorLocation()
		+ FVector::UpVector * CollectionTargetHeightCm;
	SetActorLocation(FMath::Lerp(
		CollectionStartLocation, TargetLocation, AcceleratingAlpha));
	SetActorScale3D(CollectionStartScale * FMath::Lerp(1.0f, 0.05f, Alpha));

	if (Alpha >= 1.0f)
	{
		Destroy();
	}
}

void AZeroEscapeEnergyOrb::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CollectionAnimationTimer);
	}
	CollectionTarget.Reset();
	Super::EndPlay(EndPlayReason);
}
