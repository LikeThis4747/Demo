// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationCore.h
 * 职责：声明运行时 PCG 的纯值快照、确定性随机域、抽象空间图、目标意图与抽象验证入口。
 * 边界：只有 Snapshot 构建函数读取 DataAsset；生成、随机、哈希与验证阶段不访问 UObject、World 或素材。
 * 状态 Owner：调用方持有 Snapshot、Plan 与 Report；本文件中的算法入口不保留跨调用状态。
 *
 * 纯值管线为：DataAsset -> Snapshot -> ResolvedBudget -> AbstractPlan -> LayoutSolver。
 * 这里只负责 Layout 之前的“玩家应该走过哪些逻辑位置”，不决定 Mesh、Socket 或世界坐标。
 * 所有公开构建入口都以局部候选对象完成工作；只有全部验证通过才移动到 Out 参数，因此 false 不会泄露可被误用的半成品。
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "PCG/ZeroEscapeGenerationAssets.h"

namespace ZeroEscape::LevelGeneration
{
	/**
	 * 算法复现契约的显式版本。
	 * 修改任何会改变同输入抽象结果、稳定排序、Hash 字段或随机消费顺序的规则时必须递增。
	 * 这使旧 Seed + 旧版本可被识别，而不会假装与新算法结果相同。
	 */
	inline constexpr int32 GAlgorithmVersion = 1;

	/**
	 * 相互隔离的确定性随机领域；显式常量属于算法版本契约，禁止按枚举顺序重新编号。
	 * 新领域只能追加新常量，不能复用既有值。
	 */
	enum class ERandomDomain : uint32
	{
		/** 留给未来的流程规则变体，不与拓扑或目标摆放共用随机流。 */
		Progression = 0x20B8A51Du,
		/** 关键路线之外的 ShortLeaf / ForwardRejoin 拓扑选择。 */
		Topology = 0x7D9C2E13u,
		/** 在已经固定的拓扑上选择 ObjectiveIntent 节点。 */
		ObjectivePlacement = 0xB45F0A67u,
		/** Layout 阶段的特殊 Socket 模块摆放。 */
		SocketLayout = 0x31C846D9u,
		/** Layout 阶段的 Simple-Tiled WFC 观测与加权选择。 */
		WfcLayout = 0x95E27B43u,
		/** 只影响灯光、装饰等表现层，不得影响逻辑图或可解性。 */
		Presentation = 0xE13A5C89u
	};

	/**
	 * Generation Profile 的无 UObject 值快照。
	 * BuildGenerationSnapshot 在游戏线程校验后写入，数组按 Stable Id/枚举值稳定排序。
	 */
	struct FGenerationProfileSnapshot
	{
		int32 ProfileVersion = 0;
		FZeroEscapeSharedRouteConstraints SharedRouteConstraints;
		TArray<FZeroEscapeDifficultyDefinition> Difficulties;
		TArray<FZeroEscapeFlowDefinition> Flows;
		FZeroEscapeSolverBudgets SolverBudgets;
		bool bRequireEffectiveWfcChoice = false;
	};

	/** 单个逻辑模块的纯值快照；不含 Mesh、Actor Class、显示名或第三方资源路径。 */
	struct FModuleSnapshot
	{
		int32 StableModuleId = INDEX_NONE;
		EZeroEscapeLayoutPolicy LayoutPolicy = EZeroEscapeLayoutPolicy::WfcSingleCell;
		TArray<EZeroEscapeTopologyRole> AllowedRoles;
		FIntVector Footprint = FIntVector(1, 1, 1);
		int32 AllowedQuarterTurnsMask = 0xF;
		int32 Weight = 1;
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		TArray<FZeroEscapeModulePortal> Portals;
		TArray<FZeroEscapeModuleAnchor> GameplayAnchors;
	};

	/** Module Catalog 的无 UObject 值快照；Modules 始终按 StableModuleId 升序。 */
	struct FModuleCatalogSnapshot
	{
		int32 CatalogVersion = 0;
		FVector CellSize = FVector::ZeroVector;
		TArray<FModuleSnapshot> Modules;
	};

