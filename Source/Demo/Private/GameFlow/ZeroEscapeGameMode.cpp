// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameMode.cpp
 * 职责：把正式 PCG、导航、玩家、追猎者、Exit 与 Population 原子编排成一局。
 * 边界：生成失败只做清理并返回主菜单；不得改写公开 Seed 或重载游戏关卡重试。
 */

#include "GameFlow/ZeroEscapeGameMode.h"

#include "Camera/CameraActor.h"
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
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	// Keep the PCG cell's X/Y and lower the portal onto the floor. The generator
	// already orients the transform from the exit cell's corridor opening.
	ExitTransform.AddToTranslation(FVector(0.0f, 0.0f, -71.0f));

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
	DestroyIntroCameras();
	CleanupRoundActors();
	if (IsValid(ResultMenuWidget))
	{
		ResultMenuWidget->RemoveFromParent();
	}
	ResultMenuWidget = nullptr;
	SetGameplayInputLocked(false);
	Super::EndPlay(EndPlayReason);
}

/** 开局运镜：冻结双方移动后，出口→追猎者→玩家（均硬切静止 1s），落地后再隔 1s 解锁。 */
void AZeroEscapeGameMode::PlayIntroSequence()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController) || !IsValid(SpawnedExit) || !IsValid(SpawnedPursuer))
	{
		SetGameplayInputLocked(false);
		return;
	}

	bIntroSequencePlaying = true;
	SetRoundFrozen(true);
	ShowExitView();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			IntroExitViewTimer, this, &AZeroEscapeGameMode::ShowPursuerView, 1.0f, false);
	}
}

/** 冻结/解冻玩家与追猎者的移动（运镜期间防止玩家被攻击或 AI 提前逼近）。 */
void AZeroEscapeGameMode::SetRoundFrozen(const bool bFrozen)
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (ACharacter* PlayerCharacter = Cast<ACharacter>(PlayerPawn))
	{
		if (bFrozen)
		{
			PlayerCharacter->GetCharacterMovement()->DisableMovement();
		}
		else
		{
			PlayerCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
	if (IsValid(SpawnedPursuer))
	{
		if (bFrozen)
		{
			SpawnedPursuer->GetCharacterMovement()->DisableMovement();
		}
		else
		{
			SpawnedPursuer->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
}

/** 生成一台看向目标的临时运镜相机。
 *  在"目标→参考点(玩家)"方向附近按角度与距离做探测：
 *  每个候选点先验证"看向目标胸口"的视线无遮挡，且相机背后不贴墙；
 *  全部失败时沿目标→玩家连线逐步向玩家侧收敛，兜底保证不穿墙、不黑屏。 */
ACameraActor* AZeroEscapeGameMode::SpawnIntroCamera(
	const FVector& TargetLocation,
	const FVector& ReferenceLocation,
	const float CameraHeightOffset,
	const float LookAtHeightOffset)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IntroCameraTrace), false);
	const FVector LookTarget = TargetLocation + FVector(0.0f, 0.0f, LookAtHeightOffset);

	FVector BaseDir = ReferenceLocation - TargetLocation;
	BaseDir.Z = 0.0f;
	if (!BaseDir.Normalize())
	{
		BaseDir = FVector::ForwardVector;
	}

	// 视线是否通畅：从候选点到目标胸口无遮挡才算有效机位。
	const auto bHasClearView = [World, &QueryParams](const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		return !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, QueryParams);
	};
	// 相机背后是否贴墙：向相机后方短距探测，有阻挡则该点太贴近障碍。
	const auto bBackIsFree = [World, &QueryParams](const FVector& Pos, const FVector& BackDir)
	{
		FHitResult Hit;
		return !World->LineTraceSingleByChannel(Hit, Pos, Pos + BackDir * 80.0f, ECC_Visibility, QueryParams);
	};

	static const float AnglesDeg[] = { 0.0f, 25.0f, -25.0f, 50.0f, -50.0f };
	static const float Distances[] = { 400.0f, 300.0f, 200.0f };

	FVector Chosen = FVector::ZeroVector;
	bool bFound = false;
	for (const float Distance : Distances)
	{
		for (const float AngleDeg : AnglesDeg)
		{
			const FVector Dir = BaseDir.RotateAngleAxis(AngleDeg, FVector::UpVector);
			const FVector Candidate = LookTarget + Dir * Distance + FVector(0.0f, 0.0f, CameraHeightOffset - LookAtHeightOffset);
			if (bHasClearView(Candidate, LookTarget) && bBackIsFree(Candidate, Dir))
			{
				Chosen = Candidate;
				bFound = true;
				break;
			}
		}
		if (bFound)
		{
			break;
		}
	}

	if (!bFound)
	{
		// 兜底：沿目标→玩家连线从近到远找第一个视线通畅的点。
		for (const float Distance : { 150.0f, 250.0f, 350.0f, 500.0f, 700.0f })
		{
			const FVector Candidate = LookTarget + BaseDir * Distance + FVector(0.0f, 0.0f, CameraHeightOffset - LookAtHeightOffset);
			if (bHasClearView(Candidate, LookTarget))
			{
				Chosen = Candidate;
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		// 极端兜底：直接放在目标与玩家中间，至少不穿墙。
		Chosen = (LookTarget + ReferenceLocation) * 0.5f;
	}

	ACameraActor* CameraActor = World->SpawnActor<ACameraActor>(Chosen, FRotator::ZeroRotator);
	if (IsValid(CameraActor))
	{
		CameraActor->SetActorRotation((LookTarget - Chosen).Rotation());
	}
	return CameraActor;
}

/** 销毁开局运镜生成的临时相机。 */
void AZeroEscapeGameMode::DestroyIntroCameras()
{
	if (IntroExitCamera.IsValid())
	{
		IntroExitCamera->Destroy();
	}
	if (IntroPursuerCamera.IsValid())
	{
		IntroPursuerCamera->Destroy();
	}
	IntroExitCamera.Reset();
	IntroPursuerCamera.Reset();
}

/** 运镜第一步：在出口与玩家之间生成相机看向出口，硬切静止 1s。 */
void AZeroEscapeGameMode::ShowExitView()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerController) || !IsValid(SpawnedExit) || !IsValid(PlayerPawn))
	{
		ShowPlayerViewAndUnlock();
		return;
	}

	// 出口平视：相机与看点同高，正对传送门。
	IntroExitCamera = SpawnIntroCamera(
		SpawnedExit->GetActorLocation(), PlayerPawn->GetActorLocation(), 120.0f, 120.0f);
	if (IntroExitCamera.IsValid())
	{
		PlayerController->SetViewTargetWithBlend(IntroExitCamera.Get(), 0.0f);
	}
}

