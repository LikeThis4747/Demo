// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePopulationPlacementPolicy.cpp
 * 职责：在完整三维通行图上枚举合法候选并执行机关/资源两次有限候选泊松拒绝采样。
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
		};

		struct FPlacementCandidate
		{
			EPopulationPlacementKind Kind = EPopulationPlacementKind::SpikeTrap;
			FIntVector AnchorAddress = FIntVector::ZeroValue;
			TArray<int32> SpacingNodeIndices;
			TArray<FPlacementVariant> Variants;
		};

		struct FFeasiblePool
		{
			EPopulationPlacementKind Kind = EPopulationPlacementKind::SpikeTrap;
			int32 Weight = 0;
			const TArray<FPlacementCandidate>* Candidates = nullptr;
			TArray<int32> CandidateIndices;
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
					|| Difficulty.Hazards.MinimumRouteSpacingTiles < 1
					|| Difficulty.Hazards.SpikeTrapWeight < 0
					|| Difficulty.Hazards.BatteringRamWeight < 0
					|| Difficulty.Hazards.GuidedLauncherWeight < 0
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
				|| !FMath::IsFinite(Resources.SpawnZOffsetCm)
				|| !FMath::IsFinite(Resources.PlacementFootprintRadiusCm)
				|| Resources.PlacementFootprintRadiusCm < 0.0f
				|| Resources.PlacementFootprintRadiusCm >= Plan.LogicalTileSizeCm * 0.5)
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
			const int64 WorstActorCount = MandatoryPendulumCount
				+ MaximumOrdinaryHazards * MaximumActorsPerOrdinaryHazard
				+ OutResourceTarget;
			const int64 WeightSum = static_cast<int64>(Difficulty.Hazards.SpikeTrapWeight)
				+ Difficulty.Hazards.BatteringRamWeight
				+ Difficulty.Hazards.GuidedLauncherWeight;
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
			const int32* FirstNode = Graph.NodeByAddress.Find(First);
			const int32* SecondNode = Graph.NodeByAddress.Find(Second);
			if (FirstNode == nullptr || SecondNode == nullptr)
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
			OutCandidate.SpacingNodeIndices = { *FirstNode, *SecondNode };
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
			TArray<FPlacementCandidate>& OutLaunchers)
		{
			TMap<FIntVector, const FZeroEscapeGeneratedOrdinaryCell*> OrdinaryByAddress;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				OrdinaryByAddress.Add(Cell.Coordinate, &Cell);
			}
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				if (Cell.Coordinate == Plan.PlayerSpawnCoordinate
					|| Cell.Coordinate == Plan.PursuerSpawnCoordinate
					|| Cell.Coordinate == Plan.ExitCoordinate)
				{
					continue;
				}
				const int32* Node = Graph.NodeByAddress.Find(Cell.Coordinate);
				if (Node == nullptr)
				{
					continue;
				}
				const FVector FloorCenter = AddressFloorLocation(Plan, Cell.Coordinate, FloorTopZCm);
				FPlacementCandidate Spike;
				Spike.Kind = EPopulationPlacementKind::SpikeTrap;
				Spike.AnchorAddress = Cell.Coordinate;
				Spike.SpacingNodeIndices.Add(*Node);
				FPlacementCandidate Ram;
				Ram.Kind = EPopulationPlacementKind::BatteringRam;
				Ram.AnchorAddress = Cell.Coordinate;
				Ram.SpacingNodeIndices.Add(*Node);
				FPlacementCandidate Launcher;
				Launcher.Kind = EPopulationPlacementKind::GuidedLauncher;
				Launcher.AnchorAddress = Cell.Coordinate;
				Launcher.SpacingNodeIndices.Add(*Node);

				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					const bool bOpen = (Cell.OpeningMask & Grid::DirectionBit(Direction)) != 0;
					const bool bOpenToOrdinary = IsOrdinaryNeighborOpen(
						Plan, OrdinaryByAddress, Cell, Direction);
					const FVector Forward = DirectionVector(Direction);
					if (bOpen)
					{
						FPlacementVariant Variant;
						const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
						Variant.LocalSpawnTransform = FTransform(
							Forward.Rotation(),
							FloorCenter + FVector(0.0, 0.0, Hazards.SpikeTrapFloorOffsetCm));
						Variant.SpikeLateralAxis = Right;
						Variant.ResourceBlockedAddresses.Add(Cell.Coordinate);
						Spike.Variants.Add(MoveTemp(Variant));
					}
					else
					{
						const FVector ForwardIntoCell = -Forward;
						FPlacementVariant Variant;
						Variant.LocalSpawnTransform = MakeWallMountedTransform(
							FloorCenter,
							ForwardIntoCell,
							Plan.LogicalTileSizeCm * 0.5,
							Hazards.BatteringRamWallInsetCm,
							Hazards.BatteringRamMountHeightCm);
						Variant.ResourceBlockedAddresses.Add(Cell.Coordinate);
						Ram.Variants.Add(MoveTemp(Variant));
					}

					const uint8 RearDirection = Grid::OppositeDirectionIndex(Direction);
					const bool bRearClosed =
						(Cell.OpeningMask & Grid::DirectionBit(RearDirection)) == 0;
					const FIntVector FrontAddress = StepAddress(Cell.Coordinate, Direction);
					if (bOpenToOrdinary && bRearClosed
						&& !IsFlowCoordinate(Plan, FrontAddress))
					{
						FPlacementVariant Variant;
						Variant.LocalSpawnTransform = MakeWallMountedTransform(
							FloorCenter,
							Forward,
							Plan.LogicalTileSizeCm * 0.5,
							Hazards.GuidedLauncherWallInsetCm,
							Hazards.GuidedLauncherMountHeightCm);
						Variant.ResourceBlockedAddresses = {
							Cell.Coordinate, FrontAddress };
						Launcher.Variants.Add(MoveTemp(Variant));
					}
				}
				if (!Spike.Variants.IsEmpty())
				{
					OutSpikes.Add(MoveTemp(Spike));
				}
				if (!Ram.Variants.IsEmpty())
				{
					OutRams.Add(MoveTemp(Ram));
				}
				if (!Launcher.Variants.IsEmpty())
				{
					OutLaunchers.Add(MoveTemp(Launcher));
				}
			}
			auto CandidateLess = [](const FPlacementCandidate& A, const FPlacementCandidate& B)
			{
				return CoordinateLess(A.AnchorAddress, B.AnchorAddress);
			};
			OutSpikes.Sort(CandidateLess);
			OutRams.Sort(CandidateLess);
			OutLaunchers.Sort(CandidateLess);
		}

		bool CandidateMeetsSpacing(
			const FPlacementCandidate& Candidate,
			const TArray<int32>& NearestDistances,
			const int32 MinimumSpacing)
		{
			for (const int32 Node : Candidate.SpacingNodeIndices)
			{
				if (!NearestDistances.IsValidIndex(Node)
					|| NearestDistances[Node] < MinimumSpacing)
				{
					return false;
				}
			}
			return true;
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

		bool CandidateHasConflictFreeVariant(
			const FPlacementCandidate& Candidate,
			const TSet<FIntVector>& AcceptedOperationAddresses)
		{
			for (const FPlacementVariant& Variant : Candidate.Variants)
			{
				if (!VariantHasOperationConflict(Variant, AcceptedOperationAddresses))
				{
					return true;
				}
			}
			return false;
		}

		void BuildFeasibleVariantIndices(
			const FPlacementCandidate& Candidate,
			const TSet<FIntVector>& AcceptedOperationAddresses,
			TArray<int32>& OutVariantIndices)
		{
			OutVariantIndices.Reset();
			for (int32 Index = 0; Index < Candidate.Variants.Num(); ++Index)
			{
				if (!VariantHasOperationConflict(
						Candidate.Variants[Index], AcceptedOperationAddresses))
				{
					OutVariantIndices.Add(Index);
				}
			}
		}

		void BuildFeasiblePool(
			const EPopulationPlacementKind Kind,
			const int32 Weight,
			const TArray<FPlacementCandidate>& Candidates,
			const TArray<int32>& NearestDistances,
			const int32 MinimumSpacing,
			const TSet<FIntVector>& AcceptedAnchors,
			const TSet<FIntVector>& AcceptedOperationAddresses,
			TArray<FFeasiblePool>& OutPools)
		{
			if (Weight <= 0)
			{
				return;
			}
			FFeasiblePool Pool;
			Pool.Kind = Kind;
			Pool.Weight = Weight;
			Pool.Candidates = &Candidates;
			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				if (!AcceptedAnchors.Contains(Candidates[Index].AnchorAddress)
					&& CandidateMeetsSpacing(
						Candidates[Index], NearestDistances, MinimumSpacing)
					&& CandidateHasConflictFreeVariant(
						Candidates[Index], AcceptedOperationAddresses))
				{
					Pool.CandidateIndices.Add(Index);
				}
			}
			if (!Pool.CandidateIndices.IsEmpty())
			{
				OutPools.Add(MoveTemp(Pool));
			}
		}

		int32 PickWeightedPool(const TArray<FFeasiblePool>& Pools, FRandomStream& Rng)
		{
			int64 TotalWeight = 0;
			for (const FFeasiblePool& Pool : Pools)
			{
				TotalWeight += Pool.Weight;
			}
			const int64 Draw = Rng.RandRange(1, static_cast<int32>(TotalWeight));
			int64 Running = 0;
			for (int32 Index = 0; Index < Pools.Num(); ++Index)
			{
				Running += Pools[Index].Weight;
				if (Draw <= Running)
				{
					return Index;
				}
			}
			return Pools.Num() - 1;
		}

		void AcceptPlacement(
			const FPlacementCandidate& Candidate,
			const FPlacementVariant& Variant,
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
			OutPlacements.Add(MoveTemp(Placement));
			switch (Candidate.Kind)
			{
			case EPopulationPlacementKind::Pendulum: ++InOutCounts.Pendulums; break;
			case EPopulationPlacementKind::SpikeTrap: ++InOutCounts.SpikeTrapGroups; break;
			case EPopulationPlacementKind::BatteringRam: ++InOutCounts.BatteringRams; break;
			case EPopulationPlacementKind::GuidedLauncher: ++InOutCounts.GuidedLaunchers; break;
			default: break;
			}
		}

		bool PlanHazards(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const double FloorTopZCm,
			const FZeroEscapeHazardPopulationAssembly& Hazards,
			const FZeroEscapePopulationDifficultySettings& Difficulty,
			const FTraversalGraph& Graph,
			const int32 HazardDensityTarget,
			FRandomStream& Rng,
			FPopulationPlacementPlan& InOutPlan,
			TSet<FIntVector>& OutResourceBlockedAddresses,
			FString& OutError)
		{
			TArray<int32> NearestDistances;
			NearestDistances.Init(UnreachableDistance, Graph.Addresses.Num());
			TSet<FIntVector> AcceptedAnchors;
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
				AcceptPlacement(
					Candidate, Variant, Hazards,
					InOutPlan.HazardPlacements, InOutPlan.KindCounts);
				AcceptedAnchors.Add(Candidate.AnchorAddress);
				for (const FIntVector Address : Variant.ResourceBlockedAddresses)
				{
					OutResourceBlockedAddresses.Add(Address);
				}
				UpdateNearestDistances(Graph, Candidate.SpacingNodeIndices, NearestDistances);
			}

			InOutPlan.HazardStats.TargetCount = FMath::Max(
				HazardDensityTarget, InOutPlan.KindCounts.Pendulums);
			TArray<FPlacementCandidate> Spikes;
			TArray<FPlacementCandidate> Rams;
			TArray<FPlacementCandidate> Launchers;
			BuildOrdinaryCandidates(
				Plan, FloorTopZCm, Hazards, Graph, Spikes, Rams, Launchers);
			InOutPlan.KindCounts.SpikeCandidateAnchors = Spikes.Num();
			InOutPlan.KindCounts.RamCandidateAnchors = Rams.Num();
			InOutPlan.KindCounts.LauncherCandidateAnchors = Launchers.Num();
			InOutPlan.HazardStats.CandidateAnchorCount =
				Spikes.Num() + Rams.Num() + Launchers.Num();
			int32 Remaining = InOutPlan.HazardStats.TargetCount
				- InOutPlan.KindCounts.Pendulums;
			while (Remaining > 0)
			{
				TArray<FFeasiblePool> Pools;
				BuildFeasiblePool(EPopulationPlacementKind::SpikeTrap,
					Difficulty.Hazards.SpikeTrapWeight, Spikes, NearestDistances,
					Difficulty.Hazards.MinimumRouteSpacingTiles, AcceptedAnchors,
					OutResourceBlockedAddresses, Pools);
				BuildFeasiblePool(EPopulationPlacementKind::BatteringRam,
					Difficulty.Hazards.BatteringRamWeight, Rams, NearestDistances,
					Difficulty.Hazards.MinimumRouteSpacingTiles, AcceptedAnchors,
					OutResourceBlockedAddresses, Pools);
				BuildFeasiblePool(EPopulationPlacementKind::GuidedLauncher,
					Difficulty.Hazards.GuidedLauncherWeight, Launchers, NearestDistances,
					Difficulty.Hazards.MinimumRouteSpacingTiles, AcceptedAnchors,
					OutResourceBlockedAddresses, Pools);
				if (Pools.IsEmpty())
				{
					break;
				}
				const FFeasiblePool& Pool = Pools[PickWeightedPool(Pools, Rng)];
				const int32 CandidateIndex = Pool.CandidateIndices[
					Rng.RandRange(0, Pool.CandidateIndices.Num() - 1)];
				const FPlacementCandidate& Candidate = (*Pool.Candidates)[CandidateIndex];
				TArray<int32> FeasibleVariantIndices;
				BuildFeasibleVariantIndices(
					Candidate, OutResourceBlockedAddresses, FeasibleVariantIndices);
				if (FeasibleVariantIndices.IsEmpty())
				{
					OutError = TEXT("机关候选的可用方向在抽取期间失效。");
					return false;
				}
				const FPlacementVariant& Variant = Candidate.Variants[
					FeasibleVariantIndices[
						Rng.RandRange(0, FeasibleVariantIndices.Num() - 1)]];
				AcceptPlacement(
					Candidate, Variant, Hazards,
					InOutPlan.HazardPlacements, InOutPlan.KindCounts);
				AcceptedAnchors.Add(Candidate.AnchorAddress);
				for (const FIntVector Address : Variant.ResourceBlockedAddresses)
				{
					OutResourceBlockedAddresses.Add(Address);
				}
				UpdateNearestDistances(Graph, Candidate.SpacingNodeIndices, NearestDistances);
				--Remaining;
			}
			InOutPlan.HazardStats.ActualCount = InOutPlan.HazardPlacements.Num();
			InOutPlan.HazardStats.UnderfilledCount = FMath::Max(
				0, InOutPlan.HazardStats.TargetCount - InOutPlan.HazardStats.ActualCount);
			return true;
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
			TArray<int32> NearestDistances;
			NearestDistances.Init(UnreachableDistance, Graph.Addresses.Num());
			TArray<FIntVector> SelectedAnchors;
			while (SelectedAnchors.Num() < ResourceTarget && !RemainingAnchors.IsEmpty())
			{
				const int32 PickIndex = Rng.RandRange(0, RemainingAnchors.Num() - 1);
				const FIntVector Address = RemainingAnchors[PickIndex];
				RemainingAnchors.RemoveAtSwap(PickIndex, 1, EAllowShrinking::No);
				const int32 Node = Graph.NodeByAddress.FindChecked(Address);
				if (NearestDistances[Node] < Difficulty.Resources.MinimumRouteSpacingTiles)
				{
					++InOutPlan.ResourceStats.SpacingRejectedCount;
					continue;
				}
				SelectedAnchors.Add(Address);
				UpdateNearestDistances(
					Graph, TConstArrayView<int32>(&Node, 1), NearestDistances);
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
		const FZeroEscapeHazardPopulationAssembly& HazardAssembly,
		const FZeroEscapeResourcePopulationAssembly& ResourceAssembly,
		const TConstArrayView<FZeroEscapePopulationDifficultySettings> Difficulties,
		FPopulationPlacementPlan& OutPlan,
		FString& OutError)
	{
		OutPlan = FPopulationPlacementPlan();
		OutError.Reset();
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
