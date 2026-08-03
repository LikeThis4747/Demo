// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationCore.h
 * 职责：把 Request 与多层 Generation Profile 解析为不可变纯值输入，并提供随机子流和规范 Hash。
 * 边界：除解析入口外不访问 UObject；不访问 World、Mesh、导航或求解状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "PCG/ZeroEscapeGenerationAssets.h"

namespace ZeroEscape::LevelGeneration
{
	/** V6 引入多层完整结构与三维通行结果，确定性版本随之递增。 */
	inline constexpr int32 GAlgorithmVersion = 6;

	/** 相互隔离的随机子流；结构数量或放置变化不会扰动 WFC 的随机序列。 */
	enum class ERandomDomain : uint32
	{
		FloorCount = 0x1F5A57C3u,
		RequiredTwoFloorStairPlacement = 0x20B8A51Du,
		AdditionalTwoFloorStairCount = 0x39A781E5u,
		AdditionalTwoFloorStairPlacement = 0x4C267A91u,
		ThreeFloorStairwellPlacement = 0x55E1B3A7u,
		HighCeilingRoomCount = 0x6A943D2Bu,
		HighCeilingRoomPlacement = 0x73C80E4Fu,
		PlayerPursuerSpawn = 0x84D26B19u,
		WfcLayout = 0x95E27B43u
	};

	/** 一次请求完成配置解析后的唯一纯值输入。 */
	struct FResolvedGenerationInput
	{
		FZeroEscapeGenerationSignature Signature;
		FZeroEscapeSharedRouteConstraints SharedRules;
		FZeroEscapeSharedGenerationBudget Budget;
		FZeroEscapeDifficultyDefinition Difficulty;
		TArray<FZeroEscapeStructureDefinition> StructureDefinitions;
		FZeroEscapeWfcShapeWeights WfcShapeWeights;
	};

	class FGenerationCore final
	{
	public:
		/** 一次校验并复制 Profile；后续 Grid/WFC 不再回读 DataAsset。 */
		static bool ResolveGenerationInput(
			const UZeroEscapeLevelGenerationProfile& Profile,
			const FZeroEscapeGenerationRequest& Request,
			int32 PresentationVersion,
			FResolvedGenerationInput& OutInput,
			FZeroEscapeGenerationReport& OutReport);

		/** 从请求 Seed、算法版本和固定随机域派生确定性子流。 */
		static FRandomStream MakeRandomStream(
			int32 MasterSeed,
			int32 AlgorithmVersion,
			ERandomDomain Domain,
			int32 Salt = 0);

		/** 对稳定空间字段计算 63 位 Hash；排除表现资源和浮点 Transform。 */
		static int64 ComputeCanonicalLayoutHash(
			const FZeroEscapeGeneratedLevelPlan& Plan);

		/** 判断 Transform 全部分量有限、旋转已归一且 Scale 为 1。 */
		static bool IsFiniteUnitScaleTransform(const FTransform& Transform);

		/** 判断 Transform 全部分量有限、旋转已归一且三个 Scale 分量均为正。 */
		static bool IsFinitePositiveScaleTransform(const FTransform& Transform);
	};
}
