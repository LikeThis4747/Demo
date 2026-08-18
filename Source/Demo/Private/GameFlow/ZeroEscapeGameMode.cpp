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
#include "Components/Magnetism/ElectromagneticGrabComponent.h"
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

bool FZeroEscapePlayerHealthTuning::IsConfigured(FString& OutError) const
{
	const bool bValid = FMath::IsFinite(EasyMaxHealth) && EasyMaxHealth > 0.0f
		&& FMath::IsFinite(NormalMaxHealth) && NormalMaxHealth > 0.0f
		&& FMath::IsFinite(HardMaxHealth) && HardMaxHealth > 0.0f
		&& FMath::IsFinite(DebugMaxHealth) && DebugMaxHealth > 0.0f;
	if (!bValid)
	{
		OutError = TEXT("All player health values must be finite and positive.");
		return false;
	}

	OutError.Reset();
	return true;
}

float FZeroEscapePlayerHealthTuning::Resolve(
	const EZeroEscapeDifficulty Difficulty) const
{
	if (bUseDebugOverride)
	{
		return DebugMaxHealth;
	}

	switch (Difficulty)
	{
	case EZeroEscapeDifficulty::Easy:
		return EasyMaxHealth;
	case EZeroEscapeDifficulty::Hard:
		return HardMaxHealth;
	case EZeroEscapeDifficulty::Normal:
	default:
		return NormalMaxHealth;
	}
}

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

	FString PlayerHealthError;
	if (!PlayerHealthTuning.IsConfigured(PlayerHealthError))
	{
		UE_LOG(LogZeroEscapeGameMode, Error,
			TEXT("ZE_GAME_SETUP result=Failure reason=PlayerHealthInvalid detail=%s"),
			*PlayerHealthError);
		AbortSetupAndReturnToMainMenu(TEXT("PlayerHealthInvalid"));
		return;
	}

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
	ActiveDifficulty = GameInstancePtr != nullptr
		? GameInstancePtr->GetPendingRequest().Difficulty
		: EZeroEscapeDifficulty::Normal;
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
	if (!InitializePlayerHealth())
	{
		AbortSetupAndReturnToMainMenu(TEXT("PlayerHealthInitializationFailed"));
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
	if (!BoundRoundState->InitializeEnergyOrbObjective(
			ActivePopulator->GetLastSpawnedEnergyOrbCount(),
			ActivePopulator->GetLastRequiredEnergyOrbCollectionRatio()))
	{
		AbortSetupAndReturnToMainMenu(TEXT("EnergyOrbObjectiveInvalid"));
		return;
	}
	if (BoundRoundState->IsEnergyOrbRequirementMet() && IsValid(SpawnedExit))
	{
		SpawnedExit->SetEnergyOrbRequirementMet();
	}

	ActiveGenerator->OnGenerationFinished.RemoveDynamic(
		this, &AZeroEscapeGameMode::HandleGenerationFinished);
	bRoundStarted = true;
	bSetupTerminal = true;

	// 开局运镜：出口→追猎者→玩家，最后开放输入；失败则直接开放输入。
	PlayIntroSequence();

	UE_LOG(LogZeroEscapeGameMode, Display,
		TEXT("ZE_GAME_SETUP result=Success operation=%lld player=%s pursuer=%s exit=%s energy_orbs=%d required_orbs=%d"),
		static_cast<long long>(Report.OperationId),
		*GetNameSafe(UGameplayStatics::GetPlayerPawn(this, 0)),
		*GetNameSafe(SpawnedPursuer),
		*GetNameSafe(SpawnedExit),
		BoundRoundState->GetTotalEnergyOrbCount(),
		BoundRoundState->GetRequiredEnergyOrbCount());
}

