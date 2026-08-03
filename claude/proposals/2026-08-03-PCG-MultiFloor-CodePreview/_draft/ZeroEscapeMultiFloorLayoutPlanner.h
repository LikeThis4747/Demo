// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeMultiFloorLayoutPlanner.h
 * 职责：声明完整结构先放置、逐层二维 WFC、最后整栋通行图验收的纯值入口。
 * 边界：不读取 World 或表现资产，不创建 Actor/HISM，不执行导航查询。
 */

#pragma once

#include "CoreMinimal.h"

#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

namespace ZeroEscape::LevelGeneration
{
	class FMultiFloorLayoutPlanner final
	{
	public:
		/** 成功时原子提交完整 Plan；失败时 OutPlan 保持空值。 */
		static bool Solve(
			const FResolvedGenerationInput& Input,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport);

#if WITH_DEV_AUTOMATION_TESTS
		/** 仅供自动化冻结顺时针旋转与开口邻格合同。 */
		static bool ResolveStructureForTesting(
			const FZeroEscapeStructureDefinition& Definition,
			FIntPoint GridSize,
			int32 FloorCount,
			FIntVector BaseCoordinate,
			uint8 QuarterTurnCount,
			FName OpeningSetId,
			FZeroEscapeGeneratedStructure& OutStructure,
			FString& OutError);
#endif
	};
}
