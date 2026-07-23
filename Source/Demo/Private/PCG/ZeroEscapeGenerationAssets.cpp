// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationAssets.cpp
 * 职责：严格校验 PCG Profile、逻辑 Module Catalog 与 Presentation Profile 的作者配置。
 * 边界：不生成布局、不修改资产、不静默钳制，也不把第三方素材测量值写成 C++ 常量。
 * 状态 Owner：无；校验只读取传入 DataAsset 并返回首个可定位错误。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#include "CollisionQueryParams.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "PCG/ZeroEscapeLayoutSolver.h"
#include "UObject/Class.h"

namespace
{
	/** Portal 朝向只允许数值误差；Bounds 另留明确的测量/作者余量，但不能用余量掩盖错误轴向或 Pivot。 */
	constexpr float PortalDirectionDotTolerance = 0.9999f;
	constexpr double PortalBoundsToleranceCm = 50.0;
	constexpr double PresentationBoundsToleranceCm = 2.0;
	constexpr double LogicalBoundsToleranceCm = 2.0;

	/** 所有可持久化 Transform 都拒绝 NaN、非归一化旋转和缩放，保证网格旋转与 Hash 前提成立。 */
	bool IsFiniteUnitScaleTransform(const FTransform& Transform)
	{
		return !Transform.ContainsNaN()
			&& Transform.GetRotation().IsNormalized()
			&& Transform.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER);
	}

	bool IsFiniteValidBounds(const FBox& Bounds)
	{
		if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN())
		{
			return false;
		}

		const FVector Size = Bounds.GetSize();
		return !Size.ContainsNaN()
			&& Size.X > UE_SMALL_NUMBER
			&& Size.Y > UE_SMALL_NUMBER
			&& Size.Z > UE_SMALL_NUMBER;
	}

	bool IsCellOffsetInsideFootprint(
		const FIntVector& CellOffset,
		const FIntVector& Footprint)
	{
		return CellOffset.X >= 0 && CellOffset.X < Footprint.X
			&& CellOffset.Y >= 0 && CellOffset.Y < Footprint.Y
			&& CellOffset.Z >= 0 && CellOffset.Z < Footprint.Z;
	}

	bool IsCellOnDirectionBoundary(
		const FIntVector& CellOffset,
		const FIntVector& Footprint,
		const EZeroEscapeCardinalDirection Direction)
	{
		switch (Direction)
		{
		case EZeroEscapeCardinalDirection::North:
			return CellOffset.Y == Footprint.Y - 1;
		case EZeroEscapeCardinalDirection::East:
			return CellOffset.X == Footprint.X - 1;
		case EZeroEscapeCardinalDirection::South:
			return CellOffset.Y == 0;
		case EZeroEscapeCardinalDirection::West:
			return CellOffset.X == 0;
		default:
			return false;
		}
	}

	FBox MakeLogicalFootprintBounds(
		const FIntVector& Footprint,
		const FVector& CellSize)
	{
		const FVector Extent(
			static_cast<double>(Footprint.X) * CellSize.X * 0.5,
			static_cast<double>(Footprint.Y) * CellSize.Y * 0.5,
			static_cast<double>(Footprint.Z) * CellSize.Z);
		return FBox(
			FVector(-Extent.X, -Extent.Y, 0.0),
			FVector(Extent.X, Extent.Y, Extent.Z));
	}

	bool IsValidDifficulty(const EZeroEscapeDifficulty Difficulty)
	{
		switch (Difficulty)
		{
		case EZeroEscapeDifficulty::Easy:
		case EZeroEscapeDifficulty::Normal:
		case EZeroEscapeDifficulty::Hard:
			return true;
		default:
			return false;
		}
	}

	bool IsValidTopologyRole(const EZeroEscapeTopologyRole Role)
	{
		switch (Role)
		{
		case EZeroEscapeTopologyRole::MainPath:
		case EZeroEscapeTopologyRole::ShortLeaf:
		case EZeroEscapeTopologyRole::ForwardRejoin:
		case EZeroEscapeTopologyRole::Start:
		case EZeroEscapeTopologyRole::Exit:
			return true;
		default:
			return false;
		}
	}

	bool IsValidObjectiveRole(const EZeroEscapeTopologyRole Role)
	{
		return Role == EZeroEscapeTopologyRole::MainPath
			|| Role == EZeroEscapeTopologyRole::ShortLeaf
			|| Role == EZeroEscapeTopologyRole::ForwardRejoin;
	}

	bool IsValidAnchorType(const EZeroEscapeGameplayAnchorType Type)
	{
		switch (Type)
		{
		case EZeroEscapeGameplayAnchorType::PlayerSpawn:
		case EZeroEscapeGameplayAnchorType::Exit:
		case EZeroEscapeGameplayAnchorType::Objective:
		case EZeroEscapeGameplayAnchorType::Reward:
		case EZeroEscapeGameplayAnchorType::Trap:
		case EZeroEscapeGameplayAnchorType::EnemySpawn:
			return true;
		default:
			return false;
		}
	}

	bool IsValidLayoutPolicy(const EZeroEscapeLayoutPolicy Policy)
	{
		switch (Policy)
		{
		case EZeroEscapeLayoutPolicy::WfcSingleCell:
		case EZeroEscapeLayoutPolicy::SocketModule:
		case EZeroEscapeLayoutPolicy::Cap:
		case EZeroEscapeLayoutPolicy::DecorationOnly:
			return true;
		default:
			return false;
		}
	}

	bool IsValidSocketPolicy(const EZeroEscapeSocketPolicy Policy)
	{
		// Optional 尚无 Finalize 状态；首个单层竖切只允许“必须连接”或“可连接/封口”。
		return Policy == EZeroEscapeSocketPolicy::Required
			|| Policy == EZeroEscapeSocketPolicy::Sealable;
	}

	bool TryGetPlanarDirectionVector(
		const EZeroEscapeCardinalDirection Direction,
		FVector& OutDirection)
	{
		switch (Direction)
		{
		case EZeroEscapeCardinalDirection::North:
			OutDirection = FVector::YAxisVector;
			return true;
		case EZeroEscapeCardinalDirection::East:
			OutDirection = FVector::XAxisVector;
			return true;
		case EZeroEscapeCardinalDirection::South:
			OutDirection = -FVector::YAxisVector;
			return true;
		case EZeroEscapeCardinalDirection::West:
			OutDirection = -FVector::XAxisVector;
			return true;
		default:
			// Up/Down 的 Portal roll/frame 契约尚未定义；首个单层版本明确拒绝。
			return false;
		}
	}

	bool PortalFrameMatchesPlanarDirection(const FZeroEscapeModulePortal& Portal)
	{
		// Portal Frame 的 +X 是“向模块外”，+Z 是“向上”。连接时可由统一 Frame 合成，
		// 无需知道 SFCorridors 或未来素材在导入前使用了什么轴约定。
		FVector ExpectedForward = FVector::ZeroVector;
		if (!TryGetPlanarDirectionVector(Portal.Direction, ExpectedForward))
		{
			return false;
		}

		const FQuat Rotation = Portal.LocalTransform.GetRotation();
		return Rotation.GetAxisX().Dot(ExpectedForward) >= PortalDirectionDotTolerance
			&& Rotation.GetAxisZ().Dot(FVector::ZAxisVector) >= PortalDirectionDotTolerance;
	}

	bool AreConnectorSignaturesEqual(
		const FZeroEscapeModulePortal& A,
		const FZeroEscapeModulePortal& B)
	{
		return A.ConnectorTypeId == B.ConnectorTypeId
			&& A.WidthClass == B.WidthClass
			&& A.HeightLayer == B.HeightLayer;
	}

	int32 CountAnchorType(
		const FZeroEscapeModuleDefinition& Module,
		const EZeroEscapeGameplayAnchorType Type)
	{
		int32 Count = 0;
		for (const FZeroEscapeModuleAnchor& Anchor : Module.GameplayAnchors)
		{
			Count += Anchor.Type == Type ? 1 : 0;
		}
		return Count;
	}

	int32 CountQuarterTurnVariants(const int32 Mask)
	{
		int32 Count = 0;
		for (int32 Bit = 0; Bit < 4; ++Bit)
		{
			Count += (Mask & (1 << Bit)) != 0 ? 1 : 0;
		}
		return Count;
	}

	bool CanCapPortalOpposeSource(
		const FZeroEscapeModulePortal& SourcePortal,
		const FZeroEscapeModuleDefinition& CapModule)
	{
		check(CapModule.Portals.Num() == 1);
		const EZeroEscapeCardinalDirection RequiredDirection =
			ZeroEscape::LevelGeneration::OppositeDirection(SourcePortal.Direction);
		for (uint8 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
		{
			if ((CapModule.AllowedQuarterTurnsMask & (1 << QuarterTurns)) != 0
				&& ZeroEscape::LevelGeneration::RotateDirection(
					CapModule.Portals[0].Direction,
					QuarterTurns) == RequiredDirection)
			{
				return true;
			}
		}
		return false;
	}

	bool CollisionProfileResolves(const FName ProfileName)
	{
		ECollisionChannel ObjectType = ECC_WorldStatic;
		FCollisionResponseParams ResponseParams;
		return UCollisionProfile::GetChannelAndResponseParams(
			ProfileName,
			ObjectType,
			ResponseParams);
	}
}

