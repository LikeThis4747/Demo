// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcSolver.cpp
 *
 * 职责：实现完整 0..15 OpeningMask 状态集上的有界 Grid-WFC 与 chronological backtracking。
 * 状态 Owner：Domain、Trail、决策栈、传播队列和约束 Workspace 均由单次 Solve 调用栈独占；
 *             失败时稠密 OpeningMask 输出保持为空，成功候选只在完成态验收通过后一次提交。
 */

#include "PCG/ZeroEscapeWfcSolver.h"

#include "PCG/ZeroEscapeGenerationAssets.h"

namespace ZeroEscape::LevelGeneration
{
	namespace WfcSolverPrivate
	{
		/** 16 个 Variant 分别占用 FWfcDomain 的一个 bit。 */
		constexpr int32 CanonicalVariantCount = 16;

		/** 一个 Domain 被缩减前的值；按追加顺序的逆序即可完整恢复当前分支。 */
		struct FDomainChange
		{
			int32 CellIndex = INDEX_NONE;
			FWfcDomain PreviousDomain = 0;
		};

		/**
		 * 一个最小熵决策帧。
		 *
		 * CandidateOrder 在创建帧时按权重无放回生成一次。回溯只推进 NextCandidateIndex，
		 * 不重新消耗 Random，因此同一输入与 Seed 的布局、尝试数和回溯数都可复现。
		 */
		struct FWfcDecision
		{
			int32 CellIndex = INDEX_NONE;
			TArray<uint8, TInlineAllocator<CanonicalVariantCount>> CandidateOrder;
			int32 NextCandidateIndex = 0;
			int32 TrailStart = 0;
		};

		enum class EWfcBranchStatus : uint8
		{
			Stable = 0,
			Contradiction = 1,
			InvariantFailure = 2
		};

		/**
		 * 可恢复矛盾的来源只用于测量和定位搜索爆炸，不参与求解结果或回溯选择。
		 * 完整候选 Reject 已有独立指标，因此不混入下面五类传播矛盾。
		 */
		enum class EWfcBranchContradictionSource : uint8
		{
			None = 0,
			LocalAdjacency,
			Count,
			MaxConsecutive,
			Connected,
			GlobalBan
		};

		/** 局部传播、全局约束或完整候选拒绝的统一分支结果。 */
		struct FWfcBranchResult
		{
			EWfcBranchStatus Status = EWfcBranchStatus::Stable;
			EWfcBranchContradictionSource ContradictionSource =
				EWfcBranchContradictionSource::None;
			int32 RelatedCellIndex = INDEX_NONE;
			FString Message;
		};

		/** 只重置本求解器拥有的工作量指标；Grid/实例化指标不属于这里。 */
		void ResetWfcMetrics(FZeroEscapeGenerationMetrics& Metrics)
		{
			Metrics.WfcObservationCount = 0;
			// FWfcSolver::Solve 对应一棵搜索树；Grid 层做有限重试时会累加该字段。
			Metrics.WfcSolveAttemptCount = 1;
			Metrics.WfcCandidateAttemptCount = 0;
			Metrics.WfcPropagationCount = 0;
			Metrics.WfcContradictionCount = 0;
			Metrics.WfcLocalAdjacencyContradictionCount = 0;
			Metrics.WfcCountContradictionCount = 0;
			Metrics.WfcMaxConsecutiveContradictionCount = 0;
			Metrics.WfcConnectedContradictionCount = 0;
			Metrics.WfcGlobalBanContradictionCount = 0;
			Metrics.WfcBacktrackCount = 0;
			Metrics.WfcCollapsedCandidateRejectionCount = 0;
			Metrics.WfcInvariantFailureCount = 0;
		}

		bool ReportConfigurationFailure(
			const FString& Message,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutReport.Stage = EZeroEscapeGenerationStage::Configuration;
			OutReport.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
			OutReport.RelatedStableId = INDEX_NONE;
			OutReport.ActualValue = 0;
			OutReport.LimitValue = 0;
			OutReport.Message = Message;
			return false;
		}

		bool ReportInvariantFailure(
			const FString& Message,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutReport.Stage = EZeroEscapeGenerationStage::WfcLayout;
			OutReport.Failure = EZeroEscapeGenerationFailure::SolverInvariantViolation;
			OutReport.Message = Message;
			++OutReport.Metrics.WfcInvariantFailureCount;
			return false;
		}

		bool ReportBudgetFailure(
			const FString& Message,
			const int32 ActualValue,
			const int32 LimitValue,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutReport.Stage = EZeroEscapeGenerationStage::WfcLayout;
			OutReport.Failure = EZeroEscapeGenerationFailure::SolverBudgetExhausted;
			OutReport.RelatedStableId = INDEX_NONE;
			OutReport.ActualValue = ActualValue;
			OutReport.LimitValue = LimitValue;
			OutReport.Message = Message;
			return false;
		}

		bool ReportNoValidSolution(
			const FWfcBranchResult& LastContradiction,
			FZeroEscapeGenerationReport& OutReport)
		{
			OutReport.Stage = EZeroEscapeGenerationStage::WfcLayout;
			OutReport.Failure = EZeroEscapeGenerationFailure::NoValidWfcSolution;
			OutReport.RelatedStableId = LastContradiction.RelatedCellIndex;
			OutReport.ActualValue = OutReport.Metrics.WfcContradictionCount;
			OutReport.LimitValue = 0;
			OutReport.Message = LastContradiction.Message.IsEmpty()
				? TEXT("WFC 的全部决策分支均不可满足当前约束。")
				: FString::Printf(
					TEXT("WFC 的全部决策分支均不可满足；最后一次矛盾：%s"),
					*LastContradiction.Message);
			return false;
		}

