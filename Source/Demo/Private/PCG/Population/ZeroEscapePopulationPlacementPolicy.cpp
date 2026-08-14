// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePopulationPlacementPolicy.cpp
 * 职责：在完整三维通行图上枚举合法候选，执行机关上下文加权与资源支持加权规划。
 * 边界：保持纯值、确定性和原子输出；资产加载、世界变换与 Spawn 留给 Populator。
 */

#include "PCG/Population/ZeroEscapePopulationPlacementPolicy.h"

#include "Algo/Sort.h"
#include "Containers/Queue.h"
#include "PCG/ZeroEscapeGenerationCore.h"

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		constexpr int32 UnreachableDistance = MAX_int32 / 4;

		struct FTraversalGraph
		{
			TArray<FIntVector> Addresses;
			TMap<FIntVector, int32> NodeByAddress;
			TArray<TArray<int32>> Neighbors;
		};

		struct FPlacementVariant
		{
			FTransform LocalSpawnTransform = FTransform::Identity;
			FVector SpikeLateralAxis = FVector::ZeroVector;
			TArray<FIntVector> ResourceBlockedAddresses;
			FPopulationSpikeWheelSpawnConfig SpikeWheel;
			float PositionLog2Contribution = 0.0f;
		};

		struct FPlacementCandidate
		{
			EPopulationPlacementKind Kind = EPopulationPlacementKind::SpikeTrap;
			FIntVector AnchorAddress = FIntVector::ZeroValue;
			TArray<FPlacementVariant> Variants;
		};

		struct FScoredVariant
		{
			int32 VariantIndex = INDEX_NONE;
			FPopulationPlacementScoreBreakdown Score;
			double Weight = 0.0;
		};

		struct FScoredAnchor
		{
			int32 CandidateIndex = INDEX_NONE;
			double BestLog2Score = 0.0;
			double Weight = 0.0;
			TArray<FScoredVariant> Variants;
		};

		struct FScoredKindPool
		{
			EPopulationPlacementKind Kind = EPopulationPlacementKind::SpikeTrap;
			double TypeWeight = 0.0;
			const TArray<FPlacementCandidate>* Candidates = nullptr;
			TArray<FScoredAnchor> Anchors;
		};

		struct FAcceptedHazardState
		{
			EPopulationPlacementKind Kind = EPopulationPlacementKind::SpikeTrap;
			FIntVector AnchorAddress = FIntVector::ZeroValue;
			TArray<int32> OperationNodeIndices;
			int32 GroupId = INDEX_NONE;
			float BasePressure = 0.0f;
			float Progress01 = 0.0f;
		};

		struct FWorkingHazardGroup
		{
			bool bActive = true;
			FIntVector AnchorAddress = FIntVector::ZeroValue;
			TArray<int32> PlacementIndices;
			float ActualPressure = 0.0f;
			float ProgressSum = 0.0f;
		};

		struct FNearbyHazard
		{
			int32 PlacementIndex = INDEX_NONE;
			int32 Distance = UnreachableDistance;
		};

		struct FLocalGraphScratch
		{
			TArray<int32> NodeVisitEpoch;
			TArray<int32> NodeDistance;
			TArray<int32> AcceptedVisitEpoch;
			TArray<int32> AcceptedDistance;
			TArray<int32> Queue;
			int32 Epoch = 0;
		};

		struct FHazardPlanningState
		{
			TArray<FAcceptedHazardState> AcceptedHazards;
			TArray<FWorkingHazardGroup> Groups;
			TArray<TArray<int32>> AcceptedPlacementIndicesByNode;
			TArray<int32> NearestHazardDistances;
			TArray<int32> DistanceFromPlayer;
			TArray<int32> DistanceToExit;
			TArray<EPopulationPlacementKind> RecentOrdinaryKinds;
			FLocalGraphScratch LocalGraphScratch;
		};

		struct FHazardCandidateEvaluation
		{
			FPopulationPlacementScoreBreakdown Score;
			TArray<int32> NearbyGroupIds;
			TArray<int32> AdjacentPlacementIndices;
			FIntVector ProposedGroupAnchor = FIntVector::ZeroValue;
			float CandidateProgress01 = 0.0f;
			float GroupProgress01 = 0.0f;
			float TargetPressure = 0.0f;
			float PressureAfterPlacement = 0.0f;
			float AddedCombinationPressure = 0.0f;
		};

		bool CoordinateLess(const FIntVector& A, const FIntVector& B)
		{
			return A.Z != B.Z ? A.Z < B.Z
				: A.Y != B.Y ? A.Y < B.Y
				: A.X < B.X;
		}

		bool IsInsidePlan(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntVector Address)
		{
			return Address.Z >= 0 && Address.Z < Plan.FloorCount
				&& Grid::IsInside(FIntPoint(Address.X, Address.Y), Plan.GridSize);
		}

		bool IsFlowCoordinate(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntVector Address)
		{
			return Address == Plan.PlayerSpawnCoordinate
				|| Address == Plan.PursuerSpawnCoordinate
				|| Address == Plan.ExitCoordinate;
		}

		FIntVector StepAddress(const FIntVector Address, const uint8 Direction)
		{
			const FIntPoint Stepped = Grid::Step(FIntPoint(Address.X, Address.Y), Direction);
			return FIntVector(Stepped.X, Stepped.Y, Address.Z);
		}

		FVector DirectionVector(const uint8 Direction)
		{
			switch (Direction)
			{
			case 0: return FVector(0.0, 1.0, 0.0);
			case 1: return FVector(1.0, 0.0, 0.0);
			case 2: return FVector(0.0, -1.0, 0.0);
			default: return FVector(-1.0, 0.0, 0.0);
			}
		}

		FVector AddressFloorLocation(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntVector Address,
			const double FloorTopZCm)
		{
			return FVector(
				static_cast<double>(Address.X) * Plan.LogicalTileSizeCm,
				static_cast<double>(Address.Y) * Plan.LogicalTileSizeCm,
				FloorTopZCm + static_cast<double>(Address.Z) * Plan.FloorHeightCm);
		}

		bool IsOrdinaryNeighborOpen(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const TMap<FIntVector, const FZeroEscapeGeneratedOrdinaryCell*>& OrdinaryByAddress,
			const FZeroEscapeGeneratedOrdinaryCell& Cell,
			const uint8 Direction)
		{
			if ((Cell.OpeningMask & Grid::DirectionBit(Direction)) == 0)
			{
				return false;
			}
			const FIntVector NeighborAddress = StepAddress(Cell.Coordinate, Direction);
			const FZeroEscapeGeneratedOrdinaryCell* const* Neighbor =
				OrdinaryByAddress.Find(NeighborAddress);
			return Neighbor != nullptr
				&& ((*Neighbor)->OpeningMask
					& Grid::DirectionBit(Grid::OppositeDirectionIndex(Direction))) != 0
				&& IsInsidePlan(Plan, NeighborAddress);
		}

		bool AddUndirectedEdge(
			FTraversalGraph& Graph,
			const FIntVector First,
			const FIntVector Second,
			FString& OutError)
		{
			const int32* FirstNode = Graph.NodeByAddress.Find(First);
			const int32* SecondNode = Graph.NodeByAddress.Find(Second);
			if (FirstNode == nullptr || SecondNode == nullptr || *FirstNode == *SecondNode)
			{
				OutError = TEXT("通行边端点缺失或形成自环。");
				return false;
			}
			Graph.Neighbors[*FirstNode].AddUnique(*SecondNode);
			Graph.Neighbors[*SecondNode].AddUnique(*FirstNode);
			return true;
		}

		bool BuildTraversalGraph(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			FTraversalGraph& OutGraph,
			FString& OutError)
		{
			OutGraph = FTraversalGraph();
			TSet<FIntVector> AddressSet;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				if (!IsInsidePlan(Plan, Cell.Coordinate)
					|| AddressSet.Contains(Cell.Coordinate))
				{
					OutError = TEXT("普通格地址越界或重复。");
					return false;
				}
				AddressSet.Add(Cell.Coordinate);
			}
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				for (const FIntVector Address : Structure.WalkableCells)
				{
					if (!IsInsidePlan(Plan, Address) || AddressSet.Contains(Address))
					{
						OutError = TEXT("结构可走格地址越界或与已有格重复。");
						return false;
					}
					AddressSet.Add(Address);
				}
			}

			OutGraph.Addresses = AddressSet.Array();
			OutGraph.Addresses.Sort(CoordinateLess);
			OutGraph.Neighbors.SetNum(OutGraph.Addresses.Num());
			for (int32 Index = 0; Index < OutGraph.Addresses.Num(); ++Index)
			{
				OutGraph.NodeByAddress.Add(OutGraph.Addresses[Index], Index);
			}

			TMap<FIntVector, const FZeroEscapeGeneratedOrdinaryCell*> OrdinaryByAddress;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				OrdinaryByAddress.Add(Cell.Coordinate, &Cell);
			}
			TMap<FIntVector, TSet<FIntVector>> StructureNeighborsByOrdinary;
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				for (const FZeroEscapeGeneratedStructureOpening& Opening : Structure.Openings)
				{
					StructureNeighborsByOrdinary.FindOrAdd(
						Opening.ConnectedOrdinaryCoordinate).Add(Opening.StructureCoordinate);
				}
			}
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if ((Cell.OpeningMask & Grid::DirectionBit(Direction)) == 0)
					{
						continue;
					}
					const FIntVector NeighborAddress = StepAddress(Cell.Coordinate, Direction);
					const FZeroEscapeGeneratedOrdinaryCell* const* Neighbor =
						OrdinaryByAddress.Find(NeighborAddress);
					if (Neighbor == nullptr)
					{
						const TSet<FIntVector>* StructureNeighbors =
							StructureNeighborsByOrdinary.Find(Cell.Coordinate);
						if (StructureNeighbors != nullptr
							&& StructureNeighbors->Contains(NeighborAddress))
						{
							continue;
						}
						OutError = TEXT("普通格开口没有对应普通格或结构 Opening。");
						return false;
					}
					if (((*Neighbor)->OpeningMask
						& Grid::DirectionBit(Grid::OppositeDirectionIndex(Direction))) == 0)
					{
						OutError = TEXT("普通格开口缺少相邻普通格的反向开口。");
						return false;
					}
					if (CoordinateLess(Cell.Coordinate, NeighborAddress)
						&& !AddUndirectedEdge(OutGraph, Cell.Coordinate, NeighborAddress, OutError))
					{
						return false;
					}
				}
			}
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				for (const FZeroEscapeGeneratedCellConnection& Connection :
					Structure.InternalConnections)
				{
					if (!AddUndirectedEdge(
							OutGraph,
							Connection.FirstCoordinate,
							Connection.SecondCoordinate,
							OutError))
					{
						return false;
					}
				}
				for (const FZeroEscapeGeneratedStructureOpening& Opening : Structure.Openings)
				{
					const FZeroEscapeGeneratedOrdinaryCell* const* Ordinary =
						OrdinaryByAddress.Find(Opening.ConnectedOrdinaryCoordinate);
					uint8 DirectionFromOrdinary = Grid::DirectionCount;
					for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
					{
						if (StepAddress(Opening.ConnectedOrdinaryCoordinate, Direction)
							== Opening.StructureCoordinate)
						{
							DirectionFromOrdinary = Direction;
							break;
						}
					}
					if (Ordinary == nullptr
						|| DirectionFromOrdinary >= Grid::DirectionCount
						|| (((*Ordinary)->OpeningMask
							& Grid::DirectionBit(DirectionFromOrdinary)) == 0)
						|| !AddUndirectedEdge(
							OutGraph,
							Opening.StructureCoordinate,
							Opening.ConnectedOrdinaryCoordinate,
							OutError))
					{
						OutError = TEXT("结构 Opening 与普通格开口不互相匹配。");
						return false;
					}
				}
			}
			for (TArray<int32>& NeighborList : OutGraph.Neighbors)
			{
				NeighborList.Sort();
			}

			if (OutGraph.Addresses.IsEmpty())
			{
				OutError = TEXT("通行图为空。");
				return false;
			}
			TArray<bool> Visited;
			Visited.Init(false, OutGraph.Addresses.Num());
			TQueue<int32> Queue;
			Visited[0] = true;
			Queue.Enqueue(0);
			int32 Node = INDEX_NONE;
			while (Queue.Dequeue(Node))
			{
				for (const int32 Neighbor : OutGraph.Neighbors[Node])
				{
					if (!Visited[Neighbor])
					{
						Visited[Neighbor] = true;
						Queue.Enqueue(Neighbor);
					}
				}
			}
			if (Visited.Contains(false))
			{
				OutError = TEXT("整栋通行图不连通。");
				return false;
			}
			return true;
		}

		void UpdateNearestDistances(
			const FTraversalGraph& Graph,
			const TConstArrayView<int32> SourceNodes,
			TArray<int32>& InOutNearestDistances)
		{
			TQueue<int32> Queue;
			for (const int32 SourceNode : SourceNodes)
			{
				if (InOutNearestDistances.IsValidIndex(SourceNode)
					&& InOutNearestDistances[SourceNode] > 0)
				{
					InOutNearestDistances[SourceNode] = 0;
					Queue.Enqueue(SourceNode);
				}
			}
			int32 Node = INDEX_NONE;
			while (Queue.Dequeue(Node))
			{
				const int32 NextDistance = InOutNearestDistances[Node] + 1;
				for (const int32 Neighbor : Graph.Neighbors[Node])
				{
					if (NextDistance < InOutNearestDistances[Neighbor])
					{
						InOutNearestDistances[Neighbor] = NextDistance;
						Queue.Enqueue(Neighbor);
					}
				}
			}
		}

		bool BuildGraphDistances(
			const FTraversalGraph& Graph,
			const FIntVector SourceAddress,
			TArray<int32>& OutDistances)
		{
			const int32* SourceNode = Graph.NodeByAddress.Find(SourceAddress);
			if (SourceNode == nullptr)
			{
				return false;
			}
			OutDistances.Init(UnreachableDistance, Graph.Addresses.Num());
			TQueue<int32> Queue;
			OutDistances[*SourceNode] = 0;
			Queue.Enqueue(*SourceNode);
			int32 Node = INDEX_NONE;
			while (Queue.Dequeue(Node))
			{
				const int32 NextDistance = OutDistances[Node] + 1;
				for (const int32 Neighbor : Graph.Neighbors[Node])
				{
					if (NextDistance < OutDistances[Neighbor])
					{
						OutDistances[Neighbor] = NextDistance;
						Queue.Enqueue(Neighbor);
					}
				}
			}
			return !OutDistances.Contains(UnreachableDistance);
		}

		uint32 MakeStablePlacementHash(
			const int32 PublicSeed,
			const FIntVector Address,
			const int32 VariantKey,
			const uint32 Salt)
		{
			uint32 Hash = HashCombineFast(GetTypeHash(PublicSeed), Salt);
			Hash = HashCombineFast(Hash, GetTypeHash(Address.X));
			Hash = HashCombineFast(Hash, GetTypeHash(Address.Y));
			Hash = HashCombineFast(Hash, GetTypeHash(Address.Z));
			return HashCombineFast(Hash, GetTypeHash(VariantKey));
		}

		float StablePhase01(const uint32 Hash)
		{
			// 24 位尾数保证转换成 float 后仍严格小于 1。
			return static_cast<float>(Hash >> 8) * (1.0f / 16777216.0f);
		}

		float StableSignedUnit(const uint32 Hash)
		{
			return StablePhase01(Hash) * 2.0f - 1.0f;
		}

		bool BuildOperationNodeIndices(
			const FTraversalGraph& Graph,
			const FPlacementVariant& Variant,
			TArray<int32>& OutNodeIndices)
		{
			OutNodeIndices.Reset();
			for (const FIntVector Address : Variant.ResourceBlockedAddresses)
			{
				const int32* Node = Graph.NodeByAddress.Find(Address);
				if (Node == nullptr)
				{
					return false;
				}
				OutNodeIndices.AddUnique(*Node);
			}
			OutNodeIndices.Sort();
			return !OutNodeIndices.IsEmpty();
		}

		void FindNearbyAcceptedHazards(
			const FTraversalGraph& Graph,
			const TConstArrayView<int32> SourceNodes,
			const int32 MaximumDistance,
			FHazardPlanningState& State,
			TArray<FNearbyHazard>& OutNearby)
		{
			OutNearby.Reset();
			FLocalGraphScratch& Scratch = State.LocalGraphScratch;
			if (Scratch.NodeVisitEpoch.Num() != Graph.Addresses.Num())
			{
				Scratch.NodeVisitEpoch.Init(0, Graph.Addresses.Num());
				Scratch.NodeDistance.Init(0, Graph.Addresses.Num());
			}
			if (Scratch.AcceptedVisitEpoch.Num() < State.AcceptedHazards.Num())
			{
				Scratch.AcceptedVisitEpoch.SetNumZeroed(State.AcceptedHazards.Num());
				Scratch.AcceptedDistance.SetNumZeroed(State.AcceptedHazards.Num());
			}
			if (Scratch.Epoch == MAX_int32)
			{
				Scratch.NodeVisitEpoch.Init(0, Graph.Addresses.Num());
				Scratch.AcceptedVisitEpoch.Init(0, State.AcceptedHazards.Num());
				Scratch.Epoch = 1;
			}
			else
			{
				++Scratch.Epoch;
			}

			Scratch.Queue.Reset();
			for (const int32 SourceNode : SourceNodes)
			{
				if (Graph.Addresses.IsValidIndex(SourceNode)
					&& Scratch.NodeVisitEpoch[SourceNode] != Scratch.Epoch)
				{
					Scratch.NodeVisitEpoch[SourceNode] = Scratch.Epoch;
					Scratch.NodeDistance[SourceNode] = 0;
					Scratch.Queue.Add(SourceNode);
				}
			}

			for (int32 QueueIndex = 0; QueueIndex < Scratch.Queue.Num(); ++QueueIndex)
			{
				const int32 Node = Scratch.Queue[QueueIndex];
				const int32 Distance = Scratch.NodeDistance[Node];
				for (const int32 PlacementIndex :
					State.AcceptedPlacementIndicesByNode[Node])
				{
					if (Scratch.AcceptedVisitEpoch[PlacementIndex] != Scratch.Epoch)
					{
						Scratch.AcceptedVisitEpoch[PlacementIndex] = Scratch.Epoch;
						Scratch.AcceptedDistance[PlacementIndex] = Distance;
					}
				}
				if (Distance >= MaximumDistance)
				{
					continue;
				}
				for (const int32 Neighbor : Graph.Neighbors[Node])
				{
					if (Scratch.NodeVisitEpoch[Neighbor] == Scratch.Epoch)
					{
						continue;
					}
					Scratch.NodeVisitEpoch[Neighbor] = Scratch.Epoch;
					Scratch.NodeDistance[Neighbor] = Distance + 1;
					Scratch.Queue.Add(Neighbor);
				}
			}

			for (int32 PlacementIndex = 0;
				PlacementIndex < State.AcceptedHazards.Num();
				++PlacementIndex)
			{
				if (Scratch.AcceptedVisitEpoch[PlacementIndex] == Scratch.Epoch)
				{
					FNearbyHazard& Nearby = OutNearby.AddDefaulted_GetRef();
					Nearby.PlacementIndex = PlacementIndex;
					Nearby.Distance = Scratch.AcceptedDistance[PlacementIndex];
				}
			}
		}

		const FZeroEscapeHazardRiskTuning* FindRiskTuning(
			const EPopulationPlacementKind Kind,
			const FZeroEscapeHazardPlacementScoringTuning& Scoring)
		{
			switch (Kind)
			{
			case EPopulationPlacementKind::Pendulum: return &Scoring.PendulumRisk;
			case EPopulationPlacementKind::SpikeTrap: return &Scoring.SpikeRisk;
			case EPopulationPlacementKind::BatteringRam: return &Scoring.RamRisk;
			case EPopulationPlacementKind::GuidedLauncher: return &Scoring.LauncherRisk;
			case EPopulationPlacementKind::SpikeWheel: return &Scoring.WheelRisk;
			case EPopulationPlacementKind::MagneticResource: return nullptr;
			}
			return nullptr;
		}

		float BasePressureForKind(
			const EPopulationPlacementKind Kind,
			const FZeroEscapeHazardPlacementScoringTuning& Scoring,
			const float RepresentativeTraversalSeconds)
		{
			const FZeroEscapeHazardRiskTuning* Risk = FindRiskTuning(Kind, Scoring);
			return Risk == nullptr
				? -1.0f
				: Risk->TraversalPressurePerSecond * RepresentativeTraversalSeconds
					+ Risk->HitConsequencePressure;
		}

		bool IsWheelRamPair(
			const EPopulationPlacementKind First,
			const EPopulationPlacementKind Second)
		{
			return (First == EPopulationPlacementKind::SpikeWheel
					&& Second == EPopulationPlacementKind::BatteringRam)
				|| (First == EPopulationPlacementKind::BatteringRam
					&& Second == EPopulationPlacementKind::SpikeWheel);
		}

		bool IsWheelSpikePair(
			const EPopulationPlacementKind First,
			const EPopulationPlacementKind Second)
		{
			return (First == EPopulationPlacementKind::SpikeWheel
					&& Second == EPopulationPlacementKind::SpikeTrap)
				|| (First == EPopulationPlacementKind::SpikeTrap
					&& Second == EPopulationPlacementKind::SpikeWheel);
		}

		float CalculateGroupTargetPressure(
			const FZeroEscapeHazardPopulationTuning& HazardTuning,
			const FZeroEscapeHazardPlacementScoringTuning& Scoring,
			const int32 PublicSeed,
			const FIntVector GroupAnchor,
			const float GroupProgress01)
		{
			const float ClampedProgress = FMath::Clamp(GroupProgress01, 0.0f, 1.0f);
			const float BaseTarget = FMath::Lerp(
				HazardTuning.TargetPressureAtStart,
				HazardTuning.TargetPressureAtExit,
				ClampedProgress);
			const int32 RegionBand = FMath::Clamp(
				FMath::FloorToInt(ClampedProgress * 4.0f), 0, 3);
			const FIntVector RegionKey(RegionBand, 0, GroupAnchor.Z);
			const float RegionVariation = StableSignedUnit(MakeStablePlacementHash(
				PublicSeed, RegionKey, RegionBand, 0x68BC21EBu))
				* Scoring.RegionTargetVariation;
			const float AnchorVariation = StableSignedUnit(MakeStablePlacementHash(
				PublicSeed, GroupAnchor, 0, 0xB5297A4Du))
				* Scoring.AnchorTargetVariation;
			return FMath::Max(0.25f, BaseTarget + RegionVariation + AnchorVariation);
		}

		template <typename ItemType, typename WeightFunction>
		int32 PickWeightedIndex(
			const TArray<ItemType>& Items,
			WeightFunction&& GetWeight,
			FRandomStream& Rng)
		{
			double TotalWeight = 0.0;
			for (const ItemType& Item : Items)
			{
				const double Weight = GetWeight(Item);
				if (!FMath::IsFinite(Weight) || Weight <= 0.0)
				{
					return INDEX_NONE;
				}
				TotalWeight += Weight;
			}
			if (!FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0)
			{
				return INDEX_NONE;
			}

			const double Draw = static_cast<double>(Rng.FRand()) * TotalWeight;
			double RunningWeight = 0.0;
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				RunningWeight += GetWeight(Items[Index]);
				if (Draw < RunningWeight)
				{
					return Index;
				}
			}
			return Items.IsEmpty() ? INDEX_NONE : Items.Num() - 1;
		}

		bool IsValidRiskTuning(const FZeroEscapeHazardRiskTuning& Risk)
		{
			return FMath::IsFinite(Risk.TraversalPressurePerSecond)
				&& Risk.TraversalPressurePerSecond >= 0.0f
				&& FMath::IsFinite(Risk.HitConsequencePressure)
				&& Risk.HitConsequencePressure >= 0.0f;
		}

		bool IsValidScoringTuning(
			const FZeroEscapeHazardPlacementScoringTuning& Scoring)
		{
			return FMath::IsFinite(Scoring.MaxAbsLog2Score)
				&& Scoring.MaxAbsLog2Score >= 0.25f
				&& Scoring.MaxAbsLog2Score <= 16.0f
				&& Scoring.TypeContextTopAnchorCount >= 1
				&& Scoring.TypeContextTopAnchorCount <= 8
				&& FMath::IsFinite(Scoring.TypeContextStrength)
				&& Scoring.TypeContextStrength >= 0.0f
				&& Scoring.GroupRadiusTiles >= 1
				&& Scoring.GroupRadiusTiles <= 8
				&& FMath::IsFinite(Scoring.PressureFitLog2Strength)
				&& Scoring.PressureFitLog2Strength >= 0.0f
				&& FMath::IsFinite(Scoring.PressureOverloadWidthRatio)
				&& Scoring.PressureOverloadWidthRatio > 0.0f
				&& FMath::IsFinite(Scoring.RegionTargetVariation)
				&& Scoring.RegionTargetVariation >= 0.0f
				&& FMath::IsFinite(Scoring.AnchorTargetVariation)
				&& Scoring.AnchorTargetVariation >= 0.0f
				&& FMath::IsFinite(Scoring.ProgressLog2Strength)
				&& Scoring.ProgressLog2Strength >= 0.0f
				&& FMath::IsFinite(Scoring.LauncherCornerLog2Bonus)
				&& Scoring.LauncherCornerLog2Bonus >= 0.0f
				&& FMath::IsFinite(Scoring.RouteCoverageLog2Bonus)
				&& Scoring.RouteCoverageLog2Bonus >= 0.0f
				&& Scoring.RouteCoverageLog2Bonus <= 4.0f
				&& FMath::IsFinite(Scoring.WheelRamLog2Bonus)
				&& Scoring.WheelRamLog2Bonus >= 0.0f
				&& FMath::IsFinite(Scoring.WheelSpikeLog2Bonus)
				&& Scoring.WheelSpikeLog2Bonus >= 0.0f
				&& FMath::IsFinite(Scoring.SoloWheelLog2Contribution)
				&& Scoring.SoloWheelLog2Contribution <= 0.0f
				&& Scoring.RecentKindWindow >= 1
				&& Scoring.RecentKindWindow <= 16
				&& FMath::IsFinite(Scoring.MaximumRecentKindPenalty)
				&& Scoring.MaximumRecentKindPenalty >= 0.0f
				&& IsValidRiskTuning(Scoring.PendulumRisk)
				&& IsValidRiskTuning(Scoring.SpikeRisk)
				&& IsValidRiskTuning(Scoring.RamRisk)
				&& IsValidRiskTuning(Scoring.LauncherRisk)
				&& IsValidRiskTuning(Scoring.WheelRisk)
				&& FMath::IsFinite(Scoring.WheelRamPressureBonus)
				&& Scoring.WheelRamPressureBonus >= 0.0f
				&& FMath::IsFinite(Scoring.WheelSpikePressureBonus)
				&& Scoring.WheelSpikePressureBonus >= 0.0f;
		}

		const FZeroEscapePopulationDifficultySettings* ValidateAndFindDifficulty(
			const TConstArrayView<FZeroEscapePopulationDifficultySettings> Difficulties,
			const EZeroEscapeDifficulty RequestedDifficulty,
			FString& OutError)
		{
			if (Difficulties.Num() != 3)
			{
				OutError = TEXT("Population Profile 必须恰好提供三档难度。");
				return nullptr;
			}
			TStaticArray<int32, 3> Counts = { 0, 0, 0 };
			const FZeroEscapePopulationDifficultySettings* Match = nullptr;
			for (const FZeroEscapePopulationDifficultySettings& Difficulty : Difficulties)
			{
				const int32 Index = static_cast<int32>(Difficulty.Difficulty);
				if (Index < 0 || Index >= Counts.Num()
					|| !FMath::IsFinite(Difficulty.Hazards.ExpectedHazardsPer100GameplayCells)
					|| Difficulty.Hazards.ExpectedHazardsPer100GameplayCells < 0.0f
					|| Difficulty.Hazards.SpikeTrapWeight < 0
					|| Difficulty.Hazards.BatteringRamWeight < 0
					|| Difficulty.Hazards.GuidedLauncherWeight < 0
					|| Difficulty.Hazards.SpikeWheelWeight < 0
					|| !FMath::IsFinite(Difficulty.Hazards.TargetPressureAtStart)
					|| Difficulty.Hazards.TargetPressureAtStart <= 0.0f
					|| !FMath::IsFinite(Difficulty.Hazards.TargetPressureAtExit)
					|| Difficulty.Hazards.TargetPressureAtExit <= 0.0f
					|| !FMath::IsFinite(Difficulty.Resources.ExpectedResourcesPer100GameplayCells)
					|| Difficulty.Resources.ExpectedResourcesPer100GameplayCells < 0.0f
					|| Difficulty.Resources.MinimumRouteSpacingTiles < 1)
				{
					OutError = TEXT("Population 难度数值非法。");
					return nullptr;
				}
				++Counts[Index];
				if (Difficulty.Difficulty == RequestedDifficulty)
				{
					Match = &Difficulty;
				}
			}
			if (Counts[0] != 1 || Counts[1] != 1 || Counts[2] != 1 || Match == nullptr)
			{
				OutError = TEXT("Population 难度必须是 Easy、Normal、Hard 各一条。");
				return nullptr;
			}
			return Match;
		}

		bool ValidateAssemblyAndTargets(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const double FloorTopZCm,
			const FZeroEscapeHazardPopulationAssembly& Hazards,
			const FZeroEscapeResourcePopulationAssembly& Resources,
			const FZeroEscapePopulationDifficultySettings& Difficulty,
			int32& OutHazardDensityTarget,
			int32& OutResourceTarget,
			FString& OutError)
		{
			OutHazardDensityTarget = 0;
			OutResourceTarget = 0;
			const int64 MaxGeneratedAddresses =
				static_cast<int64>(GenerationLimits::MaxGridCells)
				* GenerationLimits::MaxFloorCount;
			int64 GameplayArea = Plan.OrdinaryCells.Num();
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				if (Structure.Kind == EZeroEscapeStructureKind::HighCeilingRoom)
				{
					GameplayArea += Structure.WalkableCells.Num();
				}
			}
			if (Plan.FloorCount < GenerationLimits::MinFloorCount
				|| Plan.FloorCount > GenerationLimits::MaxFloorCount
				|| Plan.GridSize.X < GenerationLimits::MinGridAxis
				|| Plan.GridSize.Y < GenerationLimits::MinGridAxis
				|| Plan.GridSize.X > GenerationLimits::MaxGridAxis
				|| Plan.GridSize.Y > GenerationLimits::MaxGridAxis
				|| GameplayArea <= 0 || GameplayArea > MaxGeneratedAddresses
				|| !FMath::IsFinite(Plan.LogicalTileSizeCm) || Plan.LogicalTileSizeCm <= 0.0
				|| !FMath::IsFinite(Plan.FloorHeightCm) || Plan.FloorHeightCm <= 0.0
				|| !FMath::IsFinite(FloorTopZCm))
			{
				OutError = TEXT("Population 输入 Plan 尺寸非法。");
				return false;
			}
			if (Hazards.SpikeTrapActorCount <= 0
				|| !FMath::IsFinite(Hazards.SpikeTrapLateralSpacingCm)
				|| Hazards.SpikeTrapLateralSpacingCm < 0.0f
				|| !FMath::IsFinite(Hazards.SpikeTrapFloorOffsetCm)
				|| !FMath::IsFinite(Hazards.BatteringRamWallInsetCm)
				|| Hazards.BatteringRamWallInsetCm < 0.0f
				|| !FMath::IsFinite(Hazards.BatteringRamMountHeightCm)
				|| Hazards.BatteringRamMountHeightCm < 0.0f
				|| !FMath::IsFinite(Hazards.GuidedLauncherWallInsetCm)
				|| Hazards.GuidedLauncherWallInsetCm < 0.0f
				|| !FMath::IsFinite(Hazards.GuidedLauncherMountHeightCm)
				|| Hazards.GuidedLauncherMountHeightCm < 0.0f
				|| !IsValidScoringTuning(Hazards.PlacementScoring)
				|| !FMath::IsFinite(Resources.SpawnZOffsetCm)
				|| !FMath::IsFinite(Resources.PlacementFootprintRadiusCm)
				|| Resources.PlacementFootprintRadiusCm < 0.0f
				|| Resources.PlacementFootprintRadiusCm >= Plan.LogicalTileSizeCm * 0.5
				|| !FMath::IsFinite(Resources.HighPressureSupportLog2Bonus)
				|| Resources.HighPressureSupportLog2Bonus < 0.0f
				|| Resources.HighPressureSupportLog2Bonus > 4.0f
				|| !FMath::IsFinite(Resources.RouteCoverageLog2Bonus)
				|| Resources.RouteCoverageLog2Bonus < 0.0f
				|| Resources.RouteCoverageLog2Bonus > 4.0f)
			{
				OutError = TEXT("Population 共享装配数值非法。");
				return false;
			}
			int64 MandatoryPendulumCount = 0;
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				if (Structure.Kind == EZeroEscapeStructureKind::HighCeilingRoom)
				{
					++MandatoryPendulumCount;
				}
			}
			const double HazardRaw = GameplayArea
				* static_cast<double>(Difficulty.Hazards.ExpectedHazardsPer100GameplayCells) / 100.0;
			const double ResourceRaw = GameplayArea
				* static_cast<double>(Difficulty.Resources.ExpectedResourcesPer100GameplayCells) / 100.0;
			if (!FMath::IsFinite(HazardRaw) || !FMath::IsFinite(ResourceRaw)
				|| HazardRaw > MaxGeneratedAddresses || ResourceRaw > MaxGeneratedAddresses)
			{
				OutError = TEXT("Population 密度目标超过安全预算。");
				return false;
			}
			OutHazardDensityTarget = static_cast<int32>(FMath::RoundToInt64(HazardRaw));
			OutResourceTarget = static_cast<int32>(FMath::RoundToInt64(ResourceRaw));
			const int64 EffectiveHazardTarget = FMath::Max<int64>(
				OutHazardDensityTarget, MandatoryPendulumCount);
			const int64 MaximumOrdinaryHazards = FMath::Max<int64>(
				0, EffectiveHazardTarget - MandatoryPendulumCount);
			int64 MaximumActorsPerOrdinaryHazard = 0;
			if (Difficulty.Hazards.SpikeTrapWeight > 0)
			{
				MaximumActorsPerOrdinaryHazard = Hazards.SpikeTrapActorCount;
			}
			if (Difficulty.Hazards.BatteringRamWeight > 0)
			{
				MaximumActorsPerOrdinaryHazard = FMath::Max<int64>(
					MaximumActorsPerOrdinaryHazard, 1);
			}
			if (Difficulty.Hazards.GuidedLauncherWeight > 0)
			{
				// 发射器 BeginPlay 会同步预装一个真实弹体，世界预算按两个 Actor 计算。
				MaximumActorsPerOrdinaryHazard = FMath::Max<int64>(
					MaximumActorsPerOrdinaryHazard, 2);
			}
			if (Difficulty.Hazards.SpikeWheelWeight > 0)
			{
				MaximumActorsPerOrdinaryHazard = FMath::Max<int64>(
					MaximumActorsPerOrdinaryHazard, 1);
			}
			const int64 WorstActorCount = MandatoryPendulumCount
				+ MaximumOrdinaryHazards * MaximumActorsPerOrdinaryHazard
				+ OutResourceTarget;
			const int64 WeightSum = static_cast<int64>(Difficulty.Hazards.SpikeTrapWeight)
				+ Difficulty.Hazards.BatteringRamWeight
				+ Difficulty.Hazards.GuidedLauncherWeight
				+ Difficulty.Hazards.SpikeWheelWeight;
			const double SpikeCenterHalfSpan =
				static_cast<double>(Hazards.SpikeTrapActorCount - 1)
				* Hazards.SpikeTrapLateralSpacingCm * 0.5;
			if (WorstActorCount > MaxGeneratedAddresses
				|| WeightSum > MAX_int32
				|| SpikeCenterHalfSpan > Plan.LogicalTileSizeCm * 0.5
				|| Hazards.BatteringRamWallInsetCm > Plan.LogicalTileSizeCm * 0.5
				|| Hazards.GuidedLauncherWallInsetCm > Plan.LogicalTileSizeCm * 0.5)
			{
				OutError = TEXT("Population 数量、权重或安装尺寸超过安全边界。");
				return false;
			}
			return true;
		}

		FTransform MakeWallMountedTransform(
			const FVector& CellFloorCenter,
			const FVector& ForwardIntoCell,
			const double HalfTileCm,
			const double WallInsetCm,
			const double MountHeightCm)
		{
			const FVector Location = CellFloorCenter
				- ForwardIntoCell * (HalfTileCm - WallInsetCm)
				+ FVector(0.0, 0.0, MountHeightCm);
			return FTransform(ForwardIntoCell.Rotation(), Location);
		}

		bool BuildMandatoryPendulum(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FZeroEscapeGeneratedStructure& Structure,
			const double FloorTopZCm,
			const FTraversalGraph& Graph,
			FPlacementCandidate& OutCandidate)
		{
			if (Structure.WalkableCells.Num() != 2
				|| Structure.InternalConnections.Num() != 1)
			{
				return false;
			}
			const FIntVector First = Structure.WalkableCells[0];
			const FIntVector Second = Structure.WalkableCells[1];
			const FZeroEscapeGeneratedCellConnection& Connection =
				Structure.InternalConnections[0];
			const bool bConnectionMatches =
				(Connection.FirstCoordinate == First && Connection.SecondCoordinate == Second)
				|| (Connection.FirstCoordinate == Second && Connection.SecondCoordinate == First);
			if (!bConnectionMatches || First.Z != Second.Z
				|| FMath::Abs(First.X - Second.X) + FMath::Abs(First.Y - Second.Y) != 1)
			{
				return false;
			}
			if (!Graph.NodeByAddress.Contains(First)
				|| !Graph.NodeByAddress.Contains(Second))
			{
				return false;
			}
			const FVector FirstFloor = AddressFloorLocation(Plan, First, FloorTopZCm);
			const FVector SecondFloor = AddressFloorLocation(Plan, Second, FloorTopZCm);
			FPlacementVariant Variant;
			Variant.LocalSpawnTransform = FTransform(
				FRotator(0.0, Structure.QuarterTurnCount * -90.0, 0.0),
				(FirstFloor + SecondFloor) * 0.5);
			Variant.ResourceBlockedAddresses = { First, Second };
			OutCandidate.Kind = EPopulationPlacementKind::Pendulum;
			OutCandidate.AnchorAddress = Structure.BaseCoordinate;
			OutCandidate.Variants.Add(MoveTemp(Variant));
			return true;
		}

		void BuildOrdinaryCandidates(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const double FloorTopZCm,
			const FZeroEscapeHazardPopulationAssembly& Hazards,
			const FTraversalGraph& Graph,
			TArray<FPlacementCandidate>& OutSpikes,
			TArray<FPlacementCandidate>& OutRams,
			TArray<FPlacementCandidate>& OutLaunchers,
			TArray<FPlacementCandidate>& OutWheels)
		{
			TMap<FIntVector, const FZeroEscapeGeneratedOrdinaryCell*> OrdinaryByAddress;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				OrdinaryByAddress.Add(Cell.Coordinate, &Cell);
			}
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				if (IsFlowCoordinate(Plan, Cell.Coordinate)
					|| !Graph.NodeByAddress.Contains(Cell.Coordinate))
				{
					continue;
				}

				const FVector FloorCenter = AddressFloorLocation(
					Plan, Cell.Coordinate, FloorTopZCm);
				FPlacementCandidate Spike;
				Spike.Kind = EPopulationPlacementKind::SpikeTrap;
				Spike.AnchorAddress = Cell.Coordinate;
				FPlacementCandidate Ram;
				Ram.Kind = EPopulationPlacementKind::BatteringRam;
				Ram.AnchorAddress = Cell.Coordinate;
				FPlacementCandidate Launcher;
				Launcher.Kind = EPopulationPlacementKind::GuidedLauncher;
				Launcher.AnchorAddress = Cell.Coordinate;
				FPlacementCandidate Wheel;
				Wheel.Kind = EPopulationPlacementKind::SpikeWheel;
				Wheel.AnchorAddress = Cell.Coordinate;

				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					const bool bOpen =
						(Cell.OpeningMask & Grid::DirectionBit(Direction)) != 0;
					const bool bOpenToOrdinary = IsOrdinaryNeighborOpen(
						Plan, OrdinaryByAddress, Cell, Direction);
					const FVector Forward = DirectionVector(Direction);
					if (bOpen)
					{
						FPlacementVariant SpikeVariant;
						SpikeVariant.LocalSpawnTransform = FTransform(
							Forward.Rotation(),
							FloorCenter + FVector(
								0.0, 0.0, Hazards.SpikeTrapFloorOffsetCm));
						SpikeVariant.SpikeLateralAxis = FVector::CrossProduct(
							FVector::UpVector, Forward);
						SpikeVariant.ResourceBlockedAddresses.Add(Cell.Coordinate);
						Spike.Variants.Add(MoveTemp(SpikeVariant));

						FPlacementVariant WheelVariant;
						WheelVariant.LocalSpawnTransform = FTransform(
							Forward.Rotation(), FloorCenter);
						WheelVariant.ResourceBlockedAddresses.Add(Cell.Coordinate);
						WheelVariant.SpikeWheel.bIsConfigured = true;
						WheelVariant.SpikeWheel.RouteVariantSeed = static_cast<int32>(
							MakeStablePlacementHash(
								Plan.Signature.Seed,
								Cell.Coordinate,
								Direction,
								0x1B56C4E9u));
						WheelVariant.SpikeWheel.NormalizedPhase01 = StablePhase01(
							MakeStablePlacementHash(
								Plan.Signature.Seed,
								Cell.Coordinate,
								Direction,
								0xC2B2AE35u));
						Wheel.Variants.Add(MoveTemp(WheelVariant));
					}
					else
					{
						FPlacementVariant RamVariant;
						RamVariant.LocalSpawnTransform = MakeWallMountedTransform(
							FloorCenter,
							-Forward,
							Plan.LogicalTileSizeCm * 0.5,
							Hazards.BatteringRamWallInsetCm,
							Hazards.BatteringRamMountHeightCm);
						RamVariant.ResourceBlockedAddresses.Add(Cell.Coordinate);
						Ram.Variants.Add(MoveTemp(RamVariant));
					}

					const uint8 RearDirection = Grid::OppositeDirectionIndex(Direction);
					const bool bRearClosed =
						(Cell.OpeningMask & Grid::DirectionBit(RearDirection)) == 0;
					const FIntVector FrontAddress = StepAddress(Cell.Coordinate, Direction);
					if (bOpenToOrdinary && bRearClosed
						&& !IsFlowCoordinate(Plan, FrontAddress))
					{
						FPlacementVariant LauncherVariant;
						LauncherVariant.LocalSpawnTransform = MakeWallMountedTransform(
							FloorCenter,
							Forward,
							Plan.LogicalTileSizeCm * 0.5,
							Hazards.GuidedLauncherWallInsetCm,
							Hazards.GuidedLauncherMountHeightCm);
						LauncherVariant.ResourceBlockedAddresses = {
							Cell.Coordinate, FrontAddress };
						bool bHasPerpendicularOpening = false;
						for (uint8 OtherDirection = 0;
							OtherDirection < Grid::DirectionCount;
							++OtherDirection)
						{
							if (OtherDirection != Direction
								&& OtherDirection != RearDirection
								&& (Cell.OpeningMask
									& Grid::DirectionBit(OtherDirection)) != 0)
							{
								bHasPerpendicularOpening = true;
								break;
							}
						}
						LauncherVariant.PositionLog2Contribution =
							bHasPerpendicularOpening
								? Hazards.PlacementScoring.LauncherCornerLog2Bonus
								: 0.0f;
						Launcher.Variants.Add(MoveTemp(LauncherVariant));
					}
				}

				if (!Spike.Variants.IsEmpty()) OutSpikes.Add(MoveTemp(Spike));
				if (!Ram.Variants.IsEmpty()) OutRams.Add(MoveTemp(Ram));
				if (!Launcher.Variants.IsEmpty()) OutLaunchers.Add(MoveTemp(Launcher));
				if (!Wheel.Variants.IsEmpty()) OutWheels.Add(MoveTemp(Wheel));
			}

			auto CandidateLess = [](const FPlacementCandidate& A, const FPlacementCandidate& B)
			{
				return CoordinateLess(A.AnchorAddress, B.AnchorAddress);
			};
			OutSpikes.Sort(CandidateLess);
			OutRams.Sort(CandidateLess);
			OutLaunchers.Sort(CandidateLess);
			OutWheels.Sort(CandidateLess);
		}

		bool VariantHasOperationConflict(
			const FPlacementVariant& Variant,
			const TSet<FIntVector>& AcceptedOperationAddresses)
		{
			for (const FIntVector Address : Variant.ResourceBlockedAddresses)
			{
				if (AcceptedOperationAddresses.Contains(Address))
				{
					return true;
				}
			}
			return false;
		}

		bool EvaluateHazardCandidate(
			const FPlacementCandidate& Candidate,
			const FPlacementVariant& Variant,
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FZeroEscapeHazardPopulationTuning& HazardTuning,
			const FZeroEscapeHazardPlacementScoringTuning& Scoring,
			const float RepresentativeTraversalSeconds,
			const FTraversalGraph& Graph,
			FHazardPlanningState& State,
			FHazardCandidateEvaluation& OutEvaluation)
		{
			OutEvaluation = FHazardCandidateEvaluation();
			TArray<int32> OperationNodeIndices;
			if (!BuildOperationNodeIndices(Graph, Variant, OperationNodeIndices))
			{
				return false;
			}

			const int32* AnchorNode = Graph.NodeByAddress.Find(Candidate.AnchorAddress);
			if (AnchorNode == nullptr
				|| !State.DistanceFromPlayer.IsValidIndex(*AnchorNode)
				|| !State.DistanceToExit.IsValidIndex(*AnchorNode))
			{
				return false;
			}
			const int32 DistanceFromPlayer = State.DistanceFromPlayer[*AnchorNode];
			const int32 DistanceToExit = State.DistanceToExit[*AnchorNode];
			const int64 ProgressDenominator = FMath::Max<int64>(
				static_cast<int64>(DistanceFromPlayer) + DistanceToExit, 1);
			OutEvaluation.CandidateProgress01 = static_cast<float>(
				static_cast<double>(DistanceFromPlayer) / ProgressDenominator);

			TArray<FNearbyHazard> NearbyHazards;
			FindNearbyAcceptedHazards(
				Graph,
				OperationNodeIndices,
				Scoring.GroupRadiusTiles,
				State,
				NearbyHazards);
			for (const FNearbyHazard& Nearby : NearbyHazards)
			{
				if (!State.AcceptedHazards.IsValidIndex(Nearby.PlacementIndex))
				{
					return false;
				}
				const FAcceptedHazardState& Accepted =
					State.AcceptedHazards[Nearby.PlacementIndex];
				if (!State.Groups.IsValidIndex(Accepted.GroupId)
					|| !State.Groups[Accepted.GroupId].bActive)
				{
					return false;
				}
				OutEvaluation.NearbyGroupIds.AddUnique(Accepted.GroupId);
				if (Nearby.Distance == 1)
				{
					OutEvaluation.AdjacentPlacementIndices.Add(
						Nearby.PlacementIndex);
				}
			}
			OutEvaluation.NearbyGroupIds.Sort();
			OutEvaluation.AdjacentPlacementIndices.Sort();

			OutEvaluation.ProposedGroupAnchor = Candidate.AnchorAddress;
			float PressureBefore = 0.0f;
			float ProgressSum = OutEvaluation.CandidateProgress01;
			int32 GroupPlacementCount = 1;
			for (const int32 GroupId : OutEvaluation.NearbyGroupIds)
			{
				const FWorkingHazardGroup& Group = State.Groups[GroupId];
				PressureBefore += Group.ActualPressure;
				ProgressSum += Group.ProgressSum;
				GroupPlacementCount += Group.PlacementIndices.Num();
				if (CoordinateLess(Group.AnchorAddress, OutEvaluation.ProposedGroupAnchor))
				{
					OutEvaluation.ProposedGroupAnchor = Group.AnchorAddress;
				}
			}
			OutEvaluation.GroupProgress01 = ProgressSum
				/ FMath::Max(GroupPlacementCount, 1);
			OutEvaluation.TargetPressure = CalculateGroupTargetPressure(
				HazardTuning,
				Scoring,
				Plan.Signature.Seed,
				OutEvaluation.ProposedGroupAnchor,
				OutEvaluation.GroupProgress01);

			float BestCombinationLog2 = 0.0f;
			bool bHasWheelPartner = false;
			for (const int32 PlacementIndex :
				OutEvaluation.AdjacentPlacementIndices)
			{
				const EPopulationPlacementKind AcceptedKind =
					State.AcceptedHazards[PlacementIndex].Kind;
				if (IsWheelRamPair(Candidate.Kind, AcceptedKind))
				{
					bHasWheelPartner = true;
					BestCombinationLog2 = FMath::Max(
						BestCombinationLog2, Scoring.WheelRamLog2Bonus);
					OutEvaluation.AddedCombinationPressure +=
						Scoring.WheelRamPressureBonus;
				}
				else if (IsWheelSpikePair(Candidate.Kind, AcceptedKind))
				{
					bHasWheelPartner = true;
					BestCombinationLog2 = FMath::Max(
						BestCombinationLog2, Scoring.WheelSpikeLog2Bonus);
					OutEvaluation.AddedCombinationPressure +=
						Scoring.WheelSpikePressureBonus;
				}
			}

			const float BasePressure = BasePressureForKind(
				Candidate.Kind, Scoring, RepresentativeTraversalSeconds);
			if (!FMath::IsFinite(BasePressure) || BasePressure < 0.0f)
			{
				return false;
			}
			OutEvaluation.PressureAfterPlacement = PressureBefore
				+ BasePressure
				+ OutEvaluation.AddedCombinationPressure;

			const float Target = FMath::Max(OutEvaluation.TargetPressure, 0.01f);
			const float PressureError =
				(OutEvaluation.PressureAfterPlacement - Target) / Target;
			const float PressureFitSignal = PressureError <= 0.0f
				? 1.0f - 2.0f * FMath::Clamp(-PressureError, 0.0f, 1.0f)
				: 1.0f - 2.0f * FMath::Clamp(
					PressureError / Scoring.PressureOverloadWidthRatio,
					0.0f,
					1.0f);

			int32 NearestHazardDistance = UnreachableDistance;
			for (const int32 Node : OperationNodeIndices)
			{
				if (!State.NearestHazardDistances.IsValidIndex(Node))
				{
					return false;
				}
				NearestHazardDistance = FMath::Min(
					NearestHazardDistance, State.NearestHazardDistances[Node]);
			}
			const float RouteCoverageSignal = NearestHazardDistance == UnreachableDistance
				? 1.0f
				: FMath::Clamp(
					static_cast<float>(NearestHazardDistance - Scoring.GroupRadiusTiles)
						/ Scoring.GroupRadiusTiles,
					0.0f,
					1.0f);
			OutEvaluation.Score.Position = Variant.PositionLog2Contribution
				+ Scoring.RouteCoverageLog2Bonus * RouteCoverageSignal;
			OutEvaluation.Score.Progress = Scoring.ProgressLog2Strength
				* (2.0f * OutEvaluation.CandidateProgress01 - 1.0f);
			OutEvaluation.Score.GroupPressure =
				Scoring.PressureFitLog2Strength * PressureFitSignal;
			OutEvaluation.Score.Combination =
				Candidate.Kind == EPopulationPlacementKind::SpikeWheel
					&& !bHasWheelPartner
				? Scoring.SoloWheelLog2Contribution
				: BestCombinationLog2;

			const int32 ObservedKindCount = FMath::Min(
				Scoring.RecentKindWindow,
				State.RecentOrdinaryKinds.Num());
			int32 SameKindCount = 0;
			for (int32 Offset = 0; Offset < ObservedKindCount; ++Offset)
			{
				const int32 RecentIndex =
					State.RecentOrdinaryKinds.Num() - 1 - Offset;
				SameKindCount += State.RecentOrdinaryKinds[RecentIndex]
					== Candidate.Kind ? 1 : 0;
			}
			OutEvaluation.Score.Diversity = -Scoring.MaximumRecentKindPenalty
				* static_cast<float>(SameKindCount)
				/ FMath::Max(Scoring.RecentKindWindow, 1);
			OutEvaluation.Score.Diagnostic =
				PressureError > Scoring.PressureOverloadWidthRatio * 2.0f
					? -0.5f
					: 0.0f;

			const float Total = OutEvaluation.Score.Position
				+ OutEvaluation.Score.Progress
				+ OutEvaluation.Score.GroupPressure
				+ OutEvaluation.Score.Combination
				+ OutEvaluation.Score.Diversity
				+ OutEvaluation.Score.Diagnostic;
			if (!FMath::IsFinite(Total)
				|| !FMath::IsFinite(OutEvaluation.PressureAfterPlacement)
				|| !FMath::IsFinite(OutEvaluation.TargetPressure))
			{
				return false;
			}
			OutEvaluation.Score.TotalLog2Score = FMath::Clamp(
				Total, -Scoring.MaxAbsLog2Score, Scoring.MaxAbsLog2Score);
			return true;
		}

		bool BuildScoredKindPool(
			const EPopulationPlacementKind Kind,
			const int32 BaseTypeWeight,
			const TArray<FPlacementCandidate>& Candidates,
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FZeroEscapeHazardPopulationTuning& HazardTuning,
			const FZeroEscapeHazardPlacementScoringTuning& Scoring,
			const float RepresentativeTraversalSeconds,
			const FTraversalGraph& Graph,
			const TSet<FIntVector>& AcceptedOperationAddresses,
			FHazardPlanningState& State,
			TArray<FScoredKindPool>& OutPools,
			FString& OutError)
		{
			if (BaseTypeWeight <= 0)
			{
				return true;
			}

			FScoredKindPool Pool;
			Pool.Kind = Kind;
			Pool.Candidates = &Candidates;
			for (int32 CandidateIndex = 0;
				CandidateIndex < Candidates.Num();
				++CandidateIndex)
			{
				const FPlacementCandidate& Candidate = Candidates[CandidateIndex];
				FScoredAnchor Anchor;
				Anchor.CandidateIndex = CandidateIndex;
				Anchor.BestLog2Score = -TNumericLimits<double>::Max();
				for (int32 VariantIndex = 0;
					VariantIndex < Candidate.Variants.Num();
					++VariantIndex)
				{
					const FPlacementVariant& Variant = Candidate.Variants[VariantIndex];
					if (VariantHasOperationConflict(
							Variant, AcceptedOperationAddresses))
					{
						continue;
					}

					FHazardCandidateEvaluation Evaluation;
					if (!EvaluateHazardCandidate(
							Candidate,
							Variant,
							Plan,
							HazardTuning,
							Scoring,
							RepresentativeTraversalSeconds,
							Graph,
							State,
							Evaluation))
					{
						OutError = TEXT("机关候选评分遇到非法图节点或非有限数值。");
						return false;
					}
					FScoredVariant& ScoredVariant =
						Anchor.Variants.AddDefaulted_GetRef();
					ScoredVariant.VariantIndex = VariantIndex;
					ScoredVariant.Score = Evaluation.Score;
					ScoredVariant.Weight = FMath::Pow(
						2.0, static_cast<double>(Evaluation.Score.TotalLog2Score));
					Anchor.BestLog2Score = FMath::Max(
						Anchor.BestLog2Score,
						static_cast<double>(Evaluation.Score.TotalLog2Score));
				}
				if (!Anchor.Variants.IsEmpty())
				{
					Anchor.Weight = FMath::Pow(2.0, Anchor.BestLog2Score);
					Pool.Anchors.Add(MoveTemp(Anchor));
				}
			}
			if (Pool.Anchors.IsEmpty())
			{
				return true;
			}

			Pool.Anchors.Sort([&Candidates](
				const FScoredAnchor& First,
				const FScoredAnchor& Second)
			{
				if (First.BestLog2Score != Second.BestLog2Score)
				{
					return First.BestLog2Score > Second.BestLog2Score;
				}
				const FIntVector FirstAddress =
					Candidates[First.CandidateIndex].AnchorAddress;
				const FIntVector SecondAddress =
					Candidates[Second.CandidateIndex].AnchorAddress;
				return FirstAddress != SecondAddress
					? CoordinateLess(FirstAddress, SecondAddress)
					: First.CandidateIndex < Second.CandidateIndex;
			});
			const int32 ContextCount = FMath::Min(
				Scoring.TypeContextTopAnchorCount,
				Pool.Anchors.Num());
			double ContextAverage = 0.0;
			for (int32 Index = 0; Index < ContextCount; ++Index)
			{
				ContextAverage += Pool.Anchors[Index].BestLog2Score;
			}
			ContextAverage /= ContextCount;
			const double ContextLog2 = FMath::Clamp(
				ContextAverage * Scoring.TypeContextStrength,
				-static_cast<double>(Scoring.MaxAbsLog2Score),
				static_cast<double>(Scoring.MaxAbsLog2Score));
			Pool.TypeWeight = static_cast<double>(BaseTypeWeight)
				* FMath::Pow(2.0, ContextLog2);
			if (!FMath::IsFinite(Pool.TypeWeight) || Pool.TypeWeight <= 0.0)
			{
				OutError = TEXT("机关类型上下文权重非法。");
				return false;
			}
			OutPools.Add(MoveTemp(Pool));
			return true;
		}

		void AcceptPlacement(
			const FPlacementCandidate& Candidate,
			const FPlacementVariant& Variant,
			const FPopulationPlacementScoreBreakdown& Score,
			const FZeroEscapeHazardPopulationAssembly& Hazards,
			TArray<FPopulationPlannedPlacement>& OutPlacements,
			FPopulationKindCounts& InOutCounts)
		{
			FPopulationPlannedPlacement Placement;
			Placement.Kind = Candidate.Kind;
			Placement.AnchorAddress = Candidate.AnchorAddress;
			if (Candidate.Kind == EPopulationPlacementKind::SpikeTrap)
			{
				for (int32 Index = 0; Index < Hazards.SpikeTrapActorCount; ++Index)
				{
					const double LateralOffset =
						(static_cast<double>(Index)
							- (Hazards.SpikeTrapActorCount - 1) * 0.5)
						* Hazards.SpikeTrapLateralSpacingCm;
					FTransform Transform = Variant.LocalSpawnTransform;
					Transform.AddToTranslation(Variant.SpikeLateralAxis * LateralOffset);
					Placement.LocalSpawnTransforms.Add(Transform);
				}
			}
			else
			{
				Placement.LocalSpawnTransforms.Add(Variant.LocalSpawnTransform);
			}
			Placement.ResourceBlockedAddresses = Variant.ResourceBlockedAddresses;
			Placement.SpikeWheel = Variant.SpikeWheel;
			Placement.Score = Score;
			OutPlacements.Add(MoveTemp(Placement));
			switch (Candidate.Kind)
			{
			case EPopulationPlacementKind::Pendulum: ++InOutCounts.Pendulums; break;
			case EPopulationPlacementKind::SpikeTrap: ++InOutCounts.SpikeTrapGroups; break;
			case EPopulationPlacementKind::BatteringRam: ++InOutCounts.BatteringRams; break;
			case EPopulationPlacementKind::GuidedLauncher: ++InOutCounts.GuidedLaunchers; break;
			case EPopulationPlacementKind::SpikeWheel: ++InOutCounts.SpikeWheels; break;
			case EPopulationPlacementKind::MagneticResource: break;
			}
		}

		bool RegisterAcceptedHazard(
			const FPlacementCandidate& Candidate,
			const FPlacementVariant& Variant,
			const FHazardCandidateEvaluation& Evaluation,
			const FZeroEscapeHazardPlacementScoringTuning& Scoring,
			const float RepresentativeTraversalSeconds,
			const FTraversalGraph& Graph,
			const int32 PlacementIndex,
			FHazardPlanningState& State)
		{
			TArray<int32> OperationNodeIndices;
			if (!BuildOperationNodeIndices(Graph, Variant, OperationNodeIndices))
			{
				return false;
			}

			int32 SurvivorGroupId = INDEX_NONE;
			if (Evaluation.NearbyGroupIds.IsEmpty())
			{
				SurvivorGroupId = State.Groups.AddDefaulted();
			}
			else
			{
				SurvivorGroupId = Evaluation.NearbyGroupIds[0];
			}

			FWorkingHazardGroup CombinedGroup;
			CombinedGroup.AnchorAddress = Evaluation.ProposedGroupAnchor;
			for (const int32 GroupId : Evaluation.NearbyGroupIds)
			{
				if (!State.Groups.IsValidIndex(GroupId)
					|| !State.Groups[GroupId].bActive)
				{
					return false;
				}
				FWorkingHazardGroup& Group = State.Groups[GroupId];
				CombinedGroup.PlacementIndices.Append(Group.PlacementIndices);
				CombinedGroup.ActualPressure += Group.ActualPressure;
				CombinedGroup.ProgressSum += Group.ProgressSum;
				if (CoordinateLess(Group.AnchorAddress, CombinedGroup.AnchorAddress))
				{
					CombinedGroup.AnchorAddress = Group.AnchorAddress;
				}
				Group.bActive = false;
			}

			for (FAcceptedHazardState& Accepted : State.AcceptedHazards)
			{
				if (Evaluation.NearbyGroupIds.Contains(Accepted.GroupId))
				{
					Accepted.GroupId = SurvivorGroupId;
				}
			}

			const float BasePressure = BasePressureForKind(
				Candidate.Kind, Scoring, RepresentativeTraversalSeconds);
			if (!FMath::IsFinite(BasePressure) || BasePressure < 0.0f)
			{
				return false;
			}
			CombinedGroup.bActive = true;
			CombinedGroup.PlacementIndices.Add(PlacementIndex);
			CombinedGroup.ActualPressure +=
				BasePressure + Evaluation.AddedCombinationPressure;
			CombinedGroup.ProgressSum += Evaluation.CandidateProgress01;
			State.Groups[SurvivorGroupId] = MoveTemp(CombinedGroup);

			FAcceptedHazardState& Accepted =
				State.AcceptedHazards.AddDefaulted_GetRef();
			Accepted.Kind = Candidate.Kind;
			Accepted.AnchorAddress = Candidate.AnchorAddress;
			Accepted.OperationNodeIndices = OperationNodeIndices;
			Accepted.GroupId = SurvivorGroupId;
			Accepted.BasePressure = BasePressure;
			Accepted.Progress01 = Evaluation.CandidateProgress01;
			const int32 AcceptedIndex = State.AcceptedHazards.Num() - 1;
			for (const int32 Node : OperationNodeIndices)
			{
				State.AcceptedPlacementIndicesByNode[Node].Add(AcceptedIndex);
			}
			UpdateNearestDistances(
				Graph, OperationNodeIndices, State.NearestHazardDistances);
			if (Candidate.Kind != EPopulationPlacementKind::Pendulum)
			{
				State.RecentOrdinaryKinds.Add(Candidate.Kind);
			}
			return true;
		}

		bool FinalizeHazardGroups(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const FZeroEscapeHazardPopulationTuning& HazardTuning,
			const FZeroEscapeHazardPlacementScoringTuning& Scoring,
			const FTraversalGraph& Graph,
			const TSet<FIntVector>& AcceptedOperationAddresses,
			const FHazardPlanningState& State,
			FPopulationPlacementPlan& InOutPlan,
			FString& OutError)
		{
			TSet<FIntVector> OrdinaryAddresses;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				OrdinaryAddresses.Add(Cell.Coordinate);
			}

			for (const FWorkingHazardGroup& Group : State.Groups)
			{
				if (!Group.bActive)
				{
					continue;
				}
				if (Group.PlacementIndices.IsEmpty())
				{
					OutError = TEXT("活动机关组没有成员。");
					return false;
				}

				FPopulationHazardGroupRecord Record;
				Record.AnchorAddress = Group.AnchorAddress;
				Record.PlacementIndices = Group.PlacementIndices;
				Record.PlacementIndices.Sort();
				const float GroupProgress = Group.ProgressSum
					/ Group.PlacementIndices.Num();
				Record.TargetPressure = CalculateGroupTargetPressure(
					HazardTuning,
					Scoring,
					Plan.Signature.Seed,
					Group.AnchorAddress,
					GroupProgress);
				Record.ActualPressure = Group.ActualPressure;
				Record.ResourceSupportPriority = FMath::Clamp(
					Record.ActualPressure
						/ FMath::Max(Record.TargetPressure, 0.01f)
						- 0.5f,
					0.0f,
					1.0f);

				for (const int32 PlacementIndex : Record.PlacementIndices)
				{
					if (!InOutPlan.HazardPlacements.IsValidIndex(PlacementIndex))
					{
						OutError = TEXT("机关组引用了非法放置索引。");
						return false;
					}
					for (const FIntVector OperationAddress :
						InOutPlan.HazardPlacements[PlacementIndex]
							.ResourceBlockedAddresses)
					{
						const int32* OperationNode =
							Graph.NodeByAddress.Find(OperationAddress);
						if (OperationNode == nullptr)
						{
							OutError = TEXT("机关组操作格不在通行图中。");
							return false;
						}
						for (const int32 Neighbor : Graph.Neighbors[*OperationNode])
						{
							const FIntVector NeighborAddress = Graph.Addresses[Neighbor];
							if (OrdinaryAddresses.Contains(NeighborAddress)
								&& !IsFlowCoordinate(Plan, NeighborAddress)
								&& !AcceptedOperationAddresses.Contains(NeighborAddress)
								&& State.DistanceFromPlayer[Neighbor]
									< State.DistanceFromPlayer[*OperationNode])
							{
								Record.SafeApproachAddresses.AddUnique(NeighborAddress);
							}
						}
					}
				}
				Record.SafeApproachAddresses.Sort(CoordinateLess);
				InOutPlan.HazardGroups.Add(MoveTemp(Record));
			}
			InOutPlan.HazardGroups.Sort([](
				const FPopulationHazardGroupRecord& First,
				const FPopulationHazardGroupRecord& Second)
			{
				return CoordinateLess(First.AnchorAddress, Second.AnchorAddress);
			});

			for (int32 PlacementIndex = 0;
				PlacementIndex < State.AcceptedHazards.Num();
				++PlacementIndex)
			{
				const FAcceptedHazardState& Wheel =
					State.AcceptedHazards[PlacementIndex];
				if (Wheel.Kind != EPopulationPlacementKind::SpikeWheel)
				{
					continue;
				}
				TArray<int32> AdjacentPlacements;
				for (const int32 Node : Wheel.OperationNodeIndices)
				{
					for (const int32 Neighbor : Graph.Neighbors[Node])
					{
						for (const int32 OtherPlacement :
							State.AcceptedPlacementIndicesByNode[Neighbor])
						{
							if (OtherPlacement != PlacementIndex)
							{
								AdjacentPlacements.AddUnique(OtherPlacement);
							}
						}
					}
				}
				bool bHasRam = false;
				bool bHasSpike = false;
				for (const int32 OtherPlacement : AdjacentPlacements)
				{
					const EPopulationPlacementKind OtherKind =
						State.AcceptedHazards[OtherPlacement].Kind;
					bHasRam |= OtherKind == EPopulationPlacementKind::BatteringRam;
					bHasSpike |= OtherKind == EPopulationPlacementKind::SpikeTrap;
				}
				if (bHasRam)
				{
					++InOutPlan.KindCounts.WheelRamCombinations;
				}
				else if (bHasSpike)
				{
					++InOutPlan.KindCounts.WheelSpikeCombinations;
				}
				else
				{
					++InOutPlan.KindCounts.UnpairedWheels;
				}
				if (AdjacentPlacements.IsEmpty())
				{
					++InOutPlan.KindCounts.LiteralSoloWheels;
				}
			}
			return true;
		}

		bool PlanHazards(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const double FloorTopZCm,
			const float RepresentativeTraversalSeconds,
			const FZeroEscapeHazardPopulationAssembly& Hazards,
			const FZeroEscapePopulationDifficultySettings& Difficulty,
			const FTraversalGraph& Graph,
			const int32 HazardDensityTarget,
			FRandomStream& Rng,
			FPopulationPlacementPlan& InOutPlan,
			TSet<FIntVector>& OutResourceBlockedAddresses,
			FString& OutError)
		{
			FHazardPlanningState State;
			State.AcceptedPlacementIndicesByNode.SetNum(Graph.Addresses.Num());
			State.NearestHazardDistances.Init(
				UnreachableDistance, Graph.Addresses.Num());
			if (!BuildGraphDistances(
					Graph, Plan.PlayerSpawnCoordinate, State.DistanceFromPlayer)
				|| !BuildGraphDistances(
					Graph, Plan.ExitCoordinate, State.DistanceToExit))
			{
				OutError = TEXT("玩家出生点或 Exit 不在完整通行图中。");
				return false;
			}

			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				if (Structure.Kind != EZeroEscapeStructureKind::HighCeilingRoom)
				{
					continue;
				}
				FPlacementCandidate Candidate;
				if (!BuildMandatoryPendulum(
						Plan, Structure, FloorTopZCm, Graph, Candidate))
				{
					OutError = TEXT("高厅不满足正式 1x2 摆锤结构合同。");
					return false;
				}
				const FPlacementVariant& Variant = Candidate.Variants[0];
				FHazardCandidateEvaluation Evaluation;
				if (!EvaluateHazardCandidate(
						Candidate,
						Variant,
						Plan,
						Difficulty.Hazards,
						Hazards.PlacementScoring,
						RepresentativeTraversalSeconds,
						Graph,
						State,
						Evaluation))
				{
					OutError = TEXT("强制摆锤无法建立机关组诊断。");
					return false;
				}
				const int32 PlacementIndex = InOutPlan.HazardPlacements.Num();
				AcceptPlacement(
					Candidate,
					Variant,
					FPopulationPlacementScoreBreakdown(),
					Hazards,
					InOutPlan.HazardPlacements, InOutPlan.KindCounts);
				for (const FIntVector Address : Variant.ResourceBlockedAddresses)
				{
					OutResourceBlockedAddresses.Add(Address);
				}
				if (!RegisterAcceptedHazard(
						Candidate,
						Variant,
						Evaluation,
						Hazards.PlacementScoring,
						RepresentativeTraversalSeconds,
						Graph,
						PlacementIndex,
						State))
				{
					OutError = TEXT("强制摆锤无法提交机关组状态。");
					return false;
				}
			}

			InOutPlan.HazardStats.TargetCount = FMath::Max(
				HazardDensityTarget, InOutPlan.KindCounts.Pendulums);
			TArray<FPlacementCandidate> Spikes;
			TArray<FPlacementCandidate> Rams;
			TArray<FPlacementCandidate> Launchers;
			TArray<FPlacementCandidate> Wheels;
			BuildOrdinaryCandidates(
				Plan,
				FloorTopZCm,
				Hazards,
				Graph,
				Spikes,
				Rams,
				Launchers,
				Wheels);
			InOutPlan.KindCounts.SpikeCandidateAnchors = Spikes.Num();
			InOutPlan.KindCounts.RamCandidateAnchors = Rams.Num();
			InOutPlan.KindCounts.LauncherCandidateAnchors = Launchers.Num();
			InOutPlan.KindCounts.WheelCandidateAnchors = Wheels.Num();
			InOutPlan.HazardStats.CandidateAnchorCount =
				Spikes.Num() + Rams.Num() + Launchers.Num() + Wheels.Num();
			int32 Remaining = InOutPlan.HazardStats.TargetCount
				- InOutPlan.KindCounts.Pendulums;
			while (Remaining > 0)
			{
				TArray<FScoredKindPool> Pools;
				if (!BuildScoredKindPool(
						EPopulationPlacementKind::SpikeTrap,
						Difficulty.Hazards.SpikeTrapWeight,
						Spikes,
						Plan,
						Difficulty.Hazards,
						Hazards.PlacementScoring,
						RepresentativeTraversalSeconds,
						Graph,
						OutResourceBlockedAddresses,
						State,
						Pools,
						OutError)
					|| !BuildScoredKindPool(
						EPopulationPlacementKind::BatteringRam,
						Difficulty.Hazards.BatteringRamWeight,
						Rams,
						Plan,
						Difficulty.Hazards,
						Hazards.PlacementScoring,
						RepresentativeTraversalSeconds,
						Graph,
						OutResourceBlockedAddresses,
						State,
						Pools,
						OutError)
					|| !BuildScoredKindPool(
						EPopulationPlacementKind::GuidedLauncher,
						Difficulty.Hazards.GuidedLauncherWeight,
						Launchers,
						Plan,
						Difficulty.Hazards,
						Hazards.PlacementScoring,
						RepresentativeTraversalSeconds,
						Graph,
						OutResourceBlockedAddresses,
						State,
						Pools,
						OutError)
					|| !BuildScoredKindPool(
						EPopulationPlacementKind::SpikeWheel,
						Difficulty.Hazards.SpikeWheelWeight,
						Wheels,
						Plan,
						Difficulty.Hazards,
						Hazards.PlacementScoring,
						RepresentativeTraversalSeconds,
						Graph,
						OutResourceBlockedAddresses,
						State,
						Pools,
						OutError))
				{
					return false;
				}
				if (Pools.IsEmpty())
				{
					break;
				}
				const int32 PoolIndex = PickWeightedIndex(
					Pools,
					[](const FScoredKindPool& Pool)
					{
						return Pool.TypeWeight;
					},
					Rng);
				if (!Pools.IsValidIndex(PoolIndex))
				{
					OutError = TEXT("机关类型加权抽取失败。");
					return false;
				}
				const FScoredKindPool& Pool = Pools[PoolIndex];
				const int32 AnchorIndex = PickWeightedIndex(
					Pool.Anchors,
					[](const FScoredAnchor& Anchor)
					{
						return Anchor.Weight;
					},
					Rng);
				if (!Pool.Anchors.IsValidIndex(AnchorIndex))
				{
					OutError = TEXT("机关锚点加权抽取失败。");
					return false;
				}
				const FScoredAnchor& Anchor = Pool.Anchors[AnchorIndex];
				const int32 VariantPickIndex = PickWeightedIndex(
					Anchor.Variants,
					[](const FScoredVariant& Variant)
					{
						return Variant.Weight;
					},
					Rng);
				if (!Anchor.Variants.IsValidIndex(VariantPickIndex)
					|| Pool.Candidates == nullptr
					|| !Pool.Candidates->IsValidIndex(Anchor.CandidateIndex))
				{
					OutError = TEXT("机关方向加权抽取失败。");
					return false;
				}

				const FPlacementCandidate& Candidate =
					(*Pool.Candidates)[Anchor.CandidateIndex];
				const FScoredVariant& ScoredVariant =
					Anchor.Variants[VariantPickIndex];
				if (!Candidate.Variants.IsValidIndex(ScoredVariant.VariantIndex))
				{
					OutError = TEXT("机关方向索引非法。");
					return false;
				}
				const FPlacementVariant& Variant =
					Candidate.Variants[ScoredVariant.VariantIndex];
				FHazardCandidateEvaluation Evaluation;
				if (!EvaluateHazardCandidate(
						Candidate,
						Variant,
						Plan,
						Difficulty.Hazards,
						Hazards.PlacementScoring,
						RepresentativeTraversalSeconds,
						Graph,
						State,
						Evaluation))
				{
					OutError = TEXT("选中机关无法重建评分上下文。");
					return false;
				}
				const int32 PlacementIndex = InOutPlan.HazardPlacements.Num();
				AcceptPlacement(
					Candidate,
					Variant,
					Evaluation.Score,
					Hazards,
					InOutPlan.HazardPlacements, InOutPlan.KindCounts);
				for (const FIntVector Address : Variant.ResourceBlockedAddresses)
				{
					OutResourceBlockedAddresses.Add(Address);
				}
				if (!RegisterAcceptedHazard(
						Candidate,
						Variant,
						Evaluation,
						Hazards.PlacementScoring,
						RepresentativeTraversalSeconds,
						Graph,
						PlacementIndex,
						State))
				{
					OutError = TEXT("选中机关无法提交组状态。");
					return false;
				}
				--Remaining;
			}
			InOutPlan.HazardStats.ActualCount = InOutPlan.HazardPlacements.Num();
			InOutPlan.HazardStats.UnderfilledCount = FMath::Max(
				0, InOutPlan.HazardStats.TargetCount - InOutPlan.HazardStats.ActualCount);
			return FinalizeHazardGroups(
				Plan,
				Difficulty.Hazards,
				Hazards.PlacementScoring,
				Graph,
				OutResourceBlockedAddresses,
				State,
				InOutPlan,
				OutError);
		}

		void PlanResources(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const double FloorTopZCm,
			const FZeroEscapeResourcePopulationAssembly& Resources,
			const FZeroEscapePopulationDifficultySettings& Difficulty,
			const FTraversalGraph& Graph,
			const int32 ResourceTarget,
			const TSet<FIntVector>& ResourceBlockedAddresses,
			FRandomStream& Rng,
			FPopulationPlacementPlan& InOutPlan)
		{
			// 机关层已经完成且不再消费随机数；资源只读取其纯值诊断，
			// 因而资源支持参数和资源随机流都不会反向洗牌机关。
			TMap<FIntVector, float> SupportPriorityByAddress;
			for (const FPopulationHazardGroupRecord& Group : InOutPlan.HazardGroups)
			{
				const float Priority = FMath::Clamp(
					Group.ResourceSupportPriority, 0.0f, 1.0f);
				for (const FIntVector Address : Group.SafeApproachAddresses)
				{
					float& ExistingPriority =
						SupportPriorityByAddress.FindOrAdd(Address);
					ExistingPriority = FMath::Max(ExistingPriority, Priority);
				}
			}

			TArray<FIntVector> RemainingAnchors;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				if (Cell.Coordinate != Plan.PlayerSpawnCoordinate
					&& Cell.Coordinate != Plan.PursuerSpawnCoordinate
					&& Cell.Coordinate != Plan.ExitCoordinate
					&& !ResourceBlockedAddresses.Contains(Cell.Coordinate))
				{
					RemainingAnchors.Add(Cell.Coordinate);
				}
			}
			RemainingAnchors.Sort(CoordinateLess);
			InOutPlan.ResourceStats.TargetCount = ResourceTarget;
			InOutPlan.ResourceStats.CandidateAnchorCount = RemainingAnchors.Num();
			TArray<int32> NearestResourceDistances;
			NearestResourceDistances.Init(UnreachableDistance, Graph.Addresses.Num());
			TArray<int32> NearestContentDistances;
			NearestContentDistances.Init(UnreachableDistance, Graph.Addresses.Num());
			TArray<int32> HazardNodes;
			for (const FIntVector Address : ResourceBlockedAddresses)
			{
				if (const int32* Node = Graph.NodeByAddress.Find(Address))
				{
					HazardNodes.Add(*Node);
				}
			}
			HazardNodes.Sort();
			UpdateNearestDistances(Graph, HazardNodes, NearestContentDistances);
			TArray<FIntVector> SelectedAnchors;
			while (SelectedAnchors.Num() < ResourceTarget && !RemainingAnchors.IsEmpty())
			{
				// 资源之间的物理间距仍是合法性边界；先稳定移除不再可行的锚点，
				// 再在全部合法锚点中做非零软加权，避免“抽中后拒绝”稀释支持效果。
				for (int32 Index = RemainingAnchors.Num() - 1; Index >= 0; --Index)
				{
					const int32 Node = Graph.NodeByAddress.FindChecked(
						RemainingAnchors[Index]);
					if (NearestResourceDistances[Node]
						< Difficulty.Resources.MinimumRouteSpacingTiles)
					{
						RemainingAnchors.RemoveAt(Index, 1, EAllowShrinking::No);
						++InOutPlan.ResourceStats.SpacingRejectedCount;
					}
				}
				if (RemainingAnchors.IsEmpty())
				{
					break;
				}

				const int32 PickIndex = PickWeightedIndex(
					RemainingAnchors,
					[&SupportPriorityByAddress,
						&Resources,
						&Difficulty,
						&Graph,
						&NearestContentDistances](const FIntVector Address)
					{
						const float* Priority = SupportPriorityByAddress.Find(Address);
						const double SupportLog2 = static_cast<double>(
							Priority == nullptr ? 0.0f : *Priority)
							* Resources.HighPressureSupportLog2Bonus;
						const int32 Node = Graph.NodeByAddress.FindChecked(Address);
						const int32 NearestDistance = NearestContentDistances[Node];
						const float CoverageSignal = NearestDistance == UnreachableDistance
							? 1.0f
							: FMath::Clamp(
								static_cast<float>(NearestDistance
									- Difficulty.Resources.MinimumRouteSpacingTiles)
									/ Difficulty.Resources.MinimumRouteSpacingTiles,
								0.0f,
								1.0f);
						const double CoverageLog2 = static_cast<double>(CoverageSignal)
							* Resources.RouteCoverageLog2Bonus;
						return FMath::Pow(2.0, SupportLog2 + CoverageLog2);
					},
					Rng);
				if (!RemainingAnchors.IsValidIndex(PickIndex))
				{
					break;
				}
				const FIntVector Address = RemainingAnchors[PickIndex];
				RemainingAnchors.RemoveAt(PickIndex, 1, EAllowShrinking::No);
				const int32 Node = Graph.NodeByAddress.FindChecked(Address);
				SelectedAnchors.Add(Address);
				UpdateNearestDistances(
					Graph, TConstArrayView<int32>(&Node, 1), NearestResourceDistances);
				UpdateNearestDistances(
					Graph, TConstArrayView<int32>(&Node, 1), NearestContentDistances);
			}

			const float SafeHalfRange = static_cast<float>(
				Plan.LogicalTileSizeCm * 0.5 - Resources.PlacementFootprintRadiusCm);
			for (const FIntVector Address : SelectedAnchors)
			{
				const FVector FloorCenter = AddressFloorLocation(Plan, Address, FloorTopZCm);
				const FVector Offset(
					Rng.FRandRange(-SafeHalfRange, SafeHalfRange),
					Rng.FRandRange(-SafeHalfRange, SafeHalfRange),
					Resources.SpawnZOffsetCm);
				FPopulationPlannedPlacement Placement;
				Placement.Kind = EPopulationPlacementKind::MagneticResource;
				Placement.AnchorAddress = Address;
				Placement.LocalSpawnTransforms.Add(FTransform(
					FQuat::Identity, FloorCenter + Offset));
				InOutPlan.ResourcePlacements.Add(MoveTemp(Placement));
			}
			InOutPlan.KindCounts.MagneticResources = InOutPlan.ResourcePlacements.Num();
			InOutPlan.ResourceStats.ActualCount = InOutPlan.ResourcePlacements.Num();
			InOutPlan.ResourceStats.UnderfilledCount = FMath::Max(
				0, ResourceTarget - InOutPlan.ResourceStats.ActualCount);
		}

		bool ValidateActorBudget(
			const FPopulationPlacementPlan& Plan,
			FString& OutError)
		{
			int64 ActorCount = 0;
			for (const FPopulationPlannedPlacement& Placement : Plan.HazardPlacements)
			{
				ActorCount += Placement.LocalSpawnTransforms.Num();
				if (Placement.Kind == EPopulationPlacementKind::GuidedLauncher)
				{
					++ActorCount;
				}
			}
			for (const FPopulationPlannedPlacement& Placement : Plan.ResourcePlacements)
			{
				ActorCount += Placement.LocalSpawnTransforms.Num();
			}
			const int64 Maximum = static_cast<int64>(GenerationLimits::MaxGridCells)
				* GenerationLimits::MaxFloorCount;
			if (ActorCount > Maximum)
			{
				OutError = TEXT("Population 计划 Actor 数超过安全预算。");
				return false;
			}
			return true;
		}
	}

	EPopulationPlacementResult FPopulationPlacementPolicy::BuildPlan(
		const FZeroEscapeGeneratedLevelPlan& LevelPlan,
		const double FloorTopZCm,
		const float PlayerMaxWalkSpeedCmPerSecond,
		const FZeroEscapeHazardPopulationAssembly& HazardAssembly,
		const FZeroEscapeResourcePopulationAssembly& ResourceAssembly,
		const TConstArrayView<FZeroEscapePopulationDifficultySettings> Difficulties,
		FPopulationPlacementPlan& OutPlan,
		FString& OutError)
	{
		OutPlan = FPopulationPlacementPlan();
		OutError.Reset();
		if (!FMath::IsFinite(PlayerMaxWalkSpeedCmPerSecond)
			|| PlayerMaxWalkSpeedCmPerSecond <= 0.0f)
		{
			OutError = TEXT("玩家名义地面速度必须为有限正数。");
			return EPopulationPlacementResult::InvalidConfiguration;
		}
		const FZeroEscapePopulationDifficultySettings* Difficulty =
			ValidateAndFindDifficulty(
				Difficulties, LevelPlan.Signature.Difficulty, OutError);
		if (Difficulty == nullptr)
		{
			return EPopulationPlacementResult::InvalidConfiguration;
		}
		int32 HazardDensityTarget = 0;
		int32 ResourceTarget = 0;
		if (!ValidateAssemblyAndTargets(
				LevelPlan,
				FloorTopZCm,
				HazardAssembly,
				ResourceAssembly,
				*Difficulty,
				HazardDensityTarget,
				ResourceTarget,
				OutError))
		{
			return EPopulationPlacementResult::InvalidConfiguration;
		}
		const double TraversalSeconds = LevelPlan.LogicalTileSizeCm
			/ static_cast<double>(PlayerMaxWalkSpeedCmPerSecond);
		if (!FMath::IsFinite(TraversalSeconds)
			|| TraversalSeconds <= 0.0
			|| TraversalSeconds > TNumericLimits<float>::Max())
		{
			OutError = TEXT("玩家速度与逻辑格宽无法得到有限通行时间。");
			return EPopulationPlacementResult::InvalidConfiguration;
		}
		const float RepresentativeTraversalSeconds =
			static_cast<float>(TraversalSeconds);

		FTraversalGraph Graph;
		if (!BuildTraversalGraph(LevelPlan, Graph, OutError))
		{
			return EPopulationPlacementResult::InvalidTraversalGraph;
		}
		FRandomStream HazardRng = FGenerationCore::MakeRandomStream(
			LevelPlan.Signature.Seed,
			ERandomDomain::HazardPopulationPlacement);
		FRandomStream ResourceRng = FGenerationCore::MakeRandomStream(
			LevelPlan.Signature.Seed,
			ERandomDomain::ResourcePopulationPlacement);
		FPopulationPlacementPlan WorkingPlan;
		TSet<FIntVector> ResourceBlockedAddresses;
		if (!PlanHazards(
				LevelPlan,
				FloorTopZCm,
				RepresentativeTraversalSeconds,
				HazardAssembly,
				*Difficulty,
				Graph,
				HazardDensityTarget,
				HazardRng,
				WorkingPlan,
				ResourceBlockedAddresses,
				OutError))
		{
			return EPopulationPlacementResult::InvalidPlan;
		}
		PlanResources(
			LevelPlan,
			FloorTopZCm,
			ResourceAssembly,
			*Difficulty,
			Graph,
			ResourceTarget,
			ResourceBlockedAddresses,
			ResourceRng,
			WorkingPlan);
		if (!ValidateActorBudget(WorkingPlan, OutError))
		{
			return EPopulationPlacementResult::SpawnBudgetExceeded;
		}
		OutPlan = MoveTemp(WorkingPlan);
		return EPopulationPlacementResult::Success;
	}
}
