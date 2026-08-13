// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeWfcConstraints.cpp
 *
 * 职责：实现 Count、MaxConsecutive 与 Connected 三项确定性非局部约束。
 *
 * 所有辅助符号均放入独有的 WfcConstraintsPrivate 命名空间，避免 Unreal Unity
 * Build 把多个 CPP 合并后与其他 PCG 文件中的私有帮助函数发生重名。
 */

#include "PCG/ZeroEscapeWfcConstraints.h"

#include "PCG/ZeroEscapeGenerationTypes.h"

namespace ZeroEscape::LevelGeneration
{
	namespace WfcConstraintsPrivate
	{
		/** OpeningMask=0 对应的 Empty Variant bit。 */
		constexpr FWfcDomain EmptyVariantBit = static_cast<FWfcDomain>(1u << 0u);

		/** 完整 16 Variant Domain 的全部有效 bit。 */
		constexpr FWfcDomain AllVariantBits = MAX_uint16;

		/** OpeningMask=1..15 对应的全部 NonEmpty Variant bit。 */
		constexpr FWfcDomain NonEmptyVariantBits =
			static_cast<FWfcDomain>(AllVariantBits & ~EmptyVariantBit);

		/** N/E/S/W 的稳定 OpeningMask bit；必须与 ZeroEscape::Grid 约定一致。 */
		constexpr uint8 NorthEdge = ZeroEscape::Grid::DirectionBit(0);
		constexpr uint8 EastEdge = ZeroEscape::Grid::DirectionBit(1);
		constexpr uint8 SouthEdge = ZeroEscape::Grid::DirectionBit(2);
		constexpr uint8 WestEdge = ZeroEscape::Grid::DirectionBit(3);

		/**
		 * 构造“OpeningMask 至少包含 RequiredEdges”的全部 Variant Domain bit。
		 *
		 * FWfcSolver 会在调用约束前验证 VariantIndex 与 OpeningMask 0..15 完全对应，
		 * 因此这里可以稳定地把 OpeningMask 直接作为 Domain bit 下标。
		 */
		constexpr FWfcDomain BuildVariantBitsContainingEdges(const uint8 RequiredEdges)
		{
			FWfcDomain Result = 0;
			for (uint8 OpeningMask = 0; OpeningMask < 16; ++OpeningMask)
			{
				if ((OpeningMask & RequiredEdges) == RequiredEdges)
				{
					Result = static_cast<FWfcDomain>(
						Result | static_cast<FWfcDomain>(1u << OpeningMask));
				}
			}
			return Result;
		}

		/** 同时具有 East/West 开口的 Straight、T 与 Cross Variant 集。 */
		constexpr FWfcDomain HorizontalThroughVariantBits =
			BuildVariantBitsContainingEdges(EastEdge | WestEdge);

		/** 同时具有 North/South 开口的 Straight、T 与 Cross Variant 集。 */
		constexpr FWfcDomain VerticalThroughVariantBits =
			BuildVariantBitsContainingEdges(NorthEdge | SouthEdge);

		/** 每个方向上仍可能打开该边的 Variant 集，顺序固定为 N/E/S/W。 */
		constexpr FWfcDomain OpenVariantBitsByDirection[4] =
		{
			BuildVariantBitsContainingEdges(NorthEdge),
			BuildVariantBitsContainingEdges(EastEdge),
			BuildVariantBitsContainingEdges(SouthEdge),
			BuildVariantBitsContainingEdges(WestEdge)
		};

		/** 返回当前 Domain 是否仍包含至少一个 NonEmpty 候选。 */
		bool CanBeWalkable(const FWfcDomain Domain)
		{
			return (Domain & NonEmptyVariantBits) != 0;
		}

		/** 返回当前 Domain 是否已经失去 Empty，因而被迫最终为 NonEmpty。 */
		bool MustBeWalkable(const FWfcDomain Domain)
		{
			return CanBeWalkable(Domain) && (Domain & EmptyVariantBit) == 0;
		}

		/** 返回当前 Domain 是否仍有至少一个 Variant 能在指定方向打开。 */
		bool CanOpenInDirection(const FWfcDomain Domain, const uint8 Direction)
		{
			check(Direction < ZeroEscape::Grid::DirectionCount);
			return (Domain & OpenVariantBitsByDirection[Direction]) != 0;
		}

