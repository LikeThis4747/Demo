// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeLayoutSolver.h
 * 职责：声明整关 PCG 的纯数据 Graph-to-Grid、Socket、整数 A*、有界 WFC 与全局布局验证入口。
 * 边界：不读取 UObject/第三方资源，不 Spawn Actor，不修改 World；只消费 Core 构建的值快照。
 * 状态 Owner：FLayoutSolver 调用栈拥有单次求解状态；输出 Plan 成功前不可见。
 */

#pragma once

#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationTypes.h"
#include "ZeroEscapeGenerationCore.h"

namespace ZeroEscape::LevelGeneration
{
	struct FPlacementState;

	/**
	 * 所有求解阶段共享的确定性工作预算。
	 *
	 * 这里故意不用墙钟超时：同一个 Seed、Catalog 和 Profile 必须在不同机器上走过相同分支。
	 * 预算只会单调减少，失败的 TryConsume 不扣款，也不会因为 DFS/WFC 回溯而返还；因此一次
	 * Attempt 的最坏工作量有明确上界，预算耗尽本身也是可复现的求解结果。
	 */
	struct FDeterministicWorkBudget final
	{
		explicit FDeterministicWorkBudget(const int32 InMaxUnits)
			: InitialUnits(FMath::Max(0, InMaxUnits))
			, RemainingUnits(InitialUnits)
		{
		}

		/** 只在剩余预算足够时消费；失败不改变剩余值。 */
		bool TryConsume(const int32 Units)
		{
			if (Units < 0 || RemainingUnits < Units)
			{
				return false;
			}
			RemainingUnits -= Units;
			return true;
		}

		int32 GetRemainingUnits() const { return RemainingUnits; }
		int32 GetConsumedUnits() const { return InitialUnits - RemainingUnits; }

	private:
		int32 InitialUnits = 0;
		int32 RemainingUnits = 0;
	};

	/** Grid 中的 Cell 由谁拥有；同一坐标不能同时交给 WFC 和结构模块。 */
	enum class EGridCellParticipation : uint8
	{
		/** 当前求解区域之外，不进入 Domain/邻接表。 */
		Outside,
		/** A* 路线的一部分，后续必须由一个 WfcSingleCell Variant 回填。 */
		ActiveWfc,
		/** 已被 Strong Anchor 的 SocketModule 占用，仅用于碰撞/重叠验证。 */
		ReservedSocket
	};

	/** 对单个 Cell 的某一朝向施加的一元 Connector 约束。 */
	enum class EConnectorConstraintRule : uint8
	{
		/** 不由必需路线指定，仍会受相邻 WFC Cell 的二元兼容关系约束。 */
		Unconstrained,
		/** 区域外边界必须是墙面，禁止 Variant 在这一面留下开口。 */
		MustBeClosed,
		/** 必需路线或 Strong Socket 要求这一面存在指定签名的开口。 */
		MustMatchSignature
	};

	/** 首版连接签名严格相等；宽窄口转换件未来在兼容 helper 中扩展。 */
	struct FConnectorSignature
	{
		bool bOpen = false;
		int32 ConnectorTypeId = 0;
		int32 WidthClass = 1;
		int32 HeightLayer = 0;

		friend bool operator==(const FConnectorSignature& A, const FConnectorSignature& B)
		{
			return A.bOpen == B.bOpen
				&& A.ConnectorTypeId == B.ConnectorTypeId
				&& A.WidthClass == B.WidthClass
				&& A.HeightLayer == B.HeightLayer;
		}

		friend bool operator!=(const FConnectorSignature& A, const FConnectorSignature& B)
		{
			return !(A == B);
		}
	};

	struct FDirectionalConnectorConstraint
	{
		EConnectorConstraintRule Rule = EConnectorConstraintRule::Unconstrained;
		/** 仅 Rule == MustMatchSignature 时有意义。 */
		FConnectorSignature RequiredSignature;
	};

	/**
	 * 按 (StableModuleId, QuarterTurns) 稳定展开的单格 WFC 候选。
	 * Variant 已经把模块旋转后的四向 Portal 烘成 N/E/S/W 数组，求解时不再读取 UObject，
	 * StableVariantId 也因此能作为确定性排序、快照和最终回放的稳定身份。
	 */
	struct FTileVariant
	{
		int32 StableVariantId = INDEX_NONE;
		int32 ModuleIndex = INDEX_NONE;
		uint8 QuarterTurns = 0;
		uint8 OpenPortalMask = 0;
		TStaticArray<FConnectorSignature, 4> Connectors;
		TStaticArray<int32, 4> StableSocketIds = {
			INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
		int32 Weight = 1;
	};

