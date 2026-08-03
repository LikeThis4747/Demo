// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayPopulator.cpp
 * 职责：在 GameMode 明确提交开局时读取普通格候选，并原子放置全部玩法对象规则。
 * 边界：不订阅 Generator、不生成空间结构、不结算玩法后果；失败只回滚自己 Spawn 的对象。
 */

#include "PCG/Population/ZeroEscapeGameplayPopulator.h"

#include "Engine/World.h"
#include "PCG/Population/ZeroEscapePopulationPlacementPolicy.h"
#include "PCG/Population/ZeroEscapePopulationProfile.h"
#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePopulator, Log, All);

AZeroEscapeGameplayPopulator::AZeroEscapeGameplayPopulator()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AZeroEscapeGameplayPopulator::Populate(
	AZeroEscapeRuntimeLevelGenerator& Generator)
{
	ClearPopulation();
	UWorld* World = GetWorld();
	if (!IsValid(World)
		|| !IsValid(PopulationProfile)
		|| Generator.State != EZeroEscapeRuntimeGenerationState::Ready)
	{
		UE_LOG(LogZeroEscapePopulator, Error,
			TEXT("ZE_POPULATION result=Failure reason=InvalidSetup"));
		return false;
	}

	auto Fail = [this](const TCHAR* Reason, const int32 RuleIndex)
	{
		UE_LOG(LogZeroEscapePopulator, Error,
			TEXT("ZE_POPULATION result=Failure reason=%s rule=%d"), Reason, RuleIndex);
		ClearPopulation();
		return false;
	};

	FRandomStream Rng(Generator.GetGeneratedSeed());
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 RuleIndex = 0;
		RuleIndex < PopulationProfile->Rules.Num();
		++RuleIndex)
	{
		const FZeroEscapePlacementRule& Rule = PopulationProfile->Rules[RuleIndex];
		if (Rule.ActorClass.IsNull()
			|| Rule.OneEveryNCells <= 0
			|| Rule.MaxCount <= 0
			|| Rule.LateralCount <= 0
			|| !FMath::IsFinite(Rule.LateralSpacing)
			|| Rule.LateralSpacing < 0.0f
			|| !FMath::IsFinite(Rule.SpawnZOffsetCm)
			|| Rule.SpawnZOffsetCm < 0.0f)
		{
			return Fail(TEXT("InvalidRule"), RuleIndex);
		}

		UClass* ActorClass = Rule.ActorClass.LoadSynchronous();
		if (!IsValid(ActorClass) || ActorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			return Fail(TEXT("ActorClassLoadFailed"), RuleIndex);
		}

		TArray<FTransform> Candidates;
		if (!Generator.GetGeneratedOrdinaryGameplayCellWorldTransforms(
				Rule.bAvoidStartExitNeighbors,
				Rule.bStraightCorridorOnly,
				Candidates))
		{
			return Fail(TEXT("OrdinaryCandidateQueryFailed"), RuleIndex);
		}

		int32 TargetCount = 0;
		int32 PlannedActorCount = 0;
		const ZeroEscape::LevelGeneration::EPopulationPlacementBudgetResult BudgetResult =
			ZeroEscape::LevelGeneration::FPopulationPlacementPolicy::Evaluate(
				Candidates.Num(),
				Rule.OneEveryNCells,
				Rule.MaxCount,
				Rule.LateralCount,
				TargetCount,
				PlannedActorCount);
		if (BudgetResult !=
			ZeroEscape::LevelGeneration::EPopulationPlacementBudgetResult::Success)
		{
			return Fail(TEXT("PlacementBudgetInvalid"), RuleIndex);
		}
		constexpr int64 MaxGeneratedAddresses =
			static_cast<int64>(ZeroEscape::GenerationLimits::MaxGridCells)
			* ZeroEscape::GenerationLimits::MaxFloorCount;
		// Evaluate 只管当前规则；这里限制按 DataAsset 顺序累计的整局 Spawn 总量。
		if (static_cast<int64>(SpawnedActors.Num()) + PlannedActorCount
			> MaxGeneratedAddresses)
		{
			return Fail(TEXT("TotalSpawnBudgetExceeded"), RuleIndex);
		}
		if (TargetCount == 0)
		{
			UE_LOG(LogZeroEscapePopulator, Log,
				TEXT("ZE_POPULATION result=RuleSkipped rule=%d candidates=%d"),
				RuleIndex, Candidates.Num());
			continue;
		}

		// 规则间仍按 DataAsset 顺序消费随机数；同一 Seed 与同一规则表保持可复现。
		for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
		{
			Candidates.Swap(Index, Rng.RandRange(0, Index));
		}

		int32 SpawnedForRule = 0;
		for (int32 Index = 0; Index < TargetCount; ++Index)
		{
			const FTransform& CellTransform = Candidates[Index];
			const FVector LateralDirection =
				CellTransform.GetRotation().GetRightVector();
			const FRotator SpawnRotation = CellTransform.GetRotation().Rotator();
			for (int32 LateralIndex = 0;
				LateralIndex < Rule.LateralCount;
				++LateralIndex)
			{
				const float LateralOffset =
					(static_cast<float>(LateralIndex)
						- (Rule.LateralCount - 1) * 0.5f)
					* Rule.LateralSpacing;
				const FVector SpawnLocation = CellTransform.GetLocation()
					+ LateralDirection * LateralOffset
					+ FVector(0.0f, 0.0f, Rule.SpawnZOffsetCm);
				AActor* Spawned = World->SpawnActor<AActor>(
					ActorClass, SpawnLocation, SpawnRotation, SpawnParameters);
				if (!IsValid(Spawned))
				{
					return Fail(TEXT("ActorSpawnFailed"), RuleIndex);
				}
				SpawnedActors.Add(Spawned);
				++SpawnedForRule;
			}
		}
		if (SpawnedForRule != PlannedActorCount)
		{
			return Fail(TEXT("SpawnCountInvariantFailed"), RuleIndex);
		}

		UE_LOG(LogZeroEscapePopulator, Log,
			TEXT("ZE_POPULATION result=RuleSuccess rule=%d class=%s candidates=%d actors=%d"),
			RuleIndex, *ActorClass->GetName(), Candidates.Num(), SpawnedForRule);
	}

	UE_LOG(LogZeroEscapePopulator, Display,
		TEXT("ZE_POPULATION result=Success actors=%d"), SpawnedActors.Num());
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