		/** 返回当前 Domain 是否已经被迫在指定方向打开。 */
		bool MustOpenInDirection(const FWfcDomain Domain, const uint8 Direction)
		{
			check(Direction < ZeroEscape::Grid::DirectionCount);
			const FWfcDomain OpenBits = OpenVariantBitsByDirection[Direction];
			return (Domain & OpenBits) != 0
				&& (Domain & static_cast<FWfcDomain>(~OpenBits)) == 0;
		}

		/**
		 * ConnectedConstraint 使用的 expanded graph 每格节点数。
		 *
		 * slot 0 是中心，slot 1..4 依次是 N/E/S/W 出口。中心与本格四个出口相连，
		 * 出口还与相邻格的反向出口相连。这样关节点不只会证明“某格必须非空”，也能
		 * 证明“某一条公共边必须打开”，等价于 DeBroglie 的 EdgedPathView 表示。
		 */
		constexpr int32 ConnectedNodesPerCell = 5;
		constexpr uint8 ConnectedCenterSlot = 0;

		/** 返回 Cell 对应的 expanded graph 中心节点。 */
		constexpr int32 ToConnectedCenterNode(const int32 CellIndex)
		{
			return CellIndex * ConnectedNodesPerCell;
		}

		/** 返回 Cell 指定方向对应的 expanded graph 出口节点。 */
		constexpr int32 ToConnectedDirectionNode(
			const int32 CellIndex,
			const uint8 Direction)
		{
			return CellIndex * ConnectedNodesPerCell + 1 + Direction;
		}

		/** expanded graph 节点转回稳定稠密 CellIndex。 */
		constexpr int32 ConnectedNodeToCell(const int32 NodeIndex)
		{
			return NodeIndex / ConnectedNodesPerCell;
		}

		/** expanded graph 节点在本格内的 slot；0=中心，1..4=N/E/S/W。 */
		constexpr uint8 ConnectedNodeSlot(const int32 NodeIndex)
		{
			return static_cast<uint8>(NodeIndex % ConnectedNodesPerCell);
		}

		/** 把稳定稠密 CellIndex 转回左下角为原点的 Grid 坐标。 */
		FIntPoint ToCoordinate(const int32 CellIndex, const FIntPoint GridSize)
		{
			return FIntPoint(CellIndex % GridSize.X, CellIndex / GridSize.X);
		}

		/**
		 * 按稳定次序查询 expanded graph 的隐式邻居，避免为每个稳定点重复构造邻接表。
		 *
		 * 中心有四个候选邻居，顺序 N/E/S/W；方向节点先访问本格中心，再访问相邻格的
		 * 反向方向节点。边界外的第二个方向邻居返回 false，由调用者直接跳过。
		 */
		bool TryGetConnectedNeighbor(
			const int32 NodeIndex,
			const uint8 NeighborOrdinal,
			const FIntPoint GridSize,
			int32& OutNeighborNode)
		{
			const int32 CellIndex = ConnectedNodeToCell(NodeIndex);
			const uint8 Slot = ConnectedNodeSlot(NodeIndex);
			if (Slot == ConnectedCenterSlot)
			{
				if (NeighborOrdinal >= ZeroEscape::Grid::DirectionCount)
				{
					return false;
				}
				OutNeighborNode = ToConnectedDirectionNode(CellIndex, NeighborOrdinal);
				return true;
			}

			if (NeighborOrdinal == 0)
			{
				OutNeighborNode = ToConnectedCenterNode(CellIndex);
				return true;
			}
			if (NeighborOrdinal != 1)
			{
				return false;
			}

			const uint8 Direction = static_cast<uint8>(Slot - 1);
			const FIntPoint NeighborCoordinate =
				ZeroEscape::Grid::Step(ToCoordinate(CellIndex, GridSize), Direction);
			if (!ZeroEscape::Grid::IsInside(NeighborCoordinate, GridSize))
			{
				return false;
			}

			const int32 NeighborCellIndex =
				ZeroEscape::Grid::ToIndex(NeighborCoordinate, GridSize);
			OutNeighborNode = ToConnectedDirectionNode(
				NeighborCellIndex,
				ZeroEscape::Grid::OppositeDirectionIndex(Direction));
			return true;
		}