	/**
	 * Simple-Tiled WFC 的有向兼容表。
	 * A 在 Direction 方向允许 B，当且仅当 A 的该面与 B 的反向面都关闭，或两者开口签名兼容；
	 * Build 阶段同时保证反向关系存在，传播阶段才能安全地递减 Support Count。
	 */
	struct FCompatibilityTable
	{
		/** [SourceVariantIndex][N/E/S/W] -> 按 StableVariantId 升序的邻居 Variant Index。 */
		TArray<TStaticArray<TArray<int32>, 4>> SupportedNeighbors;

		const TArray<int32>& GetSupportedNeighbors(
			const int32 SourceVariantIndex,
			const int32 Direction) const
		{
			return SupportedNeighbors[SourceVariantIndex][Direction];
		}

		/** 检查维度、索引、排序/去重以及双向对称性。 */
		bool IsConfigured(const TArray<FTileVariant>& Variants, FString& OutError) const;
	};

	/** A* 路由转换成的 WFC 输入；Coordinate 是生成器局部整数格坐标。 */
	struct FGridConstraint
	{
		FIntVector Coordinate = FIntVector::ZeroValue;
		EGridCellParticipation Participation = EGridCellParticipation::Outside;
		TStaticArray<FDirectionalConnectorConstraint, 4> Directions;
	};

	/** 单个 (Cell, Variant, Direction) 当前仍有多少个邻格 Variant 可以支持它。 */
	using FWfcSupportCount = uint16;
	/** 哨兵值：该方向不存在 ActiveWfc 邻格，因此不参与 Support 传播。 */
	inline constexpr FWfcSupportCount NoActiveNeighbor = MAX_uint16;

	/**
	 * 一个 ActiveWfc Cell 的可变求解状态。
	 * 核心不变量：RemainingCandidateCount == AllowedVariants.CountSetBits() 且始终大于零；
	 * 对存在的 Active 邻格，每个仍允许 Variant 的四向 Support Count 必须大于零。
	 */
	struct FWfcCell
	{
		FGridConstraint Constraint;
		TBitArray<> AllowedVariants;
		/** [VariantIndex * 4 + Direction]；NoActiveNeighbor 表示该向没有 Active 邻格。 */
		TArray<FWfcSupportCount> SupportCountByVariantDirection;
		TStaticArray<int32, 4> NeighborCellIndices = {
			INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
		int32 RemainingCandidateCount = 0;
	};

	/** WFC 单次 Attempt 的完整可回溯状态，不持有任何资源对象。 */
	struct FWfcState
	{
		/** 只包含按 Z/Y/X 稳定排序的 Active Cell。 */
		TArray<FWfcCell> Cells;
	};

	/**
	 * 单调删除事件。某 Variant 被删后，所有依赖它的邻格 Support Count 各减一；减到零会继续
	 * 产生删除事件，直至队列清空或某个 Domain 变空。
	 */
	struct FRemovedVariantEvent
	{
		int32 CellIndex = INDEX_NONE;
		int32 VariantIndex = INDEX_NONE;
	};

	/**
	 * 一个稳定传播点的完整 Domain 快照。
	 * Support Count 不复制：恢复 Domain 后统一重建，避免把增量传播队列的隐含状态带入回溯。
	 */
	struct FWfcDomainSnapshot
	{
		TArray<TBitArray<>> Domains;
	};

	/**
	 * 一层 Observation/回溯帧。StableState 是选择前状态，UntriedVariants 记录该 Cell 尚未尝试
	 * 的候选；NestedResidentBytes 用于精确限制实时快照驻留量。
	 */
	struct FWfcDecision
	{
		int32 CellIndex = INDEX_NONE;
		FWfcDomainSnapshot StableState;
		TBitArray<> UntriedVariants;
		int64 NestedResidentBytes = 0;
	};

	/**
	 * 抽象拓扑 Node 在整数 Grid 上的锚点。
	 *
	 * Strong Anchor：Start/Exit/Objective、分叉/汇合等语义或度数关键节点，必须先绑定一个
	 * SocketModule，其 Portal 是结构路线端点。
	 * Weak Anchor：普通二度主路节点，不额外放房间模块；其 GridCoordinate 必须落在某条 WFC
	 * 路线上，Finalize 时绑定到该格的走廊 Placement。
	 */
	struct FGraphAnchorPlacement
	{
		int32 AbstractNodeId = INDEX_NONE;
		int32 StablePlacementId = INDEX_NONE;
		FIntVector GridCoordinate = FIntVector::ZeroValue;
		/** Embed 阶段用于保证相邻 Anchor 至少留出一格路由净空的保守旋转占地。 */
		FIntVector PlanningFootprint = FIntVector(1, 1, 1);
		bool bStrongAnchor = false;
		/** 当前 DFS 分支已经分配给该 Strong Anchor 的 Socket；回溯时必须严格逆序撤销。 */
		TArray<int32> AssignedSocketIds;
	};

