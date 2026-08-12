// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayPopulator.cpp
 * 职责：读取 Ready 空间快照，原子规划、校验并依次生成机关层和磁力资源层。
 * 边界：不生成空间、不实现候选算法、不改变机关行为；失败只回滚自己 Spawn 的对象。
 */

#include "PCG/Population/ZeroEscapeGameplayPopulator.h"

#include "Engine/World.h"
#include "PCG/Population/ZeroEscapePopulationPlacementPolicy.h"
#include "PCG/Population/ZeroEscapePopulationProfile.h"
#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"

namespace LevelGen = ZeroEscape::LevelGeneration;

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePopulator, Log, All);

namespace
{
	struct FPopulationSpawnRequest
	{
		LevelGen::EPopulationPlacementKind Kind =
			LevelGen::EPopulationPlacementKind::SpikeTrap;
		UClass* ActorClass = nullptr;
		FTransform WorldTransform = FTransform::Identity;
	};

	const TCHAR* PlacementKindName(const LevelGen::EPopulationPlacementKind Kind)
	{
		switch (Kind)
		{
		case LevelGen::EPopulationPlacementKind::Pendulum: return TEXT("Pendulum");
		case LevelGen::EPopulationPlacementKind::SpikeTrap: return TEXT("SpikeTrap");
		case LevelGen::EPopulationPlacementKind::BatteringRam: return TEXT("BatteringRam");
		case LevelGen::EPopulationPlacementKind::GuidedLauncher: return TEXT("GuidedLauncher");
		case LevelGen::EPopulationPlacementKind::MagneticResource: return TEXT("MagneticResource");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* PlacementResultName(const LevelGen::EPopulationPlacementResult Result)
	{
		switch (Result)
		{
		case LevelGen::EPopulationPlacementResult::Success: return TEXT("Success");
		case LevelGen::EPopulationPlacementResult::InvalidPlan: return TEXT("InvalidPlan");
		case LevelGen::EPopulationPlacementResult::InvalidConfiguration: return TEXT("InvalidConfiguration");
		case LevelGen::EPopulationPlacementResult::InvalidTraversalGraph: return TEXT("InvalidTraversalGraph");
		case LevelGen::EPopulationPlacementResult::SpawnBudgetExceeded: return TEXT("SpawnBudgetExceeded");
		default: return TEXT("Unknown");
		}
	}

	const TSoftClassPtr<AActor>* FindClassReference(
		const LevelGen::EPopulationPlacementKind Kind,
		const FZeroEscapeHazardPopulationAssembly& Hazards,
		const FZeroEscapeResourcePopulationAssembly& Resources)
	{
		switch (Kind)
		{
		case LevelGen::EPopulationPlacementKind::Pendulum: return &Hazards.PendulumClass;
		case LevelGen::EPopulationPlacementKind::SpikeTrap: return &Hazards.SpikeTrapClass;
		case LevelGen::EPopulationPlacementKind::BatteringRam: return &Hazards.BatteringRamClass;
		case LevelGen::EPopulationPlacementKind::GuidedLauncher: return &Hazards.GuidedLauncherClass;
		case LevelGen::EPopulationPlacementKind::MagneticResource: return &Resources.MagneticResourceClass;
		default: return nullptr;
		}
	}

	bool LoadUsedClasses(
		const LevelGen::FPopulationPlacementPlan& Plan,
		const FZeroEscapeHazardPopulationAssembly& Hazards,
		const FZeroEscapeResourcePopulationAssembly& Resources,
		TMap<LevelGen::EPopulationPlacementKind, UClass*>& OutClasses,
		FString& OutError)
	{
		OutClasses.Reset();
		auto LoadKind = [&](const LevelGen::EPopulationPlacementKind Kind)
		{
			if (OutClasses.Contains(Kind))
			{
				return true;
			}
			const TSoftClassPtr<AActor>* Reference =
				FindClassReference(Kind, Hazards, Resources);
			if (Reference == nullptr || Reference->IsNull())
			{
				OutError = FString::Printf(
					TEXT("已规划类型 %s 缺少 Actor Class。"), PlacementKindName(Kind));
				return false;
			}
			UClass* LoadedClass = Reference->LoadSynchronous();
			if (!IsValid(LoadedClass)
				|| !LoadedClass->IsChildOf(AActor::StaticClass())
				|| LoadedClass->HasAnyClassFlags(CLASS_Abstract))
			{
				OutError = FString::Printf(
					TEXT("类型 %s 的 Actor Class 无法加载或不可生成。"), PlacementKindName(Kind));
				return false;
			}
			OutClasses.Add(Kind, LoadedClass);
			return true;
		};

		for (const LevelGen::FPopulationPlannedPlacement& Placement : Plan.HazardPlacements)
		{
			if (!LoadKind(Placement.Kind))
			{
				return false;
			}
		}
		for (const LevelGen::FPopulationPlannedPlacement& Placement : Plan.ResourcePlacements)
		{
			if (!LoadKind(Placement.Kind))
			{
				return false;
			}
		}
		return true;
	}

	bool BuildSpawnRequests(
		const LevelGen::FPopulationPlacementPlan& Plan,
		const FTransform& GeneratedRootWorldTransform,
		const TMap<LevelGen::EPopulationPlacementKind, UClass*>& Classes,
		TArray<FPopulationSpawnRequest>& OutRequests,
		FString& OutError)
	{
		OutRequests.Reset();
		int64 DirectSpawnRequestCount = 0;
		int64 WorldActorBudgetCount = 0;
		for (const LevelGen::FPopulationPlannedPlacement& Placement : Plan.HazardPlacements)
		{
			DirectSpawnRequestCount += Placement.LocalSpawnTransforms.Num();
			WorldActorBudgetCount += Placement.LocalSpawnTransforms.Num();
			if (Placement.Kind == LevelGen::EPopulationPlacementKind::GuidedLauncher)
			{
				// 每个发射器在 BeginPlay 内同步预装一个真实弹体。
				++WorldActorBudgetCount;
			}
		}
		for (const LevelGen::FPopulationPlannedPlacement& Placement : Plan.ResourcePlacements)
		{
			DirectSpawnRequestCount += Placement.LocalSpawnTransforms.Num();
			WorldActorBudgetCount += Placement.LocalSpawnTransforms.Num();
		}
		const int64 Maximum = static_cast<int64>(ZeroEscape::GenerationLimits::MaxGridCells)
			* ZeroEscape::GenerationLimits::MaxFloorCount;
		if (DirectSpawnRequestCount < 0 || DirectSpawnRequestCount > Maximum
			|| WorldActorBudgetCount < 0 || WorldActorBudgetCount > Maximum)
		{
			OutError = TEXT("Spawn 请求数量超过安全预算。");
			return false;
		}
		OutRequests.Reserve(static_cast<int32>(DirectSpawnRequestCount));
		auto AppendLayer = [&](const TArray<LevelGen::FPopulationPlannedPlacement>& Placements)
		{
			for (const LevelGen::FPopulationPlannedPlacement& Placement : Placements)
			{
				UClass* const* ActorClass = Classes.Find(Placement.Kind);
				if (ActorClass == nullptr || !IsValid(*ActorClass)
					|| Placement.LocalSpawnTransforms.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("类型 %s 的规划 Class 或 Transform 为空。"),
						PlacementKindName(Placement.Kind));
					return false;
				}
				for (const FTransform& LocalTransform : Placement.LocalSpawnTransforms)
				{
					if (!LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(LocalTransform))
					{
						OutError = TEXT("规划生成了非法局部 Transform。");
						return false;
					}
					FPopulationSpawnRequest& Request = OutRequests.AddDefaulted_GetRef();
					Request.Kind = Placement.Kind;
					Request.ActorClass = *ActorClass;
					Request.WorldTransform = LocalTransform * GeneratedRootWorldTransform;
					if (!LevelGen::FGenerationCore::IsFiniteUnitScaleTransform(
							Request.WorldTransform))
					{
						OutError = TEXT("局部到世界组合生成了非法 Transform。");
						return false;
					}
				}
			}
			return true;
		};
		return AppendLayer(Plan.HazardPlacements)
			&& AppendLayer(Plan.ResourcePlacements);
	}
}

AZeroEscapeGameplayPopulator::AZeroEscapeGameplayPopulator()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AZeroEscapeGameplayPopulator::Populate(
	AZeroEscapeRuntimeLevelGenerator& Generator)
{
	ClearPopulation();
	UWorld* World = GetWorld();
	auto Fail = [this](const TCHAR* Reason, const FString& Detail = FString())
	{
		UE_LOG(LogZeroEscapePopulator, Error,
			TEXT("ZE_POPULATION result=Failure reason=%s detail=\"%s\""),
			Reason, *Detail.ReplaceCharWithEscapedChar());
		ClearPopulation();
		return false;
	};
	if (!IsValid(World) || !IsValid(PopulationProfile))
	{
		return Fail(TEXT("InvalidSetup"));
	}

	FZeroEscapeGeneratedLevelPlan LevelPlan;
	FTransform GeneratedRootWorldTransform;
	double FloorTopZCm = 0.0;
	if (!Generator.GetGeneratedPopulationSnapshot(
			LevelPlan, GeneratedRootWorldTransform, FloorTopZCm))
	{
		return Fail(TEXT("PopulationSnapshotFailed"));
	}

	LevelGen::FPopulationPlacementPlan PlacementPlan;
	FString PlacementError;
	const LevelGen::EPopulationPlacementResult Result =
		LevelGen::FPopulationPlacementPolicy::BuildPlan(
			LevelPlan,
			FloorTopZCm,
			PopulationProfile->HazardAssembly,
			PopulationProfile->ResourceAssembly,
			PopulationProfile->Difficulties,
			PlacementPlan,
			PlacementError);
	if (Result != LevelGen::EPopulationPlacementResult::Success)
	{
		return Fail(PlacementResultName(Result), PlacementError);
	}

	TMap<LevelGen::EPopulationPlacementKind, UClass*> LoadedClasses;
	if (!LoadUsedClasses(
			PlacementPlan,
			PopulationProfile->HazardAssembly,
			PopulationProfile->ResourceAssembly,
			LoadedClasses,
			PlacementError))
	{
		return Fail(TEXT("ActorClassLoadFailed"), PlacementError);
	}
	TArray<FPopulationSpawnRequest> SpawnRequests;
	if (!BuildSpawnRequests(
			PlacementPlan,
			GeneratedRootWorldTransform,
			LoadedClasses,
			SpawnRequests,
			PlacementError))
	{
		return Fail(TEXT("SpawnRequestInvalid"), PlacementError);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FPopulationSpawnRequest& Request : SpawnRequests)
	{
		AActor* Spawned = World->SpawnActor<AActor>(
			Request.ActorClass, Request.WorldTransform, SpawnParameters);
		if (!IsValid(Spawned))
		{
			return Fail(TEXT("ActorSpawnFailed"), PlacementKindName(Request.Kind));
		}
		SpawnedActors.Add(Spawned);
	}

	if (PlacementPlan.HazardStats.UnderfilledCount > 0
		|| PlacementPlan.ResourceStats.UnderfilledCount > 0)
	{
		UE_LOG(LogZeroEscapePopulator, Warning,
			TEXT("ZE_POPULATION_UNDERFILL seed=%d hazards=%d/%d resources=%d/%d"),
			LevelPlan.Signature.Seed,
			PlacementPlan.HazardStats.ActualCount,
			PlacementPlan.HazardStats.TargetCount,
			PlacementPlan.ResourceStats.ActualCount,
			PlacementPlan.ResourceStats.TargetCount);
	}
	UE_LOG(LogZeroEscapePopulator, Display,
		TEXT("ZE_POPULATION result=Success seed=%d layout_hash=%lld hazard_target=%d hazard_actual=%d pendulums=%d spikes=%d rams=%d launchers=%d resource_target=%d resource_actual=%d spike_candidates=%d ram_candidates=%d launcher_candidates=%d resource_candidates=%d actors=%d"),
		LevelPlan.Signature.Seed,
		static_cast<long long>(LevelPlan.CanonicalLayoutHash),
		PlacementPlan.HazardStats.TargetCount,
		PlacementPlan.HazardStats.ActualCount,
		PlacementPlan.KindCounts.Pendulums,
		PlacementPlan.KindCounts.SpikeTrapGroups,
		PlacementPlan.KindCounts.BatteringRams,
		PlacementPlan.KindCounts.GuidedLaunchers,
		PlacementPlan.ResourceStats.TargetCount,
		PlacementPlan.ResourceStats.ActualCount,
		PlacementPlan.KindCounts.SpikeCandidateAnchors,
		PlacementPlan.KindCounts.RamCandidateAnchors,
		PlacementPlan.KindCounts.LauncherCandidateAnchors,
		PlacementPlan.ResourceStats.CandidateAnchorCount,
		SpawnedActors.Num());
	return true;
}

void AZeroEscapeGameplayPopulator::ClearPopulation()
{
	for (const TObjectPtr<AActor>& Actor : SpawnedActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	SpawnedActors.Reset();
}

void AZeroEscapeGameplayPopulator::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearPopulation();
	Super::EndPlay(EndPlayReason);
}
