// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePrototypeRoundFlow.cpp
 * 职责：消费 PCG Ready 结果并完成玩家、追猎者、出口球的最小一局放置。
 * 边界：不做路径求解、批量玩法对象放置、正式胜利 UI、存档或关卡切换。
 */

#include "GameFlow/ZeroEscapePrototypeRoundFlow.h"

#include "Characters/PursuerCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeRoundFlow, Log, All);

/** 创建无 Tick 的流程 Actor，并让出口球在成功激活本局前保持隐藏和无碰撞。 */
AZeroEscapePrototypeRoundFlow::AZeroEscapePrototypeRoundFlow()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	GoalTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("GoalTrigger"));
	GoalTrigger->SetupAttachment(SceneRoot);
	GoalTrigger->InitSphereRadius(100.0f);
	GoalTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GoalTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	GoalTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	GoalVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GoalVisual"));
	GoalVisual->SetupAttachment(GoalTrigger);
	GoalVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GoalVisual->SetVisibility(false, true);
}

/** 订阅生成完成事件；兼容 Generator 已 Ready、仍 Idle 或正在生成三种进入顺序。 */
void AZeroEscapePrototypeRoundFlow::BeginPlay()
{
	Super::BeginPlay();
	GoalTrigger->OnComponentBeginOverlap.AddUniqueDynamic(
		this,
		&AZeroEscapePrototypeRoundFlow::HandleGoalBeginOverlap);

	if (!IsValid(Generator)
		|| Generator->GetWorld() != GetWorld()
		|| PursuerClass == nullptr)
	{
		UE_LOG(LogZeroEscapeRoundFlow, Error,
			TEXT("ZE_ROUND_SETUP result=Failure reason=InvalidGeneratorOrPursuerClass actor=\"%s\""),
			*GetName());
		return;
	}

	Generator->OnGenerationFinished.AddUniqueDynamic(
		this,
		&AZeroEscapePrototypeRoundFlow::HandleGenerationFinished);

	if (Generator->State == EZeroEscapeRuntimeGenerationState::Ready)
	{
		HandleGenerationFinished(true, Generator->LastReport);
	}
	else if (Generator->State == EZeroEscapeRuntimeGenerationState::Idle
		&& !Generator->Generate())
	{
		UE_LOG(LogZeroEscapeRoundFlow, Error,
			TEXT("ZE_ROUND_SETUP result=Failure reason=InitialGenerationRejected actor=\"%s\""),
			*GetName());
	}
}

/** 解除所有委托并清理由本类拥有的临时运行态，防止退出 PIE 后残留回调或追猎者。 */
void AZeroEscapePrototypeRoundFlow::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Generator))
	{
		Generator->OnGenerationFinished.RemoveDynamic(
			this,
			&AZeroEscapePrototypeRoundFlow::HandleGenerationFinished);
	}
	if (IsValid(GoalTrigger))
	{
		GoalTrigger->OnComponentBeginOverlap.RemoveDynamic(
			this,
			&AZeroEscapePrototypeRoundFlow::HandleGoalBeginOverlap);
	}

	ResetRoundState();
	Super::EndPlay(EndPlayReason);
}

/** 只接受成功提交的 PCG 结果；失败或玩法放置失败时出口始终保持禁用。 */
void AZeroEscapePrototypeRoundFlow::HandleGenerationFinished(
	const bool bSuccess,
	const FZeroEscapeGenerationReport& Report)
{
	ResetRoundState();
	if (!bSuccess)
	{
		UE_LOG(LogZeroEscapeRoundFlow, Error,
			TEXT("ZE_ROUND_SETUP result=Failure reason=GenerationFailed message=\"%s\""),
			*Report.Message.Replace(TEXT("\n"), TEXT(" ")));
		return;
	}

	if (!ActivateRound())
	{
		UE_LOG(LogZeroEscapeRoundFlow, Error,
			TEXT("ZE_ROUND_SETUP result=Failure reason=GameplayPlacementFailed"));
	}
}

