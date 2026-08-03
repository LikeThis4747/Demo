// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeLevelGenerator.cpp
 * 职责：同步求解/装配多层结构，随后用事件和单个超时 Timer 等待并验收正式导航。
 * 边界：不使用 Tick、不强制全局导航 Build、不自动更换 Seed，也不创建一次性导航探针。
 */

#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavigationSystemTypes.h"
#include "PCG/Layout/ZeroEscapeMultiFloorLayoutPlanner.h"
#include "PCG/Presentation/ZeroEscapeStructureBuilder.h"
#include "PCG/ZeroEscapeGenerationAssets.h"
#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeRuntimeNavigationGate.h"
#include "UObject/ObjectKey.h"

namespace LevelGen = ZeroEscape::LevelGeneration;

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePCG, Log, All);

namespace
{
	template <typename TEnum>
	FString GetStableEnumName(const TEnum Value)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		const FString Name = Enum != nullptr
			? Enum->GetNameStringByValue(static_cast<int64>(Value))
			: FString();
		return Name.IsEmpty()
			? FString::Printf(TEXT("Unknown(%lld)"), static_cast<long long>(Value))
			: Name;
	}

	FString MakeSingleLineLogValue(const FString& Value)
	{
		FString Result = Value;
		Result.ReplaceInline(TEXT("\r"), TEXT(" "));
		Result.ReplaceInline(TEXT("\n"), TEXT(" "));
		Result.ReplaceInline(TEXT("\t"), TEXT(" "));
		Result.ReplaceInline(TEXT("\""), TEXT("'"));
		return Result;
	}

	bool ConvertLocalToWorld(
		const USceneComponent& Root,
		const FTransform& LocalTransform,
		FTransform& OutWorldTransform)
	{
		OutWorldTransform = FTransform::Identity;
		if (!LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(LocalTransform))
		{
			return false;
		}
		OutWorldTransform = LocalTransform * Root.GetComponentTransform();
		return LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(OutWorldTransform);
	}

	bool IsWithinOneStepOnSameFloor(const FIntVector A, const FIntVector B)
	{
		return A.Z == B.Z
			&& FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y) <= 1;
	}

	bool IsAddressInsidePlan(
		const FIntVector Address,
		const FZeroEscapeGeneratedLevelPlan& Plan)
	{
		return Address.Z >= 0
			&& Address.Z < Plan.FloorCount
			&& ZeroEscape::Grid::IsInside(FIntPoint(Address.X, Address.Y), Plan.GridSize);
	}

	bool FailReport(
		FZeroEscapeGenerationReport& Report,
		const EZeroEscapeGenerationStage Stage,
		const EZeroEscapeGenerationFailure Failure,
		const int32 RelatedStableId,
		FString Message)
	{
		Report.Stage = Stage;
		Report.Failure = Failure;
		Report.RelatedStableId = RelatedStableId;
		Report.Message = MoveTemp(Message);
		return false;
	}

	const FZeroEscapeGeneratedStructure* FindStructureByStableId(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const int32 StableStructureId)
	{
		return Plan.Structures.FindByPredicate(
			[StableStructureId](const FZeroEscapeGeneratedStructure& Structure)
			{
				return Structure.StableStructureId == StableStructureId;
			});
	}
}

AZeroEscapeRuntimeLevelGenerator::AZeroEscapeRuntimeLevelGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	GeneratedRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GeneratedRoot"));
	GeneratedRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(GeneratedRoot);
}

void AZeroEscapeRuntimeLevelGenerator::BeginPlay()
{
	Super::BeginPlay();
	if (TriggerMode == EZeroEscapeGenerationTrigger::BeginPlay
		&& State == EZeroEscapeRuntimeGenerationState::Idle)
	{
		Generate();
	}
}