		/**
		 * 把一个候选删除请求合并到 Workspace，但不直接修改 Domain。
		 *
		 * 只保留当前 Domain 中真实存在的 bit，避免 Solver 为无效删除记录 Trail；多个
		 * 窗口或约束命中同一 Cell 时按位或合并，并且 BanCellCount 只增加一次。
		 */
		void AddBan(
			const int32 CellIndex,
			const FWfcDomain RequestedBanBits,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace)
		{
			check(CellIndex >= 0 && CellIndex < Domains.Num());
			check(Workspace.BanMaskByCell.IsValidIndex(CellIndex));

			const FWfcDomain EffectiveBanBits = static_cast<FWfcDomain>(
				RequestedBanBits & Domains[CellIndex]);
			if (EffectiveBanBits == 0)
			{
				return;
			}

			FWfcDomain& ExistingBanBits = Workspace.BanMaskByCell[CellIndex];
			if (ExistingBanBits == 0)
			{
				++Workspace.BanCellCount;
			}
			ExistingBanBits = static_cast<FWfcDomain>(ExistingBanBits | EffectiveBanBits);
		}

		/**
		 * 评估全图 NonEmpty Cell 数量上下界，并在边界恰好收紧时输出必要 Ban。
		 *
		 * Forced 表示已经不能选 Empty；Possible 表示仍至少能选一个 NonEmpty。
		 * 这与成熟 CountConstraint 的 Yes/Maybe/No 三态传播等价。
		 */
		bool EvaluateCount(
			const FZeroEscapeWfcSolveSettings& Settings,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure)
		{
			int32 ForcedWalkableCount = 0;
			int32 PossibleWalkableCount = 0;

			for (const FWfcDomain Domain : Domains)
			{
				// Domain==0 必须由 Solver 的统一 Domain 修改入口立即捕获，不应到达稳定点。
				check(Domain != 0);
				if (CanBeWalkable(Domain))
				{
					++PossibleWalkableCount;
					if (MustBeWalkable(Domain))
					{
						++ForcedWalkableCount;
					}
				}
			}

			if (ForcedWalkableCount > Settings.MaxWalkableCellCount)
			{
				OutFailure.Kind = EWfcConstraintContradiction::Count;
				OutFailure.ObservedCount = ForcedWalkableCount;
				OutFailure.Limit = Settings.MaxWalkableCellCount;
				OutFailure.Message = FString::Printf(
					TEXT("WFC Count：已被迫非空的 Cell=%d，超过上限=%d。"),
					ForcedWalkableCount,
					Settings.MaxWalkableCellCount);
				return false;
			}

			if (PossibleWalkableCount < Settings.MinWalkableCellCount)
			{
				OutFailure.Kind = EWfcConstraintContradiction::Count;
				OutFailure.ObservedCount = PossibleWalkableCount;
				OutFailure.Limit = Settings.MinWalkableCellCount;
				OutFailure.Message = FString::Printf(
					TEXT("WFC Count：最多只能得到 %d 个非空 Cell，低于下限=%d。"),
					PossibleWalkableCount,
					Settings.MinWalkableCellCount);
				return false;
			}

			// 已达到上限后，所有仍可 Empty 的 Cell 都不能再选择任何 NonEmpty Variant。
			if (ForcedWalkableCount == Settings.MaxWalkableCellCount)
			{
				for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
				{
					const FWfcDomain Domain = Domains[CellIndex];
					if ((Domain & EmptyVariantBit) != 0
						&& (Domain & NonEmptyVariantBits) != 0)
					{
						AddBan(CellIndex, NonEmptyVariantBits, Domains, Workspace);
					}
				}
			}

			// 只有全部 Possible Cell 都选 NonEmpty 才能达到下限，因此从 Maybe Cell 删除 Empty。
			if (PossibleWalkableCount == Settings.MinWalkableCellCount)
			{
				for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
				{
					const FWfcDomain Domain = Domains[CellIndex];
					if ((Domain & EmptyVariantBit) != 0
						&& (Domain & NonEmptyVariantBits) != 0)
					{
						AddBan(CellIndex, EmptyVariantBit, Domains, Workspace);
					}
				}
			}

			return true;
		}