		/**
		 * 一次完成稠密视图构建与静态输入校验。
		 *
		 * Solve 不会在初始化 Domain 时再次重建坐标映射。该入口验证外部输入；后续 CellIndex 全部
		 * 来自稠密下标、合法邻格或约束 Workspace，因此后续使用 checkf 维护内部不变量。
		 */
		bool BuildAndValidateDenseConstraintView(
			const FIntPoint GridSize,
			const TArray<FGridCellConstraint>& Constraints,
			TArray<const FGridCellConstraint*>& OutByIndex,
			FString& OutError)
		{
			OutByIndex.Reset();
			OutError.Reset();

			const int64 CellCount64 =
				static_cast<int64>(GridSize.X) * static_cast<int64>(GridSize.Y);
			if (GridSize.X <= 0
				|| GridSize.Y <= 0
				|| CellCount64 <= 0
				|| CellCount64 > MAX_int32)
			{
				OutError = FString::Printf(
					TEXT("WFC GridSize=(%d,%d) 非法或无法安全转换为稠密数组。"),
					GridSize.X,
					GridSize.Y);
				return false;
			}

			const int32 CellCount = static_cast<int32>(CellCount64);
			if (Constraints.Num() != CellCount)
			{
				OutError = FString::Printf(
					TEXT("WFC 约束必须完整覆盖矩形 Grid：期望 %d 个 Cell，实际 %d 个。"),
					CellCount,
					Constraints.Num());
				return false;
			}

			OutByIndex.Init(nullptr, CellCount);
			for (const FGridCellConstraint& Constraint : Constraints)
			{
				if (!ZeroEscape::Grid::IsInside(Constraint.Coordinate, GridSize))
				{
					OutError = FString::Printf(
						TEXT("WFC Cell=(%d,%d) 超出 GridSize=(%d,%d)。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y,
						GridSize.X,
						GridSize.Y);
					return false;
				}

				const int32 DenseIndex =
					ZeroEscape::Grid::ToIndex(Constraint.Coordinate, GridSize);
				if (OutByIndex[DenseIndex] != nullptr)
				{
					OutError = FString::Printf(
						TEXT("WFC Cell=(%d,%d) 在约束数组中重复出现。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y);
					return false;
				}
				OutByIndex[DenseIndex] = &Constraint;

				const uint8 InvalidOpenBits = Constraint.RequiredOpenMask
					& static_cast<uint8>(~ZeroEscape::Grid::AllOpenEdges);
				const uint8 InvalidClosedBits = Constraint.RequiredClosedMask
					& static_cast<uint8>(~ZeroEscape::Grid::AllOpenEdges);
				if (InvalidOpenBits != 0 || InvalidClosedBits != 0)
				{
					OutError = FString::Printf(
						TEXT("WFC Cell=(%d,%d) 的 Required Mask 使用了 N/E/S/W 以外的 bit。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y);
					return false;
				}
				if ((Constraint.RequiredOpenMask & Constraint.RequiredClosedMask) != 0)
				{
					OutError = FString::Printf(
						TEXT("WFC Cell=(%d,%d) 的 RequiredOpen 与 RequiredClosed 冲突。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y);
					return false;
				}

				switch (Constraint.Domain)
				{
				case EGridCellDomain::Outside:
					if (Constraint.RequiredOpenMask != 0)
					{
						OutError = FString::Printf(
							TEXT("WFC Outside Cell=(%d,%d) 不能声明 RequiredOpen。"),
							Constraint.Coordinate.X,
							Constraint.Coordinate.Y);
						return false;
					}
					break;

				case EGridCellDomain::Required:
					// Required 只声明“必须非空”；具体开口由 WFC 邻接约束共同求解。
					break;

				case EGridCellDomain::Optional:
					if (Constraint.RequiredOpenMask != 0)
					{
						OutError = FString::Printf(
							TEXT("WFC Optional Cell=(%d,%d) 声明了 RequiredOpen，应先提升为 Required。"),
							Constraint.Coordinate.X,
							Constraint.Coordinate.Y);
						return false;
					}
					break;

				default:
					OutError = FString::Printf(
						TEXT("WFC Cell=(%d,%d) 使用了未知 EGridCellDomain。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y);
					return false;
				}
			}

			for (int32 DenseIndex = 0; DenseIndex < OutByIndex.Num(); ++DenseIndex)
			{
				if (OutByIndex[DenseIndex] == nullptr)
				{
					OutError = FString::Printf(
						TEXT("WFC 稠密约束缺少下标 %d 对应的 Cell。"),
						DenseIndex);
					return false;
				}
			}

			// 所有坐标已解析为稠密视图后，再验证 RequiredOpen 的界内与双向镜像契约。
			for (const FGridCellConstraint* ConstraintPtr : OutByIndex)
			{
				checkf(ConstraintPtr != nullptr, TEXT("稠密 WFC 约束视图不得包含空指针。"));
				const FGridCellConstraint& Constraint = *ConstraintPtr;

				for (uint8 Direction = 0;
					Direction < ZeroEscape::Grid::DirectionCount;
					++Direction)
				{
					const uint8 DirectionBit = ZeroEscape::Grid::DirectionBit(Direction);
					if ((Constraint.RequiredOpenMask & DirectionBit) == 0)
					{
						continue;
					}

					const FIntPoint NeighborCoordinate =
						ZeroEscape::Grid::Step(Constraint.Coordinate, Direction);
					if (!ZeroEscape::Grid::IsInside(NeighborCoordinate, GridSize))
					{
						OutError = FString::Printf(
							TEXT("WFC RequiredOpen 从 Cell=(%d,%d) 指向 Grid 边界外。"),
							Constraint.Coordinate.X,
							Constraint.Coordinate.Y);
						return false;
					}

					const int32 NeighborIndex =
						ZeroEscape::Grid::ToIndex(NeighborCoordinate, GridSize);
					const FGridCellConstraint& Neighbor = *OutByIndex[NeighborIndex];
					const uint8 OppositeBit = ZeroEscape::Grid::DirectionBit(
						ZeroEscape::Grid::OppositeDirectionIndex(Direction));
					if (Neighbor.Domain == EGridCellDomain::Outside
						|| (Neighbor.RequiredOpenMask & OppositeBit) == 0
						|| (Neighbor.RequiredClosedMask & OppositeBit) != 0)
					{
						OutError = FString::Printf(
							TEXT("WFC RequiredOpen 边 (%d,%d)->(%d,%d) 没有合法双向镜像。"),
							Constraint.Coordinate.X,
							Constraint.Coordinate.Y,
							NeighborCoordinate.X,
							NeighborCoordinate.Y);
						return false;
					}
				}
			}

			return true;
		}

		/** 验证调用方传入的是唯一、完整、正权重且按 0..15 排序的规范状态集。 */
		bool ValidateCanonicalVariants(
			const TArray<FTileVariant>& Variants,
			FString& OutError)
		{
			if (Variants.Num() != CanonicalVariantCount)
			{
				OutError = FString::Printf(
					TEXT("WFC 必须恰好提供 16 个 OpeningMask 状态，实际为 %d。"),
					Variants.Num());
				return false;
			}

			int64 TotalWeight = 0;
			for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
			{
				const FTileVariant& Variant = Variants[VariantIndex];
				if (Variant.OpeningMask != static_cast<uint8>(VariantIndex)
					|| (Variant.OpeningMask
						& static_cast<uint8>(~ZeroEscape::Grid::AllOpenEdges)) != 0)
				{
					OutError = FString::Printf(
						TEXT("WFC Variant[%d] 必须稳定对应 OpeningMask=%d。"),
						VariantIndex,
						VariantIndex);
					return false;
				}
				if (Variant.Weight <= 0)
				{
					OutError = FString::Printf(
						TEXT("WFC OpeningMask=%d 的 Weight=%d 非正；权重不能移除状态。"),
						Variant.OpeningMask,
						Variant.Weight);
					return false;
				}

				TotalWeight += Variant.Weight;
				if (TotalWeight > MAX_int32)
				{
					OutError = TEXT("WFC 16 个状态的权重总和超过 int32 加权抽样上限。");
					return false;
				}
			}
			return true;
		}

		int32 CountDomainVariants(const FWfcDomain Domain)
		{
			return FMath::CountBits(static_cast<uint32>(Domain));
		}

		/** 只把已经折叠且沿给定轴贯通的邻格计入已形成直线。 */
		int32 CountCollapsedStraightNeighbors(
			const FIntPoint GridSize,
			FIntPoint Coordinate,
			const FIntPoint Step,
			const uint8 AxisOpeningMask,
			const TArray<FWfcDomain>& Domains)
		{
			int32 Count = 0;
			Coordinate += Step;
			while (ZeroEscape::Grid::IsInside(Coordinate, GridSize))
			{
				const int32 CellIndex = ZeroEscape::Grid::ToIndex(Coordinate, GridSize);
				const FWfcDomain Domain = Domains[CellIndex];
				if (CountDomainVariants(Domain) != 1)
				{
					break;
				}
				const uint8 OpeningMask = static_cast<uint8>(
					FMath::CountTrailingZeros(static_cast<uint32>(Domain)));
				if ((OpeningMask & AxisOpeningMask) != AxisOpeningMask)
				{
					break;
				}
				++Count;
				Coordinate += Step;
			}
			return Count;
		}

		/** 超过偏好长度后按超量平方降权；正权重下限 1 保证候选永不被删除。 */
		int32 CalculatePreferredCandidateWeight(
			const FIntPoint GridSize,
			const int32 CellIndex,
			const FTileVariant& Variant,
			const TArray<FWfcDomain>& Domains,
			const FZeroEscapeWfcSolveSettings& Settings)
		{
			if (Settings.PreferredMaxConsecutiveStraightTiles <= 0)
			{
				return Variant.Weight;
			}

			const FIntPoint Coordinate(CellIndex % GridSize.X, CellIndex / GridSize.X);
			const uint8 HorizontalMask = static_cast<uint8>(
				ZeroEscape::Grid::DirectionBit(1) | ZeroEscape::Grid::DirectionBit(3));
			const uint8 VerticalMask = static_cast<uint8>(
				ZeroEscape::Grid::DirectionBit(0) | ZeroEscape::Grid::DirectionBit(2));
			const int32 HorizontalRun = (Variant.OpeningMask & HorizontalMask) == HorizontalMask
				? 1 + CountCollapsedStraightNeighbors(
					GridSize, Coordinate, FIntPoint(1, 0), HorizontalMask, Domains)
					+ CountCollapsedStraightNeighbors(
						GridSize, Coordinate, FIntPoint(-1, 0), HorizontalMask, Domains)
				: 0;
			const int32 VerticalRun = (Variant.OpeningMask & VerticalMask) == VerticalMask
				? 1 + CountCollapsedStraightNeighbors(
					GridSize, Coordinate, FIntPoint(0, 1), VerticalMask, Domains)
					+ CountCollapsedStraightNeighbors(
						GridSize, Coordinate, FIntPoint(0, -1), VerticalMask, Domains)
				: 0;

			const int32 Excess = FMath::Max(0,
				FMath::Max(HorizontalRun, VerticalRun)
					- Settings.PreferredMaxConsecutiveStraightTiles);
			const int64 Divisor = 1 + static_cast<int64>(Excess) * Excess;
			return FMath::Max(1, static_cast<int32>(Variant.Weight / Divisor));
		}

		/** 计算带权 Shannon 熵；相同熵时由调用方保留较小稠密下标。 */
		double CalculateWeightedEntropy(
			const FWfcDomain Domain,
			const TArray<FTileVariant>& Variants)
		{
			checkf(Domain != 0, TEXT("不得计算空 WFC Domain 的熵。"));

			double WeightSum = 0.0;
			double WeightedLogSum = 0.0;
			for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
			{
				if ((Domain & static_cast<FWfcDomain>(1u << VariantIndex)) == 0)
				{
					continue;
				}

				const double Weight = static_cast<double>(Variants[VariantIndex].Weight);
				WeightSum += Weight;
				WeightedLogSum += Weight * FMath::Loge(Weight);
			}

			checkf(WeightSum > 0.0, TEXT("非空 WFC Domain 必须至少包含一个正权重 Variant。"));
			return FMath::Loge(WeightSum) - (WeightedLogSum / WeightSum);
		}

		/** 从当前 Domain 中按整数权重选择一个 Variant 下标。 */
		int32 ChooseWeightedVariantIndex(
			const FWfcDomain Domain,
			const TStaticArray<int32, CanonicalVariantCount>& CandidateWeights,
			FRandomStream& Random)
		{
			int32 TotalWeight = 0;
			for (int32 VariantIndex = 0; VariantIndex < CanonicalVariantCount; ++VariantIndex)
			{
				if ((Domain & static_cast<FWfcDomain>(1u << VariantIndex)) != 0)
				{
					TotalWeight += CandidateWeights[VariantIndex];
				}
			}
			if (TotalWeight <= 0)
			{
				return INDEX_NONE;
			}

			int32 Roll = Random.RandHelper(TotalWeight);
			for (int32 VariantIndex = 0; VariantIndex < CanonicalVariantCount; ++VariantIndex)
			{
				if ((Domain & static_cast<FWfcDomain>(1u << VariantIndex)) == 0)
				{
					continue;
				}

				Roll -= CandidateWeights[VariantIndex];
				if (Roll < 0)
				{
					return VariantIndex;
				}
			}
			return INDEX_NONE;
		}

		/**
		 * 按原版 WFC 的带权 Shannon 熵选择未折叠 Cell。
		 *
		 * 这里不能先按候选数量做 MRV：一个被既有开口触及的前沿格通常只剩 8 个非空候选，
		 * 而尚未触及的 Optional 格虽然仍有 16 个候选，却因为 Empty 权重很高而具有更低的带权熵。
		 * 若把候选数量放在第一优先级，求解器会持续追着前沿向外生长，使 Empty 权重在最关键的
		 * 搜索阶段失效，最终反复撞上 Count 与 Connected。带权熵同时考虑候选数量和配置权重，
		 * 才与本项目“权重控制形态倾向、硬约束控制合法性”的契约一致。
		 *
		 * 完全同熵时使用 Seed 驱动的蓄水池抽样，等价于原版 WFC 对同熵 Cell 加入极小随机扰动；
		 * 它避免固定行扫描偏置，同时保持同输入、同 Seed 的确定性。
		 */
		int32 FindMinimumEntropyCell(
			const FIntPoint GridSize,
			const TArray<FWfcDomain>& Domains,
			const TArray<FTileVariant>& Variants,
			FRandomStream& Random)
		{
			int32 MinimumEntropyCell = INDEX_NONE;
			double MinimumEntropy = TNumericLimits<double>::Max();
			int32 EquivalentBestCount = 0;
			for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
			{
				const int32 CandidateCount = CountDomainVariants(Domains[CellIndex]);
				checkf(CandidateCount > 0, TEXT("稳定 WFC 状态不得包含空 Domain。"));
				if (CandidateCount == 1)
				{
					continue;
				}

				const double Entropy = CalculateWeightedEntropy(Domains[CellIndex], Variants);
				const bool bStrictlyBetter = MinimumEntropyCell == INDEX_NONE
					|| Entropy < MinimumEntropy - UE_DOUBLE_SMALL_NUMBER;
				const bool bEquivalentBest = !bStrictlyBetter
					&& FMath::Abs(Entropy - MinimumEntropy) <= UE_DOUBLE_SMALL_NUMBER;

				if (bStrictlyBetter)
				{
					MinimumEntropyCell = CellIndex;
					MinimumEntropy = Entropy;
					EquivalentBestCount = 1;
				}
				else if (bEquivalentBest)
				{
					++EquivalentBestCount;
					if (Random.RandHelper(EquivalentBestCount) == 0)
					{
						MinimumEntropyCell = CellIndex;
					}
				}
			}
			return MinimumEntropyCell;
		}

		/**
		 * 创建本帧唯一的加权无放回候选顺序。
		 *
		 * 最后只剩一个候选时直接追加，避免消耗没有选择意义的随机数。
		 */
		bool BuildWeightedCandidateOrder(
			FWfcDomain RemainingDomain,
			const FIntPoint GridSize,
			const int32 CellIndex,
			const TArray<FWfcDomain>& Domains,
			const FZeroEscapeWfcSolveSettings& Settings,
			const TArray<FTileVariant>& Variants,
			FRandomStream& Random,
			TArray<uint8, TInlineAllocator<CanonicalVariantCount>>& OutOrder)
		{
			OutOrder.Reset();
			TStaticArray<int32, CanonicalVariantCount> CandidateWeights;
			for (int32 VariantIndex = 0;
				VariantIndex < CanonicalVariantCount;
				++VariantIndex)
			{
				CandidateWeights[VariantIndex] = CalculatePreferredCandidateWeight(
					GridSize,
					CellIndex,
					Variants[VariantIndex],
					Domains,
					Settings);
			}
			while (CountDomainVariants(RemainingDomain) > 1)
			{
				const int32 VariantIndex =
					ChooseWeightedVariantIndex(
						RemainingDomain,
						CandidateWeights,
						Random);
				if (!Variants.IsValidIndex(VariantIndex))
				{
					return false;
				}

				OutOrder.Add(static_cast<uint8>(VariantIndex));
				RemainingDomain &= static_cast<FWfcDomain>(~(1u << VariantIndex));
			}

			if (RemainingDomain != 0)
			{
				const uint32 LastVariantIndex =
					FMath::CountTrailingZeros(static_cast<uint32>(RemainingDomain));
				checkf(LastVariantIndex < CanonicalVariantCount,
					TEXT("WFC 最后一个候选 bit 必须落在 0..15。"));
				OutOrder.Add(static_cast<uint8>(LastVariantIndex));
			}
			return OutOrder.Num() > 0;
		}

		/**
		 * 所有候选赋值、局部传播和全局 Ban 的唯一 Domain 修改入口。
		 *
		 * 即使交集变为空也先记录旧值，使当前失败分支仍可由 Trail 完整恢复。
		 */
		bool NarrowDomain(
			const int32 CellIndex,
			const FWfcDomain AllowedVariants,
			TArray<FWfcDomain>& InOutDomains,
			TArray<FDomainChange>& InOutTrail,
			TArray<int32>& OutChangedCells,
			FWfcBranchResult& OutResult)
		{
			checkf(InOutDomains.IsValidIndex(CellIndex),
				TEXT("WFC 内部 Domain 修改下标 %d 非法。"), CellIndex);

			const FWfcDomain Previous = InOutDomains[CellIndex];
			const FWfcDomain Next = Previous & AllowedVariants;
			if (Next == Previous)
			{
				return true;
			}

			InOutTrail.Add(FDomainChange{CellIndex, Previous});
			InOutDomains[CellIndex] = Next;
			OutChangedCells.Add(CellIndex);

			if (Next == 0)
			{
				OutResult.Status = EWfcBranchStatus::Contradiction;
				OutResult.RelatedCellIndex = CellIndex;
				return false;
			}
			return true;
		}

		/** 逆序撤销 TrailStart 之后的候选赋值、局部传播和全局 Ban。 */
		void RestoreDomains(
			const int32 TrailStart,
			TArray<FWfcDomain>& InOutDomains,
			TArray<FDomainChange>& InOutTrail)
		{
			checkf(TrailStart >= 0 && TrailStart <= InOutTrail.Num(),
				TEXT("WFC TrailStart=%d 超出当前 Trail.Num=%d。"),
				TrailStart,
				InOutTrail.Num());

			for (int32 Index = InOutTrail.Num() - 1; Index >= TrailStart; --Index)
			{
				const FDomainChange& Change = InOutTrail[Index];
				checkf(InOutDomains.IsValidIndex(Change.CellIndex),
					TEXT("WFC Trail 保存了非法 CellIndex=%d。"), Change.CellIndex);
				InOutDomains[Change.CellIndex] = Change.PreviousDomain;
			}
			InOutTrail.SetNum(TrailStart, EAllowShrinking::No);
		}

		/** 删除 TargetDomain 中在指定公共边得不到 NeighborDomain 支持的状态。 */
		FWfcDomain FilterDomainAgainstNeighbor(
			const FWfcDomain TargetDomain,
			const uint8 DirectionToNeighbor,
			const FWfcDomain NeighborDomain,
			const TArray<FTileVariant>& Variants)
		{
			checkf(TargetDomain != 0 && NeighborDomain != 0,
				TEXT("局部 WFC 传播不得读取空 Domain。"));

			const uint8 TargetBit = ZeroEscape::Grid::DirectionBit(DirectionToNeighbor);
			const uint8 NeighborBit = ZeroEscape::Grid::DirectionBit(
				ZeroEscape::Grid::OppositeDirectionIndex(DirectionToNeighbor));

			bool bNeighborCanBeOpen = false;
			bool bNeighborCanBeClosed = false;
			for (int32 NeighborVariantIndex = 0;
				NeighborVariantIndex < Variants.Num();
				++NeighborVariantIndex)
			{
				if ((NeighborDomain & static_cast<FWfcDomain>(1u << NeighborVariantIndex)) == 0)
				{
					continue;
				}

				const bool bOpen =
					(Variants[NeighborVariantIndex].OpeningMask & NeighborBit) != 0;
				bNeighborCanBeOpen |= bOpen;
				bNeighborCanBeClosed |= !bOpen;
			}

			FWfcDomain Filtered = TargetDomain;
			for (int32 TargetVariantIndex = 0;
				TargetVariantIndex < Variants.Num();
				++TargetVariantIndex)
			{
				const FWfcDomain VariantBit =
					static_cast<FWfcDomain>(1u << TargetVariantIndex);
				if ((Filtered & VariantBit) == 0)
				{
					continue;
				}

				const bool bTargetOpen =
					(Variants[TargetVariantIndex].OpeningMask & TargetBit) != 0;
				const bool bHasSupport = bTargetOpen
					? bNeighborCanBeOpen
					: bNeighborCanBeClosed;
				if (!bHasSupport)
				{
					Filtered &= static_cast<FWfcDomain>(~VariantBit);
				}
			}
			return Filtered;
		}

		/** 从指定变化源执行四邻域局部传播；所有真实缩减统一写入 Trail。 */
		FWfcBranchResult PropagateDomainsWithTrail(
			const FIntPoint GridSize,
			const TArray<const FGridCellConstraint*>& ConstraintsByIndex,
			const TArray<FTileVariant>& Variants,
			TArray<int32> InitialSourceIndices,
			TArray<FWfcDomain>& InOutDomains,
			TArray<FDomainChange>& InOutTrail,
			FZeroEscapeGenerationReport& InOutReport)
		{
			FWfcBranchResult Result;
			TArray<int32> Queue;
			Queue.Reserve(InitialSourceIndices.Num());
			TArray<uint8> bQueued;
			bQueued.Init(0, InOutDomains.Num());

			for (const int32 SourceIndex : InitialSourceIndices)
			{
				checkf(InOutDomains.IsValidIndex(SourceIndex),
					TEXT("WFC 初始传播源下标 %d 非法。"), SourceIndex);
				if (bQueued[SourceIndex] == 0)
				{
					bQueued[SourceIndex] = 1;
					Queue.Add(SourceIndex);
				}
			}

			TArray<int32> ChangedCells;
			ChangedCells.Reserve(1);
			for (int32 QueueHead = 0; QueueHead < Queue.Num(); ++QueueHead)
			{
				const int32 SourceIndex = Queue[QueueHead];
				checkf(ConstraintsByIndex.IsValidIndex(SourceIndex)
					&& ConstraintsByIndex[SourceIndex] != nullptr,
					TEXT("WFC 传播源没有对应的稠密约束。"));

				bQueued[SourceIndex] = 0;
				const FGridCellConstraint& Source = *ConstraintsByIndex[SourceIndex];
				for (uint8 Direction = 0;
					Direction < ZeroEscape::Grid::DirectionCount;
					++Direction)
				{
					const FIntPoint NeighborCoordinate =
						ZeroEscape::Grid::Step(Source.Coordinate, Direction);
					if (!ZeroEscape::Grid::IsInside(NeighborCoordinate, GridSize))
					{
						// 越界开口已在一元初始化中剔除，无需建立虚拟 Outside 邻格。
						continue;
					}

					const int32 NeighborIndex =
						ZeroEscape::Grid::ToIndex(NeighborCoordinate, GridSize);
					checkf(InOutDomains.IsValidIndex(NeighborIndex),
						TEXT("WFC 邻格稠密下标非法。"));

					const FWfcDomain PreviousDomain = InOutDomains[NeighborIndex];
					const FWfcDomain FilteredDomain = FilterDomainAgainstNeighbor(
						PreviousDomain,
						ZeroEscape::Grid::OppositeDirectionIndex(Direction),
						InOutDomains[SourceIndex],
						Variants);
					if (FilteredDomain == PreviousDomain)
					{
						continue;
					}

					ChangedCells.Reset();
					if (!NarrowDomain(
							NeighborIndex,
							FilteredDomain,
							InOutDomains,
							InOutTrail,
							ChangedCells,
							Result))
					{
					Result.Message = FString::Printf(
						TEXT("WFC 邻接传播使 Cell=(%d,%d) 的 Domain 为空。"),
						NeighborCoordinate.X,
						NeighborCoordinate.Y);
					Result.ContradictionSource =
						EWfcBranchContradictionSource::LocalAdjacency;
					return Result;
					}

					++InOutReport.Metrics.WfcPropagationCount;
					if (bQueued[NeighborIndex] == 0)
					{
						bQueued[NeighborIndex] = 1;
						Queue.Add(NeighborIndex);
					}
				}
			}

			return Result;
		}

		/**
		 * 先完成局部传播，再应用 Count/MaxConsecutive/Connected；约束 Ban 后重新局部传播。
		 * 所有 Domain 只缩小，因此该循环必然在有限步内达到稳定点或形成矛盾。
		 */
		FWfcBranchResult StabilizeDomains(
			const FIntPoint GridSize,
			const TArray<const FGridCellConstraint*>& ConstraintsByIndex,
			const FZeroEscapeWfcSolveSettings& Settings,
			const TArray<FTileVariant>& Variants,
			TArray<int32> PropagationSources,
			TArray<FWfcDomain>& InOutDomains,
			TArray<FDomainChange>& InOutTrail,
			FWfcConstraintWorkspace& ConstraintWorkspace,
			FZeroEscapeGenerationReport& InOutReport)
		{
			for (;;)
			{
				FWfcBranchResult Result = PropagateDomainsWithTrail(
					GridSize,
					ConstraintsByIndex,
					Variants,
					MoveTemp(PropagationSources),
					InOutDomains,
					InOutTrail,
					InOutReport);
				if (Result.Status != EWfcBranchStatus::Stable)
				{
					return Result;
				}

				FWfcConstraintFailure ConstraintFailure;
				if (!FWfcConstraints::Evaluate(
						GridSize,
						Settings,
						InOutDomains,
						ConstraintWorkspace,
						ConstraintFailure))
				{
					Result.Status = EWfcBranchStatus::Contradiction;
					switch (ConstraintFailure.Kind)
					{
					case EWfcConstraintContradiction::Count:
						Result.ContradictionSource = EWfcBranchContradictionSource::Count;
						break;
					case EWfcConstraintContradiction::MaxConsecutive:
						Result.ContradictionSource =
							EWfcBranchContradictionSource::MaxConsecutive;
						break;
					case EWfcConstraintContradiction::Connected:
						Result.ContradictionSource = EWfcBranchContradictionSource::Connected;
						break;
					default:
						Result.ContradictionSource = EWfcBranchContradictionSource::GlobalBan;
						break;
					}
					Result.RelatedCellIndex = ConstraintFailure.CellIndex;
					Result.Message = ConstraintFailure.Message;
					return Result;
				}

				if (ConstraintWorkspace.BanCellCount == 0)
				{
					return Result;
				}

				checkf(ConstraintWorkspace.BanMaskByCell.Num() == InOutDomains.Num(),
					TEXT("WFC 约束 Workspace 的 Ban 数组必须与 Domain 稠密数组等长。"));
				PropagationSources.Reset();
				for (int32 CellIndex = 0; CellIndex < InOutDomains.Num(); ++CellIndex)
				{
					const FWfcDomain Ban = ConstraintWorkspace.BanMaskByCell[CellIndex];
					if (Ban == 0)
					{
						continue;
					}

					const int32 PreviousChangedCount = PropagationSources.Num();
					if (!NarrowDomain(
							CellIndex,
							static_cast<FWfcDomain>(~Ban),
							InOutDomains,
							InOutTrail,
							PropagationSources,
							Result))
					{
						Result.Message = TEXT("WFC 全局约束 Ban 使一个 Cell 的 Domain 为空。");
						Result.ContradictionSource = EWfcBranchContradictionSource::GlobalBan;
						return Result;
					}
					if (PropagationSources.Num() > PreviousChangedCount)
					{
						++InOutReport.Metrics.WfcPropagationCount;
					}
				}

				if (PropagationSources.IsEmpty())
				{
					// Evaluate 声称产生了 Ban，却没有真正缩小任何 Domain，只可能是约束实现错误。
					Result.Status = EWfcBranchStatus::InvariantFailure;
					Result.Message = TEXT("WFC 约束报告了 Ban，但没有产生任何真实 Domain 缩减。");
					return Result;
				}
			}
		}

		/**
		 * 调用方保证当前帧仍有候选；false 只表示候选尝试预算已耗尽。
		 * 候选来自帧创建时的 Domain，恢复到 TrailStart 后必须仍然存在。
		 */
		bool TryNextCandidate(
			FWfcDecision& Decision,
			const FZeroEscapeWfcSolveSettings& Settings,
			TArray<FWfcDomain>& InOutDomains,
			TArray<FDomainChange>& InOutTrail,
			FZeroEscapeGenerationReport& InOutReport)
		{
			checkf(Decision.NextCandidateIndex < Decision.CandidateOrder.Num(),
				TEXT("WFC 决策帧已没有可尝试候选。"));
			if (InOutReport.Metrics.WfcCandidateAttemptCount
				>= Settings.MaxCandidateAttempts)
			{
				return false;
			}

			const uint8 VariantIndex =
				Decision.CandidateOrder[Decision.NextCandidateIndex++];
			const FWfcDomain CandidateBit =
				static_cast<FWfcDomain>(1u << VariantIndex);
			checkf(InOutDomains.IsValidIndex(Decision.CellIndex),
				TEXT("WFC 决策帧保存了非法 CellIndex=%d。"), Decision.CellIndex);
			checkf((InOutDomains[Decision.CellIndex] & CandidateBit) != 0,
				TEXT("WFC 恢复到 TrailStart 后丢失了帧内候选。"));

			TArray<int32> ChangedCells;
			FWfcBranchResult Result;
			const bool bAssigned = NarrowDomain(
				Decision.CellIndex,
				CandidateBit,
				InOutDomains,
				InOutTrail,
				ChangedCells,
				Result);
			checkf(bAssigned && Result.Status == EWfcBranchStatus::Stable,
				TEXT("从合法决策 Domain 选择 singleton 不应立即制造空 Domain。"));

			++InOutReport.Metrics.WfcCandidateAttemptCount;
			return true;
		}

		/** 把完整 singleton Domain 映射为稠密 OpeningMask；不重复扫描公共边。 */
		bool BuildCollapsedOpeningMasks(
			const TArray<FWfcDomain>& Domains,
			const TArray<FTileVariant>& Variants,
			TArray<uint8>& OutOpeningMasks,
			FString& OutError)
		{
			OutOpeningMasks.SetNumUninitialized(Domains.Num());
			for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
			{
				const FWfcDomain Domain = Domains[CellIndex];
				if (CountDomainVariants(Domain) != 1)
				{
					OutError = FString::Printf(
						TEXT("WFC 完成态 CellIndex=%d 仍不是 singleton Domain。"),
						CellIndex);
					return false;
				}

				const uint32 VariantIndex =
					FMath::CountTrailingZeros(static_cast<uint32>(Domain));
				if (!Variants.IsValidIndex(static_cast<int32>(VariantIndex)))
				{
					OutError = FString::Printf(
						TEXT("WFC singleton Domain 无法映射到合法 Variant：CellIndex=%d。"),
						CellIndex);
					return false;
				}

				OutOpeningMasks[CellIndex] = Variants[VariantIndex].OpeningMask;
			}
			return true;
		}

		TArray<int32> MakeAllCellIndices(const int32 CellCount)
		{
			TArray<int32> Result;
			Result.Reserve(CellCount);
			for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
			{
				Result.Add(CellIndex);
			}
			return Result;
		}
	}

	void FWfcSolver::BuildCanonicalVariants(
		const FZeroEscapeWfcShapeWeights& Weights,
		TStaticArray<FTileVariant, 16>& OutVariants)
	{
		// OpeningMask 本身就是稳定 Variant 身份；固定顺序避免资产数组重排影响随机抽样。
		for (int32 OpeningMask = 0;
			OpeningMask < WfcSolverPrivate::CanonicalVariantCount;
			++OpeningMask)
		{
			FTileVariant& Variant = OutVariants[OpeningMask];
			Variant.OpeningMask = static_cast<uint8>(OpeningMask);
			Variant.Weight = Weights.GetWeightForMask(Variant.OpeningMask);
		}
	}

	bool FWfcSolver::Solve(
		const FIntPoint GridSize,
		const TArray<FGridCellConstraint>& Constraints,
		const FZeroEscapeWfcSolveSettings& SolveSettings,
		const TArray<FTileVariant>& Variants,
		FRandomStream& Random,
		FWfcCollapsedCandidateValidator ValidateCollapsedCandidate,
		TArray<uint8>& OutOpeningMaskByCell,
		FZeroEscapeGenerationReport& OutReport)
	{
		using namespace WfcSolverPrivate;

		OutOpeningMaskByCell.Reset();
		// Solver 允许独立测试复用 Report；每次调用先清空本层拥有的诊断字段，避免旧失败泄漏。
		OutReport.Stage = EZeroEscapeGenerationStage::None;
		OutReport.Failure = EZeroEscapeGenerationFailure::None;
		OutReport.RelatedStableId = INDEX_NONE;
		OutReport.ActualValue = 0;
		OutReport.LimitValue = 0;
		OutReport.Message.Reset();
		ResetWfcMetrics(OutReport.Metrics);

		// 配置、稠密约束和 Variant 必须在建立 Domain、执行 BFS 或消耗随机数前完成验证。
		FString ValidationError;
		if (!FWfcConstraints::ValidateSolveSettings(
				GridSize,
				SolveSettings,
				ValidationError))
		{
			return ReportConfigurationFailure(ValidationError, OutReport);
		}

		TArray<const FGridCellConstraint*> ConstraintsByIndex;
		if (!BuildAndValidateDenseConstraintView(
				GridSize,
				Constraints,
				ConstraintsByIndex,
				ValidationError))
		{
			return ReportInvariantFailure(ValidationError, OutReport);
		}

		if (!ValidateCanonicalVariants(Variants, ValidationError))
		{
			return ReportInvariantFailure(ValidationError, OutReport);
		}

		const int32 StartIndex =
			ZeroEscape::Grid::ToIndex(SolveSettings.StartCoordinate, GridSize);
		checkf(ConstraintsByIndex.IsValidIndex(StartIndex),
			TEXT("已验证 StartCoordinate 必须产生合法稠密下标。"));
		if (ConstraintsByIndex[StartIndex]->Domain != EGridCellDomain::Required)
		{
			return ReportInvariantFailure(
				TEXT("WFC Connected 的 Start 必须对应 Required Cell。"),
				OutReport);
		}

		/**
		 * 一元初始化只读取已经建立好的稠密视图：Required 明确排除 Empty，Optional 允许 Empty，
		 * Outside 固定 Empty。边界与 Outside 邻格在此直接禁止开口，局部传播随后负责公共边一致性。
		 */
		TArray<FWfcDomain> Domains;
		Domains.Init(0, ConstraintsByIndex.Num());
		for (int32 CellIndex = 0; CellIndex < ConstraintsByIndex.Num(); ++CellIndex)
		{
			const FGridCellConstraint& Constraint = *ConstraintsByIndex[CellIndex];
			FWfcDomain Domain = 0;
			for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
			{
				const uint8 OpeningMask = Variants[VariantIndex].OpeningMask;
				bool bAllowed = false;
				switch (Constraint.Domain)
				{
				case EGridCellDomain::Outside:
					bAllowed = OpeningMask == 0;
					break;

				case EGridCellDomain::Required:
					bAllowed = OpeningMask != 0
						&& (OpeningMask & Constraint.RequiredOpenMask)
							== Constraint.RequiredOpenMask
						&& (OpeningMask & Constraint.RequiredClosedMask) == 0;
					break;

				case EGridCellDomain::Optional:
					bAllowed = (OpeningMask & Constraint.RequiredOpenMask)
							== Constraint.RequiredOpenMask
						&& (OpeningMask & Constraint.RequiredClosedMask) == 0;
					break;

				default:
					return ReportInvariantFailure(
						TEXT("WFC 初始化遇到未知 EGridCellDomain。"),
						OutReport);
				}

				if (bAllowed && Constraint.Domain != EGridCellDomain::Outside)
				{
					for (uint8 Direction = 0;
						Direction < ZeroEscape::Grid::DirectionCount;
						++Direction)
					{
						const uint8 DirectionBit = ZeroEscape::Grid::DirectionBit(Direction);
						if ((OpeningMask & DirectionBit) == 0)
						{
							continue;
						}

						const FIntPoint NeighborCoordinate =
							ZeroEscape::Grid::Step(Constraint.Coordinate, Direction);
						if (!ZeroEscape::Grid::IsInside(NeighborCoordinate, GridSize))
						{
							bAllowed = false;
							break;
						}

						const int32 NeighborIndex =
							ZeroEscape::Grid::ToIndex(NeighborCoordinate, GridSize);
						checkf(ConstraintsByIndex.IsValidIndex(NeighborIndex),
							TEXT("已验证邻格坐标必须产生合法稠密下标。"));
						if (ConstraintsByIndex[NeighborIndex]->Domain == EGridCellDomain::Outside)
						{
							bAllowed = false;
							break;
						}
					}
				}

				if (bAllowed)
				{
					Domain |= static_cast<FWfcDomain>(1u << VariantIndex);
				}
			}

			if (Domain == 0)
			{
				return ReportInvariantFailure(
					FString::Printf(
						TEXT("WFC 一元初始化使 Cell=(%d,%d) 的 Domain 为空。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y),
					OutReport);
			}
			Domains[CellIndex] = Domain;
		}

		TArray<FDomainChange> Trail;
		TArray<FWfcDecision> Decisions;
		FWfcConstraintWorkspace ConstraintWorkspace;
		TArray<int32> PendingSources = MakeAllCellIndices(Domains.Num());
		bool bRootState = true;

		for (;;)
		{
			// 新决策与替代候选都从同一稳定入口继续，避免任何矛盾绕过回溯状态机。
			FWfcBranchResult BranchResult = StabilizeDomains(
				GridSize,
				ConstraintsByIndex,
				SolveSettings,
				Variants,
				MoveTemp(PendingSources),
				Domains,
				Trail,
				ConstraintWorkspace,
				OutReport);

			if (BranchResult.Status == EWfcBranchStatus::InvariantFailure)
			{
				return ReportInvariantFailure(BranchResult.Message, OutReport);
			}

			if (bRootState && BranchResult.Status == EWfcBranchStatus::Stable)
			{
				// 只提交根固定点的 Domain 缩减；不得恢复掉所有分支共享的合法 Ban。
				Trail.Reset();
				bRootState = false;
			}

			bool bNeedBacktrack = BranchResult.Status == EWfcBranchStatus::Contradiction;
			if (!bNeedBacktrack)
			{
				const int32 CellIndex = FindMinimumEntropyCell(GridSize, Domains, Variants, Random);
				if (CellIndex != INDEX_NONE)
				{
					FWfcDecision Decision;
					Decision.CellIndex = CellIndex;
					Decision.TrailStart = Trail.Num();
					if (!BuildWeightedCandidateOrder(
							Domains[CellIndex],
							GridSize,
							CellIndex,
							Domains,
							SolveSettings,
							Variants,
							Random,
							Decision.CandidateOrder))
					{
						return ReportInvariantFailure(
							TEXT("WFC 无法为非空 Domain 建立候选顺序。"),
							OutReport);
					}

					Decisions.Add(MoveTemp(Decision));
					++OutReport.Metrics.WfcObservationCount;
					if (!TryNextCandidate(
							Decisions.Last(),
							SolveSettings,
							Domains,
							Trail,
							OutReport))
					{
						return ReportBudgetFailure(
							TEXT("WFC 达到候选尝试上限。"),
							OutReport.Metrics.WfcCandidateAttemptCount,
							SolveSettings.MaxCandidateAttempts,
							OutReport);
					}

					PendingSources = {CellIndex};
					continue;
				}

				// 全部 Domain 均为 singleton；仍必须由 Grid 验收路线与玩法约束。
				TArray<uint8> CollapsedMasks;
				FString ExportError;
				if (!BuildCollapsedOpeningMasks(
						Domains,
						Variants,
						CollapsedMasks,
						ExportError))
				{
					return ReportInvariantFailure(ExportError, OutReport);
				}

				const FWfcCollapsedCandidateEvaluation Evaluation =
					ValidateCollapsedCandidate(CollapsedMasks);
				if (Evaluation.Verdict == EWfcCollapsedCandidateVerdict::Accept)
				{
					OutOpeningMaskByCell = MoveTemp(CollapsedMasks);
					return true;
				}
				if (Evaluation.Verdict == EWfcCollapsedCandidateVerdict::FatalError)
				{
					OutReport.RelatedStableId = Evaluation.RelatedStableId;
					OutReport.ActualValue = Evaluation.ActualValue;
					OutReport.LimitValue = Evaluation.LimitValue;
					return ReportInvariantFailure(
						Evaluation.Message.IsEmpty()
							? TEXT("WFC 完整候选验收报告了未说明的致命错误。")
							: Evaluation.Message,
						OutReport);
				}
				if (Evaluation.Verdict != EWfcCollapsedCandidateVerdict::RejectBranch)
				{
					return ReportInvariantFailure(
						TEXT("WFC 完整候选验收返回了未知 Verdict。"),
						OutReport);
				}

				++OutReport.Metrics.WfcCollapsedCandidateRejectionCount;
				BranchResult.Status = EWfcBranchStatus::Contradiction;
				BranchResult.RelatedCellIndex = INDEX_NONE;
				BranchResult.Message = Evaluation.Message.IsEmpty()
					? TEXT("WFC 完整候选未通过调用方玩法验收。")
					: Evaluation.Message;
				bNeedBacktrack = true;
			}

			checkf(bNeedBacktrack, TEXT("只有当前分支矛盾才应进入 WFC 回溯。"));
			++OutReport.Metrics.WfcContradictionCount;
			switch (BranchResult.ContradictionSource)
			{
			case EWfcBranchContradictionSource::LocalAdjacency:
				++OutReport.Metrics.WfcLocalAdjacencyContradictionCount;
				break;
			case EWfcBranchContradictionSource::Count:
				++OutReport.Metrics.WfcCountContradictionCount;
				break;
			case EWfcBranchContradictionSource::MaxConsecutive:
				++OutReport.Metrics.WfcMaxConsecutiveContradictionCount;
				break;
			case EWfcBranchContradictionSource::Connected:
				++OutReport.Metrics.WfcConnectedContradictionCount;
				break;
			case EWfcBranchContradictionSource::GlobalBan:
				++OutReport.Metrics.WfcGlobalBanContradictionCount;
				break;
			case EWfcBranchContradictionSource::None:
				// 完整候选 Reject 已由 WfcCollapsedCandidateRejectionCount 单独记录。
				break;
			default:
				checkNoEntry();
				break;
			}

			bool bFoundAlternative = false;
			while (!Decisions.IsEmpty())
			{
				if (OutReport.Metrics.WfcBacktrackCount >= SolveSettings.MaxBacktrackCount)
				{
					return ReportBudgetFailure(
						TEXT("WFC 达到回溯上限。"),
						OutReport.Metrics.WfcBacktrackCount,
						SolveSettings.MaxBacktrackCount,
						OutReport);
				}

				FWfcDecision& Frame = Decisions.Last();
				RestoreDomains(Frame.TrailStart, Domains, Trail);
				++OutReport.Metrics.WfcBacktrackCount;

				if (Frame.NextCandidateIndex >= Frame.CandidateOrder.Num())
				{
					Decisions.Pop(EAllowShrinking::No);
					continue;
				}

				if (!TryNextCandidate(
						Frame,
						SolveSettings,
						Domains,
						Trail,
						OutReport))
				{
					return ReportBudgetFailure(
						TEXT("WFC 达到候选尝试上限。"),
						OutReport.Metrics.WfcCandidateAttemptCount,
						SolveSettings.MaxCandidateAttempts,
						OutReport);
				}

				PendingSources = {Frame.CellIndex};
				bFoundAlternative = true;
				break;
			}

			if (!bFoundAlternative)
			{
				return ReportNoValidSolution(BranchResult, OutReport);
			}
		}
	}
}