	/**
	 * 单次 Request 已解析的难度与 Flow 权威值。
	 * 后续 Topology/Layout 只读取此结构，不重新查询 DataAsset，也不自行解释 K/N。
	 *
	 * K/N 不变量：ObjectiveCandidateCount 是 N，RequiredObjectiveCount 是 K。
	 * EscapeOnly 强制 K=N=0；CollectAll 强制 K=N；CollectKOfN 强制 1 <= K <= N。
	 * MaxLeafOneWayEdgeCount 与 MaxRequiredRouteExtraEdgeCount 来自共享约束，所有难度都必须遵守“避免长距离回头路”。
	 */
	struct FResolvedGenerationBudget
	{
		EZeroEscapeDifficulty Difficulty = EZeroEscapeDifficulty::Normal;
		FName StableFlowId = NAME_None;
		int32 FlowVersion = 0;
		EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
		FIntPoint GridExtentCells = FIntPoint::ZeroValue;
		int32 CriticalPathNodeCount = 0;
		int32 ShortLeafBranchCount = 0;
		int32 ForwardRejoinBranchCount = 0;
		int32 ObjectiveCandidateCount = 0;
		int32 RequiredObjectiveCount = 0;
		int32 MaxLeafOneWayEdgeCount = 0;
		int32 MaxRequiredRouteExtraEdgeCount = 0;
		int32 MaxProgressionSearchStates = 0;
		int32 AStarStraightStepCost = 0;
		int32 AStarTurnPenalty = 0;
		TArray<EZeroEscapeTopologyRole> AllowedObjectiveRoles;
		FZeroEscapeSolverBudgets SolverBudgets;
	};

	/**
	 * 抽象空间节点；StableNodeId 仅作身份，任何数组访问都必须先建立 Dense Index。
	 * 关键路线必须形成 ProgressIndex=[0, CriticalPathNodeCount) 的连续序列；
	 * ShortLeaf 只声明 Anchor，ForwardRejoin 同时声明 Anchor 和严格更晚的 Rejoin。
	 */
	struct FSpatialNode
	{
		int32 StableNodeId = INDEX_NONE;
		EZeroEscapeTopologyRole Role = EZeroEscapeTopologyRole::MainPath;
		/** 沿关键路线的进度索引；分支节点使用其 AnchorProgressIndex。 */
		int32 ProgressIndex = INDEX_NONE;
		/** ShortLeaf 的连接点或 ForwardRejoin 的前向入口；关键路线节点为 INDEX_NONE。 */
		int32 AnchorProgressIndex = INDEX_NONE;
		/** ForwardRejoin 的前向汇合点；其他节点为 INDEX_NONE。 */
		int32 RejoinProgressIndex = INDEX_NONE;
	};

	/** 无向抽象边；首版 TraversalCost 固定为 1，几何距离由后续 Layout 二次验证。 */
	struct FSpatialEdge
	{
		int32 StableEdgeId = INDEX_NONE;
		int32 StableNodeA = INDEX_NONE;
		int32 StableNodeB = INDEX_NONE;
		int32 TraversalCost = 1;
	};

	/** 一个候选通关目标意图；不绑定房型、Mesh 或具体 Gameplay Actor。 */
	struct FObjectivePlacement
	{
		int32 StableObjectiveId = INDEX_NONE;
		int32 StableNodeId = INDEX_NONE;
	};

	/**
	 * 纯抽象生成结果；Nodes、Edges、Objectives 分别按自己的 Stable Id 升序。
	 * Start/Exit 保存 Stable Id，不能直接作为 Nodes 下标。
	 * Objectives 只是“哪个抽象节点需要一个目标”的意图，具体 Anchor 和 Gameplay Actor 在后续阶段绑定。
	 */
	struct FAbstractLevelPlan
	{
		EZeroEscapeCompletionRule CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
		int32 RequiredObjectiveCount = 0;
		int32 StartStableNodeId = INDEX_NONE;
		int32 ExitStableNodeId = INDEX_NONE;
		TArray<FSpatialNode> Nodes;
		TArray<FSpatialEdge> Edges;
		TArray<FObjectivePlacement> Objectives;
	};

	/**
	 * 在游戏线程校验并复制 Generation Profile；失败时清空 OutSnapshot 并填写 Configuration 报告。
	 * 输出不含 UObject 指针，可在后续纯值阶段安全传递。
	 * 数组被规范排序，以消除 DataAsset 编辑顺序对结果的影响。
	 */
	bool BuildGenerationSnapshot(
		const UZeroEscapeLevelGenerationProfile& Source,
		FGenerationProfileSnapshot& OutSnapshot,
		FZeroEscapeGenerationReport& OutReport);

	/**
	 * 在游戏线程校验并复制 Module Catalog；失败时清空 OutSnapshot 并填写 Configuration 报告。
	 * 模块及其 Role、Portal、Anchor 都会按稳定键排序。
	 */
	bool BuildCatalogSnapshot(
		const UZeroEscapeModuleCatalog& Source,
		FModuleCatalogSnapshot& OutSnapshot,
		FZeroEscapeGenerationReport& OutReport);

