// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeLevelGenerator.cpp
 * 职责：同步执行 Profile 快照、流程意图、Grid/WFC、独立校验与五类结构 HISM 事务提交。
 * 边界：不在 Construction Script/Tick 中生成；Actor 不自行实现 Catalog、Socket、A-star、
 *       拓扑求解或备用布局，只消费纯值 Solver 的原子结果。
 */

#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "PCG/ZeroEscapeGenerationAssets.h"
#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeGridLayoutSolver.h"

namespace LevelGen = ZeroEscape::LevelGeneration;

/** 结果日志是排障证据，不是玩法协议；玩法只能读取 State、Report、Plan 查询或 Delegate。 */
DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePCG, Log, All);

namespace
{
	/** 返回短枚举名并为损坏/未来枚举提供数值回退。 */
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

	/** 保证每次生成只输出一条可由日志工具稳定读取的结果记录。 */
	FString MakeSingleLineLogValue(const FString& Value)
	{
		FString Result = Value;
		Result.ReplaceInline(TEXT("\r"), TEXT(" "));
		Result.ReplaceInline(TEXT("\n"), TEXT(" "));
		Result.ReplaceInline(TEXT("\t"), TEXT(" "));
		Result.ReplaceInline(TEXT("\""), TEXT("'"));
		return Result;
	}

	/** 签名逐字段比较，禁止接受属于其他请求或其他表现版本的 Plan。 */
	bool AreSignaturesEqual(
		const FZeroEscapeGenerationSignature& A,
		const FZeroEscapeGenerationSignature& B)
	{
		return A.Seed == B.Seed
			&& A.Difficulty == B.Difficulty
			&& A.FlowProfileId == B.FlowProfileId
			&& A.AlgorithmVersion == B.AlgorithmVersion
			&& A.GenerationProfileVersion == B.GenerationProfileVersion
			&& A.FlowVersion == B.FlowVersion
			&& A.PresentationVersion == B.PresentationVersion;
	}

	/** 把实例化失败写成统一报告并保留相关结构类别编号。 */
	bool FailInstantiation(
		FZeroEscapeGenerationReport& Report,
		const int32 RelatedStableId,
		const FString& Message)
	{
		Report.Stage = EZeroEscapeGenerationStage::Instantiation;
		Report.Failure = EZeroEscapeGenerationFailure::InstantiationFailed;
		Report.RelatedStableId = RelatedStableId;
		Report.Message = Message;
		return false;
	}

	/** 将结构类别映射到直接表现绑定；返回 nullptr 只表示枚举损坏。 */
	const FZeroEscapeStructureMeshBinding* ResolveBinding(
		const UZeroEscapePresentationProfile& Profile,
		const LevelGen::EStructurePieceKind Kind)
	{
		switch (Kind)
		{
		case LevelGen::EStructurePieceKind::Floor: return &Profile.Floor;
		case LevelGen::EStructurePieceKind::Ceiling: return &Profile.Ceiling;
		case LevelGen::EStructurePieceKind::Wall: return &Profile.Wall;
		case LevelGen::EStructurePieceKind::WallTopTrim: return &Profile.WallTopTrim;
		case LevelGen::EStructurePieceKind::Pillar: return &Profile.Pillar;
		default: return nullptr;
		}
	}

