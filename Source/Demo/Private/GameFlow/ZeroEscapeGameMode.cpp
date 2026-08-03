// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameMode.cpp
 * 职责：正式一局的开局编排实现——读请求、驱动 PCG 生成、摆放追猎者与玩家。
 * 边界：不做胜负、陷阱、AI 或磁力逻辑；生成失败或摆放失败时只记录错误，不做兜底重试。
 */

#include "GameFlow/ZeroEscapeGameMode.h"

#include "Characters/PursuerCharacter.h"
#include "Characters/ZeroEscapeCharacter.h"
#include "Components/Attributes/HealthComponent.h"
#include "EngineUtils.h"
#include "GameFlow/ZeroEscapeExitVolume.h"
#include "GameFlow/ZeroEscapeGameInstance.h"
#include "GameFlow/ZeroEscapeGameState.h"
#include "GameFlow/ZeroEscapePlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"
#include "UI/ZeroEscapeHUD.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeGameMode, Log, All);

/** 与原型一致的角色/Controller/HUD；完整角色资源仍由玩家角色蓝图装配。 */
AZeroEscapeGameMode::AZeroEscapeGameMode()
{
	DefaultPawnClass = AZeroEscapeCharacter::StaticClass();
	PlayerControllerClass = AZeroEscapePlayerController::StaticClass();
	HUDClass = AZeroEscapeHUD::StaticClass();
}

/** 读取本局请求，驱动 PCG 生成，成功后摆放追猎者与玩家。 */
void AZeroEscapeGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 从主菜单 OpenLevel 进来时，PlayerController 仍停留在主菜单设置的 UI-Only 输入模式，
	// 会拦截所有游戏输入导致玩家无法操控；正式关卡必须显式切回 GameOnly 并隐藏鼠标。
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		PlayerController->SetInputMode(FInputModeGameOnly());
		PlayerController->SetShowMouseCursor(false);
	}

	AZeroEscapeRuntimeLevelGenerator* Generator = FindLevelGenerator();
	if (Generator == nullptr)
	{
		UE_LOG(LogZeroEscapeGameMode, Error,
			TEXT("ZE_GAME_SETUP result=Failure reason=GeneratorNotFound"));
		return;
	}

	// Seed/难度来自 GameInstance；单独运行 L_Game（未经主菜单）时回退到 Generator 自带的 DefaultRequest。
	const UZeroEscapeGameInstance* GameInstancePtr = GetGameInstance<UZeroEscapeGameInstance>();
	if (GameInstancePtr != nullptr)
	{
		if (!Generator->GenerateFromRequest(GameInstancePtr->GetPendingRequest()))
		{
			UE_LOG(LogZeroEscapeGameMode, Error,
				TEXT("ZE_GAME_SETUP result=Failure reason=GenerationRejected"));
			return;
		}
	}
	else if (!Generator->Generate())
	{
		UE_LOG(LogZeroEscapeGameMode, Warning,
			TEXT("ZE_GAME_SETUP reason=NoGameInstanceFellBackToDefaultRequest"));
		return;
	}

	if (Generator->State != EZeroEscapeRuntimeGenerationState::Ready
		|| !PlacePlayerAndPursuer(*Generator)
		|| !PlaceExit(*Generator))
	{
		UE_LOG(LogZeroEscapeGameMode, Error,
			TEXT("ZE_GAME_SETUP result=Failure reason=PlacementFailed"));
		return;
	}

	BindPlayerDeath();
}

/** 遍历世界查找唯一 Generator；发现多于一个时选第一个并记录警告。 */
AZeroEscapeRuntimeLevelGenerator* AZeroEscapeGameMode::FindLevelGenerator() const
{
	AZeroEscapeRuntimeLevelGenerator* Found = nullptr;
	for (TActorIterator<AZeroEscapeRuntimeLevelGenerator> It(GetWorld()); It; ++It)
	{
		if (Found == nullptr)
		{
			Found = *It;
		}
		else
		{
			UE_LOG(LogZeroEscapeGameMode, Warning,
				TEXT("ZE_GAME_SETUP reason=MultipleGeneratorsUsingFirst"));
			break;
		}
	}
	return Found;
}

