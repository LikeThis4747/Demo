// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePopulationPlacementPolicy.h
 * 职责：把最终空间 Plan 与三档 Population 配置原子规划为机关、资源和奖励光团放置结果。
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
		MagneticResource = 4,
		SpikeWheel = 5,
		EnergyOrb = 6
	};

	enum class EPopulationPlacementResult : uint8
	{
		Success = 0,
		InvalidPlan = 1,
		InvalidConfiguration = 2,
		InvalidTraversalGraph = 3,
		SpawnBudgetExceeded = 4
	};

	/** 纯值计划写给延迟生成刺轮的预 BeginPlay 配置。 */
	struct FPopulationSpikeWheelSpawnConfig
	{
		bool bIsConfigured = false;
		int32 RouteVariantSeed = 0;
	};

	/** PCG 周期机关的确定性初始相位；只控制启动偏移，不参与摆位合法性。 */
	struct FPopulationPeriodicPhaseConfig
	{
		bool bIsConfigured = false;
		float NormalizedPhase01 = 0.0f;
	};

	/** 返回需要周期初始相位的机关类型；发射器由玩家触发，不属于周期机关。 */
	constexpr bool IsPeriodicHazardKind(const EPopulationPlacementKind Kind)
	{
		return Kind == EPopulationPlacementKind::Pendulum
			|| Kind == EPopulationPlacementKind::SpikeTrap
			|| Kind == EPopulationPlacementKind::BatteringRam
			|| Kind == EPopulationPlacementKind::SpikeWheel;
	}

	/** 被选中机关的六项 log2 评分拆解，供测试与调参定位。 */
	struct FPopulationPlacementScoreBreakdown
	{
		float Position = 0.0f;
		float Progress = 0.0f;
		float GroupPressure = 0.0f;
		float Combination = 0.0f;
		float Diversity = 0.0f;
		float Diagnostic = 0.0f;
		float TotalLog2Score = 0.0f;
	};

	/** 最终机关组诊断；资源层只读安全进入格与支持优先级。 */
	struct FPopulationHazardGroupRecord
	{
		FIntVector AnchorAddress = FIntVector::ZeroValue;
		TArray<int32> PlacementIndices;
		TArray<FIntVector> SafeApproachAddresses;
		float TargetPressure = 0.0f;
		float ActualPressure = 0.0f;
		float ResourceSupportPriority = 0.0f;
	};

	struct FPopulationPlannedPlacement
	{
		EPopulationPlacementKind Kind = EPopulationPlacementKind::SpikeTrap;
		FIntVector AnchorAddress = FIntVector::ZeroValue;
		TArray<FTransform> LocalSpawnTransforms;
		/** 机关实际占用/操作格；同时用于机关间互斥与后续资源避让。 */
		TArray<FIntVector> ResourceBlockedAddresses;
		FPopulationSpikeWheelSpawnConfig SpikeWheel;
		FPopulationPeriodicPhaseConfig PeriodicPhase;
		FPopulationPlacementScoreBreakdown Score;
	};

	struct FPopulationKindCounts
	{
		int32 Pendulums = 0;
		int32 SpikeTrapGroups = 0;
		int32 BatteringRams = 0;
		int32 GuidedLaunchers = 0;
		int32 SpikeWheels = 0;
		int32 MagneticResources = 0;
		int32 EnergyOrbs = 0;
		int32 SpikeCandidateAnchors = 0;
		int32 RamCandidateAnchors = 0;
		int32 LauncherCandidateAnchors = 0;
		int32 WheelCandidateAnchors = 0;
		int32 WheelRamCombinations = 0;
		int32 WheelSpikeCombinations = 0;
		int32 UnpairedWheels = 0;
		int32 LiteralSoloWheels = 0;
	};

	struct FPopulationLayerStats
	{
		int32 TargetCount = 0;
		int32 ActualCount = 0;
		int32 CandidateAnchorCount = 0;
		int32 SpacingRejectedCount = 0;
		int32 UnderfilledCount = 0;
		/** 机关层使用，单位为十分之一标准机关；资源层保持 0。 */
		int32 TargetBudgetTenths = 0;
		int32 ActualBudgetTenths = 0;
		int32 UnderfilledBudgetTenths = 0;
		/** 达到期望量后，为消除长空白覆盖缺口追加的放置数。 */
		int32 CoverageSupplementCount = 0;
	};

	struct FPopulationPlacementPlan
	{
		TArray<FPopulationPlannedPlacement> HazardPlacements;
		TArray<FPopulationPlannedPlacement> ResourcePlacements;
		TArray<FPopulationPlannedPlacement> EnergyOrbPlacements;
		TArray<FPopulationHazardGroupRecord> HazardGroups;
		FPopulationKindCounts KindCounts;
		FPopulationLayerStats HazardStats;
		FPopulationLayerStats ResourceStats;
	};

	class FPopulationPlacementPolicy final
	{
	public:
		/** 成功时原子提交完整三类计划；失败时 OutPlan 保持空值。 */
		static EPopulationPlacementResult BuildPlan(
			const FZeroEscapeGeneratedLevelPlan& LevelPlan,
			double FloorTopZCm,
			float PlayerMaxWalkSpeedCmPerSecond,
			const FZeroEscapeHazardPopulationAssembly& HazardAssembly,
			const FZeroEscapeResourcePopulationAssembly& ResourceAssembly,
			TConstArrayView<FZeroEscapePopulationDifficultySettings> Difficulties,
			FPopulationPlacementPlan& OutPlan,
			FString& OutError);
	};
}
