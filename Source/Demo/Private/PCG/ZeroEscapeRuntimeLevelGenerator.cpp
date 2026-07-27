// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeLevelGenerator.cpp
 * 职责：同步解析空间配置、运行 Grid/WFC、校验结果并事务式提交结构 HISM 与顶灯 Actor。
 * 边界：不在 Construction Script/Tick 中生成；Actor 只消费纯值 Solver 的原子结果。
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

	/** 把 Solver 输出的局部空间结果转换到 GeneratedRoot 世界空间。 */
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

	/** 判断两逻辑格是否重合或四方向相邻（曼哈顿距离 <= 1）。 */
	bool IsWithinOneStep(const FIntPoint A, const FIntPoint B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y) <= 1;
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

	const double PlanningStartSeconds = FPlatformTime::Seconds();
	LevelGen::FResolvedGenerationInput Input;
	if (!LevelGen::FGenerationCore::ResolveGenerationInput(
			*GenerationProfile,
			Request,
			PresentationProfile->PresentationVersion,
			Input,
			Report))
	{
		Report.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;
		Report.Metrics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}

	FString PresentationError;
	if (!PresentationProfile->IsConfigured(
		Input.Rules.LogicalTileSizeCm,
		PresentationError))
	{
		Report.Stage = EZeroEscapeGenerationStage::Configuration;
		Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
		Report.Message = MoveTemp(PresentationError);
		Report.Metrics.PlanningMilliseconds =
			(FPlatformTime::Seconds() - PlanningStartSeconds) * 1000.0;
		Report.Metrics.TotalMilliseconds =
			(FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}

	FZeroEscapeGeneratedLevelPlan CandidatePlan;
	if (!LevelGen::FGridLayoutSolver::Solve(
		Input.Signature,
		Input.Rules,
		Input.WfcShapeWeights,
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
	if (!(CandidatePlan.Signature == Input.Signature)
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
		return FailInstantiation(
			InOutReport,
			INDEX_NONE,
			TEXT("生成顶灯时 World、GeneratedRoot 或灯 Actor 类已失效。"));
	}

	/**
	 * 四方向方格天然是二分图：任意相邻格的 (X+Y) 奇偶性必然相反。
	 * 选择数量较少的一组可把灯数限制在约一半；平局选择 Start 所在组，
	 * 若较少组不含 Start 则只额外补一盏，避免引入 BFS、随机数或路线特判。
	 */
	int32 ParityCellCounts[2] = {0, 0};
	bool bFoundStartCell = false;
	for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
	{
		const int32 CellParity = (Cell.GridCoordinate.X + Cell.GridCoordinate.Y) & 1;
		++ParityCellCounts[CellParity];
		bFoundStartCell |= Cell.GridCoordinate == Plan.StartCoordinate;
	}

	if (!bFoundStartCell)
	{
		return FailInstantiation(
			InOutReport,
			INDEX_NONE,
			TEXT("生成顶灯时 Plan.Cells 不包含 StartCoordinate。"));
	}

	const int32 StartParity =
		(Plan.StartCoordinate.X + Plan.StartCoordinate.Y) & 1;
	int32 SelectedParity = StartParity;
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

	for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
	{
		const int32 CellParity = (Cell.GridCoordinate.X + Cell.GridCoordinate.Y) & 1;
		const bool bIsStartCell = Cell.GridCoordinate == Plan.StartCoordinate;
		if (CellParity != SelectedParity && !bIsStartCell)
		{
			continue;
		}

		// 逻辑格坐标表示 600 cm 格中心；灯使用与天花板结构相同的规范 Pivot 高度。
		const FVector CellCenterLocation(
			static_cast<double>(Cell.GridCoordinate.X) * Plan.LogicalTileSizeCm,
			static_cast<double>(Cell.GridCoordinate.Y) * Plan.LogicalTileSizeCm,
			PresentationProfile->CeilingPivotZCm);
		const FTransform CanonicalCellTransform(
			FQuat::Identity,
			CellCenterLocation,
			FVector::OneVector);
		const FTransform LightLocalTransform =
			PresentationProfile->CeilingLightCellTransform * CanonicalCellTransform;

		FTransform LightWorldTransform;
		if (!ConvertLocalToWorld(*GeneratedRoot, LightLocalTransform, LightWorldTransform))
		{
			return FailInstantiation(
				InOutReport,
				Cell.StableCellId,
				TEXT("顶灯局部 Transform 无法转换为有限 Unit Scale 世界 Transform。"));
		}

		AActor* LightActor = World->SpawnActor<AActor>(
			LightActorClass,
			LightWorldTransform,
			SpawnParameters);
		if (!IsValid(LightActor))
		{
			return FailInstantiation(
				InOutReport,
				Cell.StableCellId,
				TEXT("生成顶灯 Actor 失败。"));
		}

		// Spawn 后立即登记，后续任一灯失败时现有事务路径可销毁此前全部灯。
		GeneratedLightActors.Add(LightActor);
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
		// schema=5 记录空间输入、求解成本、房间数量和唯一 Layout Hash。
		TEXT("ZE_PCG_RESULT schema=5 success=%d seed=%d difficulty=%s stage=%s failure=%s cells=%d rooms=%d walkable=%d solve_attempts=%d observations=%d candidate_attempts=%d propagations=%d contradictions=%d contradiction_local=%d contradiction_count=%d contradiction_max_straight=%d contradiction_connected=%d contradiction_global_ban=%d backtracks=%d leaf_rejections=%d instances=%d hism=%d layout_hash=%lld total_ms=%.3f message=\"%s\""),
		bSuccess ? 1 : 0,
		Request.Seed,
		*GetStableEnumName(Request.Difficulty),
		*GetStableEnumName(Report.Stage),
		*GetStableEnumName(Report.Failure),
		LastPlan.Cells.Num(),
		LastPlan.Rooms.Num(),
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
		static_cast<long long>(LastPlan.CanonicalLayoutHash),
		Report.Metrics.TotalMilliseconds,
		*MakeSingleLineLogValue(Report.Message));

	if (!bEndingPlay)
	{
		OnGenerationFinished.Broadcast(bSuccess, LastReport);
	}
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedStartWorldTransform(
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	return State == EZeroEscapeRuntimeGenerationState::Ready
		&& IsValid(GeneratedRoot)
		&& ConvertLocalToWorld(
			*GeneratedRoot,
			LastPlan.PlayerStartLocalTransform,
			OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedExitWorldTransform(
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	return State == EZeroEscapeRuntimeGenerationState::Ready
		&& IsValid(GeneratedRoot)
		&& ConvertLocalToWorld(
			*GeneratedRoot,
			LastPlan.ExitLocalTransform,
			OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedRoomWorldTransforms(
	TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Reset();
	if (State != EZeroEscapeRuntimeGenerationState::Ready || !IsValid(GeneratedRoot))
	{
		return false;
	}

	OutTransforms.Reserve(LastPlan.Rooms.Num());
	for (const FZeroEscapeGeneratedRoom& Room : LastPlan.Rooms)
	{
		FTransform WorldTransform;
		if (!ConvertLocalToWorld(
				*GeneratedRoot,
				Room.LocalTransform,
				WorldTransform))
		{
			OutTransforms.Reset();
			return false;
		}
		OutTransforms.Add(WorldTransform);
	}
	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedCellWorldTransforms(
	EZeroEscapeGridRegionKind RegionKind,
	bool bExcludeStartExitAdjacent,
	bool bStraightCorridorOnly,
	TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Reset();
	if (State != EZeroEscapeRuntimeGenerationState::Ready
		|| !IsValid(GeneratedRoot)
		|| !IsValid(PresentationProfile))
	{
		return false;
	}

	const double FloorTopZCm = PresentationProfile->FloorTopZCm;
	for (const FZeroEscapeCollapsedTile& Cell : LastPlan.Cells)
	{
		if (Cell.RegionKind != RegionKind)
		{
			continue;
		}
		if (bExcludeStartExitAdjacent
			&& (IsWithinOneStep(Cell.GridCoordinate, LastPlan.StartCoordinate)
				|| IsWithinOneStep(Cell.GridCoordinate, LastPlan.ExitCoordinate)))
		{
			continue;
		}

		// 直路走廊判定：开口严格为南北相对或东西相对；其余形态（拐角/T/十字/死胡同）按需排除。
		const uint8 StraightNS = static_cast<uint8>(EZeroEscapeOpenEdge::North)
			| static_cast<uint8>(EZeroEscapeOpenEdge::South);
		const uint8 StraightEW = static_cast<uint8>(EZeroEscapeOpenEdge::East)
			| static_cast<uint8>(EZeroEscapeOpenEdge::West);
		const bool bIsStraightNS = (Cell.OpeningMask == StraightNS);
		const bool bIsStraightEW = (Cell.OpeningMask == StraightEW);
		if (bStraightCorridorOnly && !bIsStraightNS && !bIsStraightEW)
		{
			continue;
		}

		// 逻辑格坐标为格中心；放置点使用地板顶面高度，Actor 自身 pivot 贴地。
		const FVector CellCenterLocation(
			static_cast<double>(Cell.GridCoordinate.X) * LastPlan.LogicalTileSizeCm,
			static_cast<double>(Cell.GridCoordinate.Y) * LastPlan.LogicalTileSizeCm,
			FloorTopZCm);

		// 直路南北走向→yaw=90 使 Transform 的 X 轴对齐走向；东西走向→yaw=0。横向恒为 Y 轴。
		const FQuat CellRotation = FRotator(0.0f, bIsStraightNS ? 90.0f : 0.0f, 0.0f).Quaternion();

		const FTransform CellLocalTransform(
			CellRotation, CellCenterLocation, FVector::OneVector);

		FTransform WorldTransform;
		if (!ConvertLocalToWorld(*GeneratedRoot, CellLocalTransform, WorldTransform))
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

void AZeroEscapeRuntimeLevelGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	ClearGeneratedSceneInternal();
	Super::EndPlay(EndPlayReason);
}