		/**
		 * 在线性时间内检查一条行或列的连续轴向贯通上限。
		 *
		 * 对长度 Max+1 的每个滑动窗口维护 PossibleThrough 与 ForcedThrough 数量：
		 * 全部被迫贯通即矛盾；只有一个尚未被迫贯通则从该格 Ban 全部贯通 Variant。
		 * FlexibleCellIndexSum 仅在“恰好一个 Flexible”时读取，因此可以直接得到该格索引。
		 */
		bool EvaluateMaxConsecutiveLine(
			const int32 StartCellIndex,
			const int32 Stride,
			const int32 LineLength,
			const FWfcDomain ThroughVariantBits,
			const bool bHorizontal,
			const FIntPoint GridSize,
			const FZeroEscapeWfcSolveSettings& Settings,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure)
		{
			// 上限已覆盖整行/整列时无需构造 Max+1 窗口，也避免极端 int32 配置执行加一溢出。
			if (Settings.MaxConsecutiveStraightTiles >= LineLength)
			{
				return true;
			}
			const int32 WindowLength = Settings.MaxConsecutiveStraightTiles + 1;

			int32 PossibleThroughCount = 0;
			int32 ForcedThroughCount = 0;
			int64 FlexibleCellIndexSum = 0;

			/** 把一个 Cell 加入或移出当前窗口；Sign 只能为 +1 或 -1。 */
			const auto AccumulateCell =
				[&](const int32 CellIndex, const int32 Sign)
				{
					const FWfcDomain Domain = Domains[CellIndex];
					const bool bCanBeThrough = (Domain & ThroughVariantBits) != 0;
					const bool bMustBeThrough = bCanBeThrough
						&& (Domain & static_cast<FWfcDomain>(~ThroughVariantBits)) == 0;

					PossibleThroughCount += bCanBeThrough ? Sign : 0;
					ForcedThroughCount += bMustBeThrough ? Sign : 0;
					if (bCanBeThrough && !bMustBeThrough)
					{
						FlexibleCellIndexSum += Sign * CellIndex;
					}
				};

			for (int32 Offset = 0; Offset < WindowLength; ++Offset)
			{
				AccumulateCell(StartCellIndex + Offset * Stride, +1);
			}

			const int32 LastWindowStart = LineLength - WindowLength;
			for (int32 WindowStart = 0; WindowStart <= LastWindowStart; ++WindowStart)
			{
				if (PossibleThroughCount == WindowLength)
				{
					if (ForcedThroughCount == WindowLength)
					{
						const int32 FirstCellIndex = StartCellIndex + WindowStart * Stride;
						const FIntPoint Coordinate = ToCoordinate(FirstCellIndex, GridSize);

						OutFailure.Kind = EWfcConstraintContradiction::MaxConsecutive;
						OutFailure.CellIndex = FirstCellIndex;
						OutFailure.ObservedCount = WindowLength;
						OutFailure.Limit = Settings.MaxConsecutiveStraightTiles;
						OutFailure.Message = FString::Printf(
							TEXT(
								"WFC MaxConsecutive：从 Cell=(%d,%d) 开始的%s窗口"
								"已有 %d 个连续贯通格，超过上限=%d。"),
							Coordinate.X,
							Coordinate.Y,
							bHorizontal ? TEXT("水平") : TEXT("垂直"),
							WindowLength,
							Settings.MaxConsecutiveStraightTiles);
						return false;
					}

					if (ForcedThroughCount == WindowLength - 1)
					{
						// 此时恰好一个 Cell 同时允许贯通与非贯通，必须删除它的贯通候选。
						check(FlexibleCellIndexSum >= 0 && FlexibleCellIndexSum <= MAX_int32);
						AddBan(
							static_cast<int32>(FlexibleCellIndexSum),
							ThroughVariantBits,
							Domains,
							Workspace);
					}
				}

				if (WindowStart == LastWindowStart)
				{
					break;
				}

				const int32 LeavingCellIndex = StartCellIndex + WindowStart * Stride;
				const int32 EnteringCellIndex =
					StartCellIndex + (WindowStart + WindowLength) * Stride;
				AccumulateCell(LeavingCellIndex, -1);
				AccumulateCell(EnteringCellIndex, +1);
			}

			return true;
		}

		/** 按稳定的“全部行，再全部列”顺序执行水平和垂直连续贯通检查。 */
		bool EvaluateMaxConsecutive(
			const FIntPoint GridSize,
			const FZeroEscapeWfcSolveSettings& Settings,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure)
		{
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
			{
				if (!EvaluateMaxConsecutiveLine(
						Y * GridSize.X,
						1,
						GridSize.X,
						HorizontalThroughVariantBits,
						true,
						GridSize,
						Settings,
						Domains,
						Workspace,
						OutFailure))
				{
					return false;
				}
			}

			for (int32 X = 0; X < GridSize.X; ++X)
			{
				if (!EvaluateMaxConsecutiveLine(
						X,
						GridSize.X,
						GridSize.Y,
						VerticalThroughVariantBits,
						false,
						GridSize,
						Settings,
						Domains,
						Workspace,
						OutFailure))
				{
					return false;
				}
			}

			return true;
		}

