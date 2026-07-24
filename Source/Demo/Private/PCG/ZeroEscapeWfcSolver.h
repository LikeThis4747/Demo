// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcSolver.h
 *
 * 职责：声明单层四邻域 Grid-WFC 的纯值输入、固定 16 状态变体和无回溯求解入口。
 * 边界：不读取 UObject、DataAsset、World 或 StaticMesh；不生成房间、地标和表现实例。
 *
 * V3.2 的核心前提是完整保留 OpeningMask 0..15。每条相邻边只有 Open/Closed 两种状态，
 * 因而只要 RequiredOpen/RequiredClosed 约束自身一致，任意一次合法观察都能继续扩展，不需要
 * Snapshot、Decision Trail、回溯、换 Seed 重试或备用骨架。
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Math/RandomStream.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

struct FZeroEscapeWfcShapeWeights;

namespace ZeroEscape::LevelGeneration
{
	/**
	 * Cell 是否参与本次 WFC。
	 *
	 * Outside 固定折叠为空；Required 属于必达骨架且必须至少有一条 RequiredOpen；
	 * Optional 可以折叠为空，也可以由相邻边传播为任意非空四向形态。
	 */
	enum class EGridCellDomain : uint8
	{
		Outside = 0,
		Required = 1,
		Optional = 2
	};

	/**
	 * 单个逻辑 Cell 的一元约束。
	 *
	 * RequiredOpenMask 与 RequiredClosedMask 使用 N/E/S/W 四个稳定 bit。相邻 Cell 的开闭状态
	 * 仍由 WFC 二元传播统一保证；这里不保存 Mesh Socket、宽度等级或世界坐标。
	 */
	struct FGridCellConstraint
	{
		FIntPoint Coordinate = FIntPoint::ZeroValue;
		EGridCellDomain Domain = EGridCellDomain::Outside;
		uint8 RequiredOpenMask = 0;
		uint8 RequiredClosedMask = 0;
		int32 RegionId = INDEX_NONE;
		EZeroEscapeGridRegionKind RegionKind = EZeroEscapeGridRegionKind::Corridor;
	};

	/**
	 * 一个固定 WFC 状态。
	 *
	 * OpeningMask 同时是状态的稳定逻辑身份；Weight 只影响当前合法 Domain 内的观察概率，
	 * 不得通过零权重移除任何一个 Mask。
	 */
	struct FTileVariant
	{
		uint8 OpeningMask = 0;
		int32 Weight = 1;
	};

	/**
	 * 完整 16 状态的 Simple-Tiled WFC。
	 *
	 * 求解器只负责：一元过滤、最小熵 Cell 选择、加权观察、相邻开闭边传播和原子导出。
	 * Grid 骨架雕刻、房间区域、孤岛裁剪、全局可达性与结构 Mesh 展开属于调用方职责。
	 */
	class FWfcSolver final
	{
	public:
		/** 按 OpeningMask=0..15 的稳定顺序构建完整状态集；每个状态读取对应形态权重。 */
		static void BuildCanonicalVariants(
			const FZeroEscapeWfcShapeWeights& Weights,
			TStaticArray<FTileVariant, 16>& OutVariants);

		/**
		 * 在进入随机观察前验证“必然有解”契约。
		 *
		 * 检查完整矩形覆盖、坐标唯一、Mask 合法、RequiredOpen 双向、边界关闭、
		 * Open/Closed 不冲突以及 Required Cell 非零。成功意味着至少存在“仅打开 RequiredOpen”
		 * 的构造性见证解；失败只返回可定位文本，不修改任何 World 状态。
		 */
		static bool ValidateGuaranteedSolvableConstraints(
			FIntPoint GridSize,
			const TArray<FGridCellConstraint>& Constraints,
			FString& OutError);

		/**
		 * 运行无回溯 WFC。
		 *
		 * OutOpeningMaskByCell 按 ZeroEscape::Grid::ToIndex 的 (Y, X) 稳定顺序导出。
		 * 入口立即清空输出；只有全部 Cell 折叠成功才一次性提交结果。完整 16 状态契约下出现
		 * 空 Domain 表示配置或实现不变量被破坏，函数会报告 WfcLayout/SolverInvariantViolation，
		 * 而不会用重试掩盖错误。
		 */
		static bool Solve(
			FIntPoint GridSize,
			const TArray<FGridCellConstraint>& Constraints,
			const TArray<FTileVariant>& Variants,
			FRandomStream& Random,
			TArray<uint8>& OutOpeningMaskByCell,
			FZeroEscapeGenerationReport& OutReport);
	};
}
