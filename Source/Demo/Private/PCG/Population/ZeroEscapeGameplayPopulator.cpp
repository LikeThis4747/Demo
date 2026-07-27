// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayPopulator.cpp
 * 职责：绑定生成器完成事件，读取只读空间查询，按 Population Profile 用本局 Seed 确定性 Spawn 对象。
 * 边界：不生成空间结构，不结算玩法后果；只负责放置与本局对象清理。
 */

#include "PCG/Population/ZeroEscapeGameplayPopulator.h"

#include "Engine/World.h"
#include "PCG/Population/ZeroEscapePopulationProfile.h"
#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"

DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePopulator, Log, All);

/** 创建关闭 Tick 的放置器。 */
AZeroEscapeGameplayPopulator::AZeroEscapeGameplayPopulator()
{
	PrimaryActorTick.bCanEverTick = false;
}

/** 绑定关联生成器完成事件；若绑定前生成已完成则立即补放一次，避免漏过。 */
void AZeroEscapeGameplayPopulator::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(Generator))
	{
		UE_LOG(LogZeroEscapePopulator, Warning,
			TEXT("%s 未指定 Generator，放置层不会触发。"), *GetName());
		return;
	}

	Generator->OnGenerationFinished.AddDynamic(
		this, &AZeroEscapeGameplayPopulator::HandleGenerationFinished);

	if (Generator->State == EZeroEscapeRuntimeGenerationState::Ready)
	{
		HandleGenerationFinished(true, Generator->LastReport);
	}
}

/** 结束时解绑事件并清理本局对象。 */
void AZeroEscapeGameplayPopulator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Generator))
	{
		Generator->OnGenerationFinished.RemoveDynamic(
			this, &AZeroEscapeGameplayPopulator::HandleGenerationFinished);
	}
	ClearSpawnedActors();
	Super::EndPlay(EndPlayReason);
}

/** 生成成功后先清旧对象，再对每条规则确定性抽取候选格并 Spawn。 */
void AZeroEscapeGameplayPopulator::HandleGenerationFinished(
	bool bSuccess, const FZeroEscapeGenerationReport& /*Report*/)
{
	ClearSpawnedActors();

	if (!bSuccess || !IsValid(Generator) || !IsValid(PopulationProfile))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FRandomStream Rng(Generator->GetGeneratedSeed());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FZeroEscapePlacementRule& Rule : PopulationProfile->Rules)
	{
		UClass* ActorClass = Rule.ActorClass.LoadSynchronous();
		if (ActorClass == nullptr)
		{
			continue;
		}

		TArray<FTransform> Candidates;
		if (!Generator->GetGeneratedCellWorldTransforms(
				Rule.TargetRegionKind, Rule.bAvoidStartExitNeighbors, Candidates)
			|| Candidates.Num() == 0)
		{
			continue;
		}

		// 确定性洗牌后按稀疏度取前若干个，兼顾随机分布与 Seed 复现。
		for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
		{
			Candidates.Swap(Index, Rng.RandRange(0, Index));
		}

		const int32 TargetCount = FMath::Min(
			Rule.MaxCount, Candidates.Num() / FMath::Max(Rule.OneEveryNCells, 1));
		for (int32 Index = 0; Index < TargetCount; ++Index)
		{
			AActor* Spawned = World->SpawnActor<AActor>(
				ActorClass, Candidates[Index], SpawnParameters);
			if (IsValid(Spawned))
			{
				SpawnedActors.Add(Spawned);
			}
		}

		UE_LOG(LogZeroEscapePopulator, Log,
			TEXT("%s 放置 %s：候选 %d 格，放置 %d 个。"),
			*GetName(), *ActorClass->GetName(), Candidates.Num(), TargetCount);
	}
}

/** 销毁本局已 Spawn 的全部对象并清空登记。 */
void AZeroEscapeGameplayPopulator::ClearSpawnedActors()
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