		/**
		 * 在 expanded graph 上执行成熟 ConnectedConstraint 的三类传播：
		 *
		 * 1. Potential 节点表示“仍可能成为路径”，Relevant 节点表示“已经被迫成为路径”；
		 * 2. 两个 Potential 分量分别含 Relevant 时立即矛盾；完全不含 Relevant 的分量可以
		 *    永久删除，因为 Domain 只会继续收缩，它不可能在之后重新连接到必经路径；
		 * 3. 对唯一 Relevant 分量运行 Tarjan low-link。分隔 Relevant 节点的关节点必须保留：
		 *    中心关节点 Ban Empty，方向关节点 Ban 所有关闭该方向的 Variant。
		 *
		 * 图被展开为“每格一个中心 + 四个方向出口”，因此传播强度包含公共边方向，而不是
		 * 只证明某个 Cell 非空。实现使用 Workspace 的显式 DFS 栈，避免最大 5120 节点时
		 * 依赖平台调用栈，并复用全部数组容量。
		 */
		bool EvaluateConnected(
			const FIntPoint GridSize,
			const FZeroEscapeWfcSolveSettings& Settings,
			const TConstArrayView<FWfcDomain> Domains,
			FWfcConstraintWorkspace& Workspace,
			FWfcConstraintFailure& OutFailure)
		{
			const int32 StartCellIndex = ZeroEscape::Grid::ToIndex(
				Settings.StartCoordinate,
				GridSize);
			if (!CanBeWalkable(Domains[StartCellIndex]))
			{
				OutFailure.Kind = EWfcConstraintContradiction::Connected;
				OutFailure.CellIndex = StartCellIndex;
				OutFailure.Message = TEXT("WFC Connected：Start Cell 已不可能为非空。");
				return false;
			}

			const int32 ExpandedNodeCount = Domains.Num() * ConnectedNodesPerCell;
			check(Workspace.ConnectedPotentialNode.Num() == ExpandedNodeCount);
			check(Workspace.ConnectedRelevantNode.Num() == ExpandedNodeCount);

			// 先从 Domain 构造 Potential/Relevant 两种单调状态，不创建 UObject 或临时邻接表。
			for (int32 CellIndex = 0; CellIndex < Domains.Num(); ++CellIndex)
			{
				const FWfcDomain Domain = Domains[CellIndex];
				const int32 CenterNode = ToConnectedCenterNode(CellIndex);
				Workspace.ConnectedPotentialNode[CenterNode] = CanBeWalkable(Domain) ? 1 : 0;
				Workspace.ConnectedRelevantNode[CenterNode] = MustBeWalkable(Domain) ? 1 : 0;

				for (uint8 Direction = 0;
					Direction < ZeroEscape::Grid::DirectionCount;
					++Direction)
				{
					const int32 DirectionNode =
						ToConnectedDirectionNode(CellIndex, Direction);
					Workspace.ConnectedPotentialNode[DirectionNode] =
						CanOpenInDirection(Domain, Direction) ? 1 : 0;
					Workspace.ConnectedRelevantNode[DirectionNode] =
						MustOpenInDirection(Domain, Direction) ? 1 : 0;
				}
			}

			int32 RelevantComponentIndex = INDEX_NONE;
			int32 RelevantRootNode = INDEX_NONE;
			int32 ComponentIndex = 0;

			// 枚举全部 Potential 分量；节点索引和邻居次序都固定，Seed 不会影响传播结果。
			for (int32 FirstNode = 0; FirstNode < ExpandedNodeCount; ++FirstNode)
			{
				if (Workspace.ConnectedPotentialNode[FirstNode] == 0
					|| Workspace.ConnectedComponentByNode[FirstNode] != INDEX_NONE)
				{
					continue;
				}

				Workspace.ConnectedQueue.Reset();
				Workspace.ConnectedQueue.Add(FirstNode);
				Workspace.ConnectedComponentByNode[FirstNode] = ComponentIndex;
				int32 FirstRelevantNode = INDEX_NONE;

				for (int32 QueueHead = 0;
					QueueHead < Workspace.ConnectedQueue.Num();
					++QueueHead)
				{
					const int32 NodeIndex = Workspace.ConnectedQueue[QueueHead];
					if (Workspace.ConnectedRelevantNode[NodeIndex] != 0
						&& FirstRelevantNode == INDEX_NONE)
					{
						FirstRelevantNode = NodeIndex;
					}

					const uint8 NeighborCount =
						ConnectedNodeSlot(NodeIndex) == ConnectedCenterSlot
							? ZeroEscape::Grid::DirectionCount
							: 2;
					for (uint8 NeighborOrdinal = 0;
						NeighborOrdinal < NeighborCount;
						++NeighborOrdinal)
					{
						int32 NeighborNode = INDEX_NONE;
						if (!TryGetConnectedNeighbor(
								NodeIndex,
								NeighborOrdinal,
								GridSize,
								NeighborNode)
							|| Workspace.ConnectedPotentialNode[NeighborNode] == 0
							|| Workspace.ConnectedComponentByNode[NeighborNode] != INDEX_NONE)
						{
							continue;
						}

						Workspace.ConnectedComponentByNode[NeighborNode] = ComponentIndex;
						Workspace.ConnectedQueue.Add(NeighborNode);
					}
				}

				if (FirstRelevantNode != INDEX_NONE)
				{
					if (RelevantComponentIndex != INDEX_NONE)
					{
						const int32 CellIndex = ConnectedNodeToCell(FirstRelevantNode);
						const FIntPoint Coordinate = ToCoordinate(CellIndex, GridSize);
						OutFailure.Kind = EWfcConstraintContradiction::Connected;
						OutFailure.CellIndex = CellIndex;
						OutFailure.Message = FString::Printf(
							TEXT(
								"WFC Connected：被迫路径节点位于多个互不相连的可能分量；"
								"第二个分量包含 Cell=(%d,%d)。"),
							Coordinate.X,
							Coordinate.Y);
						return false;
					}

					RelevantComponentIndex = ComponentIndex;
					RelevantRootNode = FirstRelevantNode;
				}
				else
				{
					// 该分量完全没有被迫路径节点；当前图只会收缩，因此可以安全删除整分量。
					for (const int32 NodeIndex : Workspace.ConnectedQueue)
					{
						const int32 CellIndex = ConnectedNodeToCell(NodeIndex);
						const uint8 Slot = ConnectedNodeSlot(NodeIndex);
						if (Slot == ConnectedCenterSlot)
						{
							AddBan(CellIndex, NonEmptyVariantBits, Domains, Workspace);
						}
						else
						{
							AddBan(
								CellIndex,
								OpenVariantBitsByDirection[Slot - 1],
								Domains,
								Workspace);
						}
					}
				}

				++ComponentIndex;
			}

			if (RelevantRootNode == INDEX_NONE)
			{
				// 正常求解中 Start 是 Required，因此不会走到这里；保留纯值约束的完备边界。
				return true;
			}

			const int32 StartCenterNode = ToConnectedCenterNode(StartCellIndex);
			if (Workspace.ConnectedRelevantNode[StartCenterNode] != 0
				&& Workspace.ConnectedComponentByNode[StartCenterNode] == RelevantComponentIndex)
			{
				// 从强制 Start 中心开始可让诊断和遍历顺序更直观，但不改变关节点集合。
				RelevantRootNode = StartCenterNode;
			}

			int32 DiscoveryCounter = 0;
			const auto DiscoverNode =
				[&](const int32 NodeIndex, const int32 ParentNode)
				{
					Workspace.ConnectedParent[NodeIndex] = ParentNode;
					Workspace.ConnectedDiscoveryIndex[NodeIndex] = ++DiscoveryCounter;
					Workspace.ConnectedLowLink[NodeIndex] = DiscoveryCounter;
					Workspace.ConnectedRelevantSubtreeCount[NodeIndex] =
						Workspace.ConnectedRelevantNode[NodeIndex] != 0 ? 1 : 0;
					Workspace.ConnectedRelevantChildSubtreeCount[NodeIndex] = 0;
					Workspace.ConnectedNextNeighborOrdinal[NodeIndex] = 0;
					Workspace.ConnectedDfsStack.Add(NodeIndex);
				};

			Workspace.ConnectedDfsStack.Reset();
			DiscoverNode(RelevantRootNode, INDEX_NONE);

			// 迭代版 Tarjan DFS：离开子节点时把 low-link 和 Relevant 子树信息合并到父节点。
			while (!Workspace.ConnectedDfsStack.IsEmpty())
			{
				const int32 NodeIndex = Workspace.ConnectedDfsStack.Last();
				const uint8 NeighborCount =
					ConnectedNodeSlot(NodeIndex) == ConnectedCenterSlot
						? ZeroEscape::Grid::DirectionCount
						: 2;
				bool bDescended = false;

				while (Workspace.ConnectedNextNeighborOrdinal[NodeIndex] < NeighborCount)
				{
					const uint8 NeighborOrdinal =
						Workspace.ConnectedNextNeighborOrdinal[NodeIndex]++;
					int32 NeighborNode = INDEX_NONE;
					if (!TryGetConnectedNeighbor(
							NodeIndex,
							NeighborOrdinal,
							GridSize,
							NeighborNode)
						|| Workspace.ConnectedPotentialNode[NeighborNode] == 0
						|| Workspace.ConnectedComponentByNode[NeighborNode]
							!= RelevantComponentIndex)
					{
						continue;
					}

					if (Workspace.ConnectedDiscoveryIndex[NeighborNode] == 0)
					{
						DiscoverNode(NeighborNode, NodeIndex);
						bDescended = true;
						break;
					}

					if (NeighborNode != Workspace.ConnectedParent[NodeIndex])
					{
						Workspace.ConnectedLowLink[NodeIndex] = FMath::Min(
							Workspace.ConnectedLowLink[NodeIndex],
							Workspace.ConnectedDiscoveryIndex[NeighborNode]);
					}
				}

				if (bDescended)
				{
					continue;
				}

				Workspace.ConnectedDfsStack.Pop(EAllowShrinking::No);
				const int32 ParentNode = Workspace.ConnectedParent[NodeIndex];
				if (ParentNode == INDEX_NONE)
				{
					// 根只有在至少两个不同 DFS 子树含 Relevant 时才是连接 Relevant 的关节点。
					Workspace.ConnectedArticulationNode[NodeIndex] =
						Workspace.ConnectedRelevantChildSubtreeCount[NodeIndex] > 1 ? 1 : 0;
					continue;
				}

				const int32 RelevantInChildSubtree =
					Workspace.ConnectedRelevantSubtreeCount[NodeIndex];
				if (RelevantInChildSubtree > 0)
				{
					++Workspace.ConnectedRelevantChildSubtreeCount[ParentNode];
					if (Workspace.ConnectedLowLink[NodeIndex]
						>= Workspace.ConnectedDiscoveryIndex[ParentNode])
					{
						// 根会在自身完成时按“Relevant 子树数 > 1”覆盖这个暂存值。
						Workspace.ConnectedArticulationNode[ParentNode] = 1;
					}
				}

				Workspace.ConnectedRelevantSubtreeCount[ParentNode] +=
					RelevantInChildSubtree;
				Workspace.ConnectedLowLink[ParentNode] = FMath::Min(
					Workspace.ConnectedLowLink[ParentNode],
					Workspace.ConnectedLowLink[NodeIndex]);
			}

			// 把“连接所有 Relevant 所必需”的 expanded graph 关节点映射回稳定 Domain Ban。
			for (int32 NodeIndex = 0; NodeIndex < ExpandedNodeCount; ++NodeIndex)
			{
				if (Workspace.ConnectedArticulationNode[NodeIndex] == 0)
				{
					continue;
				}

				const int32 CellIndex = ConnectedNodeToCell(NodeIndex);
				const uint8 Slot = ConnectedNodeSlot(NodeIndex);
				if (Slot == ConnectedCenterSlot)
				{
					AddBan(CellIndex, EmptyVariantBit, Domains, Workspace);
				}
				else
				{
					const FWfcDomain ClosedDirectionBits = static_cast<FWfcDomain>(
						AllVariantBits & ~OpenVariantBitsByDirection[Slot - 1]);
					AddBan(CellIndex, ClosedDirectionBits, Domains, Workspace);
				}
			}

			return true;
		}
	}

