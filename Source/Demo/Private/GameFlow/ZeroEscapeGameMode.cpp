// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameMode.cpp
 * 职责：把正式 PCG、导航、玩家、追猎者、Exit 与 Population 原子编排成一局。
 * 边界：生成失败只做清理并返回主菜单；不得改写公开 Seed 或重载游戏关卡重试。
 */

#include "GameFlow/ZeroEscapeGameMode.h"

#include "Characters/PursuerCharacter.h"
#include "Characters/ZeroEscapeCharacter.h"
#include "Components/Attributes/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/ZeroEscapeExitVolume.h"
#include "GameFlow/ZeroEscapeGameSetupGate.h"
#include "GameFlow/ZeroEscapeGameInstance.h"
#include "GameFlow/ZeroEscapePlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PCG/Population/ZeroEscapeGameplayPopulator.h"
#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"
#include "UI/ResultMenuWidget.h"
#include "UI/ZeroEscapeHUD.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapeGameMode, Log, All);

AZeroEscapeGameMode::AZeroEscapeGameMode()
{
	DefaultPawnClass = AZeroEscapeCharacter::StaticClass();
	PlayerControllerClass = AZeroEscapePlayerController::StaticClass();
	HUDClass = AZeroEscapeHUD::StaticClass();
}

void AZeroEscapeGameMode::BeginPlay()
{
	Super::BeginPlay();
	SetGameplayInputLocked(true);

	ActiveGenerator = FindLevelGenerator();
	ActivePopulator = FindGameplayPopulator();
	if (!IsValid(ActiveGenerator))
	{
		AbortSetupAndReturnToMainMenu(TEXT("GeneratorNotUnique"));
		return;
	}
	if (!IsValid(ActivePopulator))
	{
		AbortSetupAndReturnToMainMenu(TEXT("PopulationNotUnique"));
		return;
	}
	if (MainMenuLevel.IsNull())
	{
		AbortSetupAndReturnToMainMenu(TEXT("MainMenuLevelUnset"));
		return;
	}
	if (PursuerClass == nullptr || ExitActorClass == nullptr
		|| ResultMenuWidgetClass == nullptr)
	{
		AbortSetupAndReturnToMainMenu(TEXT("RoundClassUnset"));
		return;
	}

	const APursuerCharacter* PursuerDefaults =
		PursuerClass->GetDefaultObject<APursuerCharacter>();
	if (!IsValid(PursuerDefaults))
	{
		AbortSetupAndReturnToMainMenu(TEXT("PursuerNavAgentInvalid"));
		return;
	}

	// ACharacter normally resolves the movement component's -1 nav radius/height
	// from its capsule in PostInitializeComponents. The class default object has
	// not run that instance lifecycle, so reproduce that engine step on a local
	// copy without mutating the Blueprint CDO.
	FNavAgentProperties PursuerNavigationAgent =
		PursuerDefaults->GetNavAgentPropertiesRef();
	if (!PursuerNavigationAgent.IsValid())
	{
		const UCapsuleComponent* Capsule = PursuerDefaults->GetCapsuleComponent();
		if (!IsValid(Capsule))
		{
			AbortSetupAndReturnToMainMenu(TEXT("PursuerNavAgentInvalid"));
			return;
		}
		PursuerNavigationAgent.AgentRadius = Capsule->GetScaledCapsuleRadius();
		PursuerNavigationAgent.AgentHeight =
			Capsule->GetScaledCapsuleHalfHeight() * 2.0f;
	}
	if (!ActiveGenerator->ConfigurePursuerNavigationAgent(PursuerNavigationAgent))
	{
		AbortSetupAndReturnToMainMenu(TEXT("PursuerNavAgentInvalid"));
		return;
	}

	ActiveGenerator->OnGenerationFinished.AddUniqueDynamic(
		this, &AZeroEscapeGameMode::HandleGenerationFinished);
	const UZeroEscapeGameInstance* GameInstancePtr =
		GetGameInstance<UZeroEscapeGameInstance>();
	const bool bAccepted = GameInstancePtr != nullptr
		? ActiveGenerator->GenerateFromRequest(GameInstancePtr->GetPendingRequest())
		: ActiveGenerator->Generate();
	if (GameInstancePtr == nullptr)
	{
		UE_LOG(LogZeroEscapeGameMode, Warning,
			TEXT("ZE_GAME_SETUP state=Waiting source=GeneratorDefaultRequest"));
	}
	if (!bAccepted && !bSetupTerminal)
	{
		AbortSetupAndReturnToMainMenu(TEXT("GenerationRejected"));
	}
}