bool UZeroEscapeLevelGenerationProfile::IsConfigured(FString& OutError) const
{
	OutError.Reset();

	// 第一层验证共享体验与搜索边界。困难档也不能绕过这些共同限制，
	// 因而“提高难度”不会偷偷变成扩大地图或允许更长回头路。
	const FZeroEscapeSharedRouteConstraints& Shared = SharedRouteConstraints;
	if (ProfileVersion < 1)
	{
		OutError = TEXT("Generation Profile 的 ProfileVersion 必须至少为 1。");
		return false;
	}

	if (Shared.GridExtentCells.X < 8
		|| Shared.GridExtentCells.X > 64
		|| Shared.GridExtentCells.Y < 8
		|| Shared.GridExtentCells.Y > 64
		|| Shared.CriticalPathNodeCount < 4
		|| Shared.CriticalPathNodeCount > ZeroEscape::GenerationLimits::FirstPassMaxCriticalPathNodes
		|| Shared.MaxLeafOneWayEdgeCount < 0
		|| Shared.MaxLeafOneWayEdgeCount > Shared.CriticalPathNodeCount - 2
		|| Shared.MaxRequiredRouteExtraEdgeCount < 0
		|| Shared.MaxObjectiveCandidateCount < 1
		|| Shared.MaxObjectiveCandidateCount > ZeroEscape::GenerationLimits::MaxObjectiveCandidates
		|| Shared.MaxProgressionSearchStates < 1
		|| Shared.MaxProgressionSearchStates > ZeroEscape::GenerationLimits::FirstPassMaxProgressionSearchStates
		|| Shared.AStarStraightStepCost < 1
		|| Shared.AStarTurnPenalty < 0)
	{
		OutError = TEXT("Generation Profile 的共享路线约束非法；请检查 GridExtent、CriticalPath、折返、K-of-N 与 A* 成本字段。");
		return false;
	}

	if (Flows.IsEmpty())
	{
		OutError = TEXT("Generation Profile 至少需要一个 Flow。");
		return false;
	}

	TSet<FName> FlowIds;
	bool bHasDefaultEscapeFlow = false;
	bool bHasObjectiveFlow = false;
	bool bHasKOfNFlow = false;
	for (const FZeroEscapeFlowDefinition& Flow : Flows)
	{
		// Flow Id 与版本共同构成可复现身份；数组顺序只是编辑器展示顺序。
		if (Flow.StableFlowId.IsNone()
			|| Flow.FlowVersion < 1
			|| FlowIds.Contains(Flow.StableFlowId))
		{
			OutError = FString::Printf(
				TEXT("Generation Profile 的 Flow Id 为空、重复或版本非法：%s。"),
				*Flow.StableFlowId.ToString());
			return false;
		}
		FlowIds.Add(Flow.StableFlowId);

		bool bNeedsObjectiveRoles = false;
		switch (Flow.CompletionRule)
		{
		case EZeroEscapeCompletionRule::EscapeOnly:
			bHasDefaultEscapeFlow |= Flow.StableFlowId == TEXT("EscapeOnly");
			break;
		case EZeroEscapeCompletionRule::CollectAll:
			bNeedsObjectiveRoles = true;
			bHasObjectiveFlow = true;
			break;
		case EZeroEscapeCompletionRule::CollectKOfN:
			bNeedsObjectiveRoles = true;
			bHasObjectiveFlow = true;
			bHasKOfNFlow = true;
			break;
		default:
			OutError = FString::Printf(
				TEXT("Generation Profile 的 Flow %s 使用了未知 CompletionRule。"),
				*Flow.StableFlowId.ToString());
			return false;
		}

		if (bNeedsObjectiveRoles != !Flow.AllowedObjectiveRoles.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Generation Profile 的 Flow %s 与 AllowedObjectiveRoles 不一致。"),
				*Flow.StableFlowId.ToString());
			return false;
		}

		uint8 ObjectiveRoleMask = 0;
		for (const EZeroEscapeTopologyRole Role : Flow.AllowedObjectiveRoles)
		{
			if (!IsValidObjectiveRole(Role))
			{
				OutError = FString::Printf(
					TEXT("Generation Profile 的 Flow %s 把 Start/Exit 或未知角色作为目标位置。"),
					*Flow.StableFlowId.ToString());
				return false;
			}

			const uint8 RoleBit = static_cast<uint8>(1u << static_cast<uint8>(Role));
			if ((ObjectiveRoleMask & RoleBit) != 0)
			{
				OutError = FString::Printf(
					TEXT("Generation Profile 的 Flow %s 存在重复 AllowedObjectiveRole。"),
					*Flow.StableFlowId.ToString());
				return false;
			}
			ObjectiveRoleMask |= RoleBit;
		}
	}

	if (!bHasDefaultEscapeFlow)
	{
		OutError = TEXT("Generation Profile 必须提供 StableFlowId=EscapeOnly 的 EscapeOnly Flow，保证默认 Request 可解析。");
		return false;
	}

	if (Difficulties.Num() != 3)
	{
		OutError = TEXT("Generation Profile 必须且只能为 Easy、Normal、Hard 各提供一条 Difficulty Definition。");
		return false;
	}

	uint8 DifficultyMask = 0;
	int64 WorstCaseProgressionStates = 0;
	for (const FZeroEscapeDifficultyDefinition& Definition : Difficulties)
	{
		// 这里先证明每档配置在拓扑容量上可实现，再进入运行时随机选择。
		// 不做静默降级，否则同一个资产可能因为运行环境不同得到不同语义。
		if (!IsValidDifficulty(Definition.Difficulty))
		{
			OutError = TEXT("Generation Profile 包含未知 Difficulty。");
			return false;
		}

		const uint8 DifficultyBit = static_cast<uint8>(1u << static_cast<uint8>(Definition.Difficulty));
		if ((DifficultyMask & DifficultyBit) != 0)
		{
			OutError = TEXT("Generation Profile 包含重复 Difficulty。");
			return false;
		}
		DifficultyMask |= DifficultyBit;

		const int32 InternalCriticalNodes = Shared.CriticalPathNodeCount - 2;
		const int32 NonCrossingRejoinCapacity = InternalCriticalNodes / 2;
		if (Definition.ShortLeafBranchCount < 0
			|| Definition.ShortLeafBranchCount > InternalCriticalNodes
			|| (Definition.ShortLeafBranchCount > 0 && Shared.MaxLeafOneWayEdgeCount == 0)
			|| Definition.ForwardRejoinBranchCount < 0
			|| Definition.ForwardRejoinBranchCount > NonCrossingRejoinCapacity
			|| Definition.ObjectiveCandidateCount < 0
			|| Definition.ObjectiveCandidateCount > Shared.MaxObjectiveCandidateCount
			|| Definition.RequiredObjectiveCount < 0
			|| Definition.RequiredObjectiveCount > Definition.ObjectiveCandidateCount)
		{
			OutError = FString::Printf(
				TEXT("Generation Profile 的 Difficulty=%d 在分支容量、ObjectiveCandidateCount 或 RequiredObjectiveCount 上非法。"),
				static_cast<int32>(Definition.Difficulty));
			return false;
		}

		if (bHasObjectiveFlow && Definition.ObjectiveCandidateCount < 1)
		{
			OutError = TEXT("存在 Collect Flow 时，每档 Difficulty 的 ObjectiveCandidateCount 必须至少为 1。");
			return false;
		}
		if (bHasKOfNFlow && Definition.RequiredObjectiveCount < 1)
		{
			OutError = TEXT("存在 CollectKOfN Flow 时，每档 Difficulty 的 RequiredObjectiveCount 必须至少为 1。");
			return false;
		}

		const int64 PotentialNodeCount = static_cast<int64>(Shared.CriticalPathNodeCount)
			+ Definition.ShortLeafBranchCount
			+ Definition.ForwardRejoinBranchCount;
		const int64 GridCellCapacity = static_cast<int64>(Shared.GridExtentCells.X)
			* Shared.GridExtentCells.Y;
		if (PotentialNodeCount > GridCellCapacity)
		{
			OutError = FString::Printf(
				TEXT("Difficulty=%d 需要 %lld 个抽象节点，但共享 GridExtent 只有 %lld 个 Cell。"),
				static_cast<int32>(Definition.Difficulty),
				PotentialNodeCount,
				GridCellCapacity);
			return false;
		}

		for (const FZeroEscapeFlowDefinition& Flow : Flows)
		{
			if (Flow.CompletionRule == EZeroEscapeCompletionRule::EscapeOnly)
			{
				continue;
			}

			int64 ObjectiveLocationCapacity = 0;
			for (const EZeroEscapeTopologyRole Role : Flow.AllowedObjectiveRoles)
			{
				switch (Role)
				{
				case EZeroEscapeTopologyRole::MainPath:
					ObjectiveLocationCapacity += InternalCriticalNodes;
					break;
				case EZeroEscapeTopologyRole::ShortLeaf:
					ObjectiveLocationCapacity += Definition.ShortLeafBranchCount;
					break;
				case EZeroEscapeTopologyRole::ForwardRejoin:
					ObjectiveLocationCapacity += Definition.ForwardRejoinBranchCount;
					break;
				default:
					checkNoEntry();
					break;
				}
			}

			if (ObjectiveLocationCapacity < Definition.ObjectiveCandidateCount)
			{
				OutError = FString::Printf(
					TEXT("Difficulty=%d 的 Flow %s 只有 %lld 个允许角色槽位，无法容纳 N=%d 个目标。"),
					static_cast<int32>(Definition.Difficulty),
					*Flow.StableFlowId.ToString(),
					ObjectiveLocationCapacity,
					Definition.ObjectiveCandidateCount);
				return false;
			}
		}

		if (bHasObjectiveFlow)
		{
			// 精确 progression 搜索的状态上界约为“节点数 * 目标收集掩码数”。
			// N 被限制为 12，既满足 K-of-N 扩展，又防止 2^N 在游戏线程失控。
			const int64 DifficultyProgressionStates =
				PotentialNodeCount * (1LL << Definition.ObjectiveCandidateCount);
			WorstCaseProgressionStates = FMath::Max(
				WorstCaseProgressionStates,
				DifficultyProgressionStates);
		}
	}

	if (DifficultyMask != 0x7)
	{
		OutError = TEXT("Generation Profile 的 Difficulty 集合必须完整覆盖 Easy、Normal、Hard。");
		return false;
	}

	if (WorstCaseProgressionStates > Shared.MaxProgressionSearchStates)
	{
		OutError = FString::Printf(
			TEXT("Generation Profile 最坏 Progression 状态数 %lld 超过 MaxProgressionSearchStates=%d。"),
			WorstCaseProgressionStates,
			Shared.MaxProgressionSearchStates);
		return false;
	}

	const FZeroEscapeSolverBudgets& Budgets = SolverBudgets;
	// DataAsset 只能在代码硬上限以内取值。这样错误的策划配置最多导致一次可报告失败，
	// 不能把首帧同步生成变成不可控的长时间阻塞或大内存复制。
	if (Budgets.MaxLayoutAttempts < 1
		|| Budgets.MaxLayoutAttempts > ZeroEscape::GenerationLimits::FirstPassMaxLayoutAttempts
		|| Budgets.MaxSocketBacktracks < 0
		|| Budgets.MaxSocketBacktracks > ZeroEscape::GenerationLimits::FirstPassMaxSocketBacktracks
		|| Budgets.MaxSocketCandidateChecks < 1
		|| Budgets.MaxSocketCandidateChecks > ZeroEscape::GenerationLimits::FirstPassMaxSocketCandidateChecks
		|| Budgets.MaxAStarExpandedStates < 1
		|| Budgets.MaxAStarExpandedStates > ZeroEscape::GenerationLimits::FirstPassMaxAStarExpandedStates
		|| Budgets.MaxAStarRouteAttempts < 1
		|| Budgets.MaxAStarRouteAttempts > ZeroEscape::GenerationLimits::FirstPassMaxAStarRouteAttempts
		|| Budgets.MaxWfcBacktracks < 0
		|| Budgets.MaxWfcBacktracks > ZeroEscape::GenerationLimits::FirstPassMaxWfcBacktracks
		|| Budgets.MaxWfcObservationCount < 1
		|| Budgets.MaxWfcObservationCount > ZeroEscape::GenerationLimits::FirstPassMaxWfcObservationCount
		|| Budgets.MaxWfcSupportUpdates < 1
		|| Budgets.MaxWfcSupportUpdates > ZeroEscape::GenerationLimits::FirstPassMaxWfcSupportUpdates
		|| Budgets.MaxWfcActiveCells < 1
		|| Budgets.MaxWfcActiveCells > ZeroEscape::GenerationLimits::FirstPassMaxWfcActiveCells
		|| Budgets.MaxWfcVariants < 1
		|| Budgets.MaxWfcVariants > ZeroEscape::GenerationLimits::FirstPassMaxWfcVariants
		|| Budgets.MaxWfcSnapshotMemoryMB < 1
		|| Budgets.MaxWfcSnapshotMemoryMB > ZeroEscape::GenerationLimits::FirstPassMaxWfcSnapshotMemoryMB
		|| Budgets.MaxWfcCumulativeSnapshotCopyMB < Budgets.MaxWfcSnapshotMemoryMB
		|| Budgets.MaxWfcCumulativeSnapshotCopyMB > ZeroEscape::GenerationLimits::FirstPassMaxWfcCumulativeSnapshotCopyMB
		|| Budgets.MaxTotalWorkUnits < 1
		|| Budgets.MaxTotalWorkUnits > ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits)
	{
		OutError = TEXT("Generation Profile 的 SolverBudgets 非法或越过首个竖切的具名硬上限。");
		return false;
	}

	return true;
}

