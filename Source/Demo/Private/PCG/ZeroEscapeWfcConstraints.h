// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcConstraints.h
 *
 * 职责：声明运行时 Grid-WFC 使用的 Count、MaxConsecutive 与 Connected 三项
 * 非局部约束，以及求解器和约束层之间共享的最小纯值契约。
 *
 * 边界：约束层只读取当前 Domain，并把需要删除的候选写入 Workspace 的
 * BanMaskByCell；它不直接修改 Domain，不持有 Trail，也不执行回溯。这样所有
 * Domain 变化仍由 FWfcSolver 的统一入口记录，回溯时不会遗漏全局约束造成的修改。
 *
 * 本文件位于 Private/PCG，不向蓝图、资产或其他模块暴露；不建立通用约束接口、
 * 注册表、反射类型或 UObject 层级。
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

namespace ZeroEscape::LevelGeneration
{
	/**
	 * 一个 Cell 的 WFC 候选集合。
	 *
	 * Variant bit 的稳定含义是：bit n 对应 OpeningMask=n，其中 n 的范围为 0..15。
	 * 因此完整候选集正好可以由一个 uint16 表示，OpeningMask=0 对应 Empty。
	 */
	using FWfcDomain = uint16;

	/** 项目软路线草图对单格四条边的非强制开闭偏好。 */
	struct FWfcCellOpeningPreference
	{
		uint8 PreferredOpenMask = 0;
		uint8 PreferredClosedMask = 0;
	};

	/** 当前搜索分支被哪一项非局部约束证明为不可行。 */
	enum class EWfcConstraintContradiction : uint8
	{
		/** 当前稳定点没有发现非局部约束矛盾。 */
		None = 0,
		/** 非空 Cell 的已确定数量超过上限，或最大可能数量低于下限。 */
		Count,
		/** 某一行或列已经被迫出现超过上限的连续轴向贯通 Cell。 */
		MaxConsecutive,
		/** Start 已无法在可能开放图中到达某个被迫非空 Cell。 */
		Connected
	};

	/**
	 * 一次 WFC Solve 调用期间保持不变的纯值设置。
	 *
	 * GridSize 仍由 Solve 的独立参数提供，避免在调用边界维护两份尺寸；这里不保存
	 * UObject、玩法 Actor、房间表现或第三方资产引用。
	 */
	struct FZeroEscapeWfcSolveSettings
	{
		/** Connected 约束进行可能图 BFS 时使用的确定起点。 */
		FIntPoint StartCoordinate = FIntPoint::ZeroValue;

		/** 最终非空 Cell 数量下限；Count 约束会在下限只剩唯一选择时主动 Ban Empty。 */
		int32 MinWalkableCellCount = 1;

		/** 最终非空 Cell 数量上限；Count 约束会在达到上限后主动 Ban 其余 NonEmpty。 */
		int32 MaxWalkableCellCount = 1;

		/**
		 * 单轴连续贯通 Cell 上限。
		 * 水平要求同时有 East/West，垂直要求同时有 North/South；T 与 Cross 也计入。
		 */
		int32 MaxConsecutiveStraightTiles = 1;

		/**
		 * Candidate ordering 的软偏好；0 表示禁用。
		 * 它不得参与约束传播或把任何合法 OpeningMask 的权重降为零。
		 */
		int32 PreferredMaxConsecutiveStraightTiles = 0;

		/** 与稠密 Cell 下标一一对应；空数组表示关闭软路线开口提示。 */
		TArray<FWfcCellOpeningPreference> OpeningPreferencesByCell;

		/** 开口匹配信号的 log2 权重强度；所有合法候选仍保持正权重。 */
		float OpeningPreferenceLog2Strength = 0.0f;

		/**
		 * Solver 可把某个决策 Cell 赋为 singleton 的最大候选尝试次数。
		 * 约束层只参与一次性校验，不自行消费该预算。
		 */
		int32 MaxCandidateAttempts = 1;

		/**
		 * Solver 最多允许恢复决策帧的次数。
		 * 约束层只参与一次性校验，不自行执行或统计回溯。
		 */
		int32 MaxBacktrackCount = 1;
	};

	/**
	 * 一次约束评估失败的结构化诊断。
	 *
	 * 自动化测试应优先断言 Kind；Message 只供日志定位，不作为稳定协议。
	 */
	struct FWfcConstraintFailure
	{
		/** 触发当前分支矛盾的约束类型。 */
		EWfcConstraintContradiction Kind = EWfcConstraintContradiction::None;