bool AZeroEscapeRuntimeLevelGenerator::ConfigurePursuerNavigationAgent(
	const FNavAgentProperties& AgentProperties)
{
	if (!IsInGameThread()
		|| bGenerationInProgress
		|| bNavigationWaitActive
		|| bEndingPlay
		|| !AgentProperties.IsValid())
	{
		return false;
	}
	PursuerNavigationAgentProperties = AgentProperties;
	bHasPursuerNavigationAgent = true;
	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::Generate()
{
	return GenerateFromRequest(DefaultRequest);
}

bool AZeroEscapeRuntimeLevelGenerator::CanAcceptGenerationRequest() const
{
	if (!IsInGameThread())
	{
		return false;
	}
	const UWorld* World = GetWorld();
	return World != nullptr
		&& World->IsGameWorld()
		&& !IsRunningUserConstructionScript()
		&& !bGenerationInProgress
		&& !bNavigationWaitActive
		&& !bEndingPlay;
}

bool AZeroEscapeRuntimeLevelGenerator::GenerateFromRequest(
	const FZeroEscapeGenerationRequest& Request)
{
	if (!CanAcceptGenerationRequest())
	{
		return false;
	}

	TGuardValue<bool> GenerationGuard(bGenerationInProgress, true);
	ClearGeneratedSceneInternal();
	if (NextOperationId <= 0)
	{
		NextOperationId = 1;
	}
	ActiveOperationId = NextOperationId++;
	ActiveRequest = Request;
	ActiveGenerationStartSeconds = FPlatformTime::Seconds();
	LastReport = FZeroEscapeGenerationReport();
	State = EZeroEscapeRuntimeGenerationState::Planning;

	FZeroEscapeGenerationReport Report;
	Report.OperationId = ActiveOperationId;
	if (!IsValid(GeneratedRoot)
		|| !GeneratedRoot->IsRegistered()
		|| !LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(
			GeneratedRoot->GetComponentTransform())
		|| !IsValid(GenerationProfile)
		|| !IsValid(PresentationProfile)
		|| !bHasPursuerNavigationAgent)
	{
		FailReport(
			Report,
			EZeroEscapeGenerationStage::Configuration,
			EZeroEscapeGenerationFailure::InvalidConfiguration,
			INDEX_NONE,
			TEXT("Generator Root、两份 Profile 与追猎者导航代理必须完整有效。"));
		FinishGeneration(false, Report);
		return true;
	}

	const double PlanningStartSeconds = FPlatformTime::Seconds();
	LevelGen::FResolvedGenerationInput Input;
	if (!LevelGen::FGenerationCore::ResolveGenerationInput(
			*GenerationProfile,
			Request,
			PresentationProfile->PresentationVersion,
			Input,
			Report))
	{
		Report.OperationId = ActiveOperationId;
		Report.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;
		FinishGeneration(false, Report);
		return true;
	}
	Report.OperationId = ActiveOperationId;

	FString PresentationError;
	if (!PresentationProfile->IsConfigured(
			Input.SharedRules.LogicalTileSizeCm,
			PresentationError))
	{
		FailReport(
			Report,
			EZeroEscapeGenerationStage::Configuration,
			EZeroEscapeGenerationFailure::InvalidConfiguration,
			INDEX_NONE,
			MoveTemp(PresentationError));
		Report.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;
		FinishGeneration(false, Report);
		return true;
	}
	if (!ValidateZeroEscapeStructurePresentationBindings(
			*GenerationProfile, *PresentationProfile, PresentationError))
	{
		FailReport(
			Report,
			EZeroEscapeGenerationStage::Configuration,
			EZeroEscapeGenerationFailure::InvalidConfiguration,
			INDEX_NONE,
			MoveTemp(PresentationError));
		Report.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;
		FinishGeneration(false, Report);
		return true;
	}

	FZeroEscapeGeneratedLevelPlan CandidatePlan;
	if (!LevelGen::FMultiFloorLayoutPlanner::Solve(Input, CandidatePlan, Report))
	{
		Report.OperationId = ActiveOperationId;
		Report.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;
		FinishGeneration(false, Report);
		return true;
	}
	Report.OperationId = ActiveOperationId;
	Report.Metrics.PlanningMilliseconds =
		(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;

	State = EZeroEscapeRuntimeGenerationState::Validating;
	const int64 RecomputedLayoutHash =
		LevelGen::FGenerationCore::ComputeCanonicalLayoutHash(CandidatePlan);
	if (!(CandidatePlan.Signature == Input.Signature)
		|| CandidatePlan.CanonicalLayoutHash == 0
		|| CandidatePlan.CanonicalLayoutHash != RecomputedLayoutHash)
	{
		FailReport(
			Report,
			EZeroEscapeGenerationStage::GlobalValidation,
			EZeroEscapeGenerationFailure::SolverInvariantViolation,
			INDEX_NONE,
			TEXT("多层 Planner 输出签名或规范 Hash 与本次请求不一致。"));
		FinishGeneration(false, Report);
		return true;
	}

	if (!PrepareNavigationWait(CandidatePlan, Input.Budget, Report))
	{
		CancelNavigationWait();
		FinishGeneration(false, Report);
		return true;
	}

	State = EZeroEscapeRuntimeGenerationState::Instantiating;
	bNavigationGeometryRegistrationStarted = true;
	const double InstantiationStartSeconds = FPlatformTime::Seconds();
	if (!InstantiateValidatedPlan(CandidatePlan, Report))
	{
		Report.Metrics.InstantiationMilliseconds =
			(FPlatformTime::Seconds() - InstantiationStartSeconds) * 1000.0;
		const FZeroEscapeGenerationReport FailedReport = Report;
		ClearGeneratedSceneInternal();
		FinishGeneration(false, FailedReport);
		return true;
	}
	Report.Metrics.InstantiationMilliseconds =
		(FPlatformTime::Seconds() - InstantiationStartSeconds) * 1000.0;

	LastPlan = MoveTemp(CandidatePlan);
	PendingReport = Report;
	StartNavigationWait();
	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::InstantiateValidatedPlan(
	const FZeroEscapeGeneratedLevelPlan& Plan,
	FZeroEscapeGenerationReport& InOutReport)
{
	if (!IsValid(PresentationProfile) || !IsValid(GeneratedRoot))
	{
		return FailReport(
			InOutReport,
			EZeroEscapeGenerationStage::Instantiation,
			EZeroEscapeGenerationFailure::InstantiationFailed,
			INDEX_NONE,
			TEXT("实例化时 PresentationProfile 或 GeneratedRoot 已失效。"));
	}

	LevelGen::FStructureBuildResult BuildResult;
	const bool bBuilt = LevelGen::FStructureBuilder::Build(
		*this,
		*GeneratedRoot,
		Plan,
		*PresentationProfile,
		GeneratedHismComponents,
		BuildResult);
	InOutReport.Metrics.InstancedMeshCount += BuildResult.InstancedMeshCount;
	InOutReport.Metrics.HismComponentCount += BuildResult.HismComponentCount;
	if (!bBuilt)
	{
		return FailReport(
			InOutReport,
			EZeroEscapeGenerationStage::Instantiation,
			EZeroEscapeGenerationFailure::InstantiationFailed,
			BuildResult.RelatedStableId,
			MoveTemp(BuildResult.Error));
	}
	return SpawnCeilingLights(Plan, InOutReport);
}

bool AZeroEscapeRuntimeLevelGenerator::SpawnCeilingLights(
	const FZeroEscapeGeneratedLevelPlan& Plan,
	FZeroEscapeGenerationReport& InOutReport)
{
	if (!PresentationProfile->bSpawnCeilingLights)
	{
		return true;
	}

	UWorld* World = GetWorld();
	UClass* LightActorClass = PresentationProfile->CeilingLightActorClass.Get();
	if (!IsValid(World) || !IsValid(GeneratedRoot) || !IsValid(LightActorClass))
	{
		return FailReport(
			InOutReport,
			EZeroEscapeGenerationStage::Instantiation,
			EZeroEscapeGenerationFailure::InstantiationFailed,
			INDEX_NONE,
			TEXT("生成顶灯时 World、GeneratedRoot 或灯 Actor 类无效。"));
	}

	int32 ParityCellCounts[2] = {0, 0};
	bool bFoundPlayerSpawn = false;
	for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
	{
		const int32 Parity = (Cell.Coordinate.X + Cell.Coordinate.Y) & 1;
		++ParityCellCounts[Parity];
		bFoundPlayerSpawn |= Cell.Coordinate == Plan.PlayerSpawnCoordinate;
	}
	if (!bFoundPlayerSpawn)
	{
		return FailReport(
			InOutReport,
			EZeroEscapeGenerationStage::Instantiation,
			EZeroEscapeGenerationFailure::InstantiationFailed,
			INDEX_NONE,
			TEXT("普通格中缺少 PlayerSpawnCoordinate，无法稳定选择顶灯奇偶组。"));
	}

	const int32 PlayerParity =
		(Plan.PlayerSpawnCoordinate.X + Plan.PlayerSpawnCoordinate.Y) & 1;
	int32 SelectedParity = PlayerParity;
	if (ParityCellCounts[0] < ParityCellCounts[1])
	{
		SelectedParity = 0;
	}
	else if (ParityCellCounts[1] < ParityCellCounts[0])
	{
		SelectedParity = 1;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
	{
		const int32 Parity = (Cell.Coordinate.X + Cell.Coordinate.Y) & 1;
		const bool bIsPlayerSpawn = Cell.Coordinate == Plan.PlayerSpawnCoordinate;
		if (Parity != SelectedParity && !bIsPlayerSpawn)
		{
			continue;
		}

		const FVector CellCenter(
			static_cast<double>(Cell.Coordinate.X) * Plan.LogicalTileSizeCm,
			static_cast<double>(Cell.Coordinate.Y) * Plan.LogicalTileSizeCm,
			PresentationProfile->FloorTopZCm
				+ static_cast<double>(Cell.Coordinate.Z) * Plan.FloorHeightCm
				+ PresentationProfile->CeilingPivotZCm);
		const FTransform LocalTransform =
			PresentationProfile->CeilingLightCellTransform * FTransform(CellCenter);
		FTransform WorldTransform;
		if (!ConvertLocalToWorld(*GeneratedRoot, LocalTransform, WorldTransform))
		{
			return FailReport(
				InOutReport,
				EZeroEscapeGenerationStage::Instantiation,
				EZeroEscapeGenerationFailure::InstantiationFailed,
				INDEX_NONE,
				TEXT("顶灯局部 Transform 无法转换到世界空间。"));
		}

		AActor* Light = World->SpawnActor<AActor>(
			LightActorClass, WorldTransform, SpawnParameters);
		if (!IsValid(Light))
		{
			return FailReport(
				InOutReport,
				EZeroEscapeGenerationStage::Instantiation,
				EZeroEscapeGenerationFailure::InstantiationFailed,
				INDEX_NONE,
				TEXT("生成顶灯 Actor 失败。"));
		}
		GeneratedLightActors.Add(Light);
	}
	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::PrepareNavigationWait(
	const FZeroEscapeGeneratedLevelPlan& Plan,
	const FZeroEscapeSharedGenerationBudget& Budget,
	FZeroEscapeGenerationReport& InOutReport)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!IsValid(World) || !IsValid(NavigationSystem) || !IsValid(GeneratedRoot))
	{
		return FailReport(
			InOutReport,
			EZeroEscapeGenerationStage::NavigationBuild,
			EZeroEscapeGenerationFailure::NavigationValidationFailed,
			INDEX_NONE,
			TEXT("正式 NavigationSystem 或 GeneratedRoot 不可用。"));
	}

	const FVector PursuerLocal =
		AddressToLocalLocation(Plan, Plan.PursuerSpawnCoordinate, true);
	const FVector PursuerWorld =
		GeneratedRoot->GetComponentTransform().TransformPosition(PursuerLocal);
	ANavigationData* NavigationData = NavigationSystem->GetNavDataForProps(
		PursuerNavigationAgentProperties, PursuerWorld);
	if (!IsValid(NavigationData)
		|| !NavigationData->IsA<ARecastNavMesh>()
		|| NavigationData->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic)
	{
		return FailReport(
			InOutReport,
			EZeroEscapeGenerationStage::NavigationBuild,
			EZeroEscapeGenerationFailure::NavigationValidationFailed,
			INDEX_NONE,
			TEXT("追猎者对应导航数据必须是运行时 Dynamic RecastNavMesh。"));
	}

	ExpectedNavigationData = NavigationData;
	ActiveNavigationTimeoutSeconds = Budget.NavigationBuildTimeoutSeconds;
	ActiveMaxNavigationValidationPoints = Budget.MaxNavigationValidationPoints;
	bNavigationWaitActive = true;
	bNavigationGeometryRegistrationStarted = false;
	bNavigationGeometrySubmitted = false;
	bObservedNavigationBuild = false;
	bReceivedTargetNavigationCompletion = false;
	bNavigationWaitTerminal = false;
	NavigationSystem->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(
		this,
		&AZeroEscapeRuntimeLevelGenerator::HandleNavigationGenerationFinished);
	return true;
}

void AZeroEscapeRuntimeLevelGenerator::StartNavigationWait()
{
	if (!bNavigationWaitActive || !IsValid(ExpectedNavigationData))
	{
		FZeroEscapeGenerationReport FailedReport = PendingReport;
		FailReport(
			FailedReport,
			EZeroEscapeGenerationStage::NavigationBuild,
			EZeroEscapeGenerationFailure::NavigationValidationFailed,
			INDEX_NONE,
			TEXT("导航等待状态在结构提交后失效。"));
		ClearGeneratedSceneInternal();
		FinishGeneration(false, FailedReport);
		return;
	}

	bNavigationGeometrySubmitted = true;
	State = EZeroEscapeRuntimeGenerationState::WaitingForNavigation;
	bObservedNavigationBuild |= UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this);
	GetWorldTimerManager().SetTimer(
		NavigationTimeoutTimer,
		FTimerDelegate::CreateUObject(
			this,
			&AZeroEscapeRuntimeLevelGenerator::HandleNavigationBuildTimeout,
			ActiveOperationId),
		ActiveNavigationTimeoutSeconds,
		false);
}

void AZeroEscapeRuntimeLevelGenerator::HandleNavigationGenerationFinished(
	ANavigationData* NavigationData)
{
	LevelGen::FRuntimeNavigationGateSnapshot Snapshot;
	Snapshot.OperationId = ActiveOperationId;
	Snapshot.ExpectedNavigationDataKey = FObjectKey(ExpectedNavigationData);
	Snapshot.bWaiting = bNavigationWaitActive;
	Snapshot.bGeometryRegistrationStarted = bNavigationGeometryRegistrationStarted;
	Snapshot.bGeometrySubmitted = bNavigationGeometrySubmitted;
	Snapshot.bObservedNavigationBuild = bObservedNavigationBuild;
	Snapshot.bReceivedTargetCompletion = bReceivedTargetNavigationCompletion;
	Snapshot.bTerminal = bNavigationWaitTerminal;
	const bool bAcceptedTargetCompletion =
		LevelGen::FRuntimeNavigationGate::AcceptCompletion(
			Snapshot, ActiveOperationId, FObjectKey(NavigationData));
	if (bAcceptedTargetCompletion)
	{
		// 只有目标 RecastNavMesh 的事件能置位；其他 NavData 只唤醒静止复核。
		bObservedNavigationBuild = true;
		bReceivedTargetNavigationCompletion = true;
	}

	Snapshot.bObservedNavigationBuild = bObservedNavigationBuild;
	Snapshot.bReceivedTargetCompletion = bReceivedTargetNavigationCompletion;
	if (bAcceptedTargetCompletion
		|| LevelGen::FRuntimeNavigationGate::ShouldRetryAfterAnyCompletion(
			Snapshot, ActiveOperationId))
	{
		TryCompleteNavigationWait(ActiveOperationId);
	}
}

void AZeroEscapeRuntimeLevelGenerator::TryCompleteNavigationWait(
	const int64 CallbackOperationId)
{
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!IsValid(NavigationSystem))
	{
		return;
	}

	const bool bStillBuilding = NavigationSystem->IsNavigationBuildInProgress();
	LevelGen::FRuntimeNavigationGateSnapshot Snapshot;
	Snapshot.OperationId = ActiveOperationId;
	Snapshot.ExpectedNavigationDataKey = FObjectKey(ExpectedNavigationData);
	Snapshot.bWaiting = bNavigationWaitActive;
	Snapshot.bGeometryRegistrationStarted = bNavigationGeometryRegistrationStarted;
	Snapshot.bGeometrySubmitted = bNavigationGeometrySubmitted;
	Snapshot.bObservedNavigationBuild = bObservedNavigationBuild;
	Snapshot.bReceivedTargetCompletion = bReceivedTargetNavigationCompletion;
	Snapshot.bTerminal = bNavigationWaitTerminal;
	if (!LevelGen::FRuntimeNavigationGate::CanValidate(
			Snapshot, CallbackOperationId, bStillBuilding))
	{
		return;
	}

	bNavigationWaitTerminal = true;
	GetWorldTimerManager().ClearTimer(NavigationTimeoutTimer);
	NavigationSystem->OnNavigationGenerationFinishedDelegate.RemoveDynamic(
		this,
		&AZeroEscapeRuntimeLevelGenerator::HandleNavigationGenerationFinished);
	bNavigationWaitActive = false;

	FZeroEscapeGenerationReport FinalReport = PendingReport;
	if (!ValidateNavigationEndpoints(FinalReport))
	{
		ClearGeneratedSceneInternal();
		FinishGeneration(false, FinalReport);
		return;
	}
	FinishGeneration(true, FinalReport);
}

void AZeroEscapeRuntimeLevelGenerator::HandleNavigationBuildTimeout(
	const int64 CallbackOperationId)
{
	LevelGen::FRuntimeNavigationGateSnapshot Snapshot;
	Snapshot.OperationId = ActiveOperationId;
	Snapshot.ExpectedNavigationDataKey = FObjectKey(ExpectedNavigationData);
	Snapshot.bWaiting = bNavigationWaitActive;
	Snapshot.bGeometryRegistrationStarted = bNavigationGeometryRegistrationStarted;
	Snapshot.bGeometrySubmitted = bNavigationGeometrySubmitted;
	Snapshot.bObservedNavigationBuild = bObservedNavigationBuild;
	Snapshot.bReceivedTargetCompletion = bReceivedTargetNavigationCompletion;
	Snapshot.bTerminal = bNavigationWaitTerminal;
	if (!LevelGen::FRuntimeNavigationGate::AcceptTimeout(Snapshot, CallbackOperationId))
	{
		return;
	}

	bNavigationWaitTerminal = true;
	FZeroEscapeGenerationReport FailedReport = PendingReport;
	FailReport(
		FailedReport,
		EZeroEscapeGenerationStage::NavigationBuild,
		EZeroEscapeGenerationFailure::NavigationBuildTimeout,
		INDEX_NONE,
		FString::Printf(
			TEXT("等待目标 RecastNavMesh 更新超过 %.2f 秒。"),
			ActiveNavigationTimeoutSeconds));
	ClearGeneratedSceneInternal();
	FinishGeneration(false, FailedReport);
}

bool AZeroEscapeRuntimeLevelGenerator::BuildNavigationValidationAddresses(
	TArray<FIntVector>& OutAddresses,
	FZeroEscapeGenerationReport& InOutReport) const
{
	OutAddresses.Reset();
	TSet<FIntVector> Seen;
	auto AddAddress = [&OutAddresses, &Seen](const FIntVector Address)
	{
		if (!Seen.Contains(Address))
		{
			Seen.Add(Address);
			OutAddresses.Add(Address);
		}
	};

	// 第一个地址固定为追猎者，是所有 TestPathSync 的共同起点。
	AddAddress(LastPlan.PursuerSpawnCoordinate);
	AddAddress(LastPlan.PlayerSpawnCoordinate);
	AddAddress(LastPlan.ExitCoordinate);

	TSet<int32> RequiredStableIds;
	for (const int32 StableId : LastPlan.RequiredTwoFloorStairStableIdByLowerFloor)
	{
		RequiredStableIds.Add(StableId);
		const FZeroEscapeGeneratedStructure* Structure =
			FindStructureByStableId(LastPlan, StableId);
		if (Structure == nullptr || Structure->Kind != EZeroEscapeStructureKind::TwoFloorStair)
		{
			return FailReport(
				InOutReport,
				EZeroEscapeGenerationStage::NavigationValidation,
				EZeroEscapeGenerationFailure::NavigationValidationFailed,
				StableId,
				TEXT("必需楼梯 StableId 无法解析为双层楼梯。"));
		}
		for (const FZeroEscapeGeneratedStructureLanding& Landing : Structure->Landings)
		{
			AddAddress(Landing.Coordinate);
		}
	}

	for (const FZeroEscapeGeneratedStructure& Structure : LastPlan.Structures)
	{
		if (RequiredStableIds.Contains(Structure.StableStructureId)
			|| Structure.Kind == EZeroEscapeStructureKind::HighCeilingRoom)
		{
			continue;
		}
		for (const FZeroEscapeGeneratedStructureLanding& Landing : Structure.Landings)
		{
			AddAddress(Landing.Coordinate);
		}
	}

	if (OutAddresses.Num() < 3
		|| OutAddresses.Num() > ActiveMaxNavigationValidationPoints)
	{
		return FailReport(
			InOutReport,
			EZeroEscapeGenerationStage::NavigationValidation,
			EZeroEscapeGenerationFailure::NavigationValidationFailed,
			INDEX_NONE,
			FString::Printf(
				TEXT("导航代表点数量 %d 超出 [3,%d]。"),
				OutAddresses.Num(),
				ActiveMaxNavigationValidationPoints));
	}
	for (const FIntVector Address : OutAddresses)
	{
		if (!IsAddressInsidePlan(Address, LastPlan))
		{
			return FailReport(
				InOutReport,
				EZeroEscapeGenerationStage::NavigationValidation,
				EZeroEscapeGenerationFailure::NavigationValidationFailed,
				INDEX_NONE,
				TEXT("导航代表点地址超出本局真实楼层或 Grid。"));
		}
	}
	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::ValidateNavigationEndpoints(
	FZeroEscapeGenerationReport& InOutReport)
{
	const double StartSeconds = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		InOutReport.Metrics.NavigationValidationMilliseconds =
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	};

	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!IsValid(NavigationSystem)
		|| !IsValid(ExpectedNavigationData)
		|| !IsValid(GeneratedRoot))
	{
		return FailReport(
			InOutReport,
			EZeroEscapeGenerationStage::NavigationValidation,
			EZeroEscapeGenerationFailure::NavigationValidationFailed,
			INDEX_NONE,
			TEXT("导航验收所需系统、目标数据或 Root 已失效。"));
	}

	TArray<FIntVector> Addresses;
	if (!BuildNavigationValidationAddresses(Addresses, InOutReport))
	{
		return false;
	}

	const float HorizontalExtent = FMath::Max(
		PursuerNavigationAgentProperties.AgentRadius * 2.0f, 50.0f);
	const float VerticalExtent = FMath::Min(
		FMath::Max(PursuerNavigationAgentProperties.AgentHeight, 50.0f),
		static_cast<float>(LastPlan.FloorHeightCm * 0.49));
	const FVector ProjectionExtent(HorizontalExtent, HorizontalExtent, VerticalExtent);
	const FSharedConstNavQueryFilter QueryFilter =
		ExpectedNavigationData->GetDefaultQueryFilter();

	TArray<FNavLocation> ProjectedLocations;
	ProjectedLocations.Reserve(Addresses.Num());
	for (const FIntVector Address : Addresses)
	{
		FTransform WorldTransform;
		if (!AddressToWorldTransform(LastPlan, Address, true, WorldTransform))
		{
			return FailReport(
				InOutReport,
				EZeroEscapeGenerationStage::NavigationValidation,
				EZeroEscapeGenerationFailure::NavigationValidationFailed,
				INDEX_NONE,
				TEXT("导航代表点无法转换到世界空间。"));
		}

		FNavLocation Projected;
		++InOutReport.Metrics.NavigationProjectionCount;
		if (!NavigationSystem->ProjectPointToNavigation(
				WorldTransform.GetLocation(),
				Projected,
				ProjectionExtent,
				ExpectedNavigationData,
				QueryFilter))
		{
			return FailReport(
				InOutReport,
				EZeroEscapeGenerationStage::NavigationValidation,
				EZeroEscapeGenerationFailure::NavigationValidationFailed,
				INDEX_NONE,
				FString::Printf(
					TEXT("导航代表点 (%d,%d,%d) 无法投射到目标 RecastNavMesh。"),
					Address.X, Address.Y, Address.Z));
		}
		const FVector ProjectedLocal =
			GeneratedRoot->GetComponentTransform().InverseTransformPosition(Projected.Location);
		const double ExpectedFloorZ = PresentationProfile->FloorTopZCm
			+ Address.Z * LastPlan.FloorHeightCm;
		if (FMath::Abs(ProjectedLocal.Z - ExpectedFloorZ) >= LastPlan.FloorHeightCm * 0.5)
		{
			return FailReport(
				InOutReport,
				EZeroEscapeGenerationStage::NavigationValidation,
				EZeroEscapeGenerationFailure::NavigationValidationFailed,
				INDEX_NONE,
				TEXT("导航投射结果落到了另一楼层。"));
		}
		ProjectedLocations.Add(Projected);
	}

	for (int32 Index = 1; Index < ProjectedLocations.Num(); ++Index)
	{
		FPathFindingQuery Query(
			this,
			*ExpectedNavigationData,
			ProjectedLocations[0].Location,
			ProjectedLocations[Index].Location,
			QueryFilter);
		Query.SetAllowPartialPaths(false);
		Query.SetRequireNavigableEndLocation(true);
		Query.SetNavAgentProperties(PursuerNavigationAgentProperties);
		int32 VisitedNodes = 0;
		++InOutReport.Metrics.NavigationPathTestCount;
		const bool bPathExists = NavigationSystem->TestPathSync(
			Query, EPathFindingMode::Regular, &VisitedNodes);
		InOutReport.Metrics.NavigationVisitedNodeCount += VisitedNodes;
		if (!bPathExists)
		{
			return FailReport(
				InOutReport,
				EZeroEscapeGenerationStage::NavigationValidation,
				EZeroEscapeGenerationFailure::NavigationValidationFailed,
				INDEX_NONE,
				FString::Printf(TEXT("追猎者到导航代表点 %d 不存在完整路径。"), Index));
		}
	}
	return true;
}