AZeroEscapeRuntimeLevelGenerator* AZeroEscapeGameMode::FindLevelGenerator() const
{
	AZeroEscapeRuntimeLevelGenerator* Found = nullptr;
	for (TActorIterator<AZeroEscapeRuntimeLevelGenerator> It(GetWorld()); It; ++It)
	{
		if (Found != nullptr)
		{
			UE_LOG(LogZeroEscapeGameMode, Error,
				TEXT("ZE_GAME_SETUP result=Failure reason=MultipleGenerators"));
			return nullptr;
		}
		Found = *It;
	}
	return Found;
}

AZeroEscapeGameplayPopulator* AZeroEscapeGameMode::FindGameplayPopulator() const
{
	AZeroEscapeGameplayPopulator* Found = nullptr;
	for (TActorIterator<AZeroEscapeGameplayPopulator> It(GetWorld()); It; ++It)
	{
		if (Found != nullptr)
		{
			UE_LOG(LogZeroEscapeGameMode, Error,
				TEXT("ZE_GAME_SETUP result=Failure reason=MultiplePopulators"));
			return nullptr;
		}
		Found = *It;
	}
	return Found;
}

void AZeroEscapeGameMode::HandleGenerationFinished(
	const bool bSuccess,
	const FZeroEscapeGenerationReport& Report)
{
	if (!IsValid(ActiveGenerator))
	{
		return;
	}
	ZeroEscape::GameFlow::FGameSetupGateSnapshot GateState;
	GateState.ActiveOperationId = ActiveGenerator->GetActiveOperationId();
	GateState.LastHandledOperationId = LastHandledGenerationOperationId;
	GateState.bTerminal = bSetupTerminal;
	GateState.bEndingPlay = bEndingPlay;
	if (!ZeroEscape::GameFlow::FGameSetupGate::AcceptFinalReport(
			GateState, Report.OperationId))
	{
		UE_LOG(LogZeroEscapeGameMode, Warning,
			TEXT("ZE_GAME_SETUP state=IgnoredCallback operation=%lld active=%lld"),
			static_cast<long long>(Report.OperationId),
			static_cast<long long>(ActiveGenerator->GetActiveOperationId()));
		return;
	}
	LastHandledGenerationOperationId = Report.OperationId;

	if (!bSuccess || ActiveGenerator->State != EZeroEscapeRuntimeGenerationState::Ready)
	{
		AbortSetupAndReturnToMainMenu(TEXT("GenerationFinalFailure"));
		return;
	}
	if (!PlacePlayerAndPursuer(*ActiveGenerator))
	{
		AbortSetupAndReturnToMainMenu(TEXT("PlayerOrPursuerPlacementFailed"));
		return;
	}
	if (!PlaceExit(*ActiveGenerator))
	{
		AbortSetupAndReturnToMainMenu(TEXT("ExitPlacementFailed"));
		return;
	}
	if (!IsValid(ActivePopulator) || !ActivePopulator->Populate(*ActiveGenerator))
	{
		AbortSetupAndReturnToMainMenu(TEXT("PopulationFailed"));
		return;
	}
	if (!BindPlayerDeath() || !BindRoundStateForUI())
	{
		AbortSetupAndReturnToMainMenu(TEXT("RoundDelegateBindingFailed"));
		return;
	}

	ActiveGenerator->OnGenerationFinished.RemoveDynamic(
		this, &AZeroEscapeGameMode::HandleGenerationFinished);
	bRoundStarted = true;
	bSetupTerminal = true;
	SetGameplayInputLocked(false);
	UE_LOG(LogZeroEscapeGameMode, Display,
		TEXT("ZE_GAME_SETUP result=Success operation=%lld player=%s pursuer=%s exit=%s"),
		static_cast<long long>(Report.OperationId),
		*GetNameSafe(UGameplayStatics::GetPlayerPawn(this, 0)),
		*GetNameSafe(SpawnedPursuer),
		*GetNameSafe(SpawnedExit));
}