	struct FRoutedGraphEdge
	{
		int32 AbstractEdgeId = INDEX_NONE;
		int32 SourceNodeId = INDEX_NONE;
		int32 TargetNodeId = INDEX_NONE;
		int32 SourceSocketId = INDEX_NONE;
		int32 TargetSocketId = INDEX_NONE;
		FConnectorSignature RouteSignature;
		/**
		 * 只包含需要 WFC 回填的顺序 Cell；Strong Anchor 模块由两端 Id 单独表示。
		 * 两个 Strong Portal 直接对齐时此数组合法为空，Finalize 直接导出两端 Placement。
		 */
		TArray<FIntVector> OrderedCells;
	};

	/**
	 * A* 状态不能只用 Coordinate：到达同一格时的来向会影响下一步是否产生 TurnPenalty。
	 * 把 IncomingDirection 纳入 Key，才不会错误合并代价不同的路径历史。
	 */
	struct FAStarStateKey
	{
		FIntVector Coordinate = FIntVector::ZeroValue;
		int8 IncomingDirection = INDEX_NONE;

		friend bool operator==(const FAStarStateKey& A, const FAStarStateKey& B)
		{
			return A.Coordinate == B.Coordinate
				&& A.IncomingDirection == B.IncomingDirection;
		}

		friend uint32 GetTypeHash(const FAStarStateKey& Key)
		{
			return HashCombineFast(GetTypeHash(Key.Coordinate), GetTypeHash(Key.IncomingDirection));
		}
	};

	/** 整数 A* 记录；所有成本均为 int64，避免浮点比较破坏跨平台稳定次序。 */
	struct FAStarNodeRecord
	{
		FAStarStateKey Key;
		int64 GScore = MAX_int64;
		int64 FScore = MAX_int64;
		int32 TurnCount = MAX_int32;
		int32 ParentRecordIndex = INDEX_NONE;
	};

	/**
	 * 已完成 UObject -> 值快照后的布局请求。
	 * AbstractPlan 的生命周期必须覆盖 Solve 调用；CellSize/GridExtent 只描述逻辑生成空间，
	 * Presentation 的 PivotCorrection 在运行时实例化阶段另行组合。
	 */
	struct FLayoutRequest
	{
		FVector CellSize = FVector::ZeroVector;
		FIntVector GridExtent = FIntVector(20, 20, 1);
		int32 AStarStraightStepCost = 10;
		int32 AStarTurnPenalty = 3;
		bool bRequireEffectiveWfcChoice = false;
		FZeroEscapeGenerationSignature Signature;
		int64 CanonicalAbstractHash = 0;
		const FAbstractLevelPlan* AbstractPlan = nullptr;
	};

	/** Portal 的反向；Stable enum 值不能用循环算术假设。 */
	EZeroEscapeCardinalDirection OppositeDirection(EZeroEscapeCardinalDirection Direction);

	/** 绕局部 +Z 以 +Yaw 旋转 0..3 个四分之一圈。 */
	EZeroEscapeCardinalDirection RotateDirection(
		EZeroEscapeCardinalDirection Direction,
		uint8 QuarterTurns);

	/** 奇数次四分之一圈交换 Footprint X/Y。 */
	FIntVector RotateFootprint(const FIntVector& Footprint, uint8 QuarterTurns);

	/** 旋转非负 CellOffset，并重新平移到旋转 Footprint 的最小角。 */
	FIntVector RotateCellOffset(
		const FIntVector& CellOffset,
		const FIntVector& Footprint,
		uint8 QuarterTurns);

	/** 使 Source Portal 与 Target Portal 位置重合、Forward 相反、Up 相同。 */
	FTransform SolveModuleLocalTransform(
		const FTransform& TargetPortalInGenerator,
		const FTransform& SourcePortalInModule);

	/** Asset Local -> Logical Module Local -> Generator Local。 */
	FTransform MakePresentationLocalTransform(
		const FTransform& PivotCorrection,
		const FTransform& ModuleLocalTransform);

	/** Asset Local -> Logical Module Local -> Generator Local -> World。 */
	FTransform MakePresentationWorldTransform(
		const FTransform& PivotCorrection,
		const FTransform& ModuleLocalTransform,
		const FTransform& GeneratedRootWorldTransform);