/** 追猎者放起点、玩家放两格外，双方面向同一逃跑方向。 */
bool AZeroEscapeGameMode::PlacePlayerAndPursuer(AZeroEscapeRuntimeLevelGenerator& Generator)
{
	FTransform StartTransform;
	FTransform PlayerTransform;
	if (!Generator.GetGeneratedStartWorldTransform(StartTransform)
		|| !FindPlayerSpawnTransform(Generator, StartTransform, PlayerTransform))
	{
		return false;
	}

	// 逃跑正方向：从起点（追猎者）指向玩家；玩家背对追猎者向前逃，追猎者面向玩家追击。
	const FRotator ForwardRotation =
		(PlayerTransform.GetLocation() - StartTransform.GetLocation()).Rotation();
	const FQuat ForwardYaw = FRotator(0.0, ForwardRotation.Yaw, 0.0).Quaternion();
	StartTransform.SetRotation(ForwardYaw);
	PlayerTransform.SetRotation(ForwardYaw);

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	APawn* Player = IsValid(PlayerController) ? PlayerController->GetPawn() : nullptr;
	if (!IsValid(Player))
	{
		return false;
	}

	if (UPawnMovementComponent* Movement = Player->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	const FRotator PlayerRotation = PlayerTransform.GetRotation().Rotator();
	if (!Player->TeleportTo(PlayerTransform.GetLocation(), PlayerRotation, false, false))
	{
		return false;
	}
	PlayerController->SetControlRotation(PlayerRotation);

	if (PursuerClass == nullptr)
	{
		UE_LOG(LogZeroEscapeGameMode, Error,
			TEXT("ZE_GAME_SETUP result=Failure reason=PursuerClassUnset"));
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnedPursuer = GetWorld()->SpawnActor<APursuerCharacter>(
		PursuerClass,
		StartTransform,
		SpawnParameters);
	if (!IsValid(SpawnedPursuer))
	{
		return false;
	}

	UE_LOG(LogZeroEscapeGameMode, Display,
		TEXT("ZE_GAME_SETUP result=Success player=\"%s\" pursuer=\"%s\" separation_cm=%.0f"),
		*GetNameSafe(Player),
		*GetNameSafe(SpawnedPursuer),
		FVector::Dist2D(Player->GetActorLocation(), SpawnedPursuer->GetActorLocation()));
	return true;
}

/** 稳定顺序选出距起点最接近下限的走廊格；追猎者位于起点，故此点即玩家两格缓冲出生点。 */
bool AZeroEscapeGameMode::FindPlayerSpawnTransform(
	AZeroEscapeRuntimeLevelGenerator& Generator,
	const FTransform& StartTransform,
	FTransform& OutPlayerTransform) const
{
	OutPlayerTransform = FTransform::Identity;
	TArray<FTransform> Candidates;
	if (!Generator.GetGeneratedCellWorldTransforms(
			EZeroEscapeGridRegionKind::Corridor,
			false,
			false,
			Candidates))
	{
		return false;
	}

	const FVector StartLocation = StartTransform.GetLocation();
	const double MinimumDistanceSquared = FMath::Square(PlayerStartSeparationCm);
	double BestDistanceSquared = TNumericLimits<double>::Max();
	int32 BestIndex = INDEX_NONE;

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const double DistanceSquared =
			FVector::DistSquared2D(StartLocation, Candidates[Index].GetLocation());
		if (DistanceSquared >= MinimumDistanceSquared && DistanceSquared < BestDistanceSquared)
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
	// 候选位于地板表面；玩家复用起点的胶囊中心高度，避免半身陷入地板。
	PlayerLocation.Z = StartLocation.Z;
	OutPlayerTransform = FTransform(StartTransform.GetRotation(), PlayerLocation);
	return true;
}

bool AZeroEscapeGameMode::PlaceExit(AZeroEscapeRuntimeLevelGenerator& Generator)
{
	FTransform ExitTransform;
	if (!Generator.GetGeneratedExitWorldTransform(ExitTransform))
	{
		UE_LOG(LogZeroEscapeGameMode, Error,
			TEXT("ZE_GAME_SETUP result=Failure reason=ExitTransformUnavailable"));
		return false;
	}

	if (ExitActorClass == nullptr)
	{
		UE_LOG(LogZeroEscapeGameMode, Error,
			TEXT("ZE_GAME_SETUP result=Failure reason=ExitActorClassUnset"));
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnedExit = GetWorld()->SpawnActor<AZeroEscapeExitVolume>(
		ExitActorClass,
		ExitTransform,
		SpawnParameters);
	if (!IsValid(SpawnedExit))
	{
		return false;
	}

	SpawnedExit->OnExitReached.AddDynamic(this, &AZeroEscapeGameMode::HandleExitReached);
	SpawnedExit->Activate(ExitTransform);
	return true;
}

/** 找到玩家的生命组件，绑定归零事件用于判负转发。 */
void AZeroEscapeGameMode::BindPlayerDeath()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(Player))
	{
		return;
	}

	if (UHealthComponent* Health = Player->FindComponentByClass<UHealthComponent>())
	{
		Health->OnHealthDepleted.AddDynamic(this, &AZeroEscapeGameMode::HandlePlayerDeath);
	}
}

void AZeroEscapeGameMode::HandleExitReached()
{
	if (AZeroEscapeGameState* ZeState = GetGameState<AZeroEscapeGameState>())
	{
		ZeState->SetRoundWon();
	}
}

void AZeroEscapeGameMode::HandlePlayerDeath()
{
	if (AZeroEscapeGameState* ZeState = GetGameState<AZeroEscapeGameState>())
	{
		ZeState->SetRoundLost();
	}
}