void AZeroEscapeRuntimeLevelGenerator::CancelNavigationWait()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(NavigationTimeoutTimer);
	}
	if (UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		NavigationSystem->OnNavigationGenerationFinishedDelegate.RemoveDynamic(
			this,
			&AZeroEscapeRuntimeLevelGenerator::HandleNavigationGenerationFinished);
	}
	ExpectedNavigationData = nullptr;
	bNavigationWaitActive = false;
	bNavigationGeometryRegistrationStarted = false;
	bNavigationGeometrySubmitted = false;
	bObservedNavigationBuild = false;
	bReceivedTargetNavigationCompletion = false;
	bNavigationWaitTerminal = false;
}

bool AZeroEscapeRuntimeLevelGenerator::ClearGeneratedScene()
{
	if (bGenerationInProgress || bNavigationWaitActive || bEndingPlay || !IsInGameThread())
	{
		return false;
	}
	ClearGeneratedSceneInternal();
	return true;
}

void AZeroEscapeRuntimeLevelGenerator::ClearGeneratedSceneInternal()
{
	CancelNavigationWait();
	for (AActor* LightActor : GeneratedLightActors)
	{
		if (IsValid(LightActor))
		{
			LightActor->Destroy();
		}
	}
	GeneratedLightActors.Reset();
	for (UHierarchicalInstancedStaticMeshComponent* Component : GeneratedHismComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	GeneratedHismComponents.Reset();
	LastPlan = FZeroEscapeGeneratedLevelPlan();
	State = EZeroEscapeRuntimeGenerationState::Idle;
}

void AZeroEscapeRuntimeLevelGenerator::FinishGeneration(
	const bool bSuccess,
	const FZeroEscapeGenerationReport& Report)
{
	TGuardValue<bool> FinishGuard(bGenerationInProgress, true);
	CancelNavigationWait();
	LastReport = Report;
	LastReport.OperationId = ActiveOperationId;
	LastReport.Metrics.TotalMilliseconds =
		(FPlatformTime::Seconds() - ActiveGenerationStartSeconds) * 1000.0;
	State = bSuccess
		? EZeroEscapeRuntimeGenerationState::Ready
		: EZeroEscapeRuntimeGenerationState::Failed;

	UE_LOG(
		LogZeroEscapePCG,
		Display,
		TEXT("ZE_PCG_RESULT schema=6 operation=%lld success=%d seed=%d difficulty=%s stage=%s failure=%s floors=%d ordinary=%d structures=%d walkable=%d whole_attempts=%d structure_candidates=%d wfc_candidates=%d backtracks=%d instances=%d hism=%d nav_projects=%d nav_paths=%d nav_nodes=%d nav_ms=%.3f layout_hash=%lld total_ms=%.3f message=\"%s\""),
		static_cast<long long>(ActiveOperationId),
		bSuccess ? 1 : 0,
		ActiveRequest.Seed,
		*GetStableEnumName(ActiveRequest.Difficulty),
		*GetStableEnumName(LastReport.Stage),
		*GetStableEnumName(LastReport.Failure),
		LastReport.Metrics.GeneratedFloorCount,
		LastPlan.OrdinaryCells.Num(),
		LastPlan.Structures.Num(),
		LastReport.Metrics.WalkableCellCount,
		LastReport.Metrics.WholeLayoutAttemptCount,
		LastReport.Metrics.StructureCandidateEvaluationCount,
		LastReport.Metrics.WfcCandidateAttemptCount,
		LastReport.Metrics.WfcBacktrackCount,
		LastReport.Metrics.InstancedMeshCount,
		LastReport.Metrics.HismComponentCount,
		LastReport.Metrics.NavigationProjectionCount,
		LastReport.Metrics.NavigationPathTestCount,
		LastReport.Metrics.NavigationVisitedNodeCount,
		LastReport.Metrics.NavigationValidationMilliseconds,
		static_cast<long long>(LastPlan.CanonicalLayoutHash),
		LastReport.Metrics.TotalMilliseconds,
		*MakeSingleLineLogValue(LastReport.Message));

	if (!bEndingPlay)
	{
		OnGenerationFinished.Broadcast(bSuccess, LastReport);
	}
}

FVector AZeroEscapeRuntimeLevelGenerator::AddressToLocalLocation(
	const FZeroEscapeGeneratedLevelPlan& Plan,
	const FIntVector& Address,
	const bool bUseAnchorHeight) const
{
	const double FloorTopZCm = IsValid(PresentationProfile)
		? PresentationProfile->FloorTopZCm
		: 0.0;
	return FVector(
		static_cast<double>(Address.X) * Plan.LogicalTileSizeCm,
		static_cast<double>(Address.Y) * Plan.LogicalTileSizeCm,
		FloorTopZCm + static_cast<double>(Address.Z) * Plan.FloorHeightCm
			+ (bUseAnchorHeight ? Plan.AnchorHeightCm : 0.0));
}

bool AZeroEscapeRuntimeLevelGenerator::AddressToWorldTransform(
	const FZeroEscapeGeneratedLevelPlan& Plan,
	const FIntVector& Address,
	const bool bUseAnchorHeight,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	if (!IsValid(GeneratedRoot)
		|| !IsValid(PresentationProfile)
		|| !IsAddressInsidePlan(Address, Plan))
	{
		return false;
	}
	return ConvertLocalToWorld(
		*GeneratedRoot,
		FTransform(AddressToLocalLocation(Plan, Address, bUseAnchorHeight)),
		OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedPlayerSpawnWorldTransform(
	FTransform& OutTransform) const
{
	return State == EZeroEscapeRuntimeGenerationState::Ready
		&& AddressToWorldTransform(
			LastPlan, LastPlan.PlayerSpawnCoordinate, true, OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedPursuerSpawnWorldTransform(
	FTransform& OutTransform) const
{
	return State == EZeroEscapeRuntimeGenerationState::Ready
		&& AddressToWorldTransform(
			LastPlan, LastPlan.PursuerSpawnCoordinate, true, OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedExitWorldTransform(
	FTransform& OutTransform) const
{
	return State == EZeroEscapeRuntimeGenerationState::Ready
		&& AddressToWorldTransform(LastPlan, LastPlan.ExitCoordinate, true, OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedOrdinaryGameplayCellWorldTransforms(
	const bool bAvoidSpawnExitNeighbors,
	const bool bStraightCorridorOnly,
	TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Reset();
	if (State != EZeroEscapeRuntimeGenerationState::Ready || !IsValid(GeneratedRoot))
	{
		return false;
	}

	const uint8 StraightNS = static_cast<uint8>(EZeroEscapeOpenEdge::North)
		| static_cast<uint8>(EZeroEscapeOpenEdge::South);
	const uint8 StraightEW = static_cast<uint8>(EZeroEscapeOpenEdge::East)
		| static_cast<uint8>(EZeroEscapeOpenEdge::West);
	for (const FZeroEscapeGeneratedOrdinaryCell& Cell : LastPlan.OrdinaryCells)
	{
		if (bAvoidSpawnExitNeighbors
			&& (IsWithinOneStepOnSameFloor(Cell.Coordinate, LastPlan.PlayerSpawnCoordinate)
				|| IsWithinOneStepOnSameFloor(Cell.Coordinate, LastPlan.PursuerSpawnCoordinate)
				|| IsWithinOneStepOnSameFloor(Cell.Coordinate, LastPlan.ExitCoordinate)))
		{
			continue;
		}
		const bool bStraightNS = Cell.OpeningMask == StraightNS;
		const bool bStraightEW = Cell.OpeningMask == StraightEW;
		if (bStraightCorridorOnly && !bStraightNS && !bStraightEW)
		{
			continue;
		}

		const FTransform LocalTransform(
			FRotator(0.0, bStraightNS ? 90.0 : 0.0, 0.0),
			AddressToLocalLocation(LastPlan, Cell.Coordinate, false));
		FTransform WorldTransform;
		if (!ConvertLocalToWorld(*GeneratedRoot, LocalTransform, WorldTransform))
		{
			OutTransforms.Reset();
			return false;
		}
		OutTransforms.Add(WorldTransform);
	}
	return true;
}

int32 AZeroEscapeRuntimeLevelGenerator::GetGeneratedSeed() const
{
	return LastPlan.Signature.Seed;
}

void AZeroEscapeRuntimeLevelGenerator::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	ClearGeneratedSceneInternal();
	Super::EndPlay(EndPlayReason);
}
