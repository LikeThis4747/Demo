// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeGenerationTestHarness.cpp
 * 职责：把测试关卡的 Staging、玩家 Pawn 与运行时 Generator Ready 事件安全串联。
 * 边界：不使用 Tick、不搜索名称、不强制无碰撞传送，也不修改 Generator 的事务状态机。
 * 状态 Owner：本类只拥有 Delegate/Timer/待传送标志；生成场景与报告始终属于 Generator。
 */

#include "PCG/ZeroEscapeRuntimeGenerationTestHarness.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePCGTestHarness, Log, All);

namespace
{
	/** 关卡启动时 PlayerController/Pawn 的最坏等待窗口为 5 秒；超时后明确失败而不是无限轮询。 */
	constexpr float PlayerDiscoveryIntervalSeconds = 0.1f;
	constexpr int32 MaxPlayerDiscoveryAttempts = 50;

	/** 传送目标仍属于位置契约，拒绝 NaN/非归一化旋转与非 Unit Scale。 */
	bool IsValidTransferTransform(const FTransform& Transform)
	{
		return !Transform.ContainsNaN()
			&& Transform.GetRotation().IsNormalized()
			&& Transform.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER);
	}
}

AZeroEscapeRuntimeGenerationTestHarness::AZeroEscapeRuntimeGenerationTestHarness()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void AZeroEscapeRuntimeGenerationTestHarness::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(Generator) || Generator->GetWorld() != GetWorld())
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=BeginPlay Result=Failure Reason=InvalidGenerator"));
		return;
	}

	// 先监听再读取状态，避免 Generator 恰在两步之间完成而丢失 Ready 事件。
	Generator->OnGenerationFinished.AddUniqueDynamic(
		this,
		&AZeroEscapeRuntimeGenerationTestHarness::HandleGenerationFinished);
	BindPlayerControllerIfAvailable();

	switch (Generator->State)
	{
	case EZeroEscapeRuntimeGenerationState::Ready:
		// Generator BeginPlay 先执行时不会再广播给后来者；由当前终态补做一次传送。
		QueueTransferToGeneratedStart();
		break;

	case EZeroEscapeRuntimeGenerationState::Idle:
		if (bGenerateIfIdleOnBeginPlay && !Generator->Generate())
		{
			UE_LOG(
				LogZeroEscapePCGTestHarness,
				Error,
				TEXT("ZE_PCG_HARNESS Schema=1 Event=InitialGenerate Result=Failure Reason=RequestRejected"));
		}
		break;

	case EZeroEscapeRuntimeGenerationState::Failed:
		// 失败是需要修配置/Seed 的真实结果；Harness 不擅自无限重试，也不把玩家移出 Staging。
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=BeginPlay Result=Failure Reason=GeneratorAlreadyFailed Stage=%d Failure=%d Message=\"%s\""),
			static_cast<int32>(Generator->LastReport.Stage),
			static_cast<int32>(Generator->LastReport.Failure),
			*Generator->LastReport.Message.Replace(TEXT("\n"), TEXT(" ")));
		break;

	default:
		// Planning/Validating/Instantiating 已在进行，只等待绑定好的完成事件。
		break;
	}
}

void AZeroEscapeRuntimeGenerationTestHarness::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	bPendingGeneratedStartTransfer = false;
	ClearPlayerDiscoveryTimer();

	if (IsValid(Generator))
	{
		Generator->OnGenerationFinished.RemoveDynamic(
			this,
			&AZeroEscapeRuntimeGenerationTestHarness::HandleGenerationFinished);
	}
	if (IsValid(BoundPlayerController))
	{
		BoundPlayerController->OnPossessedPawnChanged.RemoveDynamic(
			this,
			&AZeroEscapeRuntimeGenerationTestHarness::HandlePossessedPawnChanged);
	}
	BoundPlayerController = nullptr;

	Super::EndPlay(EndPlayReason);
}

bool AZeroEscapeRuntimeGenerationTestHarness::Regenerate()
{
	if (!PrepareForRegeneration())
	{
		return false;
	}

	// GenerateFromRequest 内部已经先清旧场景再事务式提交新场景；这里不能提前 Clear，
	// 否则请求被拒绝时会无谓地让测试关卡失去仍然可用的旧结构。
	const bool bAcceptedAndSucceeded = Generator->Generate();
	if (!bAcceptedAndSucceeded)
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=Regenerate Result=Failure Reason=GenerationFailedOrRejected"));
	}
	return bAcceptedAndSucceeded;
}

