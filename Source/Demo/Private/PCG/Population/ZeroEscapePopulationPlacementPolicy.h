// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePopulationPlacementPolicy.h
 * 职责：把最终空间 Plan 与三档 Population 纯值配置原子规划为机关层和资源层放置结果。
 * 边界：不访问 UObject、World 或资产；不加载 Class、不 Spawn，也不修改输入 Plan。
 */

#pragma once

#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "PCG/Population/ZeroEscapePlacementTypes.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

namespace ZeroEscape::LevelGeneration
{
	enum class EPopulationPlacementKind : uint8
	{
		Pendulum = 0,
		SpikeTrap = 1,
		BatteringRam = 2,
		GuidedLauncher = 3,
		MagneticResource = 4
	};

	enum class EPopulationPlacementResult : uint8
	{
		Success = 0,
		InvalidPlan = 1,
		InvalidConfiguration = 2,
		InvalidTraversalGraph = 3,
		SpawnBudgetExceeded = 4
	};

	struct FPopulationPlannedPlacement
	{
		EPopulationPlacementKind Kind = EPopulationPlacementKind::SpikeTrap;
		FIntVector AnchorAddress = FIntVector::ZeroValue;
		TArray<FTransform> LocalSpawnTransforms;
		/** 机关实际占用/操作格；同时用于机关间互斥与后续资源避让。 */
		TArray<FIntVector> ResourceBlockedAddresses;
	};

	struct FPopulationKindCounts
	{
		int32 Pendulums = 0;
		int32 SpikeTrapGroups = 0;
		int32 BatteringRams = 0;
		int32 GuidedLaunchers = 0;
		int32 MagneticResources = 0;
		int32 SpikeCandidateAnchors = 0;
		int32 RamCandidateAnchors = 0;
		int32 LauncherCandidateAnchors = 0;
	};

	struct FPopulationLayerStats
	{
		int32 TargetCount = 0;
		int32 ActualCount = 0;
		int32 CandidateAnchorCount = 0;
		int32 SpacingRejectedCount = 0;
		int32 UnderfilledCount = 0;
	};

	struct FPopulationPlacementPlan
	{
		TArray<FPopulationPlannedPlacement> HazardPlacements;
		TArray<FPopulationPlannedPlacement> ResourcePlacements;
		FPopulationKindCounts KindCounts;
		FPopulationLayerStats HazardStats;
		FPopulationLayerStats ResourceStats;
	};

	class FPopulationPlacementPolicy final
	{
	public:
		/** 成功时原子提交完整两层计划；失败时 OutPlan 保持空值。 */
		static EPopulationPlacementResult BuildPlan(
			const FZeroEscapeGeneratedLevelPlan& LevelPlan,
			double FloorTopZCm,
			const FZeroEscapeHazardPopulationAssembly& HazardAssembly,
			const FZeroEscapeResourcePopulationAssembly& ResourceAssembly,
			TConstArrayView<FZeroEscapePopulationDifficultySettings> Difficulties,
			FPopulationPlacementPlan& OutPlan,
			FString& OutError);
	};
}
