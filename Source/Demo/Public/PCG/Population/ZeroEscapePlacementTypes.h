// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePlacementTypes.h
 * 职责：定义机关与物理资源分层放置的共享装配数据和分难度策划数值。
 * 边界：只保存纯值与软类引用；不枚举候选、不消费随机数、不 Spawn。
 */

#pragma once

#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapePlacementTypes.generated.h"

class AActor;

/** 单类机关的基础压力输入；两项只在 Policy 中合成一次，避免重复放大同一危险。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeHazardRiskTuning
{
	GENERATED_BODY()

	FZeroEscapeHazardRiskTuning() = default;
	FZeroEscapeHazardRiskTuning(
		const float InTraversalPressurePerSecond,
		const float InHitConsequencePressure)
		: TraversalPressurePerSecond(InTraversalPressurePerSecond)
		, HitConsequencePressure(InHitConsequencePressure)
	{
	}

	/** 代表性通行时间每秒的压力；Policy 用真实格宽除以玩家名义地面速度计算时间。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure",
		meta = (ClampMin = "0.0"))
	float TraversalPressurePerSecond = 0.0f;

	/** 失误受击后果带来的压力贡献。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure",
		meta = (ClampMin = "0.0"))
	float HitConsequencePressure = 0.0f;
};

/** 普通机关共享的评分、分组与组合参数；它们只改变合法候选的相对概率。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeHazardPlacementScoringTuning
{
	GENERATED_BODY()

	/** 最终 log2 分数的绝对上限；5 对应单项权重约 1/32 到 32。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Scoring",
		meta = (ClampMin = "0.25", ClampMax = "16.0"))
	float MaxAbsLog2Score = 5.0f;

	/** 类型阶段只读取每类最佳的若干不同锚点，禁止按候选数量累加。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Scoring",
		meta = (ClampMin = "1", ClampMax = "8"))
	int32 TypeContextTopAnchorCount = 3;

	/** 最佳锚点上下文回写到基础类型权重的强度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Scoring",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float TypeContextStrength = 0.5f;

	/** 通行图上属于同一机关组的最大边数距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Groups",
		meta = (ClampMin = "1", ClampMax = "8"))
	int32 GroupRadiusTiles = 3;

	/** 组压力接近变化目标时的最大 log2 奖励或惩罚幅度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Groups",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float PressureFitLog2Strength = 1.0f;

	/** 超过目标后，从最高奖励衰减到最低权重所需的目标压力比例。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Groups",
		meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float PressureOverloadWidthRatio = 0.5f;

	/** 同一进度区域共享的 Seed 起伏幅度，单位为压力。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Groups",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float RegionTargetVariation = 1.0f;

	/** 不同组锚点的确定性抖动幅度，单位为压力。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Groups",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float AnchorTargetVariation = 1.0f;

	/** 从玩家出生点到 Exit 的整体递增强度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Scoring",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ProgressLog2Strength = 0.25f;

	/** 发射器位于转角并能提前被观察时的轻度位置奖励。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Scoring",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float LauncherCornerLog2Bonus = 0.5f;

	/**
	 * 候选远离最近已放机关时的最大 log2 位置奖励；只鼓励覆盖空白路线，
	 * 不设置最大空白长度，也不会令任何合法候选权重归零。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Scoring",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float RouteCoverageLog2Bonus = 0.5f;

	/** 刺轮与相邻冲锤的强组合奖励。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Combinations",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float WheelRamLog2Bonus = 3.0f;

	/** 刺轮与相邻地刺的中等组合奖励。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Combinations",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float WheelSpikeLog2Bonus = 2.25f;

	/** 刺轮没有冲锤或地刺搭档时的轻微贡献；保持有限且不归零。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Combinations",
		meta = (ClampMin = "-4.0", ClampMax = "0.0"))
	float SoloWheelLog2Contribution = -0.5f;

	/** 多样性只观察最近多少个普通机关。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Diversity",
		meta = (ClampMin = "1", ClampMax = "16"))
	int32 RecentKindWindow = 4;

	/** 最近窗口全部同类时的最大 log2 降权；不禁止同类连续。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Diversity",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MaximumRecentKindPenalty = 0.5f;

	/** 五类机关的独立风险输入；Policy 只把每项两维相加一次。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure")
	FZeroEscapeHazardRiskTuning PendulumRisk =
		FZeroEscapeHazardRiskTuning(1.333333f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure")
	FZeroEscapeHazardRiskTuning SpikeRisk =
		FZeroEscapeHazardRiskTuning(0.666667f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure")
	FZeroEscapeHazardRiskTuning RamRisk =
		FZeroEscapeHazardRiskTuning(1.333333f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure")
	FZeroEscapeHazardRiskTuning LauncherRisk =
		FZeroEscapeHazardRiskTuning(0.333333f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure")
	FZeroEscapeHazardRiskTuning WheelRisk =
		FZeroEscapeHazardRiskTuning(0.166667f, 0.25f);

	/** 组合压力只用于组预算诊断，与上面的选点奖励分开。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float WheelRamPressureBonus = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Pressure",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float WheelSpikePressureBonus = 0.5f;
};

/** 一档难度中的机关密度、普通机关构成与变化压力目标。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeHazardPopulationTuning
{
	GENERATED_BODY()

	/** 每 100 个玩法面积格期望出现多少处机关；一组地刺按一处计数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0.0"))
	float ExpectedHazardsPer100GameplayCells = 0.0f;

	/** 四类普通机关的基础类型权重；0 表示本难度不抽取该类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0"))
	int32 SpikeTrapWeight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0"))
	int32 BatteringRamWeight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0"))
	int32 GuidedLauncherWeight = 1;

	/** 默认保持 0，避免尚未装配刺轮 Class 的旧 Profile 意外生成刺轮。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0"))
	int32 SpikeWheelWeight = 0;

	/** 玩家出生侧的新组基础目标压力。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Groups",
		meta = (ClampMin = "0.1"))
	float TargetPressureAtStart = 4.0f;

	/** Exit 侧的新组基础目标压力。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Groups",
		meta = (ClampMin = "0.1"))
	float TargetPressureAtExit = 6.0f;
};

/** 一档难度中的磁力资源密度与图距离。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeResourcePopulationTuning
{
	GENERATED_BODY()

	/** 每 100 个玩法面积格期望出现多少个磁力资源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Resources",
		meta = (ClampMin = "0.0"))
	float ExpectedResourcesPer100GameplayCells = 0.0f;

	/** 资源锚点之间的最小整栋通行图距离，单位：格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Resources",
		meta = (ClampMin = "1"))
	int32 MinimumRouteSpacingTiles = 1;
};

/** Profile 必须恰好提供 Easy、Normal、Hard 各一条。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapePopulationDifficultySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	FZeroEscapeHazardPopulationTuning Hazards;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	FZeroEscapeResourcePopulationTuning Resources;
};

/** 五类机关跨难度共享的 Class、安装数据与软评分参数。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeHazardPopulationAssembly
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Classes")
	TSoftClassPtr<AActor> PendulumClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Classes")
	TSoftClassPtr<AActor> SpikeTrapClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Classes")
	TSoftClassPtr<AActor> BatteringRamClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Classes")
	TSoftClassPtr<AActor> GuidedLauncherClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Classes")
	TSoftClassPtr<AActor> SpikeWheelClass;

	/** 普通机关共享的评分、分组、压力与组合参数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Scoring")
	FZeroEscapeHazardPlacementScoringTuning PlacementScoring;

	/** 一处地刺生成的 Actor 数；当前正式装配为两个。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Spike",
		meta = (ClampMin = "1"))
	int32 SpikeTrapActorCount = 2;

	/** 同组地刺中心间距。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Spike",
		meta = (ClampMin = "0.0", Units = "cm"))
	float SpikeTrapLateralSpacingCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Spike",
		meta = (Units = "cm"))
	float SpikeTrapFloorOffsetCm = 0.0f;

	/** 墙面边界到完全缩回的冲锤 Actor 原点的向内距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Ram",
		meta = (ClampMin = "0.0", Units = "cm"))
	float BatteringRamWallInsetCm = 50.0f;

	/** 地板面到冲锤 Actor 原点的高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Ram",
		meta = (ClampMin = "0.0", Units = "cm"))
	float BatteringRamMountHeightCm = 100.0f;

	/** 后墙边界到发射器 Actor 原点的向内距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Launcher",
		meta = (ClampMin = "0.0", Units = "cm"))
	float GuidedLauncherWallInsetCm = 0.0f;

	/** 地板面到发射器 Actor 原点的高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards|Launcher",
		meta = (ClampMin = "0.0", Units = "cm"))
	float GuidedLauncherMountHeightCm = 100.0f;
};

/** 当前磁力资源跨难度共享的 Class 和地板装配数据。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeResourcePopulationAssembly
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Resources")
	TSoftClassPtr<AActor> MagneticResourceClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Resources",
		meta = (Units = "cm"))
	float SpawnZOffsetCm = 50.0f;

	/** 从逻辑格边缘向内保留的安全距离；不参与资源之间的泊松间距。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Resources",
		meta = (ClampMin = "0.0", Units = "cm"))
	float PlacementFootprintRadiusCm = 75.0f;

	/** 高压力组安全进入格的最大 log2 奖励；资源仍使用独立随机域。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Resources|Support",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float HighPressureSupportLog2Bonus = 1.0f;

	/** 距离已有资源与机关越远时的最大 log2 奖励；只改善覆盖，不形成硬间距。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Resources|Support",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float RouteCoverageLog2Bonus = 1.0f;
};