	/**
	 * 纯数据布局入口。
	 * 流水线依次执行 Graph Embed -> Strong Socket 放置 -> Edge/A* DFS -> WFC -> Closure/Finalize
	 * -> 全局验证。每次 Layout Attempt 使用局部 State 和 CandidatePlan；只有所有阶段成功才移动
	 * 到 OutPlan，因此任何失败、预算耗尽或回溯都不会向调用者暴露半张地图。
	 */
	class FLayoutSolver final
	{
	public:
		static bool Solve(
			const FLayoutRequest& Request,
			const FModuleCatalogSnapshot& Catalog,
			const FZeroEscapeSolverBudgets& Budgets,
			int32 MasterSeed,
			FZeroEscapeGeneratedLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport);

		/**
		 * 稳定展开 Catalog 的单格 Variant 并构建四向对称兼容表。
		 * 输入模块按 StableModuleId 排序、旋转按 QuarterTurns 递增，保证 StableVariantId 与资产
		 * 在 Catalog 数组中的偶然顺序无关。
		 */
		static bool BuildWfcVariantsAndCompatibility(
			const FModuleCatalogSnapshot& Catalog,
			FDeterministicWorkBudget& InOutWorkBudget,
			TArray<FTileVariant>& OutVariants,
			FCompatibilityTable& OutCompatibility,
			FZeroEscapeGenerationReport& OutReport);

		/**
		 * 使用删除事件 + Support Count + 完整 Domain 快照回溯求解给定 Active Cell。
		 * Observation 采用 MRV，平局和带权 Variant 选择只消费传入的 WFC 随机流；快照驻留、累计
		 * 拷贝、Observation、Backtrack、Support Update 与全局工作量均有独立上限。
		 */
		static bool SolveWfc(
			const TArray<FGridConstraint>& Constraints,
			const TArray<FTileVariant>& Variants,
			const FCompatibilityTable& Compatibility,
			FRandomStream& Random,
			const FZeroEscapeSolverBudgets& Budgets,
			bool bRequireEffectiveWfcChoice,
			FDeterministicWorkBudget& InOutWorkBudget,
			TArray<int32>& OutVariantByActiveCell,
			FZeroEscapeGenerationReport& OutReport);

		/**
		 * 可独立测试的确定性整数 A*；BlockedCells 只作查询不参与遍历。
		 * 启发式为不含转弯罚分的 Manhattan 下界；Open 集合使用完整稳定 Tie-break，输出包含
		 * Start/Goal。函数入口先清空输出，失败时不会留下可被误认为有效路线的前缀。
		 */
		static bool FindGridPath(
			const FIntVector& GridExtent,
			const TSet<FIntVector>& BlockedCells,
			const FIntVector& Start,
			const FIntVector& Goal,
			int32 StraightStepCost,
			int32 TurnPenalty,
			int32 MaxExpandedStates,
			FDeterministicWorkBudget& InOutWorkBudget,
			TArray<FIntVector>& OutOrderedCells,
			FZeroEscapeGenerationReport& OutReport);

	private:
		/** 将抽象 Node 放入 Grid，并为 Strong Anchor 预留可容纳候选模块的一格四邻域净空。 */
		static bool EmbedAbstractNodes(
			const FLayoutRequest& Request,
			const FModuleCatalogSnapshot& Catalog,
			FDeterministicWorkBudget& InOutWorkBudget,
			FPlacementState& InOutState,
			FZeroEscapeGenerationReport& OutReport);

		/** 通过 MRV + 有界 DFS 为所有 Strong Anchor 选择互不重叠的 SocketModule。 */
		static bool PlaceRequiredSocketModules(
			const FLayoutRequest& Request,
			const FModuleCatalogSnapshot& Catalog,
			FRandomStream& Random,
			const FZeroEscapeSolverBudgets& Budgets,
			FDeterministicWorkBudget& InOutWorkBudget,
			FPlacementState& InOutState,
			FZeroEscapeGenerationReport& OutReport);

		/**
		 * 按稳定 Edge 顺序执行 Socket 对选择与整数 A*；外层 DFS 可回滚某条 Edge 的约束、
		 * Socket 占用和路由记录，以处理“当前最短路堵死后续 Edge”的情况。
		 */
		static bool RouteGraphEdgesWithAStar(
			const FLayoutRequest& Request,
			const FModuleCatalogSnapshot& Catalog,
			const FZeroEscapeSolverBudgets& Budgets,
			FDeterministicWorkBudget& InOutWorkBudget,
			FPlacementState& InOutState,
			TArray<FGridConstraint>& OutConstraints,
			TArray<FRoutedGraphEdge>& OutRoutedEdges,
			FZeroEscapeGenerationReport& OutReport);
	};
}