	/** 每类结构创建一个稳定组件名，便于 PIE Outliner 诊断而不参与生成逻辑。 */
	FName GetStructureComponentName(const LevelGen::EStructurePieceKind Kind)
	{
		switch (Kind)
		{
		case LevelGen::EStructurePieceKind::Floor: return TEXT("GeneratedFloorHISM");
		case LevelGen::EStructurePieceKind::Ceiling: return TEXT("GeneratedCeilingHISM");
		case LevelGen::EStructurePieceKind::Wall: return TEXT("GeneratedWallHISM");
		case LevelGen::EStructurePieceKind::WallTopTrim: return TEXT("GeneratedWallTopTrimHISM");
		case LevelGen::EStructurePieceKind::Pillar: return TEXT("GeneratedPillarHISM");
		default: return TEXT("GeneratedUnknownHISM");
		}
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
	const double TotalStartSeconds = FPlatformTime::Seconds();
	ClearGeneratedSceneInternal();
	LastReport = FZeroEscapeGenerationReport();
	State = EZeroEscapeRuntimeGenerationState::Planning;

	FZeroEscapeGenerationReport Report;
	if (!IsValid(GeneratedRoot)
		|| !GeneratedRoot->IsRegistered()
		|| !LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(
			GeneratedRoot->GetComponentTransform()))
	{
		Report.Stage = EZeroEscapeGenerationStage::Configuration;
		Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
		Report.Message = TEXT("GeneratedRoot 必须已注册且具有有限 Unit Scale Transform。");
		Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}

	if (!IsValid(GenerationProfile) || !IsValid(PresentationProfile))
	{
		Report.Stage = EZeroEscapeGenerationStage::Configuration;
		Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
		Report.Message = TEXT("GenerationProfile 与 PresentationProfile 必须装配。");
		Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}

	FString ConfigurationError;
	if (!ValidateZeroEscapeGenerationAssetSet(
		*GenerationProfile,
		*PresentationProfile,
		ConfigurationError))
	{
		Report.Stage = EZeroEscapeGenerationStage::Configuration;
		Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
		Report.Message = MoveTemp(ConfigurationError);
		Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}

	const double PlanningStartSeconds = FPlatformTime::Seconds();
	LevelGen::FGenerationProfileSnapshot Snapshot;
	LevelGen::FResolvedProgressionSettings ProgressionSettings;
	LevelGen::FProgressionIntent Progression;
	FZeroEscapeGenerationSignature Signature;
	if (!LevelGen::FGenerationCore::BuildGenerationSnapshot(
			*GenerationProfile,
			Snapshot,
			Report)
		|| !LevelGen::FGenerationCore::ResolveProgressionSettings(
			Request,
			Snapshot,
			ProgressionSettings,
			Report)
		|| !LevelGen::FGenerationCore::BuildGenerationSignature(
			Request,
			Snapshot,
			ProgressionSettings,
			PresentationProfile->PresentationVersion,
			Signature,
			Report)
		|| !LevelGen::FGenerationCore::BuildProgressionIntent(
			Request,
			Snapshot,
			ProgressionSettings,
			Progression,
			Report))
	{
		Report.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;
		Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}

	LevelGen::FGridLayoutRequest LayoutRequest;
	LayoutRequest.Signature = Signature;
	LayoutRequest.Progression = Progression;

	/**
	 * Runtime 只把已经验证的 DataAsset 快照复制成纯值 Solver 设置。
	 * Count、连续直线和搜索预算属于所有难度共享的单局边界；形态权重则取当前难度快照。
	 */
	LevelGen::FGridLayoutSettings LayoutSettings;
	LayoutSettings.GridSize = Snapshot.SharedRouteConstraints.GridSize;
	LayoutSettings.LogicalTileSizeCm = FMath::RoundToInt(
		Snapshot.SharedRouteConstraints.LogicalTileSizeCm);
	LayoutSettings.RoomSizeTiles = Snapshot.SharedRouteConstraints.RoomSizeTiles;
	LayoutSettings.ObjectiveProgressBandCount =
		Snapshot.SharedRouteConstraints.ObjectiveProgressBandCount;
	LayoutSettings.MinWalkableCellCount =
		Snapshot.SharedRouteConstraints.MinWalkableCellCount;
	LayoutSettings.MaxWalkableCellCount =
		Snapshot.SharedRouteConstraints.MaxWalkableCellCount;
	LayoutSettings.MaxConsecutiveStraightTiles =
		Snapshot.SharedRouteConstraints.MaxConsecutiveStraightTiles;
	LayoutSettings.MaxWfcCandidateAttempts =
		Snapshot.SharedRouteConstraints.MaxWfcCandidateAttempts;
	LayoutSettings.MaxWfcBacktrackCount =
		Snapshot.SharedRouteConstraints.MaxWfcBacktrackCount;
	LayoutSettings.MaxWfcSolveAttempts =
		Snapshot.SharedRouteConstraints.MaxWfcSolveAttempts;
	LayoutSettings.MaxRequiredRouteLengthTiles =
		Snapshot.SharedRouteConstraints.MaxRequiredRouteLengthTiles;
	LayoutSettings.MaxRequiredRouteExtraTiles =
		Snapshot.SharedRouteConstraints.MaxRequiredRouteExtraTiles;
	LayoutSettings.GameplayAnchorHeightCm =
		Snapshot.SharedRouteConstraints.GameplayAnchorHeightCm;

	FZeroEscapeGeneratedLevelPlan CandidatePlan;
	if (!LevelGen::FGridLayoutSolver::Solve(
		LayoutRequest,
		LayoutSettings,
		ProgressionSettings.WfcShapeWeights,
		Request.Seed,
		CandidatePlan,
		Report))
	{
		Report.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;
		Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}
	Report.Metrics.PlanningMilliseconds =
		(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;

	State = EZeroEscapeRuntimeGenerationState::Validating;
	const int64 RecomputedLayoutHash =
		LevelGen::FGenerationCore::ComputeCanonicalLayoutHash(CandidatePlan);
	if (!AreSignaturesEqual(CandidatePlan.Signature, Signature)
		|| CandidatePlan.CanonicalProgressionHash
			!= LevelGen::FGenerationCore::ComputeCanonicalProgressionHash(Progression)
		|| CandidatePlan.CanonicalLayoutHash == 0
		|| CandidatePlan.CanonicalLayoutHash != RecomputedLayoutHash)
	{
		Report.Stage = EZeroEscapeGenerationStage::GlobalValidation;
		Report.Failure = EZeroEscapeGenerationFailure::SolverInvariantViolation;
		Report.Message = TEXT("Grid Solver 输出签名或规范 Hash 与本次请求不一致。");
		Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}

	State = EZeroEscapeRuntimeGenerationState::Instantiating;
	const double InstantiationStartSeconds = FPlatformTime::Seconds();
	if (!InstantiateValidatedPlan(CandidatePlan, Report))
	{
		ClearGeneratedSceneInternal();
		Report.Metrics.InstantiationMilliseconds =
			(FPlatformTime::Seconds() - InstantiationStartSeconds) * 1000.0;
		Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}

	Report.Metrics.InstantiationMilliseconds =
		(FPlatformTime::Seconds() - InstantiationStartSeconds) * 1000.0;
	Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
	LastPlan = MoveTemp(CandidatePlan);
	FinishGeneration(true, Report, Request);
	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::InstantiateValidatedPlan(
	const FZeroEscapeGeneratedLevelPlan& Plan,
	FZeroEscapeGenerationReport& InOutReport)
{
	if (!IsValid(PresentationProfile) || !IsValid(GeneratedRoot))
	{
		return FailInstantiation(InOutReport, INDEX_NONE, TEXT("实例化时表现配置或 GeneratedRoot 已失效。"));
	}

	LevelGen::FCanonicalStructureSettings Settings;
	Settings.LogicalTileSizeCm = FMath::RoundToInt(Plan.LogicalTileSizeCm);
	Settings.StructureUnitSizeCm = FMath::RoundToInt(PresentationProfile->StructureUnitSizeCm);
	Settings.FloorTopZCm = PresentationProfile->FloorTopZCm;
	Settings.WallBaseZCm = PresentationProfile->WallBaseZCm;
	Settings.CeilingPivotZCm = PresentationProfile->CeilingPivotZCm;

	TArray<LevelGen::FStructureInstance> StructureInstances;
	FString ExpansionError;
	if (!LevelGen::BuildCanonicalStructureInstances(
		Plan,
		Settings,
		StructureInstances,
		ExpansionError))
	{
		return FailInstantiation(InOutReport, INDEX_NONE, MoveTemp(ExpansionError));
	}

	TStaticArray<TArray<FTransform>, 5> TransformsByKind;
	for (const LevelGen::FStructureInstance& Instance : StructureInstances)
	{
		const int32 KindIndex = static_cast<int32>(Instance.Kind);
		if (KindIndex < 0 || KindIndex >= TransformsByKind.Num()
			|| !LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(
				Instance.CanonicalLocalTransform))
		{
			return FailInstantiation(
				InOutReport,
				KindIndex,
				TEXT("结构展开产生非法类别或非 Unit Scale Transform。"));
		}
		TransformsByKind[KindIndex].Add(Instance.CanonicalLocalTransform);
	}

	for (int32 KindIndex = 0; KindIndex < TransformsByKind.Num(); ++KindIndex)
	{
		const LevelGen::EStructurePieceKind Kind =
			static_cast<LevelGen::EStructurePieceKind>(KindIndex);
		const FZeroEscapeStructureMeshBinding* Binding =
			ResolveBinding(*PresentationProfile, Kind);
		if (Binding == nullptr)
		{
			return FailInstantiation(InOutReport, KindIndex, TEXT("无法解析结构表现绑定。"));
		}

		// Trim/Pillar 是可选表现；绑定为空时跳过这一组，不影响逻辑连通性和玩法 Anchor。
		if (!IsValid(Binding->StaticMesh) || TransformsByKind[KindIndex].IsEmpty())
		{
			continue;
		}

		UHierarchicalInstancedStaticMeshComponent* Component =
			NewObject<UHierarchicalInstancedStaticMeshComponent>(
				this,
				MakeUniqueObjectName(
					this,
					UHierarchicalInstancedStaticMeshComponent::StaticClass(),
					GetStructureComponentName(Kind)));
		if (!IsValid(Component))
		{
			return FailInstantiation(InOutReport, KindIndex, TEXT("创建结构 HISM 失败。"));
		}

		// 创建后立即登记，后续任何失败都能由统一回滚路径销毁该组件。
		GeneratedHismComponents.Add(Component);
		Component->SetupAttachment(GeneratedRoot);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetStaticMesh(Binding->StaticMesh);
		Component->SetCollisionProfileName(Binding->CollisionProfileName);
		Component->SetCanEverAffectNavigation(Binding->bCanEverAffectNavigation);
		Component->RegisterComponent();

		for (const FTransform& CanonicalTransform : TransformsByKind[KindIndex])
		{
			const FTransform PresentationTransform =
				Binding->PivotCorrection * CanonicalTransform;
			if (!LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(PresentationTransform)
				|| Component->AddInstance(PresentationTransform, false) == INDEX_NONE)
			{
				return FailInstantiation(
					InOutReport,
					KindIndex,
					TEXT("添加结构 HISM Instance 失败。"));
			}
			++InOutReport.Metrics.InstancedMeshCount;
		}
		++InOutReport.Metrics.HismComponentCount;
	}

	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::ClearGeneratedScene()
{
	if (bGenerationInProgress || bEndingPlay || !IsInGameThread())
	{
		return false;
	}
	ClearGeneratedSceneInternal();
	return true;
}

void AZeroEscapeRuntimeLevelGenerator::ClearGeneratedSceneInternal()
{
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
	const FZeroEscapeGenerationReport& Report,
	const FZeroEscapeGenerationRequest& Request)
{
	LastReport = Report;
	State = bSuccess
		? EZeroEscapeRuntimeGenerationState::Ready
		: EZeroEscapeRuntimeGenerationState::Failed;

	UE_LOG(
		LogZeroEscapePCG,
		Display,
		// schema=4 增加有限 WFC 尝试次数和五类矛盾来源，便于直接定位 Seed 长尾。
		TEXT("ZE_PCG_RESULT schema=4 success=%d seed=%d difficulty=%s flow=%s stage=%s failure=%s cells=%d walkable=%d solve_attempts=%d observations=%d candidate_attempts=%d propagations=%d contradictions=%d contradiction_local=%d contradiction_count=%d contradiction_max_straight=%d contradiction_connected=%d contradiction_global_ban=%d backtracks=%d leaf_rejections=%d instances=%d hism=%d progression_hash=%lld layout_hash=%lld total_ms=%.3f message=\"%s\""),
		bSuccess ? 1 : 0,
		Request.Seed,
		*GetStableEnumName(Request.Difficulty),
		*Request.FlowProfileId.ToString(),
		*GetStableEnumName(Report.Stage),
		*GetStableEnumName(Report.Failure),
		LastPlan.Cells.Num(),
		Report.Metrics.WalkableCellCount,
		Report.Metrics.WfcSolveAttemptCount,
		Report.Metrics.WfcObservationCount,
		Report.Metrics.WfcCandidateAttemptCount,
		Report.Metrics.WfcPropagationCount,
		Report.Metrics.WfcContradictionCount,
		Report.Metrics.WfcLocalAdjacencyContradictionCount,
		Report.Metrics.WfcCountContradictionCount,
		Report.Metrics.WfcMaxConsecutiveContradictionCount,
		Report.Metrics.WfcConnectedContradictionCount,
		Report.Metrics.WfcGlobalBanContradictionCount,
		Report.Metrics.WfcBacktrackCount,
		Report.Metrics.WfcCollapsedCandidateRejectionCount,
		Report.Metrics.InstancedMeshCount,
		Report.Metrics.HismComponentCount,
		static_cast<long long>(LastPlan.CanonicalProgressionHash),
		static_cast<long long>(LastPlan.CanonicalLayoutHash),
		Report.Metrics.TotalMilliseconds,
		*MakeSingleLineLogValue(Report.Message));

	if (!bEndingPlay)
	{
		OnGenerationFinished.Broadcast(bSuccess, LastReport);
	}
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedAnchorWorldTransform(
	const int32 StableAnchorInstanceId,
	const EZeroEscapeGameplayAnchorType ExpectedType,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	if (State != EZeroEscapeRuntimeGenerationState::Ready || !IsValid(GeneratedRoot))
	{
		return false;
	}

	const FZeroEscapeGeneratedAnchor* Anchor = LastPlan.GameplayAnchors.FindByPredicate(
		[StableAnchorInstanceId](const FZeroEscapeGeneratedAnchor& Candidate)
		{
			return Candidate.StableAnchorInstanceId == StableAnchorInstanceId;
		});
	if (Anchor == nullptr || Anchor->Type != ExpectedType
		|| !LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(Anchor->LocalTransform))
	{
		return false;
	}

	OutTransform = Anchor->LocalTransform * GeneratedRoot->GetComponentTransform();
	return LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedStartWorldTransform(
	FTransform& OutTransform) const
{
	return GetGeneratedAnchorWorldTransform(
		LastPlan.PlayerSpawnAnchorInstanceId,
		EZeroEscapeGameplayAnchorType::PlayerSpawn,
		OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedExitWorldTransform(
	FTransform& OutTransform) const
{
	return GetGeneratedAnchorWorldTransform(
		LastPlan.ExitAnchorInstanceId,
		EZeroEscapeGameplayAnchorType::Exit,
		OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedObjectiveWorldTransforms(
	TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Reset();
	if (State != EZeroEscapeRuntimeGenerationState::Ready || !IsValid(GeneratedRoot))
	{
		return false;
	}

	TArray<const FZeroEscapeGeneratedAnchor*> Objectives;
	for (const FZeroEscapeGeneratedAnchor& Anchor : LastPlan.GameplayAnchors)
	{
		if (Anchor.Type == EZeroEscapeGameplayAnchorType::Objective)
		{
			Objectives.Add(&Anchor);
		}
	}
	Objectives.Sort([](
		const FZeroEscapeGeneratedAnchor& A,
		const FZeroEscapeGeneratedAnchor& B)
	{
		return A.StableAnchorInstanceId < B.StableAnchorInstanceId;
	});

	OutTransforms.Reserve(Objectives.Num());
	for (const FZeroEscapeGeneratedAnchor* Anchor : Objectives)
	{
		FTransform WorldTransform;
		if (Anchor == nullptr
			|| !GetGeneratedAnchorWorldTransform(
				Anchor->StableAnchorInstanceId,
				EZeroEscapeGameplayAnchorType::Objective,
				WorldTransform))
		{
			OutTransforms.Reset();
			return false;
		}
		OutTransforms.Add(WorldTransform);
	}
	return true;
}

void AZeroEscapeRuntimeLevelGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	ClearGeneratedSceneInternal();
	Super::EndPlay(EndPlayReason);
}