bool UZeroEscapeModuleCatalog::IsConfigured(FString& OutError) const
{
	OutError.Reset();

	// CellSize 必须来自当前模块族的实测契约。这里刻意没有“看起来合理”的默认值，
	// 防止把 SFCorridors 的近似 Bounds 当成可无缝拼接的真实节距。
	if (CatalogVersion < 1
		|| CellSize.ContainsNaN()
		|| CellSize.X <= UE_SMALL_NUMBER
		|| CellSize.Y <= UE_SMALL_NUMBER
		|| CellSize.Z <= UE_SMALL_NUMBER
		|| CellSize.X > ZeroEscape::GenerationLimits::FirstPassMaxCellSizeCm
		|| CellSize.Y > ZeroEscape::GenerationLimits::FirstPassMaxCellSizeCm
		|| CellSize.Z > ZeroEscape::GenerationLimits::FirstPassMaxCellSizeCm)
	{
		OutError = TEXT("Module Catalog 的 CatalogVersion 或 CellSize 非法；CellSize 必须来自实际模块测量且三轴为正。");
		return false;
	}

	if (Modules.IsEmpty())
	{
		OutError = TEXT("Module Catalog 至少需要一个逻辑模块。");
		return false;
	}

	TSet<int32> StableModuleIds;
	TMap<int32, const FZeroEscapeModuleDefinition*> ModulesById;
	bool bHasStartModule = false;
	bool bHasExitModule = false;
	bool bHasWfcMainPathModule = false;
	int64 WfcVariantCount = 0;

	for (const FZeroEscapeModuleDefinition& Module : Modules)
	{
		// 先验证模块自身的不变量；只有全部通过后才解析跨模块 Closure 引用。
		// 两阶段校验避免数组顺序决定“引用是否已经看见”，也保证重排资产不改变结果。
		if (Module.StableModuleId < 0
			|| StableModuleIds.Contains(Module.StableModuleId)
			|| Module.DisplayName.IsNone()
			|| !IsValidLayoutPolicy(Module.LayoutPolicy)
			|| Module.Footprint.X <= 0
			|| Module.Footprint.Y <= 0
			|| Module.Footprint.Z <= 0
			|| Module.Footprint.X > ZeroEscape::GenerationLimits::FirstPassMaxModuleFootprintAxis
			|| Module.Footprint.Y > ZeroEscape::GenerationLimits::FirstPassMaxModuleFootprintAxis
			|| Module.Footprint.Z != 1
			|| Module.Weight <= 0
			|| !IsFiniteValidBounds(Module.LocalBounds)
			|| (Module.AllowedQuarterTurnsMask & 0xF) == 0
			|| (Module.AllowedQuarterTurnsMask & ~0xF) != 0)
		{
			OutError = FString::Printf(
				TEXT("Module Catalog 的模块 %d 在 Stable Id、Policy、Footprint、Weight、Bounds 或 QuarterTurnsMask 上非法。"),
				Module.StableModuleId);
			return false;
		}

		const FBox FootprintBounds = MakeLogicalFootprintBounds(Module.Footprint, CellSize);
		// Catalog Bounds 是允许表现素材占用的 envelope，而不是直接复制 Mesh Bounds。
		// 未来素材经 PivotCorrection 后只要仍落在 envelope 内，就可以替换而不改变布局规则。
		if (!FootprintBounds.ExpandBy(LogicalBoundsToleranceCm).IsInsideOrOn(Module.LocalBounds))
		{
			OutError = FString::Printf(
				TEXT("Module Catalog 的模块 %d 的 LocalBounds 超出 Footprint 占格；逻辑原点必须位于 XY 中心和 Z 底面。"),
				Module.StableModuleId);
			return false;
		}

		const bool bStructuralSelectionModule =
			Module.LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell
			|| Module.LayoutPolicy == EZeroEscapeLayoutPolicy::SocketModule;
		if (bStructuralSelectionModule != !Module.AllowedRoles.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Module Catalog 的模块 %d 的 LayoutPolicy 与 AllowedRoles 不一致。"),
				Module.StableModuleId);
			return false;
		}

		uint8 RoleMask = 0;
		bool bAllowsStart = false;
		bool bAllowsExit = false;
		for (const EZeroEscapeTopologyRole Role : Module.AllowedRoles)
		{
			if (!IsValidTopologyRole(Role))
			{
				OutError = FString::Printf(TEXT("Module Catalog 的模块 %d 包含未知 TopologyRole。"), Module.StableModuleId);
				return false;
			}

			const uint8 RoleBit = static_cast<uint8>(1u << static_cast<uint8>(Role));
			if ((RoleMask & RoleBit) != 0)
			{
				OutError = FString::Printf(TEXT("Module Catalog 的模块 %d 包含重复 TopologyRole。"), Module.StableModuleId);
				return false;
			}
			RoleMask |= RoleBit;
			bAllowsStart |= Role == EZeroEscapeTopologyRole::Start;
			bAllowsExit |= Role == EZeroEscapeTopologyRole::Exit;
		}
		if ((bAllowsStart || bAllowsExit) && Module.AllowedRoles.Num() != 1)
		{
			OutError = FString::Printf(
				TEXT("Module Catalog 的 Start/Exit 模块 %d 必须各自使用单一角色，避免玩法 Anchor 泄漏到其他用途。"),
				Module.StableModuleId);
			return false;
		}

		if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell)
		{
			if (Module.Footprint != FIntVector(1, 1, 1))
			{
				OutError = FString::Printf(TEXT("WfcSingleCell 模块 %d 的 Footprint 必须为 1x1x1。"), Module.StableModuleId);
				return false;
			}
			bHasWfcMainPathModule |= Module.AllowedRoles.Contains(EZeroEscapeTopologyRole::MainPath);
			WfcVariantCount += CountQuarterTurnVariants(Module.AllowedQuarterTurnsMask);
		}

		TSet<int32> SocketIds;
		uint8 WfcDirectionMask = 0;
		for (int32 PortalIndex = 0; PortalIndex < Module.Portals.Num(); ++PortalIndex)
		{
			const FZeroEscapeModulePortal& Portal = Module.Portals[PortalIndex];
			// Portal 同时满足离散占格面与连续局部 Frame 两套约束：前者服务 WFC/A*，
			// 后者服务最终无缝对齐。不能只靠 Mesh Socket 名或浮点 Bounds 猜连接关系。
			if (Portal.StableSocketId < 0
				|| SocketIds.Contains(Portal.StableSocketId)
				|| Portal.ConnectorTypeId < 0
				|| Portal.WidthClass <= 0
				|| Portal.HeightLayer != 0
				|| Portal.CellOffset.Z != Portal.HeightLayer
				|| !IsCellOffsetInsideFootprint(Portal.CellOffset, Module.Footprint)
				|| !IsCellOnDirectionBoundary(Portal.CellOffset, Module.Footprint, Portal.Direction)
				|| !IsFiniteUnitScaleTransform(Portal.LocalTransform)
				|| !PortalFrameMatchesPlanarDirection(Portal)
				|| !Module.LocalBounds.ExpandBy(PortalBoundsToleranceCm).IsInsideOrOn(
					Portal.LocalTransform.GetTranslation())
				|| !IsValidSocketPolicy(Portal.Policy)
				|| (Portal.Policy == EZeroEscapeSocketPolicy::Sealable && Portal.ClosureModuleId < 0)
				|| (Portal.Policy == EZeroEscapeSocketPolicy::Required && Portal.ClosureModuleId != INDEX_NONE))
			{
				OutError = FString::Printf(
					TEXT("Module Catalog 的模块 %d 包含非法 Portal；请检查 Id、平面 Direction/Frame、Unit Scale、签名、Policy 与 Closure Id。"),
					Module.StableModuleId);
				return false;
			}

			for (int32 PreviousIndex = 0; PreviousIndex < PortalIndex; ++PreviousIndex)
			{
				const FZeroEscapeModulePortal& PreviousPortal = Module.Portals[PreviousIndex];
				if (PreviousPortal.CellOffset == Portal.CellOffset
					&& PreviousPortal.Direction == Portal.Direction)
				{
					OutError = FString::Printf(
						TEXT("Module Catalog 的模块 %d 在同一 Cell 面定义了多个 Portal。"),
						Module.StableModuleId);
					return false;
				}
			}
			SocketIds.Add(Portal.StableSocketId);

			if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell)
			{
				if (Portal.CellOffset != FIntVector::ZeroValue)
				{
					OutError = FString::Printf(TEXT("WfcSingleCell 模块 %d 的 Portal CellOffset 必须为零。"), Module.StableModuleId);
					return false;
				}

				const uint8 DirectionBit = static_cast<uint8>(1u << static_cast<uint8>(Portal.Direction));
				if ((WfcDirectionMask & DirectionBit) != 0)
				{
					OutError = FString::Printf(TEXT("WfcSingleCell 模块 %d 在同一方向定义了多个 Portal。"), Module.StableModuleId);
					return false;
				}
				WfcDirectionMask |= DirectionBit;
			}
		}

		if (Module.LayoutPolicy != EZeroEscapeLayoutPolicy::DecorationOnly && Module.Portals.IsEmpty())
		{
			OutError = FString::Printf(TEXT("结构模块 %d 至少需要一个逻辑 Portal。"), Module.StableModuleId);
			return false;
		}
		if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::Cap && Module.Portals.Num() != 1)
		{
			OutError = FString::Printf(TEXT("Cap 模块 %d 必须且只能有一个 Portal。"), Module.StableModuleId);
			return false;
		}
		if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::Cap
			&& Module.Portals[0].Policy != EZeroEscapeSocketPolicy::Required)
		{
			OutError = FString::Printf(TEXT("Cap 模块 %d 的唯一 Portal 必须为 Required。"), Module.StableModuleId);
			return false;
		}
		if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::DecorationOnly
			&& (!Module.Portals.IsEmpty() || !Module.GameplayAnchors.IsEmpty()))
		{
			OutError = FString::Printf(TEXT("DecorationOnly 模块 %d 不能拥有结构 Portal 或 Gameplay Anchor。"), Module.StableModuleId);
			return false;
		}

		TSet<int32> AnchorIds;
		for (const FZeroEscapeModuleAnchor& Anchor : Module.GameplayAnchors)
		{
			if (Anchor.StableAnchorId < 0
				|| AnchorIds.Contains(Anchor.StableAnchorId)
				|| !IsValidAnchorType(Anchor.Type)
				|| !IsFiniteUnitScaleTransform(Anchor.LocalTransform))
			{
				OutError = FString::Printf(TEXT("Module Catalog 的模块 %d 包含非法或重复 Gameplay Anchor。"), Module.StableModuleId);
				return false;
			}
			AnchorIds.Add(Anchor.StableAnchorId);
		}

		const int32 PlayerSpawnAnchorCount = CountAnchorType(Module, EZeroEscapeGameplayAnchorType::PlayerSpawn);
		const int32 ExitAnchorCount = CountAnchorType(Module, EZeroEscapeGameplayAnchorType::Exit);
		if ((bAllowsStart && PlayerSpawnAnchorCount != 1)
			|| (!bAllowsStart && PlayerSpawnAnchorCount != 0))
		{
			OutError = FString::Printf(
				TEXT("模块 %d 的 PlayerSpawn Anchor 数量必须与唯一 Start 角色严格对应。"),
				Module.StableModuleId);
			return false;
		}
		if ((bAllowsExit && ExitAnchorCount != 1)
			|| (!bAllowsExit && ExitAnchorCount != 0))
		{
			OutError = FString::Printf(
				TEXT("模块 %d 的 Exit Anchor 数量必须与唯一 Exit 角色严格对应。"),
				Module.StableModuleId);
			return false;
		}
		if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::Cap
			&& !Module.GameplayAnchors.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Cap 模块 %d 不能拥有 Gameplay Anchor。"), Module.StableModuleId);
			return false;
		}

		bHasStartModule |= bAllowsStart;
		bHasExitModule |= bAllowsExit;
		StableModuleIds.Add(Module.StableModuleId);
		ModulesById.Add(Module.StableModuleId, &Module);
	}

	if (!bHasStartModule || !bHasExitModule || !bHasWfcMainPathModule)
	{
		OutError = TEXT("Module Catalog 必须至少包含 Start、Exit 与可服务 MainPath 的 WfcSingleCell 模块。");
		return false;
	}
	if (WfcVariantCount > ZeroEscape::GenerationLimits::FirstPassMaxWfcVariants)
	{
		OutError = FString::Printf(
			TEXT("Module Catalog 展开得到 %d 个 WFC Variant，超过首版硬上限 %d。"),
			WfcVariantCount,
			ZeroEscape::GenerationLimits::FirstPassMaxWfcVariants);
		return false;
	}

	for (const FZeroEscapeModuleDefinition& Module : Modules)
	{
		for (const FZeroEscapeModulePortal& Portal : Module.Portals)
		{
			if (Portal.Policy != EZeroEscapeSocketPolicy::Sealable)
			{
				continue;
			}

			const FZeroEscapeModuleDefinition* const* ClosureModulePtr = ModulesById.Find(Portal.ClosureModuleId);
			if (ClosureModulePtr == nullptr
				|| *ClosureModulePtr == nullptr
				|| (*ClosureModulePtr)->StableModuleId == Module.StableModuleId
				|| (*ClosureModulePtr)->LayoutPolicy != EZeroEscapeLayoutPolicy::Cap
				|| (*ClosureModulePtr)->Portals.Num() != 1
				|| (*ClosureModulePtr)->Portals[0].Policy != EZeroEscapeSocketPolicy::Required
				|| !AreConnectorSignaturesEqual(Portal, (*ClosureModulePtr)->Portals[0])
				|| !CanCapPortalOpposeSource(Portal, **ClosureModulePtr))
			{
				OutError = FString::Printf(
					TEXT("模块 %d 的 Sealable Portal %d 引用了缺失或签名不相容的 Cap %d。"),
					Module.StableModuleId,
					Portal.StableSocketId,
					Portal.ClosureModuleId);
				return false;
			}
		}
	}

	return true;
}

