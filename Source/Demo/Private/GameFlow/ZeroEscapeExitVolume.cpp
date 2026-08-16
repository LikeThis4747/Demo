// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeExitVolume.cpp
 * 职责：实现出口体积的激活、触发检测与一次性到达广播。
 * 边界：不裁决胜负、不弹 UI；只向 GameMode 广播到达事实。
 */

#include "GameFlow/ZeroEscapeExitVolume.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeExit, Log, All);

AZeroEscapeExitVolume::AZeroEscapeExitVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(Root);

	GoalTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("GoalTrigger"));
	GoalTrigger->SetupAttachment(Root);
	GoalTrigger->SetSphereRadius(120.0f);
	GoalTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GoalTrigger->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	GoalTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECollisionResponse::ECR_Overlap);

	GoalVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GoalVisual"));
	GoalVisual->SetupAttachment(Root);
	GoalVisual->SetVisibility(false);
	GoalVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

/** 定位到出口坐标，显示外观并启用玩家触发。 */
void AZeroEscapeExitVolume::Activate(const FTransform& WorldTransform)
{
	SetActorTransform(WorldTransform);

	GoalTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GoalVisual->SetVisibility(true);

	GoalTrigger->OnComponentBeginOverlap.AddDynamic(
		this, &AZeroEscapeExitVolume::HandleGoalBeginOverlap);

	UE_LOG(LogZeroEscapeExit, Display,
		TEXT("出口已激活，位置=%s"), *WorldTransform.GetLocation().ToString());
}

void AZeroEscapeExitVolume::ConfirmReached()
{
	if (bReached)
	{
		return;
	}
	bReached = true;
	GoalTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

/** 只认玩家 Pawn；门槛不足时允许玩家离开后再次进入，真正判胜才关闭触发。 */
void AZeroEscapeExitVolume::HandleGoalBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (bReached || !IsValid(Pawn) || !Pawn->IsPlayerControlled())
	{
		return;
	}

	OnExitReached.Broadcast();
}