	/**
	 * 只读取纯值快照，把 Request 的 Difficulty/Flow 解析为单局固定预算。
	 * 此处先做分支容量、Objective 角色容量与 K/N 预检，避免 Solver 开始后才发现配置根本无解。
	 * false 时 OutBudget 保持默认空值。
	 */
	bool ResolveGenerationBudget(
		const FZeroEscapeGenerationRequest& Request,
		const FGenerationProfileSnapshot& Profile,
		FResolvedGenerationBudget& OutBudget,
		FZeroEscapeGenerationReport& OutReport);

	/**
	 * 从 Request 与纯值快照构建可复现签名；PresentationVersion 只记录完整运行输入，
	 * 不进入 Abstract/Layout Hash。
	 * 签名用于记录“这局由哪套输入生成”；Hash 用于比较“纯逻辑结果是否一致”，两者职责不同。
	 */
	bool BuildGenerationSignature(
		const FZeroEscapeGenerationRequest& Request,
		const FGenerationProfileSnapshot& GenerationProfile,
		const FModuleCatalogSnapshot& ModuleCatalog,
		int32 PresentationVersion,
		FZeroEscapeGenerationSignature& OutSignature,
		FZeroEscapeGenerationReport& OutReport);

	/** 判断 Transform 的所有分量有限、Rotation 已归一化且 Scale 为 1。 */
	bool IsFiniteUnitScaleTransform(const FTransform& Transform);

	/**
	 * 对规范化的抽象值字段计算稳定 63 位 Hash；不读取对象地址、FName 索引、容器内存或遍历 TMap。
	 * 输入越过首版抽象硬上限时返回 0。
	 */
	int64 ComputeCanonicalAbstractHash(const FAbstractLevelPlan& Plan);

	/**
	 * 对最终布局的稳定逻辑字段计算 63 位 Hash；包含 ID、占格、旋转、Binding 与 Portal 状态，
	 * 排除 FTransform 浮点内存、资源引用和 PresentationVersion。非法超大输入返回 0。
	 */
	int64 ComputeCanonicalLayoutHash(const FZeroEscapeGeneratedLevelPlan& Plan);

	/**
	 * 纯值抽象生成与验证入口。
	 * 函数失败时 OutPlan 保持为空并写入可定位报告；调用方不得使用失败路径中的局部候选结果。
	 */
	class FGenerationCore final
	{
	public:
		/**
		 * 生成 Critical Path、ShortLeaf、ForwardRejoin 与 K-of-N ObjectiveIntent，并完成抽象验证。
		 * 阶段顺序固定为：解析预算 -> 主干 -> 分支 -> 目标绑定 -> 图/K-of-N/回头路验证。
		 * 首次目标按 Seed 和进度分布；只有“必需路线额外边数超限”时才进行一次确定性最小折返回退。
		 */
		static bool BuildAbstractPlan(
			const FZeroEscapeGenerationRequest& Request,
			const FGenerationProfileSnapshot& Profile,
			FAbstractLevelPlan& OutPlan,
			FZeroEscapeGenerationReport& OutReport);

		/**
		 * 验证规范图、分支角色、目标绑定、共享折返上限和 Stable Id/Dense Index 边界。
		 * 这是独立验证入口，既可审查本类生成的 Plan，也可拒绝外部构造的伪合法 Plan。
		 */
		static bool ValidateAbstractPlan(
			const FAbstractLevelPlan& Plan,
			const FResolvedGenerationBudget& Budget,
			FZeroEscapeGenerationReport& OutReport);

		/**
		 * 使用有界 (DenseNodeIndex, ObjectiveMask) BFS 验证 Escape/All/K-of-N 可解。
		 * 同一节点携带不同收集 Mask 是不同状态；Distance 不参与状态身份，BFS 首次到达就是最短合法路线。
		 */
		static bool ValidateProgression(
			const FAbstractLevelPlan& Plan,
			int32 MaxSearchStates,
			FZeroEscapeGenerationReport& OutReport);

		/**
		 * 从 MasterSeed、算法版本、固定随机域和 Attempt 派生互不串扰的确定性子流。
		 * 因此表现层新增一次随机抽样，不会改变拓扑、Objective 或 WFC 结果；
		 * AttemptIndex 只用于同一阶段的有界重试，不得以隐式随机消费代替。
		 */
		static FRandomStream MakeRandomStream(
			int32 MasterSeed,
			int32 AlgorithmVersion,
			ERandomDomain Domain,
			int32 AttemptIndex);
	};
}