/** 运镜第二步：在追猎者与玩家之间生成相机看向追猎者，硬切静止 1s。 */
void AZeroEscapeGameMode::ShowPursuerView()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerController) || !IsValid(SpawnedPursuer) || !IsValid(PlayerPawn))
	{
		ShowPlayerViewAndUnlock();
		return;
	}

	// 追猎者近平视：相机与胸口基本同高，避免俯视穿层。
	IntroPursuerCamera = SpawnIntroCamera(
		SpawnedPursuer->GetActorLocation(), PlayerPawn->GetActorLocation(), 140.0f, 115.0f);
	if (IntroPursuerCamera.IsValid())
	{
		PlayerController->SetViewTargetWithBlend(IntroPursuerCamera.Get(), 0.0f);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			IntroPursuerViewTimer, this, &AZeroEscapeGameMode::ShowPlayerViewAndUnlock, 1.0f, false);
	}
}

/** 运镜第三步：硬切回玩家，静止 1s 后才解锁移动。 */
void AZeroEscapeGameMode::ShowPlayerViewAndUnlock()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (IsValid(PlayerController) && IsValid(PlayerPawn))
	{
		PlayerController->SetViewTargetWithBlend(PlayerPawn, 0.0f);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			IntroPlayerViewTimer, this, &AZeroEscapeGameMode::UnlockGameplay, 1.0f, false);
	}
	else
	{
		UnlockGameplay();
	}
}

/** 运镜结束：恢复移动并开放输入（此处预留"可以开始动了"的 UI 提示钩子）。 */
void AZeroEscapeGameMode::UnlockGameplay()
{
	bIntroSequencePlaying = false;
	SetRoundFrozen(false);
	SetGameplayInputLocked(false);
	DestroyIntroCameras();
}