bool AZeroEscapeRuntimeGenerationTestHarness::RegenerateFromRequest(
	const FZeroEscapeGenerationRequest& Request)
{
	if (!PrepareForRegeneration())
	{
		return false;
	}

	const bool bAcceptedAndSucceeded = Generator->GenerateFromRequest(Request);
	if (!bAcceptedAndSucceeded)
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=RegenerateFromRequest Result=Failure Reason=GenerationFailedOrRejected Seed=%d"),
			Request.Seed);
	}
	return bAcceptedAndSucceeded;
}

void AZeroEscapeRuntimeGenerationTestHarness::HandleGenerationFinished(
	const bool bSuccess,
	const FZeroEscapeGenerationReport& Report)
{
	if (bEndingPlay)
	{
		return;
	}

	if (!bSuccess)
	{
		bPendingGeneratedStartTransfer = false;
		ClearPlayerDiscoveryTimer();
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=GenerationFinished Result=Failure Stage=%d Failure=%d Message=\"%s\""),
			static_cast<int32>(Report.Stage),
			static_cast<int32>(Report.Failure),
			*Report.Message.Replace(TEXT("\n"), TEXT(" ")));
		return;
	}

	// Generator 广播前已经提交 State=Ready 与 LastPlan；回调中只查询和传送，不递归 Generate/Clear。
	QueueTransferToGeneratedStart();
}

void AZeroEscapeRuntimeGenerationTestHarness::HandlePossessedPawnChanged(
	APawn* OldPawn,
	APawn* NewPawn)
{
	(void)OldPawn;
	if (!bEndingPlay && bPendingGeneratedStartTransfer && IsValid(NewPawn))
	{
		TryTransferToGeneratedStart();
	}
}

void AZeroEscapeRuntimeGenerationTestHarness::BindPlayerControllerIfAvailable()
{
	APlayerController* Candidate = UGameplayStatics::GetPlayerController(this, PlayerIndex);
	if (Candidate == BoundPlayerController)
	{
		return;
	}

	if (IsValid(BoundPlayerController))
	{
		BoundPlayerController->OnPossessedPawnChanged.RemoveDynamic(
			this,
			&AZeroEscapeRuntimeGenerationTestHarness::HandlePossessedPawnChanged);
	}

	BoundPlayerController = Candidate;
	if (IsValid(BoundPlayerController))
	{
		BoundPlayerController->OnPossessedPawnChanged.AddUniqueDynamic(
			this,
			&AZeroEscapeRuntimeGenerationTestHarness::HandlePossessedPawnChanged);
	}
}

void AZeroEscapeRuntimeGenerationTestHarness::QueueTransferToGeneratedStart()
{
	if (bEndingPlay)
	{
		return;
	}

	bPendingGeneratedStartTransfer = true;
	PlayerDiscoveryAttempts = 0;
	TryTransferToGeneratedStart();

	if (bPendingGeneratedStartTransfer && !PlayerDiscoveryTimerHandle.IsValid())
	{
		GetWorldTimerManager().SetTimer(
			PlayerDiscoveryTimerHandle,
			this,
			&AZeroEscapeRuntimeGenerationTestHarness::HandlePlayerDiscoveryTimer,
			PlayerDiscoveryIntervalSeconds,
			true);
	}
}

void AZeroEscapeRuntimeGenerationTestHarness::TryTransferToGeneratedStart()
{
	if (bEndingPlay || !bPendingGeneratedStartTransfer)
	{
		return;
	}

	++PlayerDiscoveryAttempts;
	BindPlayerControllerIfAvailable();
	APawn* Pawn = IsValid(BoundPlayerController) ? BoundPlayerController->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		if (PlayerDiscoveryAttempts >= MaxPlayerDiscoveryAttempts)
		{
			bPendingGeneratedStartTransfer = false;
			ClearPlayerDiscoveryTimer();
			UE_LOG(
				LogZeroEscapePCGTestHarness,
				Error,
				TEXT("ZE_PCG_HARNESS Schema=1 Event=TransferToGeneratedStart Result=Failure Reason=PlayerPawnTimeout Attempts=%d"),
				PlayerDiscoveryAttempts);
		}
		return;
	}

	FTransform StartWorldTransform = FTransform::Identity;
	if (!IsValid(Generator) || !Generator->GetGeneratedStartWorldTransform(StartWorldTransform))
	{
		bPendingGeneratedStartTransfer = false;
		ClearPlayerDiscoveryTimer();
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=TransferToGeneratedStart Result=Failure Reason=StartAnchorUnavailable"));
		return;
	}

	const bool bMoved = MoveCurrentPlayerTo(StartWorldTransform, TEXT("GeneratedStart"));
	bPendingGeneratedStartTransfer = false;
	ClearPlayerDiscoveryTimer();
	// UE_LOG 的 Verbosity 是编译期宏参数，成功与失败必须写成两个显式分支。
	if (bMoved)
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Display,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=TransferToGeneratedStart Result=Success Attempts=%d Target=(%.2f,%.2f,%.2f)"),
			PlayerDiscoveryAttempts,
			StartWorldTransform.GetLocation().X,
			StartWorldTransform.GetLocation().Y,
			StartWorldTransform.GetLocation().Z);
	}
	else
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=TransferToGeneratedStart Result=Failure Attempts=%d Target=(%.2f,%.2f,%.2f)"),
			PlayerDiscoveryAttempts,
			StartWorldTransform.GetLocation().X,
			StartWorldTransform.GetLocation().Y,
			StartWorldTransform.GetLocation().Z);
	}
}

