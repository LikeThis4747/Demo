// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationCore.cpp
 * 职责：实现 DataAsset 到纯值快照、单局 Flow/难度解析、确定性抽象图、K-of-N 搜索与规范 Hash。
 * 边界：Snapshot 之后不访问 UObject、World、素材或全局随机；不执行 Socket、WFC、实例化或玩法 Actor 逻辑。
 * 状态 Owner：无；所有临时状态只存在于当前调用栈，失败时通过 Report 返回且不提交半成品 Plan。
 *
 * 实现分成四个可独立审查的阶段：
 * 1. 快照与预算解析：在游戏线程读 DataAsset，然后立即切换到纯值世界。
 * 2. 抽象生成：构造关键路线、短叶房、前向汇合支路和 ObjectiveIntent。
 * 3. 规范验证：验证图结构、K-of-N 可解性和所有难度共享的回头路上限。
 * 4. 规范 Hash：先排序、定向无向记录，再以明确字节序列化，不 Hash 内存布局。
 */

#include "PCG/ZeroEscapeGenerationCore.h"

#include "Containers/BitArray.h"
#include "Containers/Set.h"
#include "HAL/PlatformMath.h"

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		using FObjectiveMask = uint32;

		static_assert(
			ZeroEscape::GenerationLimits::MaxObjectiveCandidates < 32,
			"FObjectiveMask requires every objective to fit in uint32.");

		constexpr int32 MaxAbstractShortLeafCount =
			ZeroEscape::GenerationLimits::FirstPassMaxCriticalPathNodes - 2;
		constexpr int32 MaxAbstractForwardRejoinCount = MaxAbstractShortLeafCount / 2;
		constexpr int32 MaxAbstractNodeCount =
			ZeroEscape::GenerationLimits::FirstPassMaxCriticalPathNodes
			+ MaxAbstractShortLeafCount
			+ MaxAbstractForwardRejoinCount;
		constexpr int32 MaxAbstractEdgeCount =
			(ZeroEscape::GenerationLimits::FirstPassMaxCriticalPathNodes - 1)
			+ MaxAbstractShortLeafCount
			+ (MaxAbstractForwardRejoinCount * 2);
		constexpr int32 MinGridExtentCells = 8;
		constexpr int32 MaxGridExtentCells = 64;

		/**
		 * 给所有显式算法循环提供同一调用内的确定性工作上限。
		 * 上限用“访问一个受控记录/展开一个受控状态”这类工作单位表示，而不是毫秒。
		 * 墙钟时间、帧率和机器性能不参与分支，所以同输入不会因硬件快慢产生不同地图。
		 */
		class FCoreWorkBudget final
		{
		public:
			explicit FCoreWorkBudget(const int32 InLimit)
				: Limit(FMath::Max(0, InLimit))
			{
			}

			/** 预扣 Count 个确定性工作单位；失败时不改变已消费计数。 */
			bool TryConsume(const int64 Count = 1)
			{
				if (Count < 0 || Count > Limit - Consumed)
				{
					return false;
				}
				Consumed += Count;
				return true;
			}

			/** 返回本次调用已消费的工作单位，数值受首版 int32 硬上限保护。 */
			int32 GetConsumed() const
			{
				return static_cast<int32>(Consumed);
			}

			/** 返回本次调用的工作单位硬上限。 */
			int32 GetLimit() const
			{
				return static_cast<int32>(Limit);
			}

		private:
			int64 Limit = 0;
			int64 Consumed = 0;
		};

		/**
		 * 目标候选的稳定排序视图。
		 * ProgressIndex 用于把 N 个候选分散到关卡前、中、后段；Role 用于回退时估算折返代价。
		 * NodeArrayIndex 仅是当前 Plan 的短命引用，不进入输出、Hash 或跨阶段协议。
		 */
		struct FObjectiveCandidate
		{
			int32 NodeArrayIndex = INDEX_NONE;
			int32 ProgressIndex = INDEX_NONE;
			EZeroEscapeTopologyRole Role = EZeroEscapeTopologyRole::MainPath;
			int32 StableNodeId = INDEX_NONE;
		};

		/** Stable Id 到连续索引的纯值图；TMap 仅查询，任何决策遍历都走已排序数组。 */
		struct FDenseGraph
		{
			TArray<int32> PlanNodeIndexByDenseIndex;
			TMap<int32, int32> DenseIndexByStableId;
			TArray<TArray<int32>> AdjacencyByDenseIndex;
		};

		/**
		 * Progression BFS 的唯一状态身份是 (DenseNodeIndex, CollectedMask)。
		 * 玩家回到同一房间但已拿到更多目标时，必须作为新状态继续搜索；否则 K-of-N 会被错判为无解。
		 * DistanceEdges 不参与身份，因为单位边 BFS 首次发现某状态即是到达该状态的最短距离。
		 */
		struct FProgressionState
		{
			int32 DenseNodeIndex = INDEX_NONE;
			FObjectiveMask CollectedMask = 0;
			int32 DistanceEdges = 0;

			friend bool operator==(const FProgressionState& A, const FProgressionState& B)
			{
				return A.DenseNodeIndex == B.DenseNodeIndex
					&& A.CollectedMask == B.CollectedMask;
			}

			friend uint32 GetTypeHash(const FProgressionState& State)
			{
				return HashCombineFast(
					GetTypeHash(State.DenseNodeIndex),
					GetTypeHash(State.CollectedMask));
			}
		};

		/** 抽象阶段 1：创建 Start -> MainPath... -> Exit 的定长骨架。 */
		bool BuildCriticalPath(
			const FResolvedGenerationBudget& Budget,
			FAbstractLevelPlan& OutPlan,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport);

		/** 抽象阶段 2a：在互异内部进度点上添加单边短叶房。 */
		bool AddShortLeafBranches(
			const FResolvedGenerationBudget& Budget,
			FRandomStream Random,
			FAbstractLevelPlan& InOutPlan,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport);

		/** 抽象阶段 2b：添加从早一步进度进入、晚一步进度离开的不交叉支路。 */
		bool AddForwardRejoinBranches(
			const FResolvedGenerationBudget& Budget,
			FRandomStream Random,
			FAbstractLevelPlan& InOutPlan,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport);

		/** 抽象阶段 3：将 N 个 ObjectiveIntent 绑定到允许角色，每节点最多一个。 */
		bool BindObjectives(
			const FResolvedGenerationBudget& Budget,
			FRandomStream Random,
			bool bPreferMinimumDetour,
			FAbstractLevelPlan& InOutPlan,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport);

		/** 验证基础设施：把稀疏 Stable Id 转换成可安全下标的连续图。 */
		bool BuildDenseGraph(
			const FAbstractLevelPlan& Plan,
			FDenseGraph& OutGraph,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport,
			EZeroEscapeGenerationStage FailureStage);

		/** 统一写入结构化失败，避免不同分支遗漏 Stage、原因或实际/上限值。 */
		void Fail(
			FZeroEscapeGenerationReport& OutReport,
			const EZeroEscapeGenerationStage Stage,
			const EZeroEscapeGenerationFailure Failure,
			FString Message,
			const int32 RelatedStableId = INDEX_NONE,
			const int32 ActualValue = 0,
			const int32 LimitValue = 0)
		{
			OutReport.Stage = Stage;
			OutReport.Failure = Failure;
			OutReport.RelatedStableId = RelatedStableId;
			OutReport.ActualValue = ActualValue;
			OutReport.LimitValue = LimitValue;
			OutReport.Message = MoveTemp(Message);
		}

		/** 清除旧失败字段并记录当前纯值阶段成功；不覆盖调用方已累计的 Metrics。 */
		void Succeed(
			FZeroEscapeGenerationReport& OutReport,
			const EZeroEscapeGenerationStage Stage,
			FString Message)
		{
			OutReport.Stage = Stage;
			OutReport.Failure = EZeroEscapeGenerationFailure::None;
			OutReport.RelatedStableId = INDEX_NONE;
			OutReport.ActualValue = 0;
			OutReport.LimitValue = 0;
			OutReport.Message = MoveTemp(Message);
		}

		/** 消费工作预算；超限时以稳定的 SearchBudgetExceeded 报告失败。 */
		bool ConsumeWork(
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport,
			const EZeroEscapeGenerationStage Stage,
			const TCHAR* Context,
			const int64 Count = 1)
		{
			if (Work.TryConsume(Count))
			{
				return true;
			}

			Fail(
				OutReport,
				Stage,
				EZeroEscapeGenerationFailure::SearchBudgetExceeded,
				FString::Printf(TEXT("%s 超过确定性工作预算。"), Context),
				INDEX_NONE,
				Work.GetConsumed(),
				Work.GetLimit());
			return false;
		}

		/** 防御反序列化出的未知 Difficulty 值。 */
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

		/** 防御反序列化出的未知 CompletionRule 值。 */
		bool IsValidCompletionRule(const EZeroEscapeCompletionRule Rule)
		{
			return Rule == EZeroEscapeCompletionRule::EscapeOnly
				|| Rule == EZeroEscapeCompletionRule::CollectAll
				|| Rule == EZeroEscapeCompletionRule::CollectKOfN;
		}

		/** 判断角色是否属于首版 ObjectiveIntent 可绑定的三类拓扑位置。 */
		bool IsObjectiveRole(const EZeroEscapeTopologyRole Role)
		{
			return Role == EZeroEscapeTopologyRole::MainPath
				|| Role == EZeroEscapeTopologyRole::ShortLeaf
				|| Role == EZeroEscapeTopologyRole::ForwardRejoin;
		}

		/** 判断角色是否是任一合法首版拓扑角色。 */
		bool IsValidTopologyRole(const EZeroEscapeTopologyRole Role)
		{
			return IsObjectiveRole(Role)
				|| Role == EZeroEscapeTopologyRole::Start
				|| Role == EZeroEscapeTopologyRole::Exit;
		}

		/** 使用显式整数比较稳定排序反射枚举，不依赖资产数组插入顺序。 */
		bool TopologyRoleLess(
			const EZeroEscapeTopologyRole A,
			const EZeroEscapeTopologyRole B)
		{
			return static_cast<uint8>(A) < static_cast<uint8>(B);
		}

		/** 对小型索引数组执行唯一允许的确定性 Fisher-Yates；不调用全局随机。 */
		bool ShuffleIndices(
			TArray<int32>& InOutValues,
			FRandomStream& Random,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport,
			const EZeroEscapeGenerationStage Stage)
		{
			for (int32 Index = InOutValues.Num() - 1; Index > 0; --Index)
			{
				if (!ConsumeWork(Work, OutReport, Stage, TEXT("确定性 Shuffle")))
				{
					return false;
				}
				const int32 SwapIndex = Random.RandHelper(Index + 1);
				InOutValues.Swap(Index, SwapIndex);
			}
			return true;
		}

		/** 32 位 avalanche mix；常量与调用顺序属于 GAlgorithmVersion。 */
		uint32 Mix32(uint32 Value)
		{
			Value ^= Value >> 16;
			Value *= 0x7FEB352Du;
			Value ^= Value >> 15;
			Value *= 0x846CA68Bu;
			Value ^= Value >> 16;
			return Value;
		}

		/** 在已按 Stable Id 排序的快照中线性解析 Flow；不使用 FName Hash 或 TMap 遍历。 */
		const FZeroEscapeFlowDefinition* FindFlow(
			const FGenerationProfileSnapshot& Profile,
			const FName StableFlowId,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			const FZeroEscapeFlowDefinition* Result = nullptr;
			for (const FZeroEscapeFlowDefinition& Flow : Profile.Flows)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Configuration, TEXT("Flow 解析")))
				{
					return nullptr;
				}
				if (Flow.StableFlowId == StableFlowId)
				{
					if (Result != nullptr)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::Configuration,
							EZeroEscapeGenerationFailure::InvalidConfiguration,
							TEXT("纯值快照包含重复 Flow Stable Id。"));
						return nullptr;
					}
					Result = &Flow;
				}
			}
			return Result;
		}

		/** 在已按 Difficulty 排序的快照中线性解析单局难度。 */
		const FZeroEscapeDifficultyDefinition* FindDifficulty(
			const FGenerationProfileSnapshot& Profile,
			const EZeroEscapeDifficulty Difficulty,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			const FZeroEscapeDifficultyDefinition* Result = nullptr;
			for (const FZeroEscapeDifficultyDefinition& Definition : Profile.Difficulties)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Configuration, TEXT("Difficulty 解析")))
				{
					return nullptr;
				}
				if (Definition.Difficulty == Difficulty)
				{
					if (Result != nullptr)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::Configuration,
							EZeroEscapeGenerationFailure::InvalidConfiguration,
							TEXT("纯值快照包含重复 Difficulty Definition。"));
						return nullptr;
					}
					Result = &Definition;
				}
			}
			return Result;
		}
	}

	bool IsFiniteUnitScaleTransform(const FTransform& Transform)
	{
		// 布局逻辑只允许平移与旋转。缩放会破坏 Cell/Portal 几何契约，因此必须在进入实例化前 fail closed。
		return !Transform.ContainsNaN()
			&& Transform.GetRotation().IsNormalized()
			&& Transform.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER);
	}

	bool BuildGenerationSnapshot(
		const UZeroEscapeLevelGenerationProfile& Source,
		FGenerationProfileSnapshot& OutSnapshot,
		FZeroEscapeGenerationReport& OutReport)
	{
		// 先清空外部可见输出：下面任何 early return 都不会留下上次调用的快照或报告。
		OutSnapshot = {};
		OutReport = {};

		if (!IsInGameThread())
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("BuildGenerationSnapshot 必须在游戏线程读取 DataAsset。"));
			return false;
		}

		// 在复制前调用 DataAsset 自校验，以便将编辑器配置错误精确归类为 Configuration 失败。
		FString ValidationError;
		if (!Source.IsConfigured(ValidationError))
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				FString::Printf(TEXT("Generation Profile 校验失败：%s"), *ValidationError));
			return false;
		}

		// 使用局部 Snapshot 完成整个事务；只有复制、计费和规范排序全部成功才提交给 OutSnapshot。
		FCoreWorkBudget Work(ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits);
		FGenerationProfileSnapshot Snapshot;
		Snapshot.ProfileVersion = Source.ProfileVersion;
		Snapshot.SharedRouteConstraints = Source.SharedRouteConstraints;
		Snapshot.SolverBudgets = Source.SolverBudgets;
		Snapshot.bRequireEffectiveWfcChoice = Source.bRequireEffectiveWfcChoice;
		Snapshot.Difficulties = Source.Difficulties;
		Snapshot.Flows = Source.Flows;

		if (!ConsumeWork(
				Work,
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				TEXT("Generation Snapshot 复制"),
				static_cast<int64>(Snapshot.Difficulties.Num()) + Snapshot.Flows.Num()))
		{
			return false;
		}

		// 规范排序隔离 DataAsset 数组的手工插入顺序，使后续查找、随机消费和 Hash 输入稳定。
		Snapshot.Difficulties.Sort(
			[](const FZeroEscapeDifficultyDefinition& A, const FZeroEscapeDifficultyDefinition& B)
			{
				return static_cast<uint8>(A.Difficulty) < static_cast<uint8>(B.Difficulty);
			});
		for (FZeroEscapeFlowDefinition& Flow : Snapshot.Flows)
		{
			if (!ConsumeWork(
					Work,
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					TEXT("Flow Role Snapshot 排序"),
					Flow.AllowedObjectiveRoles.Num()))
			{
				return false;
			}
			Flow.AllowedObjectiveRoles.Sort(TopologyRoleLess);
		}
		Snapshot.Flows.Sort(
			[](const FZeroEscapeFlowDefinition& A, const FZeroEscapeFlowDefinition& B)
			{
				return A.StableFlowId.LexicalLess(B.StableFlowId);
			});

		// 单一提交点：到这里 Snapshot 才成为调用方可见的权威输入。
		OutSnapshot = MoveTemp(Snapshot);
		Succeed(OutReport, EZeroEscapeGenerationStage::Configuration, TEXT("Generation Profile 纯值快照已构建。"));
		return true;
	}

	bool BuildCatalogSnapshot(
		const UZeroEscapeModuleCatalog& Source,
		FModuleCatalogSnapshot& OutSnapshot,
		FZeroEscapeGenerationReport& OutReport)
	{
		// Catalog 使用与 Profile 相同的失败原子性：入口清空 Out，局部构建，末尾一次提交。
		OutSnapshot = {};
		OutReport = {};

		if (!IsInGameThread())
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("BuildCatalogSnapshot 必须在游戏线程读取 DataAsset。"));
			return false;
		}

		FString ValidationError;
		if (!Source.IsConfigured(ValidationError))
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				FString::Printf(TEXT("Module Catalog 校验失败：%s"), *ValidationError));
			return false;
		}

		FCoreWorkBudget Work(ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits);
		FModuleCatalogSnapshot Snapshot;
		Snapshot.CatalogVersion = Source.CatalogVersion;
		Snapshot.CellSize = Source.CellSize;
		Snapshot.Modules.Reserve(Source.Modules.Num());

		// 每个模块只复制 Layout 需要的纯值契约；Mesh/材质/第三方路径仍留在 Presentation Profile。
		for (const FZeroEscapeModuleDefinition& SourceModule : Source.Modules)
		{
			const int64 ModuleWork = 1LL
				+ SourceModule.AllowedRoles.Num()
				+ SourceModule.Portals.Num()
				+ SourceModule.GameplayAnchors.Num();
			if (!ConsumeWork(
					Work,
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					TEXT("Module Catalog Snapshot 复制"),
					ModuleWork))
			{
				return false;
			}

			FModuleSnapshot Module;
			Module.StableModuleId = SourceModule.StableModuleId;
			Module.LayoutPolicy = SourceModule.LayoutPolicy;
			Module.AllowedRoles = SourceModule.AllowedRoles;
			Module.Footprint = SourceModule.Footprint;
			Module.AllowedQuarterTurnsMask = SourceModule.AllowedQuarterTurnsMask;
			Module.Weight = SourceModule.Weight;
			Module.LocalBounds = SourceModule.LocalBounds;
			Module.Portals = SourceModule.Portals;
			Module.GameplayAnchors = SourceModule.GameplayAnchors;

			// 先规范化模块内部的可变长字段，再把模块放入总表。
			Module.AllowedRoles.Sort(TopologyRoleLess);
			Module.Portals.Sort(
				[](const FZeroEscapeModulePortal& A, const FZeroEscapeModulePortal& B)
				{
					return A.StableSocketId < B.StableSocketId;
				});
			Module.GameplayAnchors.Sort(
				[](const FZeroEscapeModuleAnchor& A, const FZeroEscapeModuleAnchor& B)
				{
					return A.StableAnchorId < B.StableAnchorId;
				});
			Snapshot.Modules.Add(MoveTemp(Module));
		}

		// StableModuleId 是算法层的身份；编辑器显示名或数组顺序不能影响候选枚举。
		Snapshot.Modules.Sort(
			[](const FModuleSnapshot& A, const FModuleSnapshot& B)
			{
				return A.StableModuleId < B.StableModuleId;
			});

		OutSnapshot = MoveTemp(Snapshot);
		Succeed(OutReport, EZeroEscapeGenerationStage::Configuration, TEXT("Module Catalog 纯值快照已构建。"));
		return true;
	}

	bool ResolveGenerationBudget(
		const FZeroEscapeGenerationRequest& Request,
		const FGenerationProfileSnapshot& Profile,
		FResolvedGenerationBudget& OutBudget,
		FZeroEscapeGenerationReport& OutReport)
	{
		// 解析也是事务式的：所有交叉字段校验通过前，OutBudget 始终为默认空值。
		OutBudget = {};
		OutReport = {};

		const FZeroEscapeSharedRouteConstraints& Shared = Profile.SharedRouteConstraints;
		if (Profile.ProfileVersion < 1
			|| Request.FlowProfileId.IsNone()
			|| !IsValidDifficulty(Request.Difficulty)
			|| Profile.Difficulties.Num() != 3
			|| Profile.Flows.IsEmpty()
			|| Shared.GridExtentCells.X < MinGridExtentCells
			|| Shared.GridExtentCells.Y < MinGridExtentCells
			|| Shared.GridExtentCells.X > MaxGridExtentCells
			|| Shared.GridExtentCells.Y > MaxGridExtentCells
			|| Shared.CriticalPathNodeCount < 4
			|| Shared.CriticalPathNodeCount > ZeroEscape::GenerationLimits::FirstPassMaxCriticalPathNodes
			|| Shared.MaxLeafOneWayEdgeCount < 0
			|| Shared.MaxRequiredRouteExtraEdgeCount < 0
			|| Shared.MaxObjectiveCandidateCount < 1
			|| Shared.MaxObjectiveCandidateCount > ZeroEscape::GenerationLimits::MaxObjectiveCandidates
			|| Shared.MaxProgressionSearchStates < 1
			|| Shared.MaxProgressionSearchStates > ZeroEscape::GenerationLimits::FirstPassMaxProgressionSearchStates
			|| Profile.SolverBudgets.MaxTotalWorkUnits < 1
			|| Profile.SolverBudgets.MaxTotalWorkUnits > ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits)
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("Generation Request 或纯值 Profile Snapshot 的版本、边界、预算非法。"));
			return false;
		}

		FCoreWorkBudget Work(Profile.SolverBudgets.MaxTotalWorkUnits);
		const FZeroEscapeDifficultyDefinition* Difficulty = FindDifficulty(
			Profile,
			Request.Difficulty,
			Work,
			OutReport);
		if (Difficulty == nullptr)
		{
			if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("Request 指定的 Difficulty 在快照中不存在。"));
			}
			return false;
		}

		const FZeroEscapeFlowDefinition* Flow = FindFlow(
			Profile,
			Request.FlowProfileId,
			Work,
			OutReport);
		if (Flow == nullptr)
		{
			if (OutReport.Failure == EZeroEscapeGenerationFailure::None)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					FString::Printf(TEXT("Request 指定的 Flow %s 在快照中不存在。"), *Request.FlowProfileId.ToString()));
			}
			return false;
		}

		if (Flow->FlowVersion < 1 || !IsValidCompletionRule(Flow->CompletionRule))
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("解析到的 Flow 版本或 CompletionRule 非法。"));
			return false;
		}

		// 从此刻起，Difficulty 提供“多少分支/目标”，Flow 提供“如何通关”，Shared 提供所有难度必须共同遵守的时长与折返约束。
		FResolvedGenerationBudget Budget;
		Budget.Difficulty = Request.Difficulty;
		Budget.StableFlowId = Flow->StableFlowId;
		Budget.FlowVersion = Flow->FlowVersion;
		Budget.CompletionRule = Flow->CompletionRule;
		Budget.GridExtentCells = Shared.GridExtentCells;
		Budget.CriticalPathNodeCount = Shared.CriticalPathNodeCount;
		Budget.ShortLeafBranchCount = Difficulty->ShortLeafBranchCount;
		Budget.ForwardRejoinBranchCount = Difficulty->ForwardRejoinBranchCount;
		Budget.MaxLeafOneWayEdgeCount = Shared.MaxLeafOneWayEdgeCount;
		Budget.MaxRequiredRouteExtraEdgeCount = Shared.MaxRequiredRouteExtraEdgeCount;
		Budget.MaxProgressionSearchStates = Shared.MaxProgressionSearchStates;
		Budget.AStarStraightStepCost = Shared.AStarStraightStepCost;
		Budget.AStarTurnPenalty = Shared.AStarTurnPenalty;
		Budget.AllowedObjectiveRoles = Flow->AllowedObjectiveRoles;
		Budget.AllowedObjectiveRoles.Sort(TopologyRoleLess);
		Budget.SolverBudgets = Profile.SolverBudgets;

		// 将三种通关语义统一归一化为 K/N，后续搜索不再猜测 DataAsset 字段的意图。
		// EscapeOnly: K=N=0；CollectAll: K=N=Difficulty.N；CollectKOfN: K=Difficulty.K, N=Difficulty.N。
		switch (Flow->CompletionRule)
		{
		case EZeroEscapeCompletionRule::EscapeOnly:
			Budget.ObjectiveCandidateCount = 0;
			Budget.RequiredObjectiveCount = 0;
			Budget.AllowedObjectiveRoles.Reset();
			break;
		case EZeroEscapeCompletionRule::CollectAll:
			Budget.ObjectiveCandidateCount = Difficulty->ObjectiveCandidateCount;
			Budget.RequiredObjectiveCount = Difficulty->ObjectiveCandidateCount;
			break;
		case EZeroEscapeCompletionRule::CollectKOfN:
			Budget.ObjectiveCandidateCount = Difficulty->ObjectiveCandidateCount;
			Budget.RequiredObjectiveCount = Difficulty->RequiredObjectiveCount;
			break;
		default:
			checkNoEntry();
			return false;
		}

		// 在真正构图前完成容量证明。这些计数是构造器的硬上限，不是“尝试一下看能否成功”的启发式估计。
		const int32 InternalCriticalNodeCount = Budget.CriticalPathNodeCount - 2;
		const int32 ForwardCapacity = InternalCriticalNodeCount / 2;
		if (Budget.ShortLeafBranchCount < 0
			|| Budget.ShortLeafBranchCount > InternalCriticalNodeCount
			|| (Budget.ShortLeafBranchCount > 0 && Budget.MaxLeafOneWayEdgeCount < 1)
			|| Budget.ForwardRejoinBranchCount < 0
			|| Budget.ForwardRejoinBranchCount > ForwardCapacity
			|| Budget.ObjectiveCandidateCount < 0
			|| Budget.ObjectiveCandidateCount > Shared.MaxObjectiveCandidateCount
			|| Budget.ObjectiveCandidateCount > ZeroEscape::GenerationLimits::MaxObjectiveCandidates
			|| Budget.RequiredObjectiveCount < 0
			|| Budget.RequiredObjectiveCount > Budget.ObjectiveCandidateCount
			|| (Budget.CompletionRule != EZeroEscapeCompletionRule::EscapeOnly
				&& (Budget.ObjectiveCandidateCount < 1 || Budget.RequiredObjectiveCount < 1)))
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Progression,
				EZeroEscapeGenerationFailure::InvalidKOfN,
				TEXT("解析后的分支容量或 K-of-N 预算非法。"),
				INDEX_NONE,
				Budget.RequiredObjectiveCount,
				Budget.ObjectiveCandidateCount);
			return false;
		}

		// 角色容量是 N 的第二道预检：即使 K<=N 合法，Flow 所允许的节点类型也必须真实容纳 N 个互异目标。
		uint8 ObjectiveRoleMask = 0;
		int32 ObjectiveCapacity = 0;
		for (const EZeroEscapeTopologyRole Role : Budget.AllowedObjectiveRoles)
		{
			if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("Objective Role 解析")))
			{
				return false;
			}
			if (!IsObjectiveRole(Role))
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("Flow 包含 Start、Exit 或未知 Objective Role。"));
				return false;
			}
			const uint8 RoleBit = static_cast<uint8>(1u << static_cast<uint8>(Role));
			if ((ObjectiveRoleMask & RoleBit) != 0)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Configuration,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("Flow Snapshot 包含重复 Objective Role。"));
				return false;
			}
			ObjectiveRoleMask |= RoleBit;

			switch (Role)
			{
			case EZeroEscapeTopologyRole::MainPath:
				ObjectiveCapacity += InternalCriticalNodeCount;
				break;
			case EZeroEscapeTopologyRole::ShortLeaf:
				ObjectiveCapacity += Budget.ShortLeafBranchCount;
				break;
			case EZeroEscapeTopologyRole::ForwardRejoin:
				ObjectiveCapacity += Budget.ForwardRejoinBranchCount;
				break;
			default:
				checkNoEntry();
				break;
			}
		}

		if (Budget.ObjectiveCandidateCount > 0
			&& ObjectiveCapacity < Budget.ObjectiveCandidateCount)
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Progression,
				EZeroEscapeGenerationFailure::TopologyCapacityInsufficient,
				TEXT("允许的拓扑角色无法容纳本局 ObjectiveCandidateCount。"),
				INDEX_NONE,
				ObjectiveCapacity,
				Budget.ObjectiveCandidateCount);
			return false;
		}

		// 唯一提交点；之前任何失败都不会泄露只填了一部分字段的 Budget。
		OutBudget = MoveTemp(Budget);
		Succeed(OutReport, EZeroEscapeGenerationStage::Configuration, TEXT("Difficulty 与 Flow 已解析为单局固定预算。"));
		return true;
	}

	bool BuildGenerationSignature(
		const FZeroEscapeGenerationRequest& Request,
		const FGenerationProfileSnapshot& GenerationProfile,
		const FModuleCatalogSnapshot& ModuleCatalog,
		const int32 PresentationVersion,
		FZeroEscapeGenerationSignature& OutSignature,
		FZeroEscapeGenerationReport& OutReport)
	{
		// 签名构建同样 fail closed：先解析并验证全套输入版本，再一次性提交。
		OutSignature = {};
		FResolvedGenerationBudget Budget;
		if (!ResolveGenerationBudget(Request, GenerationProfile, Budget, OutReport))
		{
			return false;
		}

		if (GAlgorithmVersion < 1
			|| ModuleCatalog.CatalogVersion < 1
			|| ModuleCatalog.Modules.IsEmpty()
			|| ModuleCatalog.CellSize.ContainsNaN()
			|| ModuleCatalog.CellSize.X <= UE_SMALL_NUMBER
			|| ModuleCatalog.CellSize.Y <= UE_SMALL_NUMBER
			|| ModuleCatalog.CellSize.Z <= UE_SMALL_NUMBER
			|| PresentationVersion < 1)
		{
			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("生成签名所需的 Algorithm/Catalog/Presentation 版本或 Catalog Snapshot 非法。"));
			return false;
		}

		// Signature 记录复现所需的“输入身份”；不在这里重复编码 Plan 结果，结果一致性由后续两个 Canonical Hash 负责。
		FZeroEscapeGenerationSignature Signature;
		Signature.Seed = Request.Seed;
		Signature.Difficulty = Request.Difficulty;
		Signature.FlowProfileId = Budget.StableFlowId;
		Signature.AlgorithmVersion = GAlgorithmVersion;
		Signature.GenerationProfileVersion = GenerationProfile.ProfileVersion;
		Signature.FlowVersion = Budget.FlowVersion;
		Signature.CatalogVersion = ModuleCatalog.CatalogVersion;
		Signature.PresentationVersion = PresentationVersion;
		OutSignature = MoveTemp(Signature);
		Succeed(OutReport, EZeroEscapeGenerationStage::Configuration, TEXT("Generation Signature 已由纯值快照构建。"));
		return true;
	}

	FRandomStream FGenerationCore::MakeRandomStream(
		const int32 MasterSeed,
		const int32 AlgorithmVersion,
		const ERandomDomain Domain,
		const int32 AttemptIndex)
	{
		// 每一级都做 avalanche mix，避免相邻 Seed/Attempt 仅在低位有微小差异。
		// Domain 与 Attempt 是显式输入，所以一个阶段增删随机抽样不会推动其他阶段的随机序列。
		uint32 Value = Mix32(static_cast<uint32>(MasterSeed));
		Value = Mix32(Value ^ static_cast<uint32>(AlgorithmVersion));
		Value = Mix32(Value ^ static_cast<uint32>(Domain));
		Value = Mix32(Value ^ static_cast<uint32>(AttemptIndex));
		// FRandomStream 接受 int32 Seed；清除符号位使日志/序列化更直观，不依赖有符号转换的实现细节。
		return FRandomStream(static_cast<int32>(Value & 0x7FFFFFFFu));
	}
}

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		/**
		 * 逐字节写入固定小端整数的 FNV-1a；不读取结构体 Padding 或平台相关对象内存。
		 * 每个调用点都明确写入字段和数量，所以容器分配地址、编译器 Padding、主机字节序都不影响结果。
		 * Finalize 保留 63 位正数范围，并把 0 留给“输入非法/超预算”这一明确哨兵值。
		 */
		class FCanonicalHashBuilder final
		{
		public:
			void AddUInt8(const uint8 Value)
			{
				Hash ^= Value;
				Hash *= 1099511628211ULL;
			}

			void AddUInt32(const uint32 Value)
			{
				for (uint32 Shift = 0; Shift < 32; Shift += 8)
				{
					AddUInt8(static_cast<uint8>((Value >> Shift) & 0xFFu));
				}
			}

			void AddInt32(const int32 Value)
			{
				AddUInt32(static_cast<uint32>(Value));
			}

			void AddUInt64(const uint64 Value)
			{
				for (uint32 Shift = 0; Shift < 64; Shift += 8)
				{
					AddUInt8(static_cast<uint8>((Value >> Shift) & 0xFFu));
				}
			}

			void AddInt64(const int64 Value)
			{
				AddUInt64(static_cast<uint64>(Value));
			}

			void AddIntVector(const FIntVector& Value)
			{
				AddInt32(Value.X);
				AddInt32(Value.Y);
				AddInt32(Value.Z);
			}

			int64 Finalize() const
			{
				const uint64 PositiveHash = Hash & 0x7FFFFFFFFFFFFFFFULL;
				return static_cast<int64>(PositiveHash == 0 ? 1 : PositiveHash);
			}

		private:
			uint64 Hash = 14695981039346656037ULL;
		};

		/** 累加待规范化的记录数，避免恶意超大数组在复制或排序阶段耗尽资源。 */
		bool AddCanonicalWork(const int64 Count, int64& InOutWork)
		{
			constexpr int64 Limit = ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits;
			if (Count < 0 || InOutWork > Limit - Count)
			{
				return false;
			}
			InOutWork += Count;
			return true;
		}

		/** 网格坐标使用 X -> Y -> Z 词典序，不依赖 FVector 浮点比较或容器内部顺序。 */
		bool IntVectorLess(const FIntVector& A, const FIntVector& B)
		{
			if (A.X != B.X)
			{
				return A.X < B.X;
			}
			if (A.Y != B.Y)
			{
				return A.Y < B.Y;
			}
			return A.Z < B.Z;
		}

		/** 从 PlacedModule 投影出仅影响逻辑布局的字段，故意排除 WorldTransform 和表现资源。 */
		struct FCanonicalModuleRecord
		{
			int32 StablePlacementId = INDEX_NONE;
			int32 StableModuleId = INDEX_NONE;
			int32 StableVariantId = INDEX_NONE;
			uint8 QuarterTurns = 0;
			int32 AbstractNodeId = INDEX_NONE;
			FIntVector GridOrigin = FIntVector::ZeroValue;
		};

		/** 无向抽象边的规范路由；端点反转时同时反转 Placement 序列。 */
		struct FCanonicalEdgeRouteRecord
		{
			int32 AbstractEdgeId = INDEX_NONE;
			int32 FromNodeId = INDEX_NONE;
			int32 ToNodeId = INDEX_NONE;
			TArray<int32> OrderedStablePlacementIds;
		};

		/** Portal 连接的规范无向表示；A/B 按 (PlacementId, SocketId) 排序。 */
		struct FCanonicalPortalRecord
		{
			int32 StableConnectionId = INDEX_NONE;
			int32 AbstractEdgeId = INDEX_NONE;
			int32 StablePlacementAId = INDEX_NONE;
			int32 StableSocketAId = INDEX_NONE;
			int32 StablePlacementBId = INDEX_NONE;
			int32 StableSocketBId = INDEX_NONE;
		};

		/** 只保留 Gameplay Anchor 身份与归属；浮点 Transform 可由模块契约重建而不进入 Hash。 */
		struct FCanonicalAnchorRecord
		{
			int32 StableAnchorInstanceId = INDEX_NONE;
			EZeroEscapeGameplayAnchorType Type = EZeroEscapeGameplayAnchorType::Objective;
			int32 StablePlacementId = INDEX_NONE;
			int32 StableModuleAnchorId = INDEX_NONE;
		};

		bool ModuleRecordLess(const FCanonicalModuleRecord& A, const FCanonicalModuleRecord& B)
		{
			if (A.StablePlacementId != B.StablePlacementId)
			{
				return A.StablePlacementId < B.StablePlacementId;
			}
			if (A.StableModuleId != B.StableModuleId)
			{
				return A.StableModuleId < B.StableModuleId;
			}
			if (A.StableVariantId != B.StableVariantId)
			{
				return A.StableVariantId < B.StableVariantId;
			}
			if (A.QuarterTurns != B.QuarterTurns)
			{
				return A.QuarterTurns < B.QuarterTurns;
			}
			if (A.AbstractNodeId != B.AbstractNodeId)
			{
				return A.AbstractNodeId < B.AbstractNodeId;
			}
			return IntVectorLess(A.GridOrigin, B.GridOrigin);
		}

		bool EdgeRouteRecordLess(const FCanonicalEdgeRouteRecord& A, const FCanonicalEdgeRouteRecord& B)
		{
			if (A.AbstractEdgeId != B.AbstractEdgeId)
			{
				return A.AbstractEdgeId < B.AbstractEdgeId;
			}
			if (A.FromNodeId != B.FromNodeId)
			{
				return A.FromNodeId < B.FromNodeId;
			}
			if (A.ToNodeId != B.ToNodeId)
			{
				return A.ToNodeId < B.ToNodeId;
			}
			const int32 SharedCount = FMath::Min(
				A.OrderedStablePlacementIds.Num(),
				B.OrderedStablePlacementIds.Num());
			for (int32 Index = 0; Index < SharedCount; ++Index)
			{
				if (A.OrderedStablePlacementIds[Index] != B.OrderedStablePlacementIds[Index])
				{
					return A.OrderedStablePlacementIds[Index] < B.OrderedStablePlacementIds[Index];
				}
			}
			return A.OrderedStablePlacementIds.Num() < B.OrderedStablePlacementIds.Num();
		}

		bool PortalRecordLess(const FCanonicalPortalRecord& A, const FCanonicalPortalRecord& B)
		{
			if (A.StableConnectionId != B.StableConnectionId)
			{
				return A.StableConnectionId < B.StableConnectionId;
			}
			if (A.AbstractEdgeId != B.AbstractEdgeId)
			{
				return A.AbstractEdgeId < B.AbstractEdgeId;
			}
			if (A.StablePlacementAId != B.StablePlacementAId)
			{
				return A.StablePlacementAId < B.StablePlacementAId;
			}
			if (A.StableSocketAId != B.StableSocketAId)
			{
				return A.StableSocketAId < B.StableSocketAId;
			}
			if (A.StablePlacementBId != B.StablePlacementBId)
			{
				return A.StablePlacementBId < B.StablePlacementBId;
			}
			return A.StableSocketBId < B.StableSocketBId;
		}

		bool AnchorRecordLess(const FCanonicalAnchorRecord& A, const FCanonicalAnchorRecord& B)
		{
			if (A.StableAnchorInstanceId != B.StableAnchorInstanceId)
			{
				return A.StableAnchorInstanceId < B.StableAnchorInstanceId;
			}
			if (A.Type != B.Type)
			{
				return static_cast<uint8>(A.Type) < static_cast<uint8>(B.Type);
			}
			if (A.StablePlacementId != B.StablePlacementId)
			{
				return A.StablePlacementId < B.StablePlacementId;
			}
			return A.StableModuleAnchorId < B.StableModuleAnchorId;
		}
	}

	int64 ComputeCanonicalAbstractHash(const FAbstractLevelPlan& Plan)
	{
		// 先检查硬上限，再复制和排序；0 表示拒绝计算，不是一个有效关卡 Hash。
		int64 Work = 0;
		if (Plan.Nodes.Num() > MaxAbstractNodeCount
			|| Plan.Edges.Num() > MaxAbstractEdgeCount
			|| Plan.Objectives.Num() > ZeroEscape::GenerationLimits::MaxObjectiveCandidates
			|| !AddCanonicalWork(Plan.Nodes.Num(), Work)
			|| !AddCanonicalWork(Plan.Edges.Num(), Work)
			|| !AddCanonicalWork(Plan.Objectives.Num(), Work))
		{
			return 0;
		}

		// 输入数组即使来自外部或以不同插入顺序构造，同一组逻辑记录也必须得到同一 Hash。
		TArray<FSpatialNode> Nodes = Plan.Nodes;
		Nodes.Sort(
			[](const FSpatialNode& A, const FSpatialNode& B)
			{
				if (A.StableNodeId != B.StableNodeId)
				{
					return A.StableNodeId < B.StableNodeId;
				}
				if (A.Role != B.Role)
				{
					return static_cast<uint8>(A.Role) < static_cast<uint8>(B.Role);
				}
				if (A.ProgressIndex != B.ProgressIndex)
				{
					return A.ProgressIndex < B.ProgressIndex;
				}
				if (A.AnchorProgressIndex != B.AnchorProgressIndex)
				{
					return A.AnchorProgressIndex < B.AnchorProgressIndex;
				}
				return A.RejoinProgressIndex < B.RejoinProgressIndex;
			});

		// 抽象边是无向的；先将较小 StableNodeId 放在 A，避免 A-B 与 B-A 被误视为两个结果。
		TArray<FSpatialEdge> Edges = Plan.Edges;
		for (FSpatialEdge& Edge : Edges)
		{
			if (Edge.StableNodeB < Edge.StableNodeA)
			{
				Swap(Edge.StableNodeA, Edge.StableNodeB);
			}
		}
		Edges.Sort(
			[](const FSpatialEdge& A, const FSpatialEdge& B)
			{
				if (A.StableEdgeId != B.StableEdgeId)
				{
					return A.StableEdgeId < B.StableEdgeId;
				}
				if (A.StableNodeA != B.StableNodeA)
				{
					return A.StableNodeA < B.StableNodeA;
				}
				if (A.StableNodeB != B.StableNodeB)
				{
					return A.StableNodeB < B.StableNodeB;
				}
				return A.TraversalCost < B.TraversalCost;
			});

		TArray<FObjectivePlacement> Objectives = Plan.Objectives;
		Objectives.Sort(
			[](const FObjectivePlacement& A, const FObjectivePlacement& B)
			{
				return A.StableObjectiveId != B.StableObjectiveId
					? A.StableObjectiveId < B.StableObjectiveId
					: A.StableNodeId < B.StableNodeId;
			});

		// 格式魔数是 Hash Schema 版本标识；未来变更字段语义时必须同步更新契约。
		FCanonicalHashBuilder Hash;
		Hash.AddUInt32(0x41425331u); // "ABS1"
		Hash.AddUInt8(static_cast<uint8>(Plan.CompletionRule));
		Hash.AddInt32(Plan.RequiredObjectiveCount);
		Hash.AddInt32(Plan.StartStableNodeId);
		Hash.AddInt32(Plan.ExitStableNodeId);
		Hash.AddInt32(Nodes.Num());
		for (const FSpatialNode& Node : Nodes)
		{
			Hash.AddInt32(Node.StableNodeId);
			Hash.AddUInt8(static_cast<uint8>(Node.Role));
			Hash.AddInt32(Node.ProgressIndex);
			Hash.AddInt32(Node.AnchorProgressIndex);
			Hash.AddInt32(Node.RejoinProgressIndex);
		}
		Hash.AddInt32(Edges.Num());
		for (const FSpatialEdge& Edge : Edges)
		{
			Hash.AddInt32(Edge.StableEdgeId);
			Hash.AddInt32(Edge.StableNodeA);
			Hash.AddInt32(Edge.StableNodeB);
			Hash.AddInt32(Edge.TraversalCost);
		}
		Hash.AddInt32(Objectives.Num());
		for (const FObjectivePlacement& Objective : Objectives)
		{
			Hash.AddInt32(Objective.StableObjectiveId);
			Hash.AddInt32(Objective.StableNodeId);
		}
		return Hash.Finalize();
	}

	int64 ComputeCanonicalLayoutHash(const FZeroEscapeGeneratedLevelPlan& Plan)
	{
		// Layout 可变长字段更多，因此在任何复制前先统计顶层记录与所有嵌套路由长度。
		int64 Work = 0;
		if (!AddCanonicalWork(Plan.Modules.Num(), Work)
			|| !AddCanonicalWork(Plan.NodeBindings.Num(), Work)
			|| !AddCanonicalWork(Plan.EdgeRoutes.Num(), Work)
			|| !AddCanonicalWork(Plan.PortalConnections.Num(), Work)
			|| !AddCanonicalWork(Plan.ClosedPortals.Num(), Work)
			|| !AddCanonicalWork(Plan.GameplayAnchors.Num(), Work)
			|| !AddCanonicalWork(Plan.ObjectiveBindings.Num(), Work))
		{
			return 0;
		}
		for (const FZeroEscapeEdgeRouteBinding& Route : Plan.EdgeRoutes)
		{
			if (!AddCanonicalWork(Route.OrderedStablePlacementIds.Num(), Work))
			{
				return 0;
			}
		}

		// 将运行结构投影为精简规范记录，防止今后在原 USTRUCT 中新增表现字段时无意改变逻辑 Hash。
		TArray<FCanonicalModuleRecord> Modules;
		Modules.Reserve(Plan.Modules.Num());
		for (const FZeroEscapePlacedModule& Module : Plan.Modules)
		{
			Modules.Add({
				Module.StablePlacementId,
				Module.StableModuleId,
				Module.StableVariantId,
				Module.QuarterTurns,
				Module.AbstractNodeId,
				Module.GridOrigin });
		}
		Modules.Sort(ModuleRecordLess);

		TArray<FZeroEscapeNodePlacementBinding> NodeBindings = Plan.NodeBindings;
		NodeBindings.Sort(
			[](const FZeroEscapeNodePlacementBinding& A, const FZeroEscapeNodePlacementBinding& B)
			{
				return A.AbstractNodeId != B.AbstractNodeId
					? A.AbstractNodeId < B.AbstractNodeId
					: A.StablePlacementId < B.StablePlacementId;
			});

		// 路由边是无向契约：若端点对调，路径序列也必须同时反转后才能比较。
		TArray<FCanonicalEdgeRouteRecord> EdgeRoutes;
		EdgeRoutes.Reserve(Plan.EdgeRoutes.Num());
		for (const FZeroEscapeEdgeRouteBinding& Source : Plan.EdgeRoutes)
		{
			FCanonicalEdgeRouteRecord Route;
			Route.AbstractEdgeId = Source.AbstractEdgeId;
			const bool bReverse = Source.ToNodeId < Source.FromNodeId;
			Route.FromNodeId = bReverse ? Source.ToNodeId : Source.FromNodeId;
			Route.ToNodeId = bReverse ? Source.FromNodeId : Source.ToNodeId;
			Route.OrderedStablePlacementIds.Reserve(Source.OrderedStablePlacementIds.Num());
			if (bReverse)
			{
				for (int32 Index = Source.OrderedStablePlacementIds.Num() - 1; Index >= 0; --Index)
				{
					Route.OrderedStablePlacementIds.Add(Source.OrderedStablePlacementIds[Index]);
				}
			}
			else
			{
				Route.OrderedStablePlacementIds = Source.OrderedStablePlacementIds;
			}
			EdgeRoutes.Add(MoveTemp(Route));
		}
		EdgeRoutes.Sort(EdgeRouteRecordLess);

		// Portal A/B 同样没有方向性，通过端点二元组统一定向。
		TArray<FCanonicalPortalRecord> PortalConnections;
		PortalConnections.Reserve(Plan.PortalConnections.Num());
		for (const FZeroEscapePortalConnection& Source : Plan.PortalConnections)
		{
			FCanonicalPortalRecord Portal;
			Portal.StableConnectionId = Source.StableConnectionId;
			Portal.AbstractEdgeId = Source.AbstractEdgeId;
			const bool bSwapEndpoints = Source.StablePlacementBId < Source.StablePlacementAId
				|| (Source.StablePlacementBId == Source.StablePlacementAId
					&& Source.StableSocketBId < Source.StableSocketAId);
			Portal.StablePlacementAId = bSwapEndpoints ? Source.StablePlacementBId : Source.StablePlacementAId;
			Portal.StableSocketAId = bSwapEndpoints ? Source.StableSocketBId : Source.StableSocketAId;
			Portal.StablePlacementBId = bSwapEndpoints ? Source.StablePlacementAId : Source.StablePlacementBId;
			Portal.StableSocketBId = bSwapEndpoints ? Source.StableSocketAId : Source.StableSocketBId;
			PortalConnections.Add(Portal);
		}
		PortalConnections.Sort(PortalRecordLess);

		TArray<FZeroEscapeClosedPortal> ClosedPortals = Plan.ClosedPortals;
		ClosedPortals.Sort(
			[](const FZeroEscapeClosedPortal& A, const FZeroEscapeClosedPortal& B)
			{
				if (A.StablePlacementId != B.StablePlacementId)
				{
					return A.StablePlacementId < B.StablePlacementId;
				}
				if (A.StableSocketId != B.StableSocketId)
				{
					return A.StableSocketId < B.StableSocketId;
				}
				return A.StableClosurePlacementId < B.StableClosurePlacementId;
			});

		TArray<FCanonicalAnchorRecord> GameplayAnchors;
		GameplayAnchors.Reserve(Plan.GameplayAnchors.Num());
		for (const FZeroEscapeGeneratedAnchor& Anchor : Plan.GameplayAnchors)
		{
			GameplayAnchors.Add({
				Anchor.StableAnchorInstanceId,
				Anchor.Type,
				Anchor.StablePlacementId,
				Anchor.StableModuleAnchorId });
		}
		GameplayAnchors.Sort(AnchorRecordLess);

		TArray<FZeroEscapeObjectiveBinding> ObjectiveBindings = Plan.ObjectiveBindings;
		ObjectiveBindings.Sort(
			[](const FZeroEscapeObjectiveBinding& A, const FZeroEscapeObjectiveBinding& B)
			{
				if (A.StableObjectiveId != B.StableObjectiveId)
				{
					return A.StableObjectiveId < B.StableObjectiveId;
				}
				if (A.AbstractNodeId != B.AbstractNodeId)
				{
					return A.AbstractNodeId < B.AbstractNodeId;
				}
				if (A.StablePlacementId != B.StablePlacementId)
				{
					return A.StablePlacementId < B.StablePlacementId;
				}
				return A.StableAnchorInstanceId < B.StableAnchorInstanceId;
			});

		// 不写入 PresentationVersion、Mesh、材质或浮点 WorldTransform：换皮和等价实例化不应伪装成逻辑地图变化。
		FCanonicalHashBuilder Hash;
		Hash.AddUInt32(0x4C415931u); // "LAY1"
		Hash.AddInt64(Plan.CanonicalAbstractHash);
		Hash.AddInt32(Plan.StartPlacementId);
		Hash.AddInt32(Plan.ExitPlacementId);
		Hash.AddInt32(Plan.PlayerSpawnAnchorInstanceId);
		Hash.AddInt32(Plan.ExitAnchorInstanceId);

		Hash.AddInt32(Modules.Num());
		for (const FCanonicalModuleRecord& Module : Modules)
		{
			Hash.AddInt32(Module.StablePlacementId);
			Hash.AddInt32(Module.StableModuleId);
			Hash.AddInt32(Module.StableVariantId);
			Hash.AddUInt8(Module.QuarterTurns);
			Hash.AddInt32(Module.AbstractNodeId);
			Hash.AddIntVector(Module.GridOrigin);
		}

		Hash.AddInt32(NodeBindings.Num());
		for (const FZeroEscapeNodePlacementBinding& Binding : NodeBindings)
		{
			Hash.AddInt32(Binding.AbstractNodeId);
			Hash.AddInt32(Binding.StablePlacementId);
		}

		Hash.AddInt32(EdgeRoutes.Num());
		for (const FCanonicalEdgeRouteRecord& Route : EdgeRoutes)
		{
			Hash.AddInt32(Route.AbstractEdgeId);
			Hash.AddInt32(Route.FromNodeId);
			Hash.AddInt32(Route.ToNodeId);
			Hash.AddInt32(Route.OrderedStablePlacementIds.Num());
			for (const int32 StablePlacementId : Route.OrderedStablePlacementIds)
			{
				Hash.AddInt32(StablePlacementId);
			}
		}

		Hash.AddInt32(PortalConnections.Num());
		for (const FCanonicalPortalRecord& Portal : PortalConnections)
		{
			Hash.AddInt32(Portal.StableConnectionId);
			Hash.AddInt32(Portal.AbstractEdgeId);
			Hash.AddInt32(Portal.StablePlacementAId);
			Hash.AddInt32(Portal.StableSocketAId);
			Hash.AddInt32(Portal.StablePlacementBId);
			Hash.AddInt32(Portal.StableSocketBId);
		}

		Hash.AddInt32(ClosedPortals.Num());
		for (const FZeroEscapeClosedPortal& Portal : ClosedPortals)
		{
			Hash.AddInt32(Portal.StablePlacementId);
			Hash.AddInt32(Portal.StableSocketId);
			Hash.AddInt32(Portal.StableClosurePlacementId);
		}

		Hash.AddInt32(GameplayAnchors.Num());
		for (const FCanonicalAnchorRecord& Anchor : GameplayAnchors)
		{
			Hash.AddInt32(Anchor.StableAnchorInstanceId);
			Hash.AddUInt8(static_cast<uint8>(Anchor.Type));
			Hash.AddInt32(Anchor.StablePlacementId);
			Hash.AddInt32(Anchor.StableModuleAnchorId);
		}

		Hash.AddInt32(ObjectiveBindings.Num());
		for (const FZeroEscapeObjectiveBinding& Binding : ObjectiveBindings)
		{
			Hash.AddInt32(Binding.StableObjectiveId);
			Hash.AddInt32(Binding.AbstractNodeId);
			Hash.AddInt32(Binding.StablePlacementId);
			Hash.AddInt32(Binding.StableAnchorInstanceId);
		}
		return Hash.Finalize();
	}
}

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		/**
		 * 判断当前 Objective Mask 是否满足首版三种 CompletionRule。
		 * Mask 的 bit 位由 StableObjectiveId 规范排序后的位置决定，不依赖 Objectives 原始数组顺序。
		 */
		bool IsFlowSatisfied(
			const EZeroEscapeCompletionRule Rule,
			const FObjectiveMask Mask,
			const int32 RequiredCount,
			const int32 CandidateCount)
		{
			const int32 CollectedCount = FPlatformMath::CountBits(Mask);
			switch (Rule)
			{
			case EZeroEscapeCompletionRule::EscapeOnly:
				return true;
			case EZeroEscapeCompletionRule::CollectAll:
				return CollectedCount == CandidateCount;
			case EZeroEscapeCompletionRule::CollectKOfN:
				return CollectedCount >= RequiredCount;
			default:
				return false;
			}
		}

		/**
		 * 在 Dense Graph 上执行有界状态 BFS，并返回满足 Flow 后抵达 Exit 的最短边数。
		 *
		 * 搜索分两层：先用普通节点 BFS 证明 Exit 和每个候选目标都可达；
		 * 再在 (节点, 已收集 Mask) 的乘积状态空间中搜索“满足 K 后到达 Exit”。
		 * 第一层是故意的强约束：CollectKOfN 不能用“只要任意 K 个可达”掩盖一个已断连的候选房。
		 * MaxSearchStates 只限制去重后的乘积状态数；另有 FCoreWorkBudget 统一限制枚举与邻接展开。
		 */
		bool FindShortestProgressionRoute(
			const FAbstractLevelPlan& Plan,
			const int32 MaxSearchStates,
			int32& OutShortestDistanceEdges,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutShortestDistanceEdges = INDEX_NONE;
			if (MaxSearchStates < 1
				|| MaxSearchStates > ZeroEscape::GenerationLimits::FirstPassMaxProgressionSearchStates)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::InvalidConfiguration,
					TEXT("MaxProgressionSearchStates 越过首版硬上限。"),
					INDEX_NONE,
					MaxSearchStates,
					ZeroEscape::GenerationLimits::FirstPassMaxProgressionSearchStates);
				return false;
			}
			if (Plan.Objectives.Num() > ZeroEscape::GenerationLimits::MaxObjectiveCandidates)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::ObjectiveLimitExceeded,
					TEXT("Objective 数超过 uint32 Mask 的首版具名上限。"),
					INDEX_NONE,
					Plan.Objectives.Num(),
					ZeroEscape::GenerationLimits::MaxObjectiveCandidates);
				return false;
			}
			if (!IsValidCompletionRule(Plan.CompletionRule)
				|| Plan.RequiredObjectiveCount < 0
				|| Plan.RequiredObjectiveCount > Plan.Objectives.Num()
				|| (Plan.CompletionRule == EZeroEscapeCompletionRule::CollectAll
					&& Plan.RequiredObjectiveCount != Plan.Objectives.Num())
				|| (Plan.CompletionRule != EZeroEscapeCompletionRule::EscapeOnly
					&& Plan.RequiredObjectiveCount < 1)
				|| (Plan.CompletionRule == EZeroEscapeCompletionRule::EscapeOnly
					&& (!Plan.Objectives.IsEmpty() || Plan.RequiredObjectiveCount != 0)))
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::InvalidKOfN,
					TEXT("Abstract Plan 的 CompletionRule 或 K/N 非法。"),
					INDEX_NONE,
					Plan.RequiredObjectiveCount,
					Plan.Objectives.Num());
				return false;
			}

			FDenseGraph Graph;
			if (!BuildDenseGraph(
					Plan,
					Graph,
					Work,
					OutReport,
					EZeroEscapeGenerationStage::Progression))
			{
				return false;
			}

			const int32* StartDenseIndex = Graph.DenseIndexByStableId.Find(Plan.StartStableNodeId);
			const int32* ExitDenseIndex = Graph.DenseIndexByStableId.Find(Plan.ExitStableNodeId);
			if (StartDenseIndex == nullptr || ExitDenseIndex == nullptr || *StartDenseIndex == *ExitDenseIndex)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("Start/Exit Stable Id 缺失、相同或未映射到 Dense Index。"));
				return false;
			}

			// 先按 StableObjectiveId 构造规范顺序，再分配 bit；这样同一 Plan 的 Mask 语义不受输入容器顺序影响。
			TArray<int32> ObjectiveArrayIndices;
			ObjectiveArrayIndices.Reserve(Plan.Objectives.Num());
			for (int32 ObjectiveIndex = 0; ObjectiveIndex < Plan.Objectives.Num(); ++ObjectiveIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("Objective Mask 规范化")))
				{
					return false;
				}
				ObjectiveArrayIndices.Add(ObjectiveIndex);
			}
			ObjectiveArrayIndices.Sort(
				[&Plan](const int32 A, const int32 B)
				{
					return Plan.Objectives[A].StableObjectiveId < Plan.Objectives[B].StableObjectiveId;
				});

			// 一个节点首版最多绑定一个 Objective；仍使用 OR 写入，使状态转移形式与今后可能的扩展保持一致。
			TArray<FObjectiveMask> ObjectiveMaskByDenseNode;
			ObjectiveMaskByDenseNode.Init(0, Plan.Nodes.Num());
			TArray<int32> ObjectiveDenseNodes;
			ObjectiveDenseNodes.Reserve(Plan.Objectives.Num());
			TSet<int32> ObjectiveStableIds;
			TSet<int32> ObjectiveNodeIds;
			for (int32 MaskIndex = 0; MaskIndex < ObjectiveArrayIndices.Num(); ++MaskIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("Objective Mask 构建")))
				{
					return false;
				}
				const FObjectivePlacement& Objective = Plan.Objectives[ObjectiveArrayIndices[MaskIndex]];
				const int32* DenseNodeIndex = Graph.DenseIndexByStableId.Find(Objective.StableNodeId);
				if (Objective.StableObjectiveId < 0
					|| DenseNodeIndex == nullptr
					|| ObjectiveStableIds.Contains(Objective.StableObjectiveId)
					|| ObjectiveNodeIds.Contains(Objective.StableNodeId))
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::Progression,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("Objective Stable Id/Node 为空、重复或无法映射。"),
						Objective.StableObjectiveId);
					return false;
				}
				ObjectiveStableIds.Add(Objective.StableObjectiveId);
				ObjectiveNodeIds.Add(Objective.StableNodeId);
				ObjectiveDenseNodes.Add(*DenseNodeIndex);
				ObjectiveMaskByDenseNode[*DenseNodeIndex] |= (FObjectiveMask{ 1 } << MaskIndex);
			}

			// 先验证 Exit 与每个候选目标都从 Start 可达，避免 K-of-N 掩盖坏掉的候选房。
			TBitArray<> Reachable(false, Plan.Nodes.Num());
			TArray<int32> ReachabilityQueue;
			ReachabilityQueue.Reserve(Plan.Nodes.Num());
			Reachable[*StartDenseIndex] = true;
			ReachabilityQueue.Add(*StartDenseIndex);
			for (int32 QueueHead = 0; QueueHead < ReachabilityQueue.Num(); ++QueueHead)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("目标可达性 BFS")))
				{
					return false;
				}
				for (const int32 Neighbor : Graph.AdjacencyByDenseIndex[ReachabilityQueue[QueueHead]])
				{
					if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("目标可达性邻接")))
					{
						return false;
					}
					if (!Reachable[Neighbor])
					{
						Reachable[Neighbor] = true;
						ReachabilityQueue.Add(Neighbor);
					}
				}
			}
			if (!Reachable[*ExitDenseIndex])
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::ProgressionNoSolution,
					TEXT("Exit 无法从 Start 到达。"),
					Plan.ExitStableNodeId);
				return false;
			}
			for (int32 ObjectiveIndex = 0; ObjectiveIndex < ObjectiveDenseNodes.Num(); ++ObjectiveIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("候选目标可达性确认")))
				{
					return false;
				}
				if (!Reachable[ObjectiveDenseNodes[ObjectiveIndex]])
				{
					const FObjectivePlacement& Objective = Plan.Objectives[ObjectiveArrayIndices[ObjectiveIndex]];
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::Progression,
						EZeroEscapeGenerationFailure::ProgressionNoSolution,
						TEXT("至少一个 K-of-N 候选目标不可达。"),
						Objective.StableObjectiveId);
					return false;
				}
			}

			// 进入节点即视为收集其 Objective；因此 Start 节点也使用同一转移规则，虽然当前 Flow 不允许它放目标。
			FProgressionState StartState;
			StartState.DenseNodeIndex = *StartDenseIndex;
			StartState.CollectedMask = ObjectiveMaskByDenseNode[*StartDenseIndex];
			TArray<FProgressionState> Open;
			Open.Reserve(FMath::Min(MaxSearchStates, 4096));
			TSet<FProgressionState> Visited;
			Visited.Reserve(FMath::Min(MaxSearchStates, 4096));
			Open.Add(StartState);
			Visited.Add(StartState);

			// Open 是 append-only 队列；邻居已按 StableNodeId 排序，使“第一个最短解”的发现顺序也可复现。
			for (int32 QueueHead = 0; QueueHead < Open.Num(); ++QueueHead)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("K-of-N 状态 BFS")))
				{
					return false;
				}
				const FProgressionState State = Open[QueueHead];
				if (State.DenseNodeIndex == *ExitDenseIndex
					&& IsFlowSatisfied(
						Plan.CompletionRule,
						State.CollectedMask,
						Plan.RequiredObjectiveCount,
						Plan.Objectives.Num()))
				{
					OutShortestDistanceEdges = State.DistanceEdges;
					return true;
				}

				for (const int32 Neighbor : Graph.AdjacencyByDenseIndex[State.DenseNodeIndex])
				{
					if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("K-of-N 状态扩展")))
					{
						return false;
					}
					FProgressionState Next;
					Next.DenseNodeIndex = Neighbor;
					Next.CollectedMask = State.CollectedMask | ObjectiveMaskByDenseNode[Neighbor];
					Next.DistanceEdges = State.DistanceEdges + 1;
					if (Visited.Contains(Next))
					{
						continue;
					}
					if (Visited.Num() >= MaxSearchStates)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::Progression,
							EZeroEscapeGenerationFailure::SearchBudgetExceeded,
							TEXT("K-of-N BFS 需要发现超过 MaxProgressionSearchStates 的状态。"),
							INDEX_NONE,
							Visited.Num() + 1,
							MaxSearchStates);
						return false;
					}
					Visited.Add(Next);
					Open.Add(Next);
				}
			}

			Fail(
				OutReport,
				EZeroEscapeGenerationStage::Progression,
				EZeroEscapeGenerationFailure::ProgressionNoSolution,
				TEXT("在有界状态空间内不存在满足 Flow 后抵达 Exit 的路线。"));
			return false;
		}

		/** 判断节点是否属于 Critical Path 的 Start/MainPath/Exit 三类。 */
		bool IsCriticalRole(const EZeroEscapeTopologyRole Role)
		{
			return Role == EZeroEscapeTopologyRole::Start
				|| Role == EZeroEscapeTopologyRole::MainPath
				|| Role == EZeroEscapeTopologyRole::Exit;
		}

		/**
		 * 验证构造式抽象图的全部拓扑不变量，并复用同一工作预算执行 Progression。
		 * 验证顺序从便宜的结构检查逐步走向较贵的搜索：
		 * 数量/排序 -> Dense Graph -> Role 字段 -> 边和 Degree -> 整图连通 -> Objective Role -> K-of-N 最短必需路线。
		 * 一旦失败就保留第一个结构化原因，避免后续派生错误覆盖根因。
		 */
		bool ValidateAbstractPlanInternal(
			const FAbstractLevelPlan& Plan,
			const FResolvedGenerationBudget& Budget,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			// 构造器是封闭形式：主干每节点一 Node，ShortLeaf 每个 1 Node/1 Edge，ForwardRejoin 每个 1 Node/2 Edges。
			const int32 ExpectedNodeCount = Budget.CriticalPathNodeCount
				+ Budget.ShortLeafBranchCount
				+ Budget.ForwardRejoinBranchCount;
			const int32 ExpectedEdgeCount = Budget.CriticalPathNodeCount - 1
				+ Budget.ShortLeafBranchCount
				+ (Budget.ForwardRejoinBranchCount * 2);
			if (ExpectedNodeCount < 1
				|| ExpectedNodeCount > MaxAbstractNodeCount
				|| ExpectedEdgeCount < 1
				|| ExpectedEdgeCount > MaxAbstractEdgeCount
				|| Plan.Nodes.Num() != ExpectedNodeCount
				|| Plan.Edges.Num() != ExpectedEdgeCount
				|| Plan.Objectives.Num() != Budget.ObjectiveCandidateCount
				|| Plan.CompletionRule != Budget.CompletionRule
				|| Plan.RequiredObjectiveCount != Budget.RequiredObjectiveCount)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("Abstract Plan 的 Node/Edge/Objective 数量或已解析 Flow 不匹配。"));
				return false;
			}

			for (int32 Index = 1; Index < Plan.Nodes.Num(); ++Index)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("Node Stable 排序验证")))
				{
					return false;
				}
				if (Plan.Nodes[Index - 1].StableNodeId >= Plan.Nodes[Index].StableNodeId)
				{
					Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Nodes 未按唯一 StableNodeId 升序保存。"));
					return false;
				}
			}
			for (int32 Index = 1; Index < Plan.Edges.Num(); ++Index)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("Edge Stable 排序验证")))
				{
					return false;
				}
				if (Plan.Edges[Index - 1].StableEdgeId >= Plan.Edges[Index].StableEdgeId)
				{
					Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Edges 未按唯一 StableEdgeId 升序保存。"));
					return false;
				}
			}
			for (int32 Index = 1; Index < Plan.Objectives.Num(); ++Index)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("Objective Stable 排序验证")))
				{
					return false;
				}
				if (Plan.Objectives[Index - 1].StableObjectiveId >= Plan.Objectives[Index].StableObjectiveId)
				{
					Fail(OutReport, EZeroEscapeGenerationStage::Progression, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Objectives 未按唯一 StableObjectiveId 升序保存。"));
					return false;
				}
			}

			FDenseGraph Graph;
			if (!BuildDenseGraph(
					Plan,
					Graph,
					Work,
					OutReport,
					EZeroEscapeGenerationStage::Topology))
			{
				return false;
			}

			// 这个数组同时验证 Critical Progress 无重复、无缺口，并为后续分支边检查提供 O(1) 定位。
			TArray<int32> CriticalDenseIndexByProgress;
			CriticalDenseIndexByProgress.Init(INDEX_NONE, Budget.CriticalPathNodeCount);
			int32 StartCount = 0;
			int32 ExitCount = 0;
			int32 ShortLeafCount = 0;
			int32 ForwardRejoinCount = 0;
			for (int32 DenseIndex = 0; DenseIndex < Graph.PlanNodeIndexByDenseIndex.Num(); ++DenseIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("Node Role 验证")))
				{
					return false;
				}
				const FSpatialNode& Node = Plan.Nodes[Graph.PlanNodeIndexByDenseIndex[DenseIndex]];
				if (!IsValidTopologyRole(Node.Role))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Node 包含未知 TopologyRole。"), Node.StableNodeId);
					return false;
				}

				if (IsCriticalRole(Node.Role))
				{
					if (Node.ProgressIndex < 0
						|| Node.ProgressIndex >= Budget.CriticalPathNodeCount
						|| Node.AnchorProgressIndex != INDEX_NONE
						|| Node.RejoinProgressIndex != INDEX_NONE
						|| CriticalDenseIndexByProgress[Node.ProgressIndex] != INDEX_NONE)
					{
						Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Critical Path Node 的 Progress/Anchor/Rejoin 字段非法。"), Node.StableNodeId);
						return false;
					}
					if ((Node.ProgressIndex == 0) != (Node.Role == EZeroEscapeTopologyRole::Start)
						|| (Node.ProgressIndex == Budget.CriticalPathNodeCount - 1) != (Node.Role == EZeroEscapeTopologyRole::Exit)
						|| (Node.ProgressIndex > 0
							&& Node.ProgressIndex < Budget.CriticalPathNodeCount - 1
							&& Node.Role != EZeroEscapeTopologyRole::MainPath))
					{
						Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Start/MainPath/Exit 与 ProgressIndex 的规范关系非法。"), Node.StableNodeId);
						return false;
					}
					CriticalDenseIndexByProgress[Node.ProgressIndex] = DenseIndex;
					StartCount += Node.Role == EZeroEscapeTopologyRole::Start ? 1 : 0;
					ExitCount += Node.Role == EZeroEscapeTopologyRole::Exit ? 1 : 0;
				}
				else if (Node.Role == EZeroEscapeTopologyRole::ShortLeaf)
				{
					++ShortLeafCount;
					if (Node.AnchorProgressIndex < 1
						|| Node.AnchorProgressIndex >= Budget.CriticalPathNodeCount - 1
						|| Node.ProgressIndex != Node.AnchorProgressIndex
						|| Node.RejoinProgressIndex != INDEX_NONE)
					{
						Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("ShortLeaf 的 Progress/Anchor/Rejoin 字段非法。"), Node.StableNodeId);
						return false;
					}
				}
				else
				{
					++ForwardRejoinCount;
					if (Node.AnchorProgressIndex < 1
						|| Node.RejoinProgressIndex != Node.AnchorProgressIndex + 1
						|| Node.RejoinProgressIndex >= Budget.CriticalPathNodeCount - 1
						|| Node.ProgressIndex != Node.AnchorProgressIndex)
					{
						Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("ForwardRejoin 必须连接相邻内部进度并向前汇合。"), Node.StableNodeId);
						return false;
					}
				}
			}

			if (StartCount != 1
				|| ExitCount != 1
				|| ShortLeafCount != Budget.ShortLeafBranchCount
				|| ForwardRejoinCount != Budget.ForwardRejoinBranchCount)
			{
				Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Start/Exit/ShortLeaf/ForwardRejoin 角色计数不匹配。"));
				return false;
			}
			for (int32 ProgressIndex = 0; ProgressIndex < CriticalDenseIndexByProgress.Num(); ++ProgressIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("Critical Progress 完整性验证")))
				{
					return false;
				}
				if (CriticalDenseIndexByProgress[ProgressIndex] == INDEX_NONE)
				{
					Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Critical Path ProgressIndex 不连续。"), ProgressIndex);
					return false;
				}
			}

			const int32 StartDenseIndex = CriticalDenseIndexByProgress[0];
			const int32 ExitDenseIndex = CriticalDenseIndexByProgress.Last();
			const FSpatialNode& StartNode = Plan.Nodes[Graph.PlanNodeIndexByDenseIndex[StartDenseIndex]];
			const FSpatialNode& ExitNode = Plan.Nodes[Graph.PlanNodeIndexByDenseIndex[ExitDenseIndex]];
			if (Plan.StartStableNodeId != StartNode.StableNodeId
				|| Plan.ExitStableNodeId != ExitNode.StableNodeId)
			{
				Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Plan 的 Start/Exit Stable Id 与规范角色不一致。"));
				return false;
			}

			// 每条边必须是关键路线相邻边，或某个分支与其声明的 Anchor/Rejoin 连接。
			for (const FSpatialEdge& Edge : Plan.Edges)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("抽象边角色验证")))
				{
					return false;
				}
				const int32* DenseA = Graph.DenseIndexByStableId.Find(Edge.StableNodeA);
				const int32* DenseB = Graph.DenseIndexByStableId.Find(Edge.StableNodeB);
				check(DenseA != nullptr && DenseB != nullptr);
				const FSpatialNode& A = Plan.Nodes[Graph.PlanNodeIndexByDenseIndex[*DenseA]];
				const FSpatialNode& B = Plan.Nodes[Graph.PlanNodeIndexByDenseIndex[*DenseB]];
				bool bAllowedEdge = false;
				if (IsCriticalRole(A.Role) && IsCriticalRole(B.Role))
				{
					bAllowedEdge = FMath::Abs(A.ProgressIndex - B.ProgressIndex) == 1;
				}
				else if (IsCriticalRole(A.Role) != IsCriticalRole(B.Role))
				{
					const FSpatialNode& Critical = IsCriticalRole(A.Role) ? A : B;
					const FSpatialNode& Branch = IsCriticalRole(A.Role) ? B : A;
					bAllowedEdge = Critical.ProgressIndex == Branch.AnchorProgressIndex
						|| (Branch.Role == EZeroEscapeTopologyRole::ForwardRejoin
							&& Critical.ProgressIndex == Branch.RejoinProgressIndex);
				}
				if (!bAllowedEdge)
				{
					Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("抽象边不符合 Critical/ShortLeaf/ForwardRejoin 构造契约。"), Edge.StableEdgeId);
					return false;
				}
			}

			for (int32 DenseIndex = 0; DenseIndex < Graph.PlanNodeIndexByDenseIndex.Num(); ++DenseIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("分支 Degree 验证")))
				{
					return false;
				}
				const FSpatialNode& Node = Plan.Nodes[Graph.PlanNodeIndexByDenseIndex[DenseIndex]];
				if (Node.Role == EZeroEscapeTopologyRole::ShortLeaf)
				{
					const int32 RequiredNeighbor = CriticalDenseIndexByProgress[Node.AnchorProgressIndex];
					if (Graph.AdjacencyByDenseIndex[DenseIndex].Num() != 1
						|| Graph.AdjacencyByDenseIndex[DenseIndex][0] != RequiredNeighbor
						|| Budget.MaxLeafOneWayEdgeCount < 1)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::GlobalValidation,
							EZeroEscapeGenerationFailure::LongRetraceLimitExceeded,
							TEXT("ShortLeaf 必须是一条边的短死路且不得越过共同折返上限。"),
							Node.StableNodeId,
							1,
							Budget.MaxLeafOneWayEdgeCount);
						return false;
					}
				}
				else if (Node.Role == EZeroEscapeTopologyRole::ForwardRejoin)
				{
					const int32 AnchorDense = CriticalDenseIndexByProgress[Node.AnchorProgressIndex];
					const int32 RejoinDense = CriticalDenseIndexByProgress[Node.RejoinProgressIndex];
					const TArray<int32>& Neighbors = Graph.AdjacencyByDenseIndex[DenseIndex];
					if (Neighbors.Num() != 2 || !Neighbors.Contains(AnchorDense) || !Neighbors.Contains(RejoinDense))
					{
						Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("ForwardRejoin 必须同时连接声明的 Anchor 与更晚 Rejoin。"), Node.StableNodeId);
						return false;
					}
				}
			}

			// 图整体连通；队列最多容纳 MaxAbstractNodeCount 个 Dense Index。
			TBitArray<> Reachable(false, Plan.Nodes.Num());
			TArray<int32> Queue;
			Queue.Reserve(Plan.Nodes.Num());
			Reachable[StartDenseIndex] = true;
			Queue.Add(StartDenseIndex);
			for (int32 QueueHead = 0; QueueHead < Queue.Num(); ++QueueHead)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("抽象图连通性 BFS")))
				{
					return false;
				}
				for (const int32 Neighbor : Graph.AdjacencyByDenseIndex[Queue[QueueHead]])
				{
					if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("抽象图连通性邻接")))
					{
						return false;
					}
					if (!Reachable[Neighbor])
					{
						Reachable[Neighbor] = true;
						Queue.Add(Neighbor);
					}
				}
			}
			if (Queue.Num() != Plan.Nodes.Num())
			{
				Fail(OutReport, EZeroEscapeGenerationStage::Topology, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("抽象图包含不可达节点。"), INDEX_NONE, Queue.Num(), Plan.Nodes.Num());
				return false;
			}

			TSet<int32> ObjectiveNodeIds;
			for (const FObjectivePlacement& Objective : Plan.Objectives)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("Objective Role 验证")))
				{
					return false;
				}
				const int32* DenseNode = Graph.DenseIndexByStableId.Find(Objective.StableNodeId);
				if (DenseNode == nullptr || ObjectiveNodeIds.Contains(Objective.StableNodeId))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::Progression, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Objective 绑定缺失或多个 Objective 绑定同一节点。"), Objective.StableObjectiveId);
					return false;
				}
				const FSpatialNode& Node = Plan.Nodes[Graph.PlanNodeIndexByDenseIndex[*DenseNode]];
				if (!Budget.AllowedObjectiveRoles.Contains(Node.Role))
				{
					Fail(OutReport, EZeroEscapeGenerationStage::Progression, EZeroEscapeGenerationFailure::InvalidGraph, TEXT("Objective 绑定到了 Flow 未允许的 TopologyRole。"), Objective.StableObjectiveId);
					return false;
				}
				ObjectiveNodeIds.Add(Objective.StableNodeId);
			}

			// “必需路线”是满足当前 Flow 后到达 Exit 的最短路，不是收集全地图所有奖励的路线。
			int32 ShortestRequiredRouteEdges = INDEX_NONE;
			if (!FindShortestProgressionRoute(
					Plan,
					Budget.MaxProgressionSearchStates,
					ShortestRequiredRouteEdges,
					Work,
					OutReport))
			{
				return false;
			}
			// 用必需路线减去直走主干的基线，得到玩家为通关额外付出的边数，并对所有难度应用同一上限。
			const int32 BaselineCriticalPathEdges = Budget.CriticalPathNodeCount - 1;
			const int32 RequiredRouteExtraEdges = ShortestRequiredRouteEdges - BaselineCriticalPathEdges;
			if (RequiredRouteExtraEdges < 0
				|| RequiredRouteExtraEdges > Budget.MaxRequiredRouteExtraEdgeCount)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::GlobalValidation,
					EZeroEscapeGenerationFailure::RequiredRouteTooLong,
					TEXT("完成本局必需目标的最短抽象路线超过共享额外边上限。"),
					INDEX_NONE,
					RequiredRouteExtraEdges,
					Budget.MaxRequiredRouteExtraEdgeCount);
				return false;
			}

			return true;
		}
	}

	bool FGenerationCore::BuildAbstractPlan(
		const FZeroEscapeGenerationRequest& Request,
		const FGenerationProfileSnapshot& Profile,
		FAbstractLevelPlan& OutPlan,
		FZeroEscapeGenerationReport& OutReport)
	{
		// 失败原子性的外层边界：OutPlan 只在候选通过全部验证后赋值。
		OutPlan = {};
		OutReport = {};

		FResolvedGenerationBudget Budget;
		if (!ResolveGenerationBudget(Request, Profile, Budget, OutReport))
		{
			return false;
		}

		// 主干、分支、目标两次尝试与验证共享同一确定性预算；回退不会偷偷获得额外无上限工作量。
		FCoreWorkBudget Work(Budget.SolverBudgets.MaxTotalWorkUnits);
		// CandidatePlan 是本调用唯一可变工作区；任何失败都由栈展开自然丢弃。
		FAbstractLevelPlan CandidatePlan;
		if (!BuildCriticalPath(Budget, CandidatePlan, Work, OutReport)
			|| !AddShortLeafBranches(
				Budget,
				MakeRandomStream(Request.Seed, GAlgorithmVersion, ERandomDomain::Topology, 0),
				CandidatePlan,
				Work,
				OutReport)
			|| !AddForwardRejoinBranches(
				Budget,
				MakeRandomStream(Request.Seed, GAlgorithmVersion, ERandomDomain::Topology, 1),
				CandidatePlan,
				Work,
				OutReport))
		{
			return false;
		}

		// 分支是按阶段追加的；进入 Objective 和验证前先恢复对外契约要求的 Stable Id 顺序。
		CandidatePlan.Nodes.Sort(
			[](const FSpatialNode& A, const FSpatialNode& B)
			{
				return A.StableNodeId < B.StableNodeId;
			});
		CandidatePlan.Edges.Sort(
			[](const FSpatialEdge& A, const FSpatialEdge& B)
			{
				return A.StableEdgeId < B.StableEdgeId;
			});
		// 保留首次按 Seed 分布的结果；只在它违反共享折返上限时，
		// 再尝试一次稳定的最小折返候选。EscapeOnly 始终只走第一次。
		// 回退只重绑 Objectives，不重建拓扑；这保证“为减少必需回头路”不会顺便改变房间结构。
		constexpr int32 MaxObjectiveBindingAttempts = 2;
		const int32 ObjectiveBindingAttemptCount =
			Budget.ObjectiveCandidateCount > 0 ? MaxObjectiveBindingAttempts : 1;
		for (int32 ObjectiveAttemptIndex = 0;
			ObjectiveAttemptIndex < ObjectiveBindingAttemptCount;
			++ObjectiveAttemptIndex)
		{
			// Attempt 0 使用 Seed 子流；Attempt 1 虽然仍获得独立子流，当前最小折返策略不消费它，以保持稳定选择。
			const bool bPreferMinimumDetour = ObjectiveAttemptIndex > 0;
			if (!BindObjectives(
					Budget,
					MakeRandomStream(
						Request.Seed,
						GAlgorithmVersion,
						ERandomDomain::ObjectivePlacement,
						ObjectiveAttemptIndex),
					bPreferMinimumDetour,
					CandidatePlan,
					Work,
					OutReport))
			{
				return false;
			}
			CandidatePlan.Objectives.Sort(
				[](const FObjectivePlacement& A, const FObjectivePlacement& B)
				{
					return A.StableObjectiveId < B.StableObjectiveId;
				});

			if (ValidateAbstractPlanInternal(CandidatePlan, Budget, Work, OutReport))
			{
				OutPlan = MoveTemp(CandidatePlan);
				Succeed(OutReport, EZeroEscapeGenerationStage::GlobalValidation, TEXT("抽象空间图与 Progression 已通过全部纯值验证。"));
				return true;
			}

			// 只对产品规则允许修复的“必需路线过长”回退；图错误、无解、超预算等均保留原失败并立即返回。
			const bool bCanRetryObjectiveBinding =
				OutReport.Failure == EZeroEscapeGenerationFailure::RequiredRouteTooLong
				&& ObjectiveAttemptIndex + 1 < ObjectiveBindingAttemptCount;
			if (!bCanRetryObjectiveBinding)
			{
				return false;
			}
		}

		Fail(
			OutReport,
			EZeroEscapeGenerationStage::Progression,
			EZeroEscapeGenerationFailure::SolverInvariantViolation,
			TEXT("Objective 候选尝试退出时没有成功或结构化失败。"));
		return false;
	}

	bool FGenerationCore::ValidateAbstractPlan(
		const FAbstractLevelPlan& Plan,
		const FResolvedGenerationBudget& Budget,
		FZeroEscapeGenerationReport& OutReport)
	{
		// 公开审查入口不信任外部 Budget，先检查其工作上限再分配验证器状态。
		OutReport = {};
		if (Budget.SolverBudgets.MaxTotalWorkUnits < 1
			|| Budget.SolverBudgets.MaxTotalWorkUnits > ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits)
		{
			Fail(OutReport, EZeroEscapeGenerationStage::Configuration, EZeroEscapeGenerationFailure::InvalidConfiguration, TEXT("ValidateAbstractPlan 收到非法 MaxTotalWorkUnits。"));
			return false;
		}

		FCoreWorkBudget Work(Budget.SolverBudgets.MaxTotalWorkUnits);
		if (!ValidateAbstractPlanInternal(Plan, Budget, Work, OutReport))
		{
			return false;
		}
		Succeed(OutReport, EZeroEscapeGenerationStage::GlobalValidation, TEXT("Abstract Plan 验证通过。"));
		return true;
	}

	bool FGenerationCore::ValidateProgression(
		const FAbstractLevelPlan& Plan,
		const int32 MaxSearchStates,
		FZeroEscapeGenerationReport& OutReport)
	{
		// 该入口只回答可解性与最短合法边数，不验证特定 ShortLeaf/ForwardRejoin 构造形状。
		OutReport = {};
		FCoreWorkBudget Work(ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits);
		int32 ShortestDistanceEdges = INDEX_NONE;
		if (!FindShortestProgressionRoute(
				Plan,
				MaxSearchStates,
				ShortestDistanceEdges,
				Work,
				OutReport))
		{
			return false;
		}
		Succeed(
			OutReport,
			EZeroEscapeGenerationStage::Progression,
			FString::Printf(TEXT("Progression 可解，最短合法路线为 %d 条边。"), ShortestDistanceEdges));
		return true;
	}
}

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		/**
		 * 创建固定长度关键路线；Start/Exit 与内部 MainPath 的 Stable Id 按进度递增。
		 * 此骨架定义“不做任何可选探索时的基线通关距离”，后续共享回头路上限将以它为参照。
		 * 调用方传入的是局部 CandidatePlan；即使本 helper 中途失败，公开 OutPlan 仍然不可见。
		 */
		bool BuildCriticalPath(
			const FResolvedGenerationBudget& Budget,
			FAbstractLevelPlan& OutPlan,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (Budget.CriticalPathNodeCount < 4
				|| Budget.CriticalPathNodeCount > ZeroEscape::GenerationLimits::FirstPassMaxCriticalPathNodes)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					EZeroEscapeGenerationFailure::TopologyCapacityInsufficient,
					TEXT("CriticalPathNodeCount 越过首版抽象图上限。"),
					INDEX_NONE,
					Budget.CriticalPathNodeCount,
					ZeroEscape::GenerationLimits::FirstPassMaxCriticalPathNodes);
				return false;
			}

			OutPlan.Nodes.Reserve(
				Budget.CriticalPathNodeCount
				+ Budget.ShortLeafBranchCount
				+ Budget.ForwardRejoinBranchCount);
			OutPlan.Edges.Reserve(
				Budget.CriticalPathNodeCount - 1
				+ Budget.ShortLeafBranchCount
				+ (Budget.ForwardRejoinBranchCount * 2));

			for (int32 ProgressIndex = 0; ProgressIndex < Budget.CriticalPathNodeCount; ++ProgressIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("Critical Path 节点构建")))
				{
					return false;
				}

				FSpatialNode Node;
				Node.StableNodeId = ProgressIndex;
				Node.ProgressIndex = ProgressIndex;
				if (ProgressIndex == 0)
				{
					Node.Role = EZeroEscapeTopologyRole::Start;
				}
				else if (ProgressIndex == Budget.CriticalPathNodeCount - 1)
				{
					Node.Role = EZeroEscapeTopologyRole::Exit;
				}
				else
				{
					Node.Role = EZeroEscapeTopologyRole::MainPath;
				}
				OutPlan.Nodes.Add(Node);
			}

			OutPlan.StartStableNodeId = 0;
			OutPlan.ExitStableNodeId = Budget.CriticalPathNodeCount - 1;
			for (int32 ProgressIndex = 0; ProgressIndex < Budget.CriticalPathNodeCount - 1; ++ProgressIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("Critical Path 边构建")))
				{
					return false;
				}

				FSpatialEdge Edge;
				Edge.StableEdgeId = OutPlan.Edges.Num();
				Edge.StableNodeA = ProgressIndex;
				Edge.StableNodeB = ProgressIndex + 1;
				OutPlan.Edges.Add(Edge);
			}
			return true;
		}

		/** 查找关键路线 ProgressIndex 对应的 Stable Id；只遍历受 64 节点硬上限保护的前缀。 */
		bool FindCriticalNodeStableId(
			const FAbstractLevelPlan& Plan,
			const int32 ProgressIndex,
			int32& OutStableNodeId,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutStableNodeId = INDEX_NONE;
			for (const FSpatialNode& Node : Plan.Nodes)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("关键路线节点查询")))
				{
					return false;
				}
				const bool bCriticalRole = Node.Role == EZeroEscapeTopologyRole::Start
					|| Node.Role == EZeroEscapeTopologyRole::MainPath
					|| Node.Role == EZeroEscapeTopologyRole::Exit;
				if (bCriticalRole && Node.ProgressIndex == ProgressIndex)
				{
					if (OutStableNodeId != INDEX_NONE)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::Topology,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("构造中的关键路线出现重复 ProgressIndex。"));
						return false;
					}
					OutStableNodeId = Node.StableNodeId;
				}
			}
			if (OutStableNodeId == INDEX_NONE)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					EZeroEscapeGenerationFailure::SolverInvariantViolation,
					TEXT("构造分支时找不到关键路线 Anchor。"),
					ProgressIndex);
				return false;
			}
			return true;
		}

		/**
		 * 每个 ShortLeaf 首版只创建一个房间节点和一条边，严格满足所有难度的共同折返上限。
		 * Anchor 从 Start/Exit 之间的内部进度点无放回抽取，所以同一进度最多一个 ShortLeaf。
		 * 抽取后重新按 ProgressIndex 排序，使 Stable Id 分配不受 Fisher-Yates 遍历顺序影响。
		 */
		bool AddShortLeafBranches(
			const FResolvedGenerationBudget& Budget,
			FRandomStream Random,
			FAbstractLevelPlan& InOutPlan,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (Budget.ShortLeafBranchCount == 0)
			{
				return true;
			}
			if (Budget.MaxLeafOneWayEdgeCount < 1)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					EZeroEscapeGenerationFailure::LongRetraceLimitExceeded,
					TEXT("Profile 要求 ShortLeaf，但共同 MaxLeafOneWayEdgeCount 小于 1。"),
					INDEX_NONE,
					1,
					Budget.MaxLeafOneWayEdgeCount);
				return false;
			}

			// 先枚举完整候选集，再使用 Topology/Attempt0 子流做无偏洗牌与截断。
			TArray<int32> AnchorProgressIndices;
			AnchorProgressIndices.Reserve(Budget.CriticalPathNodeCount - 2);
			for (int32 ProgressIndex = 1; ProgressIndex < Budget.CriticalPathNodeCount - 1; ++ProgressIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("ShortLeaf Anchor 枚举")))
				{
					return false;
				}
				AnchorProgressIndices.Add(ProgressIndex);
			}
			if (Budget.ShortLeafBranchCount > AnchorProgressIndices.Num())
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					EZeroEscapeGenerationFailure::TopologyCapacityInsufficient,
					TEXT("内部 Critical Path 节点不足以放置互异 ShortLeaf Anchor。"),
					INDEX_NONE,
					AnchorProgressIndices.Num(),
					Budget.ShortLeafBranchCount);
				return false;
			}
			if (!ShuffleIndices(
					AnchorProgressIndices,
					Random,
					Work,
					OutReport,
					EZeroEscapeGenerationStage::Topology))
			{
				return false;
			}
			AnchorProgressIndices.SetNum(Budget.ShortLeafBranchCount);
			AnchorProgressIndices.Sort();

			int32 NextStableNodeId = InOutPlan.Nodes.Num();
			int32 NextStableEdgeId = InOutPlan.Edges.Num();
			for (const int32 AnchorProgressIndex : AnchorProgressIndices)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("ShortLeaf 构建"), 2))
				{
					return false;
				}
				int32 AnchorStableNodeId = INDEX_NONE;
				if (!FindCriticalNodeStableId(
						InOutPlan,
						AnchorProgressIndex,
						AnchorStableNodeId,
						Work,
						OutReport))
				{
					return false;
				}

				FSpatialNode Leaf;
				Leaf.StableNodeId = NextStableNodeId++;
				Leaf.Role = EZeroEscapeTopologyRole::ShortLeaf;
				Leaf.ProgressIndex = AnchorProgressIndex;
				Leaf.AnchorProgressIndex = AnchorProgressIndex;
				InOutPlan.Nodes.Add(Leaf);

				FSpatialEdge Edge;
				Edge.StableEdgeId = NextStableEdgeId++;
				Edge.StableNodeA = AnchorStableNodeId;
				Edge.StableNodeB = Leaf.StableNodeId;
				InOutPlan.Edges.Add(Edge);
			}
			return true;
		}

		/**
		 * 创建不交叉的单节点前向汇合支路；每条支路连接相邻内部关键节点，
		 * 玩家进入后可从更晚进度点离开，不被强制原路返回。
		 * 候选 Anchor 每次跨过两个 ProgressIndex，使两条支路不会共用同一段主干区间；
		 * 这是首版可简单验证的“不交叉”构造约束，不代表未来 Layout 不能实现更长的环。
		 */
		bool AddForwardRejoinBranches(
			const FResolvedGenerationBudget& Budget,
			FRandomStream Random,
			FAbstractLevelPlan& InOutPlan,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			if (Budget.ForwardRejoinBranchCount == 0)
			{
				return true;
			}

			// Attempt1 子流与 ShortLeaf 的 Attempt0 隔离，所以今后 ShortLeaf 内部增加随机抽样不会改变 Rejoin 位置。
			TArray<int32> CandidateAnchorProgressIndices;
			for (int32 AnchorProgressIndex = 1;
				AnchorProgressIndex + 1 < Budget.CriticalPathNodeCount - 1;
				AnchorProgressIndex += 2)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("ForwardRejoin Anchor 枚举")))
				{
					return false;
				}
				CandidateAnchorProgressIndices.Add(AnchorProgressIndex);
			}
			if (Budget.ForwardRejoinBranchCount > CandidateAnchorProgressIndices.Num())
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Topology,
					EZeroEscapeGenerationFailure::TopologyCapacityInsufficient,
					TEXT("Critical Path 没有足够的不交叉 ForwardRejoin 区间。"),
					INDEX_NONE,
					CandidateAnchorProgressIndices.Num(),
					Budget.ForwardRejoinBranchCount);
				return false;
			}
			if (!ShuffleIndices(
					CandidateAnchorProgressIndices,
					Random,
					Work,
					OutReport,
					EZeroEscapeGenerationStage::Topology))
			{
				return false;
			}
			CandidateAnchorProgressIndices.SetNum(Budget.ForwardRejoinBranchCount);
			CandidateAnchorProgressIndices.Sort();

			int32 NextStableNodeId = InOutPlan.Nodes.Num();
			int32 NextStableEdgeId = InOutPlan.Edges.Num();
			for (const int32 AnchorProgressIndex : CandidateAnchorProgressIndices)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Topology, TEXT("ForwardRejoin 构建"), 3))
				{
					return false;
				}
				const int32 RejoinProgressIndex = AnchorProgressIndex + 1;
				int32 AnchorStableNodeId = INDEX_NONE;
				int32 RejoinStableNodeId = INDEX_NONE;
				if (!FindCriticalNodeStableId(
						InOutPlan,
						AnchorProgressIndex,
						AnchorStableNodeId,
						Work,
						OutReport)
					|| !FindCriticalNodeStableId(
						InOutPlan,
						RejoinProgressIndex,
						RejoinStableNodeId,
						Work,
						OutReport))
				{
					return false;
				}

				FSpatialNode RejoinNode;
				RejoinNode.StableNodeId = NextStableNodeId++;
				RejoinNode.Role = EZeroEscapeTopologyRole::ForwardRejoin;
				RejoinNode.ProgressIndex = AnchorProgressIndex;
				RejoinNode.AnchorProgressIndex = AnchorProgressIndex;
				RejoinNode.RejoinProgressIndex = RejoinProgressIndex;
				InOutPlan.Nodes.Add(RejoinNode);

				FSpatialEdge EntryEdge;
				EntryEdge.StableEdgeId = NextStableEdgeId++;
				EntryEdge.StableNodeA = AnchorStableNodeId;
				EntryEdge.StableNodeB = RejoinNode.StableNodeId;
				InOutPlan.Edges.Add(EntryEdge);

				FSpatialEdge ExitEdge;
				ExitEdge.StableEdgeId = NextStableEdgeId++;
				ExitEdge.StableNodeA = RejoinNode.StableNodeId;
				ExitEdge.StableNodeB = RejoinStableNodeId;
				InOutPlan.Edges.Add(ExitEdge);
			}
			return true;
		}

		/**
		 * 首次按进度分桶随机选择；有界回退时改用稳定的最小折返候选。
		 * 分桶保证 N 个候选覆盖关卡的前/中/后进度，而不是 N 次独立随机导致全部挤在同一区域。
		 * 回退不减少 N、不降低 K、不改变允许角色；它只在原候选集中优先 MainPath，其次 ForwardRejoin，最后 ShortLeaf。
		 */
		bool BindObjectives(
			const FResolvedGenerationBudget& Budget,
			FRandomStream Random,
			const bool bPreferMinimumDetour,
			FAbstractLevelPlan& InOutPlan,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport)
		{
			// 每次 Attempt 先清除上一次目标绑定；拓扑部分保持不变，所以回退只替换 ObjectiveIntent。
			InOutPlan.Objectives.Reset();
			InOutPlan.CompletionRule = Budget.CompletionRule;
			InOutPlan.RequiredObjectiveCount = Budget.RequiredObjectiveCount;
			if (Budget.ObjectiveCandidateCount == 0)
			{
				return true;
			}

			TArray<FObjectiveCandidate> Candidates;
			Candidates.Reserve(InOutPlan.Nodes.Num());
			for (int32 NodeIndex = 0; NodeIndex < InOutPlan.Nodes.Num(); ++NodeIndex)
			{
				if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("Objective 候选枚举")))
				{
					return false;
				}
				const FSpatialNode& Node = InOutPlan.Nodes[NodeIndex];
				if (Budget.AllowedObjectiveRoles.Contains(Node.Role))
				{
					Candidates.Add({ NodeIndex, Node.ProgressIndex, Node.Role, Node.StableNodeId });
				}
			}
			// 三级稳定键是进度、角色、StableNodeId；任何随机决策都发生在该规范序列之上。
			Candidates.Sort(
				[](const FObjectiveCandidate& A, const FObjectiveCandidate& B)
				{
					if (A.ProgressIndex != B.ProgressIndex)
					{
						return A.ProgressIndex < B.ProgressIndex;
					}
					if (A.Role != B.Role)
					{
						return static_cast<uint8>(A.Role) < static_cast<uint8>(B.Role);
					}
					return A.StableNodeId < B.StableNodeId;
				});

			if (Candidates.Num() < Budget.ObjectiveCandidateCount)
			{
				Fail(
					OutReport,
					EZeroEscapeGenerationStage::Progression,
					EZeroEscapeGenerationFailure::TopologyCapacityInsufficient,
					TEXT("生成后的拓扑没有足够的 ObjectiveIntent 候选节点。"),
					INDEX_NONE,
					Candidates.Num(),
					Budget.ObjectiveCandidateCount);
				return false;
			}

			TArray<FObjectiveCandidate> SelectedCandidates;
			SelectedCandidates.Reserve(Budget.ObjectiveCandidateCount);
			if (!bPreferMinimumDetour)
			{
				// 整数比例分桶覆盖全候选数组；已预检 Candidates.Num() >= N，因此每桶至少有一个元素。
				for (int32 ObjectiveIndex = 0; ObjectiveIndex < Budget.ObjectiveCandidateCount; ++ObjectiveIndex)
				{
					if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("Objective 进度分桶选择")))
					{
						return false;
					}
					const int32 BucketBegin = static_cast<int32>(
						(static_cast<int64>(ObjectiveIndex) * Candidates.Num())
						/ Budget.ObjectiveCandidateCount);
					const int32 BucketEnd = static_cast<int32>(
						(static_cast<int64>(ObjectiveIndex + 1) * Candidates.Num())
						/ Budget.ObjectiveCandidateCount);
					if (BucketEnd <= BucketBegin)
					{
						Fail(
							OutReport,
							EZeroEscapeGenerationStage::Progression,
							EZeroEscapeGenerationFailure::SolverInvariantViolation,
							TEXT("Objective 进度桶为空，候选容量检查与分桶结果不一致。"));
						return false;
					}

					const int32 PickedIndex = BucketBegin + Random.RandHelper(BucketEnd - BucketBegin);
					SelectedCandidates.Add(Candidates[PickedIndex]);
				}
			}
			else
			{
				// 第二次候选不放宽上限：先选不需折返的主路，再选前向汇合，最后才是短叶房。
				// 同一折返等级内仍按进度均匀取样，避免回退把所有目标挤到起点附近。
				auto GetDetourRank = [](const EZeroEscapeTopologyRole Role)
				{
					switch (Role)
					{
					case EZeroEscapeTopologyRole::MainPath:
						return 0;
					case EZeroEscapeTopologyRole::ForwardRejoin:
						return 1;
					case EZeroEscapeTopologyRole::ShortLeaf:
						return 2;
					default:
						return MAX_int32;
					}
				};

				if (!ConsumeWork(
						Work,
						OutReport,
						EZeroEscapeGenerationStage::Progression,
						TEXT("Objective 最小折返候选排序"),
						Candidates.Num()))
				{
					return false;
				}
				TArray<FObjectiveCandidate> RankedCandidates = Candidates;
				RankedCandidates.Sort(
					[&GetDetourRank](const FObjectiveCandidate& A, const FObjectiveCandidate& B)
					{
						const int32 RankA = GetDetourRank(A.Role);
						const int32 RankB = GetDetourRank(B.Role);
						if (RankA != RankB)
						{
							return RankA < RankB;
						}
						if (A.ProgressIndex != B.ProgressIndex)
						{
							return A.ProgressIndex < B.ProgressIndex;
						}
						if (A.Role != B.Role)
						{
							return static_cast<uint8>(A.Role) < static_cast<uint8>(B.Role);
						}
						return A.StableNodeId < B.StableNodeId;
					});

				// 第 N 优先候选的 Rank 就是截止等级：更优等级全选，截止等级再按进度均匀取足余额。
				const int32 ThresholdRank = GetDetourRank(
					RankedCandidates[Budget.ObjectiveCandidateCount - 1].Role);
				TArray<FObjectiveCandidate> ThresholdCandidates;
				for (const FObjectiveCandidate& Candidate : RankedCandidates)
				{
					const int32 Rank = GetDetourRank(Candidate.Role);
					if (Rank < ThresholdRank)
					{
						SelectedCandidates.Add(Candidate);
					}
					else if (Rank == ThresholdRank)
					{
						ThresholdCandidates.Add(Candidate);
					}
				}

				const int32 RemainingSelectionCount =
					Budget.ObjectiveCandidateCount - SelectedCandidates.Num();
				if (RemainingSelectionCount < 1
					|| ThresholdCandidates.Num() < RemainingSelectionCount)
				{
					Fail(
						OutReport,
						EZeroEscapeGenerationStage::Progression,
						EZeroEscapeGenerationFailure::SolverInvariantViolation,
						TEXT("Objective 最小折返候选集与已验证容量不一致。"));
					return false;
				}

				for (int32 SelectionIndex = 0;
					SelectionIndex < RemainingSelectionCount;
					++SelectionIndex)
				{
					if (!ConsumeWork(Work, OutReport, EZeroEscapeGenerationStage::Progression, TEXT("Objective 最小折返进度取样")))
					{
						return false;
					}
					const int32 BucketBegin = static_cast<int32>(
						(static_cast<int64>(SelectionIndex) * ThresholdCandidates.Num())
						/ RemainingSelectionCount);
					const int32 BucketEnd = static_cast<int32>(
						(static_cast<int64>(SelectionIndex + 1) * ThresholdCandidates.Num())
						/ RemainingSelectionCount);
					const int32 PickedIndex = BucketBegin + ((BucketEnd - BucketBegin - 1) / 2);
					SelectedCandidates.Add(ThresholdCandidates[PickedIndex]);
				}
			}

			// StableObjectiveId 按进度顺序重新分配，不泄露候选排名或回退分支的内部顺序。
			SelectedCandidates.Sort(
				[](const FObjectiveCandidate& A, const FObjectiveCandidate& B)
				{
					if (A.ProgressIndex != B.ProgressIndex)
					{
						return A.ProgressIndex < B.ProgressIndex;
					}
					if (A.Role != B.Role)
					{
						return static_cast<uint8>(A.Role) < static_cast<uint8>(B.Role);
					}
					return A.StableNodeId < B.StableNodeId;
				});
			for (int32 ObjectiveIndex = 0; ObjectiveIndex < SelectedCandidates.Num(); ++ObjectiveIndex)
			{
				FObjectivePlacement Objective;
				Objective.StableObjectiveId = ObjectiveIndex;
				Objective.StableNodeId = SelectedCandidates[ObjectiveIndex].StableNodeId;
				InOutPlan.Objectives.Add(Objective);
			}
			return true;
		}

		/**
		 * 把稀疏 StableNodeId 映射为 Dense Index，并构建按邻居 Stable Id 排序的无向邻接。
		 * 任何 TMap/TSet 只做存在性和 O(1) 查询，绝不作为决策遍历来源。
		 * StableNodeId 可以稀疏且不等于 Plan.Nodes 下标；Dense Index 才是 BitArray、邻接表与 BFS 的合法下标。
		 * 构建期同时拒绝重复 StableEdgeId、自环、重复无向边和非单位 TraversalCost，
		 * 因为上层 Progression BFS 的“首次到达即最短”假设依赖单位边。
		 */
		bool BuildDenseGraph(
			const FAbstractLevelPlan& Plan,
			FDenseGraph& OutGraph,
			FCoreWorkBudget& Work,
			FZeroEscapeGenerationReport& OutReport,
			const EZeroEscapeGenerationStage FailureStage)
		{
			OutGraph = {};
			if (Plan.Nodes.IsEmpty()
				|| Plan.Nodes.Num() > MaxAbstractNodeCount
				|| Plan.Edges.Num() > MaxAbstractEdgeCount)
			{
				Fail(
					OutReport,
					FailureStage,
					EZeroEscapeGenerationFailure::InvalidGraph,
					TEXT("抽象图为空或越过首版 Node/Edge 硬上限。"),
					INDEX_NONE,
					Plan.Nodes.Num(),
					MaxAbstractNodeCount);
				return false;
			}

			// 先用 StableNodeId 排序原数组下标，再按该规范顺序分配 [0, Num) Dense Index。
			OutGraph.PlanNodeIndexByDenseIndex.Reserve(Plan.Nodes.Num());
			for (int32 NodeIndex = 0; NodeIndex < Plan.Nodes.Num(); ++NodeIndex)
			{
				if (!ConsumeWork(Work, OutReport, FailureStage, TEXT("Dense Node Index 构建")))
				{
					return false;
				}
				OutGraph.PlanNodeIndexByDenseIndex.Add(NodeIndex);
			}
			OutGraph.PlanNodeIndexByDenseIndex.Sort(
				[&Plan](const int32 A, const int32 B)
				{
					return Plan.Nodes[A].StableNodeId < Plan.Nodes[B].StableNodeId;
				});

			OutGraph.DenseIndexByStableId.Reserve(Plan.Nodes.Num());
			for (int32 DenseIndex = 0; DenseIndex < OutGraph.PlanNodeIndexByDenseIndex.Num(); ++DenseIndex)
			{
				if (!ConsumeWork(Work, OutReport, FailureStage, TEXT("Stable Node Id 映射")))
				{
					return false;
				}
				const FSpatialNode& Node = Plan.Nodes[OutGraph.PlanNodeIndexByDenseIndex[DenseIndex]];
				if (Node.StableNodeId < 0 || OutGraph.DenseIndexByStableId.Contains(Node.StableNodeId))
				{
					Fail(
						OutReport,
						FailureStage,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("抽象图包含负数或重复 StableNodeId。"),
						Node.StableNodeId);
					return false;
				}
				OutGraph.DenseIndexByStableId.Add(Node.StableNodeId, DenseIndex);
			}

			OutGraph.AdjacencyByDenseIndex.SetNum(Plan.Nodes.Num());
			// TSet 仅用于拒绝重复；最终遍历只使用按 Stable Id 排序的 AdjacencyByDenseIndex。
			TSet<int32> StableEdgeIds;
			TSet<uint64> UndirectedEdgeKeys;
			StableEdgeIds.Reserve(Plan.Edges.Num());
			UndirectedEdgeKeys.Reserve(Plan.Edges.Num());
			for (const FSpatialEdge& Edge : Plan.Edges)
			{
				if (!ConsumeWork(Work, OutReport, FailureStage, TEXT("Dense Adjacency 构建")))
				{
					return false;
				}
				const int32* DenseA = OutGraph.DenseIndexByStableId.Find(Edge.StableNodeA);
				const int32* DenseB = OutGraph.DenseIndexByStableId.Find(Edge.StableNodeB);
				if (Edge.StableEdgeId < 0
					|| StableEdgeIds.Contains(Edge.StableEdgeId)
					|| DenseA == nullptr
					|| DenseB == nullptr
					|| *DenseA == *DenseB
					|| Edge.TraversalCost != 1)
				{
					Fail(
						OutReport,
						FailureStage,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("抽象边的 Stable Id、端点或统一成本非法。"),
						Edge.StableEdgeId);
					return false;
				}

				const uint32 MinDense = static_cast<uint32>(FMath::Min(*DenseA, *DenseB));
				const uint32 MaxDense = static_cast<uint32>(FMath::Max(*DenseA, *DenseB));
				const uint64 EdgeKey = (static_cast<uint64>(MinDense) << 32) | MaxDense;
				if (UndirectedEdgeKeys.Contains(EdgeKey))
				{
					Fail(
						OutReport,
						FailureStage,
						EZeroEscapeGenerationFailure::InvalidGraph,
						TEXT("抽象图包含重复无向边。"),
						Edge.StableEdgeId);
					return false;
				}

				StableEdgeIds.Add(Edge.StableEdgeId);
				UndirectedEdgeKeys.Add(EdgeKey);
				OutGraph.AdjacencyByDenseIndex[*DenseA].Add(*DenseB);
				OutGraph.AdjacencyByDenseIndex[*DenseB].Add(*DenseA);
			}

			// 邻接排序是确定性的最后一环：后续 BFS 可以直接按数组顺序展开，不再做临时比较。
			for (TArray<int32>& Neighbors : OutGraph.AdjacencyByDenseIndex)
			{
				if (!ConsumeWork(Work, OutReport, FailureStage, TEXT("邻接稳定排序"), Neighbors.Num()))
				{
					return false;
				}
				Neighbors.Sort(
					[&Plan, &OutGraph](const int32 A, const int32 B)
					{
						return Plan.Nodes[OutGraph.PlanNodeIndexByDenseIndex[A]].StableNodeId
							< Plan.Nodes[OutGraph.PlanNodeIndexByDenseIndex[B]].StableNodeId;
					});
			}
			return true;
		}
	}
}