bool UZeroEscapePresentationProfile::IsConfigured(
	const UZeroEscapeModuleCatalog& Catalog,
	FString& OutError) const
{
	OutError.Reset();

	FString CatalogError;
	if (!Catalog.IsConfigured(CatalogError))
	{
		OutError = FString::Printf(TEXT("Presentation Profile 引用的 Module Catalog 非法：%s"), *CatalogError);
		return false;
	}
	if (PresentationVersion < 1)
	{
		OutError = TEXT("Presentation Profile 的 PresentationVersion 必须至少为 1。");
		return false;
	}

	TMap<int32, const FZeroEscapeModuleDefinition*> ModulesById;
	for (const FZeroEscapeModuleDefinition& Module : Catalog.Modules)
	{
		ModulesById.Add(Module.StableModuleId, &Module);
	}

	TSet<int32> BoundModuleIds;
	for (const FZeroEscapePresentationBinding& Binding : Bindings)
	{
		// Presentation 只适配“StableModuleId -> 具体资源”。它不能引入新的结构模块，
		// 也不能通过缩放把素材硬塞进逻辑占格，否则替换素材会反向改变拓扑语义。
		const FZeroEscapeModuleDefinition* const* ModulePtr = ModulesById.Find(Binding.StableModuleId);
		if (Binding.StableModuleId < 0
			|| ModulePtr == nullptr
			|| *ModulePtr == nullptr
			|| BoundModuleIds.Contains(Binding.StableModuleId)
			|| !IsFiniteUnitScaleTransform(Binding.PivotCorrection)
			|| !FMath::IsFinite(Binding.BoundsOverhangAllowanceCm)
			|| Binding.BoundsOverhangAllowanceCm < 0.0
			|| Binding.BoundsOverhangAllowanceCm
				> ZeroEscape::GenerationLimits::FirstPassMaxPresentationBoundsOverhangCm)
		{
			OutError = FString::Printf(
				TEXT("Presentation Profile 的模块绑定 %d 缺失、重复，或 Pivot/Bounds Overhang 契约非法。"),
				Binding.StableModuleId);
			return false;
		}

		const FZeroEscapeModuleDefinition& Module = **ModulePtr;
		switch (Binding.SpawnPolicy)
		{
		case EZeroEscapePresentationSpawnPolicy::InstancedStaticMesh:
			if (!IsValid(Binding.StaticMesh)
				|| Binding.ActorClass != nullptr
				|| Binding.ActorAssetLocalBounds.IsValid
				|| !CollisionProfileResolves(Binding.CollisionProfileName))
			{
				OutError = FString::Printf(
					TEXT("Presentation Binding %d 的 InstancedStaticMesh 资源、Actor 字段或 CollisionProfileName 非法。"),
					Binding.StableModuleId);
				return false;
			}
			{
				// StaticMesh Bounds 是可直接读取的纯资产数据；无需 Spawn Actor 或执行构造脚本。
				const FBox PresentationBounds = Binding.StaticMesh->GetBoundingBox().TransformBy(Binding.PivotCorrection);
				// 逻辑 Cell 仍由 Catalog 决定；这里只把素材作者明确声明且受代码硬上限保护的
				// 装饰外探量加入 Bounds 校验。余量不会进入求解器占格，也不会静默缩放 Mesh。
				const FBox AllowedBounds = Module.LocalBounds.ExpandBy(
					Binding.BoundsOverhangAllowanceCm + PresentationBoundsToleranceCm);
				if (!IsFiniteValidBounds(PresentationBounds) || !AllowedBounds.IsInsideOrOn(PresentationBounds))
				{
					OutError = FString::Printf(
						TEXT("Presentation Binding %d 经 PivotCorrection 后的 StaticMesh Bounds 超出 Catalog Bounds。"),
						Binding.StableModuleId);
					return false;
				}
			}
			break;
		case EZeroEscapePresentationSpawnPolicy::Actor:
			if (Binding.ActorClass == nullptr
				|| Binding.StaticMesh != nullptr
				|| !IsFiniteValidBounds(Binding.ActorAssetLocalBounds)
				|| Binding.ActorClass->HasAnyClassFlags(
					CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				OutError = FString::Printf(
					TEXT("Presentation Binding %d 的 ActorClass、作者声明 Bounds 或互斥资源字段非法。"),
					Binding.StableModuleId);
				return false;
			}
			{
				// Actor 类可能在构造/BeginPlay 中产生副作用，所以校验只相信作者声明的本地 Bounds。
				// 当前运行时仍对 Actor Policy fail-closed；保留契约是为未来受控包装类扩展。
				const FBox PresentationBounds = Binding.ActorAssetLocalBounds.TransformBy(Binding.PivotCorrection);
				const FBox AllowedBounds = Module.LocalBounds.ExpandBy(
					Binding.BoundsOverhangAllowanceCm + PresentationBoundsToleranceCm);
				if (!IsFiniteValidBounds(PresentationBounds) || !AllowedBounds.IsInsideOrOn(PresentationBounds))
				{
					OutError = FString::Printf(
						TEXT("Presentation Binding %d 经 PivotCorrection 后的 Actor 作者声明 Bounds 超出 Catalog Bounds。"),
						Binding.StableModuleId);
					return false;
				}
			}
			break;
		default:
			OutError = FString::Printf(TEXT("Presentation Binding %d 使用未知 SpawnPolicy。"), Binding.StableModuleId);
			return false;
		}

		BoundModuleIds.Add(Binding.StableModuleId);
	}

	for (const FZeroEscapeModuleDefinition& Module : Catalog.Modules)
	{
		// 每个结构模块必须恰有一条表现绑定。DecorationOnly 不参与首版结构实例化，允许暂缺。
		if (Module.LayoutPolicy != EZeroEscapeLayoutPolicy::DecorationOnly
			&& !BoundModuleIds.Contains(Module.StableModuleId))
		{
			OutError = FString::Printf(
				TEXT("Presentation Profile 缺少结构模块 %d 的唯一绑定。"),
				Module.StableModuleId);
			return false;
		}
	}

	return true;
}

bool ValidateZeroEscapeGenerationAssetSet(
	const UZeroEscapeLevelGenerationProfile& Profile,
	const UZeroEscapeModuleCatalog& Catalog,
	const UZeroEscapePresentationProfile& Presentation,
	FString& OutError)
{
	OutError.Reset();

	// 先分别验证三份资产，再检查只有组合后才能判断的容量与语义覆盖。
	// 调用顺序固定，因此同一错误配置总会报告同一个首要原因。
	FString InnerError;
	if (!Profile.IsConfigured(InnerError))
	{
		OutError = FString::Printf(TEXT("Generation Profile 非法：%s"), *InnerError);
		return false;
	}
	if (!Catalog.IsConfigured(InnerError))
	{
		OutError = FString::Printf(TEXT("Module Catalog 非法：%s"), *InnerError);
		return false;
	}
	if (!Presentation.IsConfigured(Catalog, InnerError))
	{
		OutError = FString::Printf(TEXT("Presentation Profile 非法：%s"), *InnerError);
		return false;
	}

	int64 WfcVariantCount = 0;
	for (const FZeroEscapeModuleDefinition& Module : Catalog.Modules)
	{
		if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell)
		{
			WfcVariantCount += CountQuarterTurnVariants(Module.AllowedQuarterTurnsMask);
		}
	}
	if (WfcVariantCount > Profile.SolverBudgets.MaxWfcVariants)
	{
		OutError = FString::Printf(
			TEXT("Catalog 展开的 WFC Variant 数 %lld 超过当前 Profile 上限 %d。"),
			WfcVariantCount,
			Profile.SolverBudgets.MaxWfcVariants);
		return false;
	}

	for (const FZeroEscapeFlowDefinition& Flow : Profile.Flows)
	{
		if (Flow.CompletionRule == EZeroEscapeCompletionRule::EscapeOnly)
		{
			continue;
		}

		for (const EZeroEscapeTopologyRole RequiredRole : Flow.AllowedObjectiveRoles)
		{
			// Flow 允许某角色承载目标时，Catalog 至少要提供一个同角色且带 Objective Anchor
			// 的结构模块；否则抽象图合法却永远无法落地，失败会被错误推迟到布局阶段。
			const bool bHasCompatibleObjectiveModule = Catalog.Modules.ContainsByPredicate(
				[RequiredRole](const FZeroEscapeModuleDefinition& Module)
				{
					return Module.AllowedRoles.Contains(RequiredRole)
						&& CountAnchorType(Module, EZeroEscapeGameplayAnchorType::Objective) > 0;
				});
			if (!bHasCompatibleObjectiveModule)
			{
				OutError = FString::Printf(
					TEXT("Flow %s 允许的 Objective Role=%d 在 Catalog 中没有带 Objective Anchor 的模块。"),
					*Flow.StableFlowId.ToString(),
					static_cast<int32>(RequiredRole));
				return false;
			}
		}
	}

	return true;
}
