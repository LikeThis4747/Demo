// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayPopulator.cpp
 * 职责：读取 Ready 空间快照，原子规划、校验并生成机关、磁力资源和奖励光团。
 * 边界：不生成空间、不实现候选算法、不改变机关行为；失败只回滚自己 Spawn 的对象。
 */

#include "PCG/Population/ZeroEscapeGameplayPopulator.h"

#include "Actors/Hazards/BatteringRamHazard.h"
#include "Actors/Hazards/PendulumHazard.h"
#include "Actors/Hazards/SpikeTrapHazard.h"
#include "Actors/Hazards/SpikeWheelHazard.h"
#include "Actors/Items/ZeroEscapeEnergyOrb.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
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
		LevelGen::FPopulationSpikeWheelSpawnConfig SpikeWheel;
		LevelGen::FPopulationPeriodicPhaseConfig PeriodicPhase;
	};

	const TCHAR* PlacementKindName(const LevelGen::EPopulationPlacementKind Kind)
	{
		switch (Kind)
		{
		case LevelGen::EPopulationPlacementKind::Pendulum: return TEXT("Pendulum");
		case LevelGen::EPopulationPlacementKind::SpikeTrap: return TEXT("SpikeTrap");
		case LevelGen::EPopulationPlacementKind::BatteringRam: return TEXT("BatteringRam");
		case LevelGen::EPopulationPlacementKind::GuidedLauncher: return TEXT("GuidedLauncher");
		case LevelGen::EPopulationPlacementKind::SpikeWheel: return TEXT("SpikeWheel");
		case LevelGen::EPopulationPlacementKind::MagneticResource: return TEXT("MagneticResource");
		case LevelGen::EPopulationPlacementKind::EnergyOrb: return TEXT("EnergyOrb");
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
		case LevelGen::EPopulationPlacementKind::SpikeWheel: return &Hazards.SpikeWheelClass;
		case LevelGen::EPopulationPlacementKind::MagneticResource: return &Resources.MagneticResourceClass;
		case LevelGen::EPopulationPlacementKind::EnergyOrb: return &Resources.EnergyOrbClass;
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
			if (Kind == LevelGen::EPopulationPlacementKind::SpikeWheel
				&& !LoadedClass->IsChildOf(ASpikeWheelHazard::StaticClass()))
			{
				OutError = FString::Printf(
					TEXT("Type %s must use an ASpikeWheelHazard subclass."),
					PlacementKindName(Kind));
				return false;
			}
			if (Kind == LevelGen::EPopulationPlacementKind::EnergyOrb
				&& !LoadedClass->IsChildOf(AZeroEscapeEnergyOrb::StaticClass()))
			{
				OutError = TEXT("EnergyOrb must use an AZeroEscapeEnergyOrb subclass.");
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
		for (const LevelGen::FPopulationPlannedPlacement& Placement : Plan.EnergyOrbPlacements)
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
		for (const LevelGen::FPopulationPlannedPlacement& Placement : Plan.EnergyOrbPlacements)
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
				const bool bIsSpikeWheel =
					Placement.Kind == LevelGen::EPopulationPlacementKind::SpikeWheel;
				const bool bIsEnergyOrb =
					Placement.Kind == LevelGen::EPopulationPlacementKind::EnergyOrb;
				if (bIsSpikeWheel
					&& (Placement.LocalSpawnTransforms.Num() != 1
						|| !Placement.SpikeWheel.bIsConfigured))
				{
					OutError = TEXT("SpikeWheel placement requires one transform and a route configuration.");
					return false;
				}
				if (bIsEnergyOrb && Placement.LocalSpawnTransforms.Num() != 1)
				{
					OutError = TEXT("EnergyOrb placement requires exactly one transform.");
					return false;
				}
				if (!bIsSpikeWheel && Placement.SpikeWheel.bIsConfigured)
				{
					OutError = FString::Printf(
						TEXT("Type %s unexpectedly contains SpikeWheel spawn configuration."),
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
					Request.SpikeWheel = Placement.SpikeWheel;
					if (LevelGen::IsPeriodicHazardKind(Placement.Kind))
					{
						Request.PeriodicPhase.bIsConfigured = true;
						Request.PeriodicPhase.NormalizedPhase01 =
							Placement.PeriodicPhase.bIsConfigured
								&& FMath::IsFinite(
									Placement.PeriodicPhase.NormalizedPhase01)
								&& Placement.PeriodicPhase.NormalizedPhase01 >= 0.0f
								&& Placement.PeriodicPhase.NormalizedPhase01 < 1.0f
							? Placement.PeriodicPhase.NormalizedPhase01
							: 0.0f;
					}
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
			&& AppendLayer(Plan.ResourcePlacements)
			&& AppendLayer(Plan.EnergyOrbPlacements);
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
	const ACharacter* PlayerCharacter = UGameplayStatics::GetPlayerCharacter(this, 0);
	const ACharacter* PlayerClassDefault = IsValid(PlayerCharacter)
		? PlayerCharacter->GetClass()->GetDefaultObject<ACharacter>()
		: nullptr;
	const UCharacterMovementComponent* PlayerMovement = IsValid(PlayerClassDefault)
		? PlayerClassDefault->GetCharacterMovement()
		: nullptr;
	const float PlayerMaxWalkSpeedCmPerSecond = IsValid(PlayerMovement)
		? PlayerMovement->MaxWalkSpeed
		: 0.0f;
	if (!FMath::IsFinite(PlayerMaxWalkSpeedCmPerSecond)
		|| PlayerMaxWalkSpeedCmPerSecond <= 0.0f)
	{
		return Fail(
			TEXT("PlayerTraversalSpeedUnavailable"),
			FString::Printf(
				TEXT("player=%s class_default=%s max_walk_speed_cm_s=%.3f"),
				*GetNameSafe(PlayerCharacter),
				*GetNameSafe(PlayerClassDefault),
				PlayerMaxWalkSpeedCmPerSecond));
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
			PlayerMaxWalkSpeedCmPerSecond,
			PopulationProfile->HazardAssembly,
			PopulationProfile->ResourceAssembly,
			PopulationProfile->Difficulties,
			PlacementPlan,
			PlacementError);
	if (Result != LevelGen::EPopulationPlacementResult::Success)
	{
		return Fail(PlacementResultName(Result), PlacementError);
	}
	const FZeroEscapePopulationDifficultySettings* ActiveDifficulty =
		PopulationProfile->Difficulties.FindByPredicate(
			[&LevelPlan](const FZeroEscapePopulationDifficultySettings& Entry)
			{
				return Entry.Difficulty == LevelPlan.Signature.Difficulty;
			});
	if (ActiveDifficulty == nullptr)
	{
		return Fail(TEXT("PopulationDifficultyUnavailable"));
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
		AActor* Spawned = nullptr;
		if (LevelGen::IsPeriodicHazardKind(Request.Kind))
		{
			AActor* DeferredActor = World->SpawnActorDeferred<AActor>(
				Request.ActorClass,
				Request.WorldTransform,
				this,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
				ESpawnActorScaleMethod::OverrideRootScale);
			if (!IsValid(DeferredActor))
			{
				return Fail(TEXT("PeriodicHazardDeferredSpawnFailed"));
			}

			bool bPhaseConfigured = false;
			switch (Request.Kind)
			{
			case LevelGen::EPopulationPlacementKind::Pendulum:
				if (APendulumHazard* Pendulum = Cast<APendulumHazard>(DeferredActor))
				{
					bPhaseConfigured = Pendulum->ConfigurePopulationPhase(
						Request.PeriodicPhase.NormalizedPhase01);
				}
				break;
			case LevelGen::EPopulationPlacementKind::SpikeTrap:
				if (ASpikeTrapHazard* Spike = Cast<ASpikeTrapHazard>(DeferredActor))
				{
					bPhaseConfigured = Spike->ConfigurePopulationPhase(
						Request.PeriodicPhase.NormalizedPhase01);
				}
				break;
			case LevelGen::EPopulationPlacementKind::BatteringRam:
				if (ABatteringRamHazard* Ram = Cast<ABatteringRamHazard>(DeferredActor))
				{
					bPhaseConfigured = Ram->ConfigurePopulationPhase(
						Request.PeriodicPhase.NormalizedPhase01);
				}
				break;
			case LevelGen::EPopulationPlacementKind::SpikeWheel:
				if (ASpikeWheelHazard* Wheel = Cast<ASpikeWheelHazard>(DeferredActor))
				{
					bPhaseConfigured = Wheel->ConfigurePopulationPlacement(
						Request.SpikeWheel.RouteVariantSeed,
						Request.PeriodicPhase.NormalizedPhase01);
				}
				break;
			default:
				break;
			}
			if (!bPhaseConfigured)
			{
				UE_LOG(LogZeroEscapePopulator, Warning,
					TEXT("Periodic phase fell back to the actor default for type %s."),
					PlacementKindName(Request.Kind));
			}

			Spawned = UGameplayStatics::FinishSpawningActor(
				DeferredActor,
				Request.WorldTransform,
				ESpawnActorScaleMethod::OverrideRootScale);
			if (!IsValid(Spawned))
			{
				if (IsValid(DeferredActor))
				{
					DeferredActor->Destroy();
				}
				return Fail(TEXT("PeriodicHazardFinishSpawningFailed"));
			}
		}
		else
		{
			Spawned = World->SpawnActor<AActor>(
				Request.ActorClass, Request.WorldTransform, SpawnParameters);
		}
		if (!IsValid(Spawned))
		{
			return Fail(TEXT("ActorSpawnFailed"), PlacementKindName(Request.Kind));
		}
		SpawnedActors.Add(Spawned);
	}

	if (PlacementPlan.HazardStats.UnderfilledBudgetTenths > 0
		|| PlacementPlan.ResourceStats.UnderfilledCount > 0)
	{
		UE_LOG(LogZeroEscapePopulator, Warning,
			TEXT("ZE_POPULATION_UNDERFILL seed=%d hazard_budget_tenths=%d/%d hazard_placements=%d resources=%d/%d"),
			LevelPlan.Signature.Seed,
			PlacementPlan.HazardStats.ActualBudgetTenths,
			PlacementPlan.HazardStats.TargetBudgetTenths,
			PlacementPlan.HazardStats.ActualCount,
			PlacementPlan.ResourceStats.ActualCount,
			PlacementPlan.ResourceStats.TargetCount);
	}
	for (int32 PlacementIndex = 0;
		PlacementIndex < PlacementPlan.HazardPlacements.Num();
		++PlacementIndex)
	{
		const LevelGen::FPopulationPlannedPlacement& Placement =
			PlacementPlan.HazardPlacements[PlacementIndex];
		UE_LOG(LogZeroEscapePopulator, Verbose,
			TEXT("ZE_POPULATION_SCORE index=%d kind=%s anchor=%s total=%.3f position=%.3f progress=%.3f pressure=%.3f combination=%.3f diversity=%.3f diagnostic=%.3f phase_configured=%d phase=%.6f wheel_route_seed=%d"),
			PlacementIndex,
			PlacementKindName(Placement.Kind),
			*Placement.AnchorAddress.ToString(),
			Placement.Score.TotalLog2Score,
			Placement.Score.Position,
			Placement.Score.Progress,
			Placement.Score.GroupPressure,
			Placement.Score.Combination,
			Placement.Score.Diversity,
			Placement.Score.Diagnostic,
			Placement.PeriodicPhase.bIsConfigured ? 1 : 0,
			Placement.PeriodicPhase.NormalizedPhase01,
			Placement.SpikeWheel.RouteVariantSeed);
	}
	for (int32 GroupIndex = 0;
		GroupIndex < PlacementPlan.HazardGroups.Num();
		++GroupIndex)
	{
		const LevelGen::FPopulationHazardGroupRecord& Group =
			PlacementPlan.HazardGroups[GroupIndex];
		UE_LOG(LogZeroEscapePopulator, Verbose,
			TEXT("ZE_POPULATION_GROUP index=%d anchor=%s members=%d target_pressure=%.3f actual_pressure=%.3f support_priority=%.3f safe_approaches=%d"),
			GroupIndex,
			*Group.AnchorAddress.ToString(),
			Group.PlacementIndices.Num(),
			Group.TargetPressure,
			Group.ActualPressure,
			Group.ResourceSupportPriority,
			Group.SafeApproachAddresses.Num());
	}
	LastSpawnedEnergyOrbCount = PlacementPlan.KindCounts.EnergyOrbs;
	LastRequiredEnergyOrbCollectionRatio =
		ActiveDifficulty->RequiredEnergyOrbCollectionRatio;
	UE_LOG(LogZeroEscapePopulator, Display,
		TEXT("ZE_POPULATION result=Success seed=%d layout_hash=%lld player_max_walk_speed_cm_s=%.3f hazard_budget_tenths=%d/%d hazard_placements=%d pendulums=%d spikes=%d rams=%d launchers=%d wheels=%d resource_target=%d resource_actual=%d energy_orbs=%d spike_candidates=%d ram_candidates=%d launcher_candidates=%d wheel_candidates=%d groups=%d wheel_ram_combos=%d wheel_spike_combos=%d unpaired_wheels=%d literal_solo_wheels=%d resource_candidates=%d actors=%d"),
		LevelPlan.Signature.Seed,
		static_cast<long long>(LevelPlan.CanonicalLayoutHash),
		PlayerMaxWalkSpeedCmPerSecond,
		PlacementPlan.HazardStats.ActualBudgetTenths,
		PlacementPlan.HazardStats.TargetBudgetTenths,
		PlacementPlan.HazardStats.ActualCount,
		PlacementPlan.KindCounts.Pendulums,
		PlacementPlan.KindCounts.SpikeTrapGroups,
		PlacementPlan.KindCounts.BatteringRams,
		PlacementPlan.KindCounts.GuidedLaunchers,
		PlacementPlan.KindCounts.SpikeWheels,
		PlacementPlan.ResourceStats.TargetCount,
		PlacementPlan.ResourceStats.ActualCount,
		PlacementPlan.KindCounts.EnergyOrbs,
		PlacementPlan.KindCounts.SpikeCandidateAnchors,
		PlacementPlan.KindCounts.RamCandidateAnchors,
		PlacementPlan.KindCounts.LauncherCandidateAnchors,
		PlacementPlan.KindCounts.WheelCandidateAnchors,
		PlacementPlan.HazardGroups.Num(),
		PlacementPlan.KindCounts.WheelRamCombinations,
		PlacementPlan.KindCounts.WheelSpikeCombinations,
		PlacementPlan.KindCounts.UnpairedWheels,
		PlacementPlan.KindCounts.LiteralSoloWheels,
		PlacementPlan.ResourceStats.CandidateAnchorCount,
		SpawnedActors.Num());
	return true;
}

void AZeroEscapeGameplayPopulator::ClearPopulation()
{
	LastSpawnedEnergyOrbCount = 0;
	LastRequiredEnergyOrbCollectionRatio = 0.0f;
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
