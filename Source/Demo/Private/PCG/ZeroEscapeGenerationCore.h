// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationCore.h
 * 职责：声明 DataAsset 快照、K-of-N 解析、轻量流程意图、确定性随机域与规范 Hash。
 * 边界：除快照入口外只处理纯值；不访问 World、Mesh、组件，也不包含旧 Graph/Socket/A-star 模型。
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "PCG/ZeroEscapeGenerationAssets.h"

namespace ZeroEscape::LevelGeneration
{
	/** V3.2 纯 Grid/WFC 的确定性算法版本。 */
	inline constexpr int32 GAlgorithmVersion = 3;

	/** 相互隔离的随机子流；向一个阶段增加抽样不会扰动其他阶段。 */
	enum class ERandomDomain : uint32
	{
		Landmark = 0x20B8A51Du,
		OptionalLayout = 0x7D9C2E13u,
		WfcLayout = 0x95E27B43u,
		Presentation = 0xE13A5C89u
	};

	/** Generation Profile 的无 UObject、稳定排序快照。 */
	struct FGenerationProfileSnapshot
	{
		int32 ProfileVersion = 0;
		FZeroEscapeSharedRouteConstraints SharedRouteConstraints;
		TArray<FZeroEscapeDifficultyDefinition> Difficulties;
		TArray<FZeroEscapeFlowDefinition> Flows;
		FZeroEscapeWfcShapeWeights WfcShapeWeights;
	};

	/** 一次 Request 解析后的权威流程参数；Grid/WFC 不再回读 DataAsset。 */
	struct FResolvedProgressionSettings
	{
		EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;
		FName StableFlowId = NAME_None;
		int32 FlowVersion = 0;
		EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
		int32 ObjectiveCandidateCount = 0;
		int32 RequiredObjectiveCount = 0;
		int32 MaxOptionalSideBranches = 0;
		int32 MaxOptionalForwardLinks = 0;
	};

	/** Landmark 只描述路线进度和候选房间槽，不预先写死道路形状。 */
	enum class EProgressionLandmarkKind : uint8
	{
		Start = 0,
		Objective = 1,
		Exit = 2
	};

	/** 一个起点、目标或终点意图；ProgressBandIndex 从左向右单调递增。 */
	struct FProgressionLandmark
	{
		int32 StableLandmarkId = INDEX_NONE;
		EProgressionLandmarkKind Kind = EProgressionLandmarkKind::Objective;
		int32 ProgressBandIndex = INDEX_NONE;
		/** 目标房间的上下候选通道：0=下，1=上；起终点为 INDEX_NONE。 */
		int32 LaneIndex = INDEX_NONE;
		/** Objective Landmark 对应的稳定目标 Id；起终点为 INDEX_NONE。 */
		int32 StableObjectiveId = INDEX_NONE;
	};

	/** 流程层的最小输出：只含必须出现的语义位置和 K-of-N，不含抽象边或具体路径。 */
	struct FProgressionIntent
	{
		EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
		int32 ObjectiveCandidateCount = 0;
		int32 RequiredObjectiveCount = 0;
		int32 StartStableLandmarkId = INDEX_NONE;
		int32 ExitStableLandmarkId = INDEX_NONE;
		TArray<FProgressionLandmark> Landmarks;
	};

	/**
	 * 纯值核心入口。
	 * 每个构建函数都使用局部候选并在全部校验成功后写出，失败不会泄漏半成品。
	 */
	class FGenerationCore final
	{
	public:
		/** 在游戏线程校验并规范化 Profile；数组编辑顺序不会影响后续结果。 */
		static bool BuildGenerationSnapshot(
			const UZeroEscapeLevelGenerationProfile& Source,
			FGenerationProfileSnapshot& OutSnapshot,
			FZeroEscapeGenerationReport& OutReport);

		/** 解析难度、Flow 与 K/N 契约；EscapeOnly 强制 K=N=0，CollectAll 强制 K=N。 */
		static bool ResolveProgressionSettings(
			const FZeroEscapeGenerationRequest& Request,
			const FGenerationProfileSnapshot& Profile,
			FResolvedProgressionSettings& OutSettings,
			FZeroEscapeGenerationReport& OutReport);

		/** 建立完整运行签名；PresentationVersion 只标识换皮，不进入纯逻辑 Hash。 */
		static bool BuildGenerationSignature(
			const FZeroEscapeGenerationRequest& Request,
			const FGenerationProfileSnapshot& Profile,
			const FResolvedProgressionSettings& Settings,
			int32 PresentationVersion,
			FZeroEscapeGenerationSignature& OutSignature,
			FZeroEscapeGenerationReport& OutReport);

		/** 根据进度带容量确定性选择目标房间槽；不创建边或执行路径搜索。 */
		static bool BuildProgressionIntent(
			const FZeroEscapeGenerationRequest& Request,
			const FGenerationProfileSnapshot& Profile,
			const FResolvedProgressionSettings& Settings,
			FProgressionIntent& OutIntent,
			FZeroEscapeGenerationReport& OutReport);

		/** 从 Master Seed、算法版本和固定随机域派生互不串扰的确定性子流。 */
		static FRandomStream MakeRandomStream(
			int32 MasterSeed,
			int32 AlgorithmVersion,
			ERandomDomain Domain,
			int32 Salt = 0);

		/** 对规范化流程字段计算稳定 63 位 Hash；非法意图返回 0。 */
		static int64 ComputeCanonicalProgressionHash(const FProgressionIntent& Intent);

		/** 对最终逻辑格、绑定与 Anchor 计算稳定 63 位 Hash；排除表现资源和浮点 Transform。 */
		static int64 ComputeCanonicalLayoutHash(const FZeroEscapeGeneratedLevelPlan& Plan);

		/** 判断 Transform 全部分量有限、旋转已归一且 Scale 为 1。 */
		static bool IsFiniteUnitScaleTransform(const FTransform& Transform);
	};
}
