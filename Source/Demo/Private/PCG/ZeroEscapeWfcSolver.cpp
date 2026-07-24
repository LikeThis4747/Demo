// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcSolver.cpp
 *
 * 职责：实现完整 0..15 OpeningMask 状态集上的无回溯 Grid-WFC。
 * 状态 Owner：所有 Domain、传播队列和候选输出均由单次 Solve 调用栈拥有；失败时输出保持为空。
 */

#include "PCG/ZeroEscapeWfcSolver.h"

#include "PCG/ZeroEscapeGenerationAssets.h"

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		/** 16 个 Variant 分别占用 uint16 Domain 的一个 bit。 */
		constexpr int32 CanonicalVariantCount = 16;

		/**
		 * 把任意顺序的完整 Cell 描述建立为稳定的 (Y, X) 稠密视图。
		 *
		 * 求解过程中绝不遍历 TMap；坐标只在入口解析一次，后续邻接访问全部通过稳定整数下标完成。
		 * 这既避免容器遍历顺序影响 Seed，也能在真正开始随机观察前发现漏格与重复坐标。
		 */
		bool BuildDenseConstraintView(
			const FIntPoint GridSize,
			const TArray<FGridCellConstraint>& Constraints,
			TArray<const FGridCellConstraint*>& OutByIndex,
			FString& OutError)
		{
			OutByIndex.Reset();

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
				if (!OutByIndex.IsValidIndex(DenseIndex))
				{
					OutError = FString::Printf(
						TEXT("WFC Cell=(%d,%d) 产生非法稠密下标 %d。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y,
						DenseIndex);
					return false;
				}
				if (OutByIndex[DenseIndex] != nullptr)
				{
					OutError = FString::Printf(
						TEXT("WFC Cell=(%d,%d) 在约束数组中重复出现。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y);
					return false;
				}
				OutByIndex[DenseIndex] = &Constraint;
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
			return true;
		}

		/**
		 * 验证调用方传入的是唯一、完整且正权重的 0..15 状态集。
		 *
		 * Solve 接收 TArray 是为了测试和组合方便，但生产路径仍必须遵循 BuildCanonicalVariants 的
		 * 稳定顺序。显式拒绝重排可防止 Domain bit 的语义在不同调用方之间悄悄变化。
		 */
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
					|| (Variant.OpeningMask & static_cast<uint8>(~ZeroEscape::Grid::AllOpenEdges)) != 0)
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

		/** 返回 Domain 中仍被允许的 Variant 数量。 */
		int32 CountDomainVariants(const uint16 Domain)
		{
			return FMath::CountBits(static_cast<uint32>(Domain));
		}

		/**
		 * 使用带权 Shannon 熵选择真正的不确定度，而不是简单把候选数量当作熵。
		 * H = log(sum(w)) - sum(w*log(w))/sum(w)。同熵时固定选择较小的 (Y, X) 稠密下标，
		 * 随机流只用于合法 Variant 的加权观察，不用于隐藏的浮点噪声或重试。
		 */
		double CalculateWeightedEntropy(
			const uint16 Domain,
			const TArray<FTileVariant>& Variants)
		{
			double WeightSum = 0.0;
			double WeightedLogSum = 0.0;
			for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
			{
				if ((Domain & static_cast<uint16>(1u << VariantIndex)) == 0)
				{
					continue;
				}

				const double Weight = static_cast<double>(Variants[VariantIndex].Weight);
				WeightSum += Weight;
				WeightedLogSum += Weight * FMath::Loge(Weight);
			}

			check(WeightSum > 0.0);
			return FMath::Loge(WeightSum) - (WeightedLogSum / WeightSum);
		}

		/**
		 * 从 Domain 中按整数权重选择一个 Variant 下标。
		 * 权重总和已在入口验证为 int32 安全范围，因此 RandHelper 不会截断或溢出。
		 */
		int32 ChooseWeightedVariantIndex(
			const uint16 Domain,
			const TArray<FTileVariant>& Variants,
			FRandomStream& Random)
		{
			int32 TotalWeight = 0;
			for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
			{
				if ((Domain & static_cast<uint16>(1u << VariantIndex)) != 0)
				{
					TotalWeight += Variants[VariantIndex].Weight;
				}
			}
			if (TotalWeight <= 0)
			{
				return INDEX_NONE;
			}

			int32 Roll = Random.RandHelper(TotalWeight);
			for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
			{
				if ((Domain & static_cast<uint16>(1u << VariantIndex)) == 0)
				{
					continue;
				}

				Roll -= Variants[VariantIndex].Weight;
				if (Roll < 0)
				{
					return VariantIndex;
				}
			}
			return INDEX_NONE;
		}

		/**
		 * 删除 TargetDomain 中在指定边上得不到 NeighborDomain 支持的状态。
		 *
		 * 当前二元规则只有一个 bit 的严格相等：Target 朝 Neighbor 的边是否打开，必须等于
		 * Neighbor 反向边是否打开。完整 16 状态下无需通用 Connector 表或 Support Count 快照。
		 */
		uint16 FilterDomainAgainstNeighbor(
			const uint16 TargetDomain,
			const uint8 DirectionToNeighbor,
			const uint16 NeighborDomain,
			const TArray<FTileVariant>& Variants)
		{
			const uint8 TargetBit = ZeroEscape::Grid::DirectionBit(DirectionToNeighbor);
			const uint8 NeighborBit = ZeroEscape::Grid::DirectionBit(
				ZeroEscape::Grid::OppositeDirectionIndex(DirectionToNeighbor));

			bool bNeighborCanBeOpen = false;
			bool bNeighborCanBeClosed = false;
			for (int32 NeighborVariantIndex = 0;
				NeighborVariantIndex < Variants.Num();
				++NeighborVariantIndex)
			{
				if ((NeighborDomain & static_cast<uint16>(1u << NeighborVariantIndex)) == 0)
				{
					continue;
				}
				const bool bOpen =
					(Variants[NeighborVariantIndex].OpeningMask & NeighborBit) != 0;
				bNeighborCanBeOpen |= bOpen;
				bNeighborCanBeClosed |= !bOpen;
			}

			uint16 Filtered = TargetDomain;
			for (int32 TargetVariantIndex = 0;
				TargetVariantIndex < Variants.Num();
				++TargetVariantIndex)
			{
				const uint16 VariantDomainBit =
					static_cast<uint16>(1u << TargetVariantIndex);
				if ((Filtered & VariantDomainBit) == 0)
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
					Filtered &= static_cast<uint16>(~VariantDomainBit);
				}
			}
			return Filtered;
		}

		/**
		 * 以“发生变化的 Cell”为传播源执行确定性邻接队列。
		 *
		 * Source Domain 缩小时，只有它的四个邻格可能失去支持；若 Neighbor 实际收缩，再把
		 * Neighbor 入队继续向外传播。队列始终按首次触发顺序处理，方向固定 N/E/S/W。
		 */
		bool PropagateDomains(
			const FIntPoint GridSize,
			const TArray<const FGridCellConstraint*>& ConstraintsByIndex,
			const TArray<FTileVariant>& Variants,
			const TArray<int32>& InitialSourceIndices,
			TArray<uint16>& InOutDomains,
			FZeroEscapeGenerationReport& InOutReport,
			FString& OutError)
		{
			TArray<int32> Queue = InitialSourceIndices;
			TArray<uint8> bQueued;
			bQueued.Init(0, InOutDomains.Num());
			for (const int32 SourceIndex : InitialSourceIndices)
			{
				if (!bQueued.IsValidIndex(SourceIndex))
				{
					OutError = TEXT("WFC 传播队列收到非法初始下标。");
					return false;
				}
				bQueued[SourceIndex] = 1;
			}

			for (int32 QueueHead = 0; QueueHead < Queue.Num(); ++QueueHead)
			{
				const int32 SourceIndex = Queue[QueueHead];
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
						// 边界开口已在一元 Domain 初始化阶段剔除，不需要创建虚拟邻格。
						continue;
					}

					const int32 NeighborIndex =
						ZeroEscape::Grid::ToIndex(NeighborCoordinate, GridSize);
					const uint16 PreviousDomain = InOutDomains[NeighborIndex];
					const uint16 FilteredDomain = FilterDomainAgainstNeighbor(
						PreviousDomain,
						ZeroEscape::Grid::OppositeDirectionIndex(Direction),
						InOutDomains[SourceIndex],
						Variants);
					if (FilteredDomain == PreviousDomain)
					{
						continue;
					}

					InOutDomains[NeighborIndex] = FilteredDomain;
					++InOutReport.Metrics.WfcPropagationCount;
					if (FilteredDomain == 0)
					{
						OutError = FString::Printf(
							TEXT("WFC 邻接传播使 Cell=(%d,%d) 的 Domain 为空。"),
							NeighborCoordinate.X,
							NeighborCoordinate.Y);
						return false;
					}

					if (bQueued[NeighborIndex] == 0)
					{
						bQueued[NeighborIndex] = 1;
						Queue.Add(NeighborIndex);
					}
				}
			}
			return true;
		}

		/** 统一写入“不应在完整状态集下发生”的结构化失败。 */
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
	}

	void FWfcSolver::BuildCanonicalVariants(
		const FZeroEscapeWfcShapeWeights& Weights,
		TStaticArray<FTileVariant, 16>& OutVariants)
	{
		// OpeningMask 本身就是稳定 Variant 身份；固定顺序避免素材或数组编辑顺序影响随机抽样。
		for (int32 OpeningMask = 0; OpeningMask < CanonicalVariantCount; ++OpeningMask)
		{
			FTileVariant& Variant = OutVariants[OpeningMask];
			Variant.OpeningMask = static_cast<uint8>(OpeningMask);
			Variant.Weight = Weights.GetWeightForMask(Variant.OpeningMask);
		}
	}

	bool FWfcSolver::ValidateGuaranteedSolvableConstraints(
		const FIntPoint GridSize,
		const TArray<FGridCellConstraint>& Constraints,
		FString& OutError)
	{
		OutError.Reset();

		TArray<const FGridCellConstraint*> ConstraintsByIndex;
		if (!BuildDenseConstraintView(
				GridSize,
				Constraints,
				ConstraintsByIndex,
				OutError))
		{
			return false;
		}

		for (const FGridCellConstraint* ConstraintPtr : ConstraintsByIndex)
		{
			check(ConstraintPtr != nullptr);
			const FGridCellConstraint& Constraint = *ConstraintPtr;
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
				if (Constraint.RequiredOpenMask == 0)
				{
					OutError = FString::Printf(
						TEXT("WFC Required Cell=(%d,%d) 必须至少声明一条 RequiredOpen。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y);
					return false;
				}
				break;

			case EGridCellDomain::Optional:
				if (Constraint.RequiredOpenMask != 0)
				{
					OutError = FString::Printf(
						TEXT("WFC Optional Cell=(%d,%d) 声明了 RequiredOpen；应先提升为 Required。"),
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
				const FGridCellConstraint& Neighbor = *ConstraintsByIndex[NeighborIndex];
				const uint8 OppositeBit = ZeroEscape::Grid::DirectionBit(
					ZeroEscape::Grid::OppositeDirectionIndex(Direction));
				if (Neighbor.Domain == EGridCellDomain::Outside
					|| (Neighbor.RequiredOpenMask & OppositeBit) == 0
					|| (Neighbor.RequiredClosedMask & OppositeBit) != 0)
				{
					OutError = FString::Printf(
						TEXT("WFC RequiredOpen 边 (%d,%d)->(%d,%d) 没有合法的双向镜像。"),
						Constraint.Coordinate.X,
						Constraint.Coordinate.Y,
						NeighborCoordinate.X,
						NeighborCoordinate.Y);
					return false;
				}
			}
		}

		/**
		 * 构造性见证：Required Cell 只打开 RequiredOpen，Optional/Outside 全部为空。
		 * 上方已经证明 RequiredOpen 双向且不与 Closed/边界冲突，因此该赋值的每条公共边必然相等。
		 * 再显式逐边核对一次，防止以后修改方向 bit 或 ToIndex 契约时破坏这项数学前提。
		 */
		for (int32 CellIndex = 0; CellIndex < ConstraintsByIndex.Num(); ++CellIndex)
		{
			const FGridCellConstraint& Cell = *ConstraintsByIndex[CellIndex];
			const uint8 WitnessMask = Cell.Domain == EGridCellDomain::Required
				? Cell.RequiredOpenMask
				: 0;
			for (uint8 Direction = 0;
				Direction < ZeroEscape::Grid::DirectionCount;
				++Direction)
			{
				const FIntPoint NeighborCoordinate =
					ZeroEscape::Grid::Step(Cell.Coordinate, Direction);
				const bool bCellOpen =
					(WitnessMask & ZeroEscape::Grid::DirectionBit(Direction)) != 0;
				if (!ZeroEscape::Grid::IsInside(NeighborCoordinate, GridSize))
				{
					if (bCellOpen)
					{
						OutError = TEXT("WFC 构造性见证意外打开了 Grid 边界。");
						return false;
					}
					continue;
				}

				const FGridCellConstraint& Neighbor = *ConstraintsByIndex[
					ZeroEscape::Grid::ToIndex(NeighborCoordinate, GridSize)];
				const uint8 NeighborWitnessMask =
					Neighbor.Domain == EGridCellDomain::Required
						? Neighbor.RequiredOpenMask
						: 0;
				const bool bNeighborOpen = (
					NeighborWitnessMask
					& ZeroEscape::Grid::DirectionBit(
						ZeroEscape::Grid::OppositeDirectionIndex(Direction))) != 0;
				if (bCellOpen != bNeighborOpen)
				{
					OutError = TEXT("WFC 构造性见证的公共边开闭不一致。");
					return false;
				}
			}
		}

		return true;
	}

	bool FWfcSolver::Solve(
		const FIntPoint GridSize,
		const TArray<FGridCellConstraint>& Constraints,
		const TArray<FTileVariant>& Variants,
		FRandomStream& Random,
		TArray<uint8>& OutOpeningMaskByCell,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutOpeningMaskByCell.Reset();
		OutReport.Metrics.WfcObservationCount = 0;
		OutReport.Metrics.WfcPropagationCount = 0;
		OutReport.Metrics.WfcInvariantFailureCount = 0;

		FString ValidationError;
		if (!ValidateGuaranteedSolvableConstraints(
				GridSize,
				Constraints,
				ValidationError))
		{
			return ReportInvariantFailure(ValidationError, OutReport);
		}
		if (!ValidateCanonicalVariants(Variants, ValidationError))
		{
			return ReportInvariantFailure(ValidationError, OutReport);
		}

		TArray<const FGridCellConstraint*> ConstraintsByIndex;
		if (!BuildDenseConstraintView(
				GridSize,
				Constraints,
				ConstraintsByIndex,
				ValidationError))
		{
			return ReportInvariantFailure(ValidationError, OutReport);
		}

		/**
		 * 一元初始化只处理当前 Cell 自身已知的信息：RequiredOpen/Closed、Grid 边界和 Outside 邻格。
		 * 相邻非 Outside Cell 的开闭一致性统一留给传播队列，避免在两处实现不同的二元规则。
		 */
		TArray<uint16> Domains;
		Domains.Init(0, ConstraintsByIndex.Num());
		for (int32 CellIndex = 0; CellIndex < ConstraintsByIndex.Num(); ++CellIndex)
		{
			const FGridCellConstraint& Constraint = *ConstraintsByIndex[CellIndex];
			uint16 Domain = 0;
			for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
			{
				const uint8 OpeningMask = Variants[VariantIndex].OpeningMask;
				bool bAllowed = Constraint.Domain == EGridCellDomain::Outside
					? OpeningMask == 0
					: (OpeningMask & Constraint.RequiredOpenMask)
							== Constraint.RequiredOpenMask
						&& (OpeningMask & Constraint.RequiredClosedMask) == 0;

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

						const FGridCellConstraint& Neighbor = *ConstraintsByIndex[
							ZeroEscape::Grid::ToIndex(NeighborCoordinate, GridSize)];
						if (Neighbor.Domain == EGridCellDomain::Outside)
						{
							bAllowed = false;
							break;
						}
					}
				}

				if (bAllowed)
				{
					Domain |= static_cast<uint16>(1u << VariantIndex);
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

		// 所有 Cell 作为初始传播源，先达到稳定点，再进行第一次随机观察。
		TArray<int32> InitialSources;
		InitialSources.Reserve(Domains.Num());
		for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
		{
			InitialSources.Add(CellIndex);
		}
		FString PropagationError;
		if (!PropagateDomains(
				GridSize,
				ConstraintsByIndex,
				Variants,
				InitialSources,
				Domains,
				OutReport,
				PropagationError))
		{
			return ReportInvariantFailure(PropagationError, OutReport);
		}

		for (;;)
		{
			int32 MinimumEntropyCell = INDEX_NONE;
			double MinimumEntropy = TNumericLimits<double>::Max();
			for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
			{
				const int32 CandidateCount = CountDomainVariants(Domains[CellIndex]);
				if (CandidateCount <= 1)
				{
					continue;
				}

				const double Entropy = CalculateWeightedEntropy(Domains[CellIndex], Variants);
				if (MinimumEntropyCell == INDEX_NONE
					|| Entropy < MinimumEntropy - UE_DOUBLE_SMALL_NUMBER)
				{
					MinimumEntropyCell = CellIndex;
					MinimumEntropy = Entropy;
				}
			}

			if (MinimumEntropyCell == INDEX_NONE)
			{
				break;
			}

			const int32 ChosenVariantIndex = ChooseWeightedVariantIndex(
				Domains[MinimumEntropyCell],
				Variants,
				Random);
			if (!Variants.IsValidIndex(ChosenVariantIndex))
			{
				return ReportInvariantFailure(
					TEXT("WFC 加权观察未能从非空 Domain 中选择 Variant。"),
					OutReport);
			}

			Domains[MinimumEntropyCell] =
				static_cast<uint16>(1u << ChosenVariantIndex);
			++OutReport.Metrics.WfcObservationCount;
			const TArray<int32> ObservationSource = { MinimumEntropyCell };
			if (!PropagateDomains(
					GridSize,
					ConstraintsByIndex,
					Variants,
					ObservationSource,
					Domains,
					OutReport,
					PropagationError))
			{
				return ReportInvariantFailure(PropagationError, OutReport);
			}
		}

		TArray<uint8> CandidateOutput;
		CandidateOutput.Init(0, Domains.Num());
		for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
		{
			if (CountDomainVariants(Domains[CellIndex]) != 1)
			{
				return ReportInvariantFailure(
					TEXT("WFC 结束时仍存在未折叠或空 Domain。"),
					OutReport);
			}

			int32 VariantIndex = INDEX_NONE;
			for (int32 CandidateIndex = 0; CandidateIndex < Variants.Num(); ++CandidateIndex)
			{
				if ((Domains[CellIndex] & static_cast<uint16>(1u << CandidateIndex)) != 0)
				{
					VariantIndex = CandidateIndex;
					break;
				}
			}
			if (!Variants.IsValidIndex(VariantIndex))
			{
				return ReportInvariantFailure(
					TEXT("WFC 单一 Domain 无法映射到合法 Variant。"),
					OutReport);
			}
			CandidateOutput[CellIndex] = Variants[VariantIndex].OpeningMask;
		}

		/**
		 * 导出后独立复核一元和每条相邻边。该检查不依赖传播内部状态，能捕获方向 bit、
		 * 邻接下标或导出映射的实现回归；失败仍属于 SolverInvariantViolation。
		 */
		for (int32 CellIndex = 0; CellIndex < CandidateOutput.Num(); ++CellIndex)
		{
			const FGridCellConstraint& Cell = *ConstraintsByIndex[CellIndex];
			const uint8 OpeningMask = CandidateOutput[CellIndex];
			if ((OpeningMask & Cell.RequiredOpenMask) != Cell.RequiredOpenMask
				|| (OpeningMask & Cell.RequiredClosedMask) != 0
				|| (Cell.Domain == EGridCellDomain::Outside && OpeningMask != 0)
				|| (Cell.Domain == EGridCellDomain::Required && OpeningMask == 0))
			{
				return ReportInvariantFailure(
					FString::Printf(
						TEXT("WFC 导出 Cell=(%d,%d) 不满足一元约束。"),
						Cell.Coordinate.X,
						Cell.Coordinate.Y),
					OutReport);
			}

			for (uint8 Direction = 0;
				Direction < ZeroEscape::Grid::DirectionCount;
				++Direction)
			{
				const bool bOpen =
					(OpeningMask & ZeroEscape::Grid::DirectionBit(Direction)) != 0;
				const FIntPoint NeighborCoordinate =
					ZeroEscape::Grid::Step(Cell.Coordinate, Direction);
				if (!ZeroEscape::Grid::IsInside(NeighborCoordinate, GridSize))
				{
					if (bOpen)
					{
						return ReportInvariantFailure(
							TEXT("WFC 导出结果包含指向 Grid 边界外的开口。"),
							OutReport);
					}
					continue;
				}

				const int32 NeighborIndex =
					ZeroEscape::Grid::ToIndex(NeighborCoordinate, GridSize);
				const bool bNeighborOpen = (
					CandidateOutput[NeighborIndex]
					& ZeroEscape::Grid::DirectionBit(
						ZeroEscape::Grid::OppositeDirectionIndex(Direction))) != 0;
				if (bOpen != bNeighborOpen)
				{
					return ReportInvariantFailure(
						FString::Printf(
							TEXT("WFC 导出公共边 (%d,%d)->(%d,%d) 开闭不一致。"),
							Cell.Coordinate.X,
							Cell.Coordinate.Y,
							NeighborCoordinate.X,
							NeighborCoordinate.Y),
						OutReport);
				}
			}
		}

		OutOpeningMaskByCell = MoveTemp(CandidateOutput);
		return true;
	}
}
