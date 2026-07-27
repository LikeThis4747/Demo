// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGridLayoutSolver.h
 * 职责：声明单层 Grid/WFC 空间布局入口，以及逻辑 Tile 到 300 cm 结构件的展开契约。
 * 边界：不读取 UObject、不创建 Actor/HISM；中间态只存在于 Solve 调用栈。
 */

#pragma once

#include "CoreMinimal.h"

#include "PCG/ZeroEscapeGenerationAssets.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

namespace ZeroEscape::LevelGeneration
{
	/** 规范结构件种类；运行时表现层据此选择具体 Mesh Binding。 */
	enum class EStructurePieceKind : uint8
	{
		Floor,
		Ceiling,
		Wall,
		WallTopTrim,
		Pillar
	};

	/** 逻辑 Tile 展开为结构件时使用的纯值坐标设置。 */
	struct FCanonicalStructureSettings
	{
		int32 LogicalTileSizeCm = 600;
		int32 StructureUnitSizeCm = 300;
		double FloorTopZCm = 0.0;
		double WallBaseZCm = 0.0;
		double CeilingPivotZCm = 300.0;
	};

	/** 一个已去重、不带资产引用的结构实例。 */
	struct FStructureInstance
	{
		EStructurePieceKind Kind = EStructurePieceKind::Floor;
		FTransform CanonicalLocalTransform = FTransform::Identity;
	};

	/** Start/Exit/中立房间局部约束与完整 16-mask WFC 的纯值布局求解器。 */
	class FGridLayoutSolver final
	{
	public:
		/**
		 * 成功时原子移交完整 Plan；失败时 OutPlan 保持空值。
		 * 单棵树内由时间序回溯处理候选失败，分片预算耗尽时才使用同一 Seed 的下一确定性子流。
		 */
		static bool Solve(
			const FZeroEscapeGenerationSignature& Signature,
			const FZeroEscapeSharedRouteConstraints& Rules,
			const FZeroEscapeWfcShapeWeights& Weights,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport);
	};

	/**
	 * 把已验证 Plan 展开为规范结构件。
	 * 共享闭边以 300 cm Edge Key 去重；柱子只放在端点、转角和 T/Cross 墙图顶点。
	 */
	bool BuildCanonicalStructureInstances(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const FCanonicalStructureSettings& Settings,
		TArray<FStructureInstance>& OutInstances,
		FString& OutError);
}