bool AZeroEscapeGameMode::TryCollectEnergyOrb(APawn& PlayerPawn)
{
	if (!bRoundStarted
		|| !IsValid(BoundRoundState)
		|| &PlayerPawn != UGameplayStatics::GetPlayerPawn(this, 0)
		|| !BoundRoundState->TryCollectEnergyOrb())
	{
		return false;
	}

	UElectromagneticGrabComponent* MagneticGrab =
		PlayerPawn.FindComponentByClass<UElectromagneticGrabComponent>();
	const int32 AddedCharges = IsValid(MagneticGrab)
		? MagneticGrab->TryAddExplosionCharges(1)
		: 0;
	if (BoundRoundState->IsEnergyOrbRequirementMet() && IsValid(SpawnedExit))
	{
		SpawnedExit->SetEnergyOrbRequirementMet();
	}
	UE_LOG(LogZeroEscapeGameMode, Display,
		TEXT("ZE_ENERGY_PICKUP player=%s charge_added=%d charges=%d/%d collected=%d/%d"),
		*GetNameSafe(&PlayerPawn),
		AddedCharges,
		IsValid(MagneticGrab) ? MagneticGrab->GetAvailableExplosionCharges() : 0,
		IsValid(MagneticGrab) ? MagneticGrab->GetMaximumExplosionCharges() : 0,
		BoundRoundState->GetCollectedEnergyOrbCount(),
		BoundRoundState->GetTotalEnergyOrbCount());
	return true;
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

bool AZeroEscapeGameMode::InitializePlayerHealth()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	UHealthComponent* Health = IsValid(Player)
		? Player->FindComponentByClass<UHealthComponent>()
		: nullptr;
	const float ResolvedMaxHealth = PlayerHealthTuning.Resolve(ActiveDifficulty);
	if (!IsValid(Health) || !Health->InitializeForRound(ResolvedMaxHealth))
	{
		return false;
	}

	UE_LOG(LogZeroEscapeGameMode, Display,
		TEXT("ZE_PLAYER_HEALTH difficulty=%d max=%.1f debug_override=%s"),
		static_cast<int32>(ActiveDifficulty),
		ResolvedMaxHealth,
		PlayerHealthTuning.bUseDebugOverride ? TEXT("true") : TEXT("false"));
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
		if (!BoundRoundState->IsEnergyOrbRequirementMet())
		{
			UE_LOG(LogZeroEscapeGameMode, Display,
				TEXT("ZE_EXIT_LOCKED collected=%d required=%d total=%d"),
				BoundRoundState->GetCollectedEnergyOrbCount(),
				BoundRoundState->GetRequiredEnergyOrbCount(),
				BoundRoundState->GetTotalEnergyOrbCount());
			return;
		}
		if (IsValid(SpawnedExit))
		{
			SpawnedExit->ConfirmReached();
		}
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
		World->GetTimerManager().ClearTimer(IntroExitViewTimer);
		World->GetTimerManager().ClearTimer(IntroPursuerViewTimer);
		World->GetTimerManager().ClearTimer(IntroPlayerViewTimer);
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

/** 开局运镜：出口→追猎者→玩家，最后开放输入；任何一步失败则直接开放输入。 */
void AZeroEscapeGameMode::PlayIntroSequence()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController) || !IsValid(SpawnedExit) || !IsValid(SpawnedPursuer))
	{
		SetGameplayInputLocked(false);
		return;
	}

	bIntroSequencePlaying = true;
	ShowExitView();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			IntroExitViewTimer, this, &AZeroEscapeGameMode::ShowPursuerView, 1.0f, false);
	}
}

/** 运镜第一步：切到出口。 */
void AZeroEscapeGameMode::ShowExitView()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController) || !IsValid(SpawnedExit))
	{
		ShowPlayerViewAndUnlock();
		return;
	}
	PlayerController->SetViewTargetWithBlend(SpawnedExit, 1.0f);
}

/** 运镜第二步：切到追猎者。 */
void AZeroEscapeGameMode::ShowPursuerView()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController) || !IsValid(SpawnedPursuer))
	{
		ShowPlayerViewAndUnlock();
		return;
	}
	PlayerController->SetViewTargetWithBlend(SpawnedPursuer, 1.0f);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			IntroPursuerViewTimer, this, &AZeroEscapeGameMode::ShowPlayerViewAndUnlock, 1.0f, false);
	}
}

/** 运镜第三步：切回玩家并开放输入。 */
void AZeroEscapeGameMode::ShowPlayerViewAndUnlock()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (IsValid(PlayerController) && IsValid(PlayerPawn))
	{
		PlayerController->SetViewTargetWithBlend(PlayerPawn, 0.5f);
	}

	bIntroSequencePlaying = false;
	SetGameplayInputLocked(false);
}