	void FWfcConstraintFailure::Reset()
	{
		Kind = EWfcConstraintContradiction::None;
		CellIndex = INDEX_NONE;
		ObservedCount = 0;
		Limit = 0;
		Message.Reset();
	}

	void FWfcConstraintWorkspace::PrepareForPass(const int32 CellCount)
	{
		check(CellCount > 0);
		check(CellCount <= MAX_int32 / WfcConstraintsPrivate::ConnectedNodesPerCell);
		const int32 ExpandedNodeCount =
			CellCount * WfcConstraintsPrivate::ConnectedNodesPerCell;

		BanMaskByCell.Init(0, CellCount);

		ConnectedPotentialNode.Init(0, ExpandedNodeCount);
		ConnectedRelevantNode.Init(0, ExpandedNodeCount);
		ConnectedArticulationNode.Init(0, ExpandedNodeCount);

		ConnectedComponentByNode.Init(INDEX_NONE, ExpandedNodeCount);
		ConnectedDiscoveryIndex.Init(0, ExpandedNodeCount);
		ConnectedLowLink.Init(0, ExpandedNodeCount);
		ConnectedParent.Init(INDEX_NONE, ExpandedNodeCount);
		ConnectedRelevantSubtreeCount.Init(0, ExpandedNodeCount);
		ConnectedRelevantChildSubtreeCount.Init(0, ExpandedNodeCount);
		ConnectedNextNeighborOrdinal.Init(0, ExpandedNodeCount);

		// Reset 保留前一稳定点申请的容量；Reserve 只在网格首次变大时发生分配。
		ConnectedQueue.Reset(ExpandedNodeCount);
		ConnectedDfsStack.Reset(ExpandedNodeCount);
		BanCellCount = 0;
	}