void AZeroEscapeRuntimeGenerationTestHarness::HandlePlayerDiscoveryTimer()
{
	TryTransferToGeneratedStart();
}

void AZeroEscapeRuntimeGenerationTestHarness::ClearPlayerDiscoveryTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerDiscoveryTimerHandle);
	}
	PlayerDiscoveryTimerHandle.Invalidate();
}

bool AZeroEscapeRuntimeGenerationTestHarness::PrepareForRegeneration()
{
	if (bEndingPlay
		|| !IsValid(Generator)
		|| Generator->GetWorld() != GetWorld()
		|| !IsValid(StagingAnchor)
		|| StagingAnchor->GetWorld() != GetWorld())
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=PrepareForRegeneration Result=Failure Reason=InvalidDependency"));
		return false;
	}

	const bool bStateDescribesBusyWork =
		Generator->State == EZeroEscapeRuntimeGenerationState::Planning
		|| Generator->State == EZeroEscapeRuntimeGenerationState::Validating
		|| Generator->State == EZeroEscapeRuntimeGenerationState::Instantiating;
	// 不能只看公开 State：FinishGeneration 广播前已经提交 Ready/Failed，但广播期间重入锁仍为 true。
	// 必须在移动玩家之前确认真正入口会接纳请求，否则旧地图仍在而玩家会被错误送回 Staging。
	if (!Generator->CanAcceptGenerationRequest() || bStateDescribesBusyWork)
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Warning,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=PrepareForRegeneration Result=Failure Reason=GeneratorCannotAccept State=%d"),
			static_cast<int32>(Generator->State));
		return false;
	}

	bPendingGeneratedStartTransfer = false;
	ClearPlayerDiscoveryTimer();
	BindPlayerControllerIfAvailable();
	return MoveCurrentPlayerTo(StagingAnchor->GetActorTransform(), TEXT("Staging"));
}

bool AZeroEscapeRuntimeGenerationTestHarness::MoveCurrentPlayerTo(
	const FTransform& TargetTransform,
	const TCHAR* MoveReason)
{
	if (!IsValidTransferTransform(TargetTransform))
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=Teleport Result=Failure Reason=InvalidTarget MoveReason=%s"),
			MoveReason);
		return false;
	}

	BindPlayerControllerIfAvailable();
	APawn* Pawn = IsValid(BoundPlayerController) ? BoundPlayerController->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=Teleport Result=Failure Reason=MissingPawn MoveReason=%s"),
			MoveReason);
		return false;
	}

	if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}

	// 长距离 Sweep 会被 Staging 与地图之间的墙阻断；TeleportTo(false, false) 直接检查目标处
	// 碰撞并允许 UE 寻找邻近合法位置。失败时 Pawn 保持原地，绝不以 bNoCheck=true 掩盖坏 Anchor。
	const FRotator TargetRotation = TargetTransform.GetRotation().Rotator();
	const bool bMoved = Pawn->TeleportTo(
		TargetTransform.GetLocation(),
		TargetRotation,
		false,
		false);
	if (bMoved && IsValid(BoundPlayerController))
	{
		BoundPlayerController->SetControlRotation(TargetRotation);
	}

	if (bMoved)
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Display,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=Teleport Result=Success MoveReason=%s Pawn=\"%s\" Actual=(%.2f,%.2f,%.2f)"),
			MoveReason,
			*GetNameSafe(Pawn),
			Pawn->GetActorLocation().X,
			Pawn->GetActorLocation().Y,
			Pawn->GetActorLocation().Z);
	}
	else
	{
		UE_LOG(
			LogZeroEscapePCGTestHarness,
			Error,
			TEXT("ZE_PCG_HARNESS Schema=1 Event=Teleport Result=Failure MoveReason=%s Pawn=\"%s\" Actual=(%.2f,%.2f,%.2f)"),
			MoveReason,
			*GetNameSafe(Pawn),
			Pawn->GetActorLocation().X,
			Pawn->GetActorLocation().Y,
			Pawn->GetActorLocation().Z);
	}
	return bMoved;
}
