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

/** 一档难度中的机关密度、图距离与普通机关构成。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeHazardPopulationTuning
{
	GENERATED_BODY()

	/** 每 100 个玩法面积格期望出现多少处机关；一组地刺按一处计数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0.0"))
	float ExpectedHazardsPer100GameplayCells = 0.0f;

	/** 机关锚点之间的最小整栋通行图距离，单位：格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "1"))
	int32 MinimumRouteSpacingTiles = 1;

	/** 三类普通机关的相对权重；0 表示本难度不抽取该类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0"))
	int32 SpikeTrapWeight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0"))
	int32 BatteringRamWeight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population|Hazards",
		meta = (ClampMin = "0"))
	int32 GuidedLauncherWeight = 1;
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

/** 四类机关跨难度共享的 Class 与安装数据。 */
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
};