bool AZeroEscapeGameMode::PlacePlayerAndPursuer(
	AZeroEscapeRuntimeLevelGenerator& Generator)
{
	FTransform PlayerTransform;
	FTransform PursuerTransform;
	if (!Generator.GetGeneratedPlayerSpawnWorldTransform(PlayerTransform)
		|| !Generator.GetGeneratedPursuerSpawnWorldTransform(PursuerTransform))
	{
		return false;
	}

	const FVector ChaseDirection =
		PlayerTransform.GetLocation() - PursuerTransform.GetLocation();
	if (ChaseDirection.SizeSquared2D() <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const FQuat ForwardYaw = FRotator(
		0.0, ChaseDirection.Rotation().Yaw, 0.0).Quaternion();
	PlayerTransform.SetRotation(ForwardYaw);
	PursuerTransform.SetRotation(ForwardYaw);

	APlayerController* PlayerController =
		UGameplayStatics::GetPlayerController(this, 0);
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
	if (!Player->TeleportTo(
			PlayerTransform.GetLocation(), PlayerRotation, false, false))
	{
		return false;
	}
	PlayerController->SetControlRotation(PlayerRotation);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnedPursuer = GetWorld()->SpawnActor<APursuerCharacter>(
		PursuerClass, PursuerTransform, SpawnParameters);
	return IsValid(SpawnedPursuer);
}

bool AZeroEscapeGameMode::PlaceExit(AZeroEscapeRuntimeLevelGenerator& Generator)
{
	FTransform ExitTransform;
	if (!Generator.GetGeneratedExitWorldTransform(ExitTransform))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnedExit = GetWorld()->SpawnActor<AZeroEscapeExitVolume>(
		ExitActorClass, ExitTransform, SpawnParameters);
	if (!IsValid(SpawnedExit))
	{
		return false;
	}
	SpawnedExit->OnExitReached.AddUniqueDynamic(
		this, &AZeroEscapeGameMode::HandleExitReached);
	SpawnedExit->Activate(ExitTransform);
	return true;
}

bool AZeroEscapeGameMode::BindPlayerDeath()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	BoundHealthComponent = IsValid(Player)
		? Player->FindComponentByClass<UHealthComponent>()
		: nullptr;
	if (!IsValid(BoundHealthComponent))
	{
		return false;
	}
	BoundHealthComponent->OnHealthDepleted.AddUniqueDynamic(
		this, &AZeroEscapeGameMode::HandlePlayerDeath);
	return true;
}

bool AZeroEscapeGameMode::BindRoundStateForUI()
{
	BoundRoundState = GetGameState<AZeroEscapeGameState>();
	if (!IsValid(BoundRoundState) || ResultMenuWidgetClass == nullptr)
	{
		return false;
	}
	BoundRoundState->OnRoundStateChanged.AddUniqueDynamic(
		this, &AZeroEscapeGameMode::HandleRoundStateChanged);
	return true;
}

void AZeroEscapeGameMode::HandleExitReached()
{
	if (bRoundStarted && IsValid(BoundRoundState))
	{
		BoundRoundState->SetRoundWon();
	}
}

void AZeroEscapeGameMode::HandlePlayerDeath()
{
	if (bRoundStarted && IsValid(BoundRoundState))
	{
		BoundRoundState->SetRoundLost();
	}
}

void AZeroEscapeGameMode::HandleRoundStateChanged(
	const EZeroEscapeRoundState NewState)
{
	if (!bRoundStarted
		|| NewState == EZeroEscapeRoundState::InProgress
		|| ResultMenuWidgetClass == nullptr
		|| IsValid(ResultMenuWidget))
	{
		return;
	}

	APlayerController* PlayerController =
		UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController))
	{
		return;
	}
	ResultMenuWidget = CreateWidget<UResultMenuWidget>(
		PlayerController, ResultMenuWidgetClass);
	if (!IsValid(ResultMenuWidget))
	{
		return;
	}
	ResultMenuWidget->ShowResult(NewState == EZeroEscapeRoundState::Won);
	ResultMenuWidget->AddToViewport();
	PlayerController->SetInputMode(FInputModeUIOnly());
	PlayerController->SetShowMouseCursor(true);
	UGameplayStatics::SetGamePaused(this, true);
}

