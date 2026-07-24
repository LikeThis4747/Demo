// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGridLayoutSolver.h
 * 职责：定义 V3.2 单层 Grid 布局入口，以及逻辑 Tile 到 300 cm 结构件的规范展开契约。
 * 边界：不读取 UObject 资产、不创建 Actor/HISM，不保留 Socket、A* 或 WFC 回溯兼容层。
 * 状态 Owner：求解中间态只存在于 Solve 调用栈；成功时才一次性移交最终 Plan。
 */

#pragma once

#include "CoreMinimal.h"

#include "PCG/ZeroEscapeGenerationTypes.h"
#include "ZeroEscapeGenerationCore.h"

struct FZeroEscapeWfcShapeWeights;

namespace ZeroEscape::LevelGeneration
{
	/**
	 * Grid 算法的纯值设置。这些值必须在进入 Solve 前由 Profile 校验；
	 * Solver 仍会 fail-closed，避免非法资产值在运行时被静默修正。
	 */
	struct FGridLayoutSettings
	{
		/** 单层逻辑网格宽高，单位 Tile。 */
		FIntPoint GridSize = FIntPoint(24, 16);

		/** 一个 WFC 逻辑 Tile 的世界边长，单位 cm；V3.2 固定为 600。 */
		int32 LogicalTileSizeCm = 600;

		/** Objective 房间单边占用的逻辑 Tile 数；首版固定为 2。 */
		int32 RoomSizeTiles = 2;

		/** Start/Exit 之间可用的 Objective 推进带数；每带提供上下两个 Lane。 */
		int32 ObjectiveProgressBandCount = 3;

		/** 每个可选侧支中心周围参与 WFC 的 Manhattan 半径，单位 Tile。 */
		int32 OptionalEnvelopeRadius = 2;

		/** 无目标时 Start -> Exit 最短路的共享上限，单位边。 */
		int32 MaxRequiredRouteLengthTiles = 64;

		/** 收集路线相对 Start -> Exit 最短路允许增加的共享上限，单位边。 */
		int32 MaxRequiredRouteExtraTiles = 16;

		/** 玩法 Anchor 相对 Generator 原点的高度，单位 cm。 */
		double GameplayAnchorHeightCm = 100.0;
	};

	/** 已解析的单局请求；不持有 Profile/DataAsset 指针。 */
	struct FGridLayoutRequest
	{
		/** Seed/难度/Flow/版本签名，原样进入最终 Plan。 */
		FZeroEscapeGenerationSignature Signature;

		/** Core 输出的 Start/Objective/Exit 进度意图。 */
		FProgressionIntent Progression;

		/** 有限 Optional Envelope 中心数量上限；不是必须生成的死路配额。 */
		int32 MaxOptionalSideBranches = 0;

		/** 最多为多少 Objective 房增加第二 Gate；不是路口数量配额。 */
		int32 MaxOptionalForwardLinks = 0;
	};

	/** 规范结构件种类；运行时表现层使用对应 Binding 选择具体 Mesh。 */
	enum class EStructurePieceKind : uint8
	{
		Floor,
		Ceiling,
		Wall,
		WallTopTrim,
		Pillar
	};

	/**
	 * HydroLab 首套表现的规范坐标参数。算法只用它建立可复现 Transform，
	 * 具体 Mesh Pivot 的差异仍必须由 Presentation Binding 的 PivotCorrection 吸收。
	 */
	struct FCanonicalStructureSettings
	{
		/** 逻辑 Tile 边长，单位 cm；V3.2 固定 600。 */
		int32 LogicalTileSizeCm = 600;

		/** Floor/Ceiling/Wall/Trim 的基础拼装单元，单位 cm；HydroLab 固定 300。 */
		int32 StructureUnitSizeCm = 300;

		/** Floor 规范 Pivot 高度，单位 cm。 */
		double FloorTopZCm = 0.0;

		/** Wall/Pillar 规范底部 Pivot 高度，单位 cm。 */
		double WallBaseZCm = 0.0;

		/** Ceiling 与顶部 Trim 的规范 Pivot 高度，单位 cm。 */
		double CeilingPivotZCm = 300.0;
	};

	/** 一个已去重、不带资产引用的结构实例。 */
	struct FStructureInstance
	{
		/** 选择 Presentation Binding 的稳定类别。 */
		EStructurePieceKind Kind = EStructurePieceKind::Floor;

		/** Generator Local 中的规范 Transform；始终保持 Unit Scale。 */
		FTransform CanonicalLocalTransform = FTransform::Identity;
	};

	/** 固定骨架 + 完整 16-mask WFC 的纯值布局求解器。 */
	class FGridLayoutSolver final
	{
	public:
		/**
		 * 生成一份原子性 Plan。正常 Seed 不重试；false 仅表示配置、容量或算法不变量被破坏。
		 * 失败时 OutPlan 保持默认空值，OutReport 指向首个可操作问题。
		 */
		static bool Solve(
			const FGridLayoutRequest& Request,
			const FGridLayoutSettings& Settings,
			const FZeroEscapeWfcShapeWeights& Weights,
			int32 MasterSeed,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport);
	};

	/**
	 * 把已验证 Plan 展开为可直接分组实例化的规范结构件。
	 * 共享闭边以 300 cm Edge Key 去重；柱子只放在端点、转角和 T/Cross 墙图顶点。
	 */
	bool BuildCanonicalStructureInstances(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const FCanonicalStructureSettings& Settings,
		TArray<FStructureInstance>& OutInstances,
		FString& OutError);
}