	bool FWfcConstraints::ValidateSolveSettings(
		const FIntPoint GridSize,
		const FZeroEscapeWfcSolveSettings& Settings,
		FString& OutError)
	{
		OutError.Reset();

		const int64 CellCount64 =
			static_cast<int64>(GridSize.X) * static_cast<int64>(GridSize.Y);
		if (GridSize.X <= 0
			|| GridSize.Y <= 0
			|| CellCount64 <= 0
			|| CellCount64 > MAX_int32 / WfcConstraintsPrivate::ConnectedNodesPerCell)
		{
			OutError = FString::Printf(
				TEXT("WFC Solve Settings 的 GridSize=(%d,%d) 非法或无法安全展开 Connected 图。"),
				GridSize.X,
				GridSize.Y);
			return false;
		}

		if (!ZeroEscape::Grid::IsInside(Settings.StartCoordinate, GridSize))
		{
			OutError = FString::Printf(
				TEXT("WFC Connected 的 Start=(%d,%d) 超出 GridSize=(%d,%d)。"),
				Settings.StartCoordinate.X,
				Settings.StartCoordinate.Y,
				GridSize.X,
				GridSize.Y);
			return false;
		}

		const int32 CellCount = static_cast<int32>(CellCount64);
		if (Settings.MinWalkableCellCount <= 0
			|| Settings.MinWalkableCellCount > Settings.MaxWalkableCellCount
			|| Settings.MaxWalkableCellCount > CellCount)
		{
			OutError = FString::Printf(
				TEXT(
					"WFC Count 必须满足 0 < Min <= Max <= CellCount；"
					"当前 Min=%d Max=%d CellCount=%d。"),
				Settings.MinWalkableCellCount,
				Settings.MaxWalkableCellCount,
				CellCount);
			return false;
		}

		const int32 MaxAxisLength = FMath::Max(GridSize.X, GridSize.Y);
		if (Settings.MaxConsecutiveStraightTiles <= 0
			|| Settings.MaxConsecutiveStraightTiles > MaxAxisLength)
		{
			OutError = FString::Printf(
				TEXT(
					"WFC MaxConsecutiveStraightTiles 必须位于 [1,%d]；当前为 %d。"),
				MaxAxisLength,
				Settings.MaxConsecutiveStraightTiles);
			return false;
		}
		if (Settings.PreferredMaxConsecutiveStraightTiles < 0)
		{
			OutError = TEXT("WFC PreferredMaxConsecutiveStraightTiles 不能小于 0。");
			return false;
		}

		if (Settings.MaxCandidateAttempts <= 0 || Settings.MaxBacktrackCount <= 0)
		{
			OutError = FString::Printf(
				TEXT(
					"WFC 求解预算必须为正；当前 CandidateAttempts=%d Backtracks=%d。"),
				Settings.MaxCandidateAttempts,
				Settings.MaxBacktrackCount);
			return false;
		}

		return true;
	}

	bool FWfcConstraints::Evaluate(
		const FIntPoint GridSize,
		const FZeroEscapeWfcSolveSettings& Settings,
		const TConstArrayView<FWfcDomain> Domains,
		FWfcConstraintWorkspace& Workspace,
		FWfcConstraintFailure& OutFailure)
	{
		const int64 ExpectedCellCount =
			static_cast<int64>(GridSize.X) * static_cast<int64>(GridSize.Y);
		check(ExpectedCellCount == Domains.Num());

		Workspace.PrepareForPass(Domains.Num());
		OutFailure.Reset();

		// 固定顺序属于确定性契约；不得改成依赖注册表或不稳定容器的遍历顺序。
		if (!WfcConstraintsPrivate::EvaluateCount(
				Settings,
				Domains,
				Workspace,
				OutFailure))
		{
			return false;
		}

		if (!WfcConstraintsPrivate::EvaluateMaxConsecutive(
				GridSize,
				Settings,
				Domains,
				Workspace,
				OutFailure))
		{
			return false;
		}

		if (!WfcConstraintsPrivate::EvaluateConnected(
				GridSize,
				Settings,
				Domains,
				Workspace,
				OutFailure))
		{
			return false;
		}

		return true;
	}
}