		/** 可定位到单格时保存稠密 CellIndex，否则为 INDEX_NONE。 */
		int32 CellIndex = INDEX_NONE;

		/** Count 的已确定/可能数量，或 MaxConsecutive 的实际窗口长度。 */
		int32 ObservedCount = 0;

		/** 与 ObservedCount 对应的配置下限或上限。 */
		int32 Limit = 0;

		/** 仅在失败时构造的人类可读诊断，成功热路径不会格式化字符串。 */
		FString Message;

		/** 清空上一次稳定点留下的失败状态。 */
		void Reset();
	};

	/**
	 * 同一次 Solve 内由三项约束重复使用的临时空间。
	 *
	 * TArray 容量会跨稳定点复用，避免每次 Connected BFS 和 Ban 输出重新申请内存。
	 */
	struct FWfcConstraintWorkspace
	{
		/**
		 * 每个 Cell 需要删除的 Variant bit。
		 * 多项约束命中同一 Cell 时使用按位或合并，最终由 Solver 按稠密顺序应用。
		 */
		TArray<FWfcDomain> BanMaskByCell;

		/**
		 * Connected 的 expanded graph 节点状态。
		 *
		 * 每个 Cell 展开为五个节点：一个中心节点和 N/E/S/W 四个方向节点。
		 * 中心节点表示“该格非空”，方向节点表示“该格在对应方向开口”。这种表示与
		 * DeBroglie EdgedPathView 一致，使割点传播不仅能强制必经 Cell，还能强制
		 * 必经公共边的两侧 OpeningMask。
		 */
		TArray<uint8> ConnectedPotentialNode;
		TArray<uint8> ConnectedRelevantNode;
		TArray<uint8> ConnectedArticulationNode;

		/** expanded graph 的连通分量与 Tarjan low-link 工作数组。 */
		TArray<int32> ConnectedComponentByNode;
		TArray<int32> ConnectedDiscoveryIndex;
		TArray<int32> ConnectedLowLink;
		TArray<int32> ConnectedParent;
		TArray<int32> ConnectedRelevantSubtreeCount;
		TArray<int32> ConnectedRelevantChildSubtreeCount;

		/**
		 * 迭代 Tarjan 的每节点邻接游标。使用显式栈而不是递归，避免最大 1024 Cell
		 * 展开为 5120 节点后依赖平台调用栈深度。
		 */
		TArray<uint8> ConnectedNextNeighborOrdinal;
		TArray<int32> ConnectedQueue;
		TArray<int32> ConnectedDfsStack;

		/** 当前稳定点至少产生一个真实 Ban 的 Cell 数，重复命中同格只计一次。 */
		int32 BanCellCount = 0;

		/** 按给定 Cell 数清空本轮内容，同时保留已经申请的容器容量。 */
		void PrepareForPass(int32 CellCount);

		/** 返回当前稳定点是否至少产生了一个需要 Solver 应用的 Ban。 */
		bool HasBans() const
		{
			return BanCellCount > 0;
		}
	};

	/** Count、MaxConsecutive 与 Connected 三项具体约束的纯值入口。 */
	class FWfcConstraints final
	{
	public:
		/**
		 * 在建立 Domain 和开始随机观察前校验一次 Solve 设置。
		 *
		 * 失败表示调用方配置或契约错误，Solver 应报告为不变量/配置失败，不得通过
		 * 回溯或更换 Seed 掩盖。
		 */
		static bool ValidateSolveSettings(
			FIntPoint GridSize,
			const FZeroEscapeWfcSolveSettings& Settings,
			FString& OutError);

		/**
		 * 在局部 OpeningMask 邻接传播达到稳定点后依次评估
		 * Count -> MaxConsecutive -> Connected。
		 *
		 * 返回 true 表示当前分支尚未被证明无解；Workspace 可能没有 Ban，也可能包含
		 * 一批 Ban。返回 false 表示 OutFailure 所述的可回溯分支矛盾。
		 * 本函数绝不直接修改 Domains。
		 */
		static bool Evaluate(
			FIntPoint GridSize,
			const FZeroEscapeWfcSolveSettings& Settings,
			TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure);
	};
}