void AZeroEscapeGameMode::SetGameplayInputLocked(const bool bLocked) const
{
	APlayerController* PlayerController =
		UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController))
	{
		return;
	}
	if (AZeroEscapePlayerController* ZeroEscapeController =
		Cast<AZeroEscapePlayerController>(PlayerController))
	{
		ZeroEscapeController->SetPauseMenuEnabled(!bLocked);
	}
	PlayerController->ResetIgnoreMoveInput();
	PlayerController->ResetIgnoreLookInput();
	if (bLocked)
	{
		PlayerController->SetIgnoreMoveInput(true);
		PlayerController->SetIgnoreLookInput(true);
	}
	PlayerController->SetInputMode(FInputModeGameOnly());
	PlayerController->SetShowMouseCursor(false);
}

void AZeroEscapeGameMode::AbortSetupAndReturnToMainMenu(const TCHAR* Reason)
{
	if (bSetupTerminal || bEndingPlay)
	{
		return;
	}
	const FString FailureReason = Reason != nullptr ? Reason : TEXT("Unknown");
	UE_LOG(LogZeroEscapeGameMode, Error,
		TEXT("ZE_GAME_SETUP result=Failure reason=%s operation=%lld"),
		*FailureReason,
		static_cast<long long>(LastHandledGenerationOperationId));
	BeginSetupTransition(*FailureReason, MainMenuLevel);
}

void AZeroEscapeGameMode::BeginSetupTransition(
	const TCHAR* Reason,
	const TSoftObjectPtr<UWorld>& TargetLevel)
{
	if (bSetupTerminal || bEndingPlay)
	{
		return;
	}
	bSetupTerminal = true;
	bSetupTransitionScheduled = true;
	PendingTransitionReason = Reason != nullptr ? Reason : TEXT("Unknown");
	PendingTransitionLevel = TargetLevel;
	SetGameplayInputLocked(true);
	UnbindRuntimeDelegates();
	CleanupRoundActors();

	if (UWorld* World = GetWorld())
	{
		SetupTransitionTimer = World->GetTimerManager().SetTimerForNextTick(
			this, &AZeroEscapeGameMode::FinalizeSetupTransition);
	}
}

void AZeroEscapeGameMode::FinalizeSetupTransition()
{
	if (bEndingPlay || !bSetupTransitionScheduled)
	{
		return;
	}
	if (IsValid(ActiveGenerator) && !ActiveGenerator->ClearGeneratedScene())
	{
		UE_LOG(LogZeroEscapeGameMode, Warning,
			TEXT("ZE_GAME_SETUP reason=GeneratorClearDeferredOrRejected"));
	}
	if (PendingTransitionLevel.IsNull())
	{
		UE_LOG(LogZeroEscapeGameMode, Error,
			TEXT("ZE_GAME_SETUP reason=TransitionLevelUnset transition=%s"),
			*PendingTransitionReason);
		return;
	}
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, PendingTransitionLevel);
}

void AZeroEscapeGameMode::UnbindRuntimeDelegates()
{
	if (IsValid(ActiveGenerator))
	{
		ActiveGenerator->OnGenerationFinished.RemoveDynamic(
			this, &AZeroEscapeGameMode::HandleGenerationFinished);
	}
	if (IsValid(BoundHealthComponent))
	{
		BoundHealthComponent->OnHealthDepleted.RemoveDynamic(
			this, &AZeroEscapeGameMode::HandlePlayerDeath);
	}
	BoundHealthComponent = nullptr;
	if (IsValid(BoundRoundState))
	{
		BoundRoundState->OnRoundStateChanged.RemoveDynamic(
			this, &AZeroEscapeGameMode::HandleRoundStateChanged);
	}
	BoundRoundState = nullptr;
	if (IsValid(SpawnedExit))
	{
		SpawnedExit->OnExitReached.RemoveDynamic(
			this, &AZeroEscapeGameMode::HandleExitReached);
	}
}

void AZeroEscapeGameMode::CleanupRoundActors()
{
	if (IsValid(ActivePopulator))
	{
		ActivePopulator->ClearPopulation();
	}
	if (IsValid(SpawnedExit))
	{
		SpawnedExit->Destroy();
	}
	SpawnedExit = nullptr;
	if (IsValid(SpawnedPursuer))
	{
		SpawnedPursuer->Destroy();
	}
	SpawnedPursuer = nullptr;
}

void AZeroEscapeGameMode::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SetupTransitionTimer);
	}
	UnbindRuntimeDelegates();
	CleanupRoundActors();
	if (IsValid(ResultMenuWidget))
	{
		ResultMenuWidget->RemoveFromParent();
	}
	ResultMenuWidget = nullptr;
	SetGameplayInputLocked(false);
	Super::EndPlay(EndPlayReason);
}