/** 使用稳定候选顺序选择最接近 1200 cm 下限的位置，不增加路径搜索或随机状态。 */
bool AZeroEscapePrototypeRoundFlow::FindPlayerSpawnTransform(
	const FTransform& PursuerStartTransform,
	FTransform& OutPlayerTransform) const
{
	OutPlayerTransform = FTransform::Identity;
	TArray<FTransform> Candidates;
	if (!Generator->GetGeneratedCellWorldTransforms(
			EZeroEscapeGridRegionKind::Corridor,
			false,
			Candidates))
	{
		return false;
	}

	const FVector StartLocation = PursuerStartTransform.GetLocation();
	const double MinimumDistanceSquared = FMath::Square(PlayerStartSeparationCm);
	double BestDistanceSquared = TNumericLimits<double>::Max();
	int32 BestIndex = INDEX_NONE;

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const double DistanceSquared = FVector::DistSquared2D(
			StartLocation,
			Candidates[Index].GetLocation());
		if (DistanceSquared >= MinimumDistanceSquared
			&& DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestIndex = Index;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return false;
	}

	FVector PlayerLocation = Candidates[BestIndex].GetLocation();
	// 候选位于地板表面；角色复用 Start 的胶囊中心高度，避免半身陷入地板。
	PlayerLocation.Z = StartLocation.Z;
	const FRotator AwayFromStartRotation = (PlayerLocation - StartLocation).Rotation();
	OutPlayerTransform = FTransform(
		FRotator(0.0, AwayFromStartRotation.Yaw, 0.0),
		PlayerLocation);
	return true;
}

/** 完成当前 Pawn 传送、追猎者 Spawn 与出口启用；不创建第二个玩家 Pawn。 */
bool AZeroEscapePrototypeRoundFlow::ActivateRound()
{
	FTransform PursuerStartTransform;
	FTransform ExitTransform;
	FTransform PlayerTransform;
	if (!Generator->GetGeneratedStartWorldTransform(PursuerStartTransform)
		|| !Generator->GetGeneratedExitWorldTransform(ExitTransform)
		|| !FindPlayerSpawnTransform(PursuerStartTransform, PlayerTransform))
	{
		return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	ActivePlayer = IsValid(PlayerController) ? PlayerController->GetPawn() : nullptr;
	if (!IsValid(ActivePlayer))
	{
		return false;
	}

	if (UPawnMovementComponent* Movement = ActivePlayer->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	const FRotator PlayerRotation = PlayerTransform.GetRotation().Rotator();
	if (!ActivePlayer->TeleportTo(
			PlayerTransform.GetLocation(),
			PlayerRotation,
			false,
			false))
	{
		ActivePlayer = nullptr;
		return false;
	}
	PlayerController->SetControlRotation(PlayerRotation);

	FTransform PursuerTransform = PursuerStartTransform;
	const FRotator FacingPlayerRotation =
		(PlayerTransform.GetLocation() - PursuerTransform.GetLocation()).Rotation();
	PursuerTransform.SetRotation(
		FRotator(0.0, FacingPlayerRotation.Yaw, 0.0).Quaternion());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnedPursuer = GetWorld()->SpawnActor<APursuerCharacter>(
		PursuerClass,
		PursuerTransform,
		SpawnParameters);
	if (!IsValid(SpawnedPursuer))
	{
		ResetRoundState();
		return false;
	}

	GoalTrigger->SetWorldTransform(ExitTransform);
	GoalVisual->SetVisibility(true, true);
	GoalTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	bRoundActive = true;

	UE_LOG(LogZeroEscapeRoundFlow, Display,
		TEXT("ZE_ROUND_SETUP result=Success player=\"%s\" pursuer=\"%s\" separation_cm=%.0f"),
		*GetNameSafe(ActivePlayer),
		*GetNameSafe(SpawnedPursuer),
		FVector::Dist2D(
			ActivePlayer->GetActorLocation(),
			SpawnedPursuer->GetActorLocation()));
	return true;
}

/** 只认本类记录的当前玩家；追猎者或其他 Pawn 进入不会触发通关。 */
void AZeroEscapePrototypeRoundFlow::HandleGoalBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!bRoundActive || OtherActor != ActivePlayer)
	{
		return;
	}

	bRoundActive = false;
	GoalTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogZeroEscapeRoundFlow, Display,
		TEXT("ZE_ROUND_RESULT success=1 reason=PlayerReachedExit"));
}

/** 恢复未激活状态，并只销毁本类生成的追猎者。 */
void AZeroEscapePrototypeRoundFlow::ResetRoundState()
{
	bRoundActive = false;
	ActivePlayer = nullptr;
	if (IsValid(GoalTrigger))
	{
		GoalTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (IsValid(GoalVisual))
	{
		GoalVisual->SetVisibility(false, true);
	}
	if (IsValid(SpawnedPursuer))
	{
		SpawnedPursuer->Destroy();
	}
	SpawnedPursuer = nullptr;
}
