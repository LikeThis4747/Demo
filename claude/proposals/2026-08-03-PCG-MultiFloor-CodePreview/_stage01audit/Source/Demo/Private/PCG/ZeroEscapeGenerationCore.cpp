// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationCore.cpp
 * 职责：实现单次配置解析、确定性随机域和纯空间 Layout Hash。
 * 边界：不生成道路，不执行 WFC，不访问 World，也不保存任何玩法完成条件。
 */

#include "PCG/ZeroEscapeGenerationCore.h"

#include "Containers/StringConv.h"

namespace ZeroEscape::LevelGeneration
{
	namespace GenerationCorePrivate
	{
		inline constexpr uint64 HashOffset = 1469598103934665603ull;
		inline constexpr uint64 HashPrime = 1099511628211ull;

		/** 按字节混入整数，避免结构体填充和平台内存布局进入 Hash。 */
		void HashUInt64(uint64& Hash, const uint64 Value)
		{
			for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				Hash ^= (Value >> (ByteIndex * 8)) & 0xFFu;
				Hash *= HashPrime;
			}
		}

		/** FName 必须按字符串内容写入 Hash，不能使用本进程的 Name 索引。 */
		void HashName(uint64& Hash, const FName Name)
		{
			const FString Text = Name.ToString();
			const FTCHARToUTF8 Utf8(*Text);
			HashUInt64(Hash, static_cast<uint64>(Utf8.Length()));
			for (int32 Index = 0; Index < Utf8.Length(); ++Index)
			{
				Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
				Hash *= HashPrime;
			}
		}

		void HashCoordinate(uint64& Hash, const FIntVector Coordinate)
		{
			HashUInt64(Hash, static_cast<uint64>(Coordinate.X));
			HashUInt64(Hash, static_cast<uint64>(Coordinate.Y));
			HashUInt64(Hash, static_cast<uint64>(Coordinate.Z));
		}

		/** 精确写入 IEEE 754 bit，避免不同配置因十进制定点舍入得到同一 Hash。 */
		void HashDouble(uint64& Hash, const double Value)
		{
			static_assert(sizeof(double) == sizeof(uint64));
			uint64 Bits = 0;
			FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
			HashUInt64(Hash, Bits);
		}

		bool IsInsideBuilding(
			const FIntVector Coordinate,
			const FIntPoint GridSize,
			const int32 FloorCount)
		{
			return Coordinate.Z >= 0 && Coordinate.Z < FloorCount
				&& Grid::IsInside(FIntPoint(Coordinate.X, Coordinate.Y), GridSize);
		}

		bool CoordinateLess(const FIntVector A, const FIntVector B)
		{
			return A.Z != B.Z ? A.Z < B.Z
				: A.Y != B.Y ? A.Y < B.Y
				: A.X < B.X;
		}

		bool NameLess(const FName A, const FName B)
		{
			return A.ToString().Compare(B.ToString()) < 0;
		}

		bool HashCoordinateArray(
			uint64& Hash,
			const TArray<FIntVector>& Coordinates,
			const FIntPoint GridSize,
			const int32 FloorCount)
		{
			HashUInt64(Hash, Coordinates.Num());
			FIntVector Previous;
			bool bHasPrevious = false;
			for (const FIntVector Coordinate : Coordinates)
			{
				if (!IsInsideBuilding(Coordinate, GridSize, FloorCount)
					|| (bHasPrevious && !CoordinateLess(Previous, Coordinate)))
				{
					return false;
				}
				HashCoordinate(Hash, Coordinate);
				Previous = Coordinate;
				bHasPrevious = true;
			}
			return true;
		}

		/** 文件专属失败入口，避免 Unity Build 中匿名命名空间的同名函数冲突。 */
		bool FailCore(
			FZeroEscapeGenerationReport& Report,
			const EZeroEscapeGenerationStage Stage,
			const EZeroEscapeGenerationFailure Failure,
			const FString& Message)
		{
			Report.Stage = Stage;
			Report.Failure = Failure;
			Report.Message = Message;
			return false;
		}
	}

	bool FGenerationCore::ResolveGenerationInput(
		const UZeroEscapeLevelGenerationProfile& Profile,
		const FZeroEscapeGenerationRequest& Request,
		const int32 PresentationVersion,
		FResolvedGenerationInput& OutInput,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutInput = {};
		OutReport = {};
		FString Error;
		if (!Profile.IsConfigured(Error) || PresentationVersion <= 0)
		{
			if (Error.IsEmpty())
			{
				Error = TEXT("PresentationVersion 必须大于 0。");
			}
			return GenerationCorePrivate::FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				Error);
		}

		const FZeroEscapeDifficultyDefinition* Difficulty =
			Profile.Difficulties.FindByPredicate(
				[&Request](const FZeroEscapeDifficultyDefinition& Candidate)
				{
					return Candidate.Difficulty == Request.Difficulty;
				});
		if (Difficulty == nullptr)
		{
			return GenerationCorePrivate::FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("Request.Difficulty 无法从 Generation Profile 解析。"));
		}

		OutInput.SharedRules = Profile.SharedRouteConstraints;
		OutInput.Budget = Profile.SharedBudget;
		OutInput.Difficulty = *Difficulty;
		OutInput.Difficulty.FloorCountOptions.Sort(
			[](const FZeroEscapeFloorCountOption& A, const FZeroEscapeFloorCountOption& B)
			{
				return A.FloorCount < B.FloorCount;
			});
		for (FZeroEscapeFloorCountOption& Option : OutInput.Difficulty.FloorCountOptions)
		{
			Option.HighCeilingRoomTargetCounts.Sort(
				[](const FZeroEscapeWeightedCount& A, const FZeroEscapeWeightedCount& B)
				{
					return A.Count < B.Count;
				});
		}
		OutInput.StructureDefinitions = Profile.StructureDefinitions;
		OutInput.StructureDefinitions.Sort(
			[](const FZeroEscapeStructureDefinition& A, const FZeroEscapeStructureDefinition& B)
			{
				if (A.Kind != B.Kind)
				{
					return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
				}
				return A.DefinitionId.ToString().Compare(B.DefinitionId.ToString()) < 0;
			});
		for (FZeroEscapeStructureDefinition& Definition : OutInput.StructureDefinitions)
		{
			Definition.WalkableCells.Sort(GenerationCorePrivate::CoordinateLess);
			Definition.SolidCells.Sort(GenerationCorePrivate::CoordinateLess);
			Definition.ClearanceCells.Sort(GenerationCorePrivate::CoordinateLess);
			for (FZeroEscapeLocalCellConnection& Connection : Definition.InternalConnections)
			{
				if (GenerationCorePrivate::CoordinateLess(
					Connection.SecondCell, Connection.FirstCell))
				{
					Swap(Connection.FirstCell, Connection.SecondCell);
				}
			}
			Definition.InternalConnections.Sort(
				[](const FZeroEscapeLocalCellConnection& A,
					const FZeroEscapeLocalCellConnection& B)
				{
					if (A.FirstCell != B.FirstCell)
					{
						return GenerationCorePrivate::CoordinateLess(
							A.FirstCell, B.FirstCell);
					}
					return GenerationCorePrivate::CoordinateLess(
						A.SecondCell, B.SecondCell);
				});
			Definition.Openings.Sort(
				[](const FZeroEscapeStructureOpeningDefinition& A,
					const FZeroEscapeStructureOpeningDefinition& B)
				{
					return GenerationCorePrivate::NameLess(A.OpeningId, B.OpeningId);
				});
			Definition.Landings.Sort(
				[](const FZeroEscapeStructureLandingDefinition& A,
					const FZeroEscapeStructureLandingDefinition& B)
				{
					return GenerationCorePrivate::NameLess(A.LandingId, B.LandingId);
				});
			for (FZeroEscapeStructureOpeningSetDefinition& OpeningSet :
				Definition.AllowedOpeningSets)
			{
				OpeningSet.OpenOpeningIds.Sort(GenerationCorePrivate::NameLess);
			}
			Definition.AllowedOpeningSets.Sort(
				[](const FZeroEscapeStructureOpeningSetDefinition& A,
					const FZeroEscapeStructureOpeningSetDefinition& B)
				{
					return GenerationCorePrivate::NameLess(A.SetId, B.SetId);
				});
		}
		OutInput.WfcShapeWeights = Difficulty->WfcShapeWeights;
		OutInput.Signature.Seed = Request.Seed;
		OutInput.Signature.Difficulty = Request.Difficulty;
		OutInput.Signature.AlgorithmVersion = GAlgorithmVersion;
		OutInput.Signature.GenerationProfileVersion = Profile.ProfileVersion;
		OutInput.Signature.PresentationVersion = PresentationVersion;
		return true;
	}

	FRandomStream FGenerationCore::MakeRandomStream(
		const int32 MasterSeed,
		const int32 AlgorithmVersion,
		const ERandomDomain Domain,
		const int32 Salt)
	{
		// SplitMix 风格整数混合只派生 Seed；实际随机序列仍由 UE FRandomStream 提供。
		uint32 Mixed = static_cast<uint32>(MasterSeed);
		Mixed ^= static_cast<uint32>(AlgorithmVersion) * 0x9E3779B9u;
		Mixed ^= static_cast<uint32>(Domain);
		Mixed ^= static_cast<uint32>(Salt) * 0x85EBCA6Bu;
		Mixed ^= Mixed >> 16;
		Mixed *= 0x7FEB352Du;
		Mixed ^= Mixed >> 15;
		Mixed *= 0x846CA68Bu;
		Mixed ^= Mixed >> 16;
		return FRandomStream(static_cast<int32>(Mixed));
	}

	int64 FGenerationCore::ComputeCanonicalLayoutHash(
		const FZeroEscapeGeneratedLevelPlan& Plan)
	{
		if (Plan.Signature.AlgorithmVersion <= 0
			|| Plan.Signature.GenerationProfileVersion <= 0
			|| Plan.FloorCount < GenerationLimits::MinFloorCount
			|| Plan.FloorCount > GenerationLimits::MaxFloorCount
			|| Plan.GridSize.X <= 0 || Plan.GridSize.Y <= 0
			|| !FMath::IsFinite(Plan.LogicalTileSizeCm)
			|| Plan.LogicalTileSizeCm <= 0.0
			|| !FMath::IsFinite(Plan.FloorHeightCm)
			|| Plan.FloorHeightCm <= 0.0
			|| !FMath::IsFinite(Plan.AnchorHeightCm)
			|| Plan.AnchorHeightCm < 0.0
			|| Plan.OrdinaryCells.IsEmpty()
			|| Plan.Floors.Num() != Plan.FloorCount
			|| Plan.RequiredTwoFloorStairStableIdByLowerFloor.Num()
				!= Plan.FloorCount - 1
			|| Plan.PlayerToExitRouteLengthTiles <= 0
			|| Plan.VerticalTransitionCountOnShortestRoute < Plan.FloorCount - 1
			|| Plan.PlayerSpawnCoordinate.Z != 0
			|| Plan.PursuerSpawnCoordinate.Z != 0
			|| Plan.ExitCoordinate.Z != Plan.FloorCount - 1
			|| !GenerationCorePrivate::IsInsideBuilding(
				Plan.PlayerSpawnCoordinate, Plan.GridSize, Plan.FloorCount)
			|| !GenerationCorePrivate::IsInsideBuilding(
				Plan.PursuerSpawnCoordinate, Plan.GridSize, Plan.FloorCount)
			|| !GenerationCorePrivate::IsInsideBuilding(
				Plan.ExitCoordinate, Plan.GridSize, Plan.FloorCount))
		{
			return 0;
		}

		using GenerationCorePrivate::HashCoordinate;
		using GenerationCorePrivate::HashCoordinateArray;
		using GenerationCorePrivate::HashDouble;
		using GenerationCorePrivate::HashName;
		using GenerationCorePrivate::HashUInt64;
		using GenerationCorePrivate::IsInsideBuilding;

		uint64 Hash = GenerationCorePrivate::HashOffset;
		HashUInt64(Hash, Plan.Signature.Seed);
		HashUInt64(Hash, static_cast<uint8>(Plan.Signature.Difficulty));
		HashUInt64(Hash, Plan.Signature.AlgorithmVersion);
		HashUInt64(Hash, Plan.Signature.GenerationProfileVersion);
		HashUInt64(Hash, Plan.FloorCount);
		HashUInt64(Hash, Plan.GridSize.X);
		HashUInt64(Hash, Plan.GridSize.Y);
		HashDouble(Hash, Plan.LogicalTileSizeCm);
		HashDouble(Hash, Plan.FloorHeightCm);
		HashDouble(Hash, Plan.AnchorHeightCm);
		HashCoordinate(Hash, Plan.PlayerSpawnCoordinate);
		HashCoordinate(Hash, Plan.PursuerSpawnCoordinate);
		HashCoordinate(Hash, Plan.ExitCoordinate);
		HashUInt64(Hash, Plan.PlayerToExitRouteLengthTiles);
		HashUInt64(Hash, Plan.VerticalTransitionCountOnShortestRoute);

		HashUInt64(Hash, Plan.OrdinaryCells.Num());
		FIntVector PreviousOrdinaryCoordinate;
		bool bHasPreviousOrdinaryCoordinate = false;
		for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
		{
			if (!IsInsideBuilding(Cell.Coordinate, Plan.GridSize, Plan.FloorCount)
				|| Cell.OpeningMask == 0
				|| (Cell.OpeningMask & ~Grid::AllOpenEdges) != 0
				|| (bHasPreviousOrdinaryCoordinate
					&& !GenerationCorePrivate::CoordinateLess(
						PreviousOrdinaryCoordinate, Cell.Coordinate)))
			{
				return 0;
			}
			HashCoordinate(Hash, Cell.Coordinate);
			HashUInt64(Hash, Cell.OpeningMask);
			PreviousOrdinaryCoordinate = Cell.Coordinate;
			bHasPreviousOrdinaryCoordinate = true;
		}

		HashUInt64(Hash, Plan.Structures.Num());
		TMap<int32, const FZeroEscapeGeneratedStructure*> StructureByStableId;
		int32 PreviousStructureId = INDEX_NONE;
		int32 ThreeFloorStairwellCount = 0;
		for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
		{
			const int32 StructureKindIndex = static_cast<int32>(Structure.Kind);
			if (Structure.StableStructureId <= PreviousStructureId
				|| Structure.DefinitionId.IsNone()
				|| Structure.ActiveOpeningSetId.IsNone()
				|| StructureKindIndex < 0 || StructureKindIndex > 2
				|| Structure.QuarterTurnCount > 3
				|| !IsInsideBuilding(Structure.BaseCoordinate, Plan.GridSize, Plan.FloorCount)
				|| !HashCoordinateArray(
					Hash, Structure.WalkableCells, Plan.GridSize, Plan.FloorCount)
				|| !HashCoordinateArray(
					Hash, Structure.SolidCells, Plan.GridSize, Plan.FloorCount)
				|| !HashCoordinateArray(
					Hash, Structure.ClearanceCells, Plan.GridSize, Plan.FloorCount))
			{
				return 0;
			}

			HashUInt64(Hash, Structure.StableStructureId);
			HashName(Hash, Structure.DefinitionId);
			HashUInt64(Hash, static_cast<uint8>(Structure.Kind));
			HashCoordinate(Hash, Structure.BaseCoordinate);
			HashUInt64(Hash, Structure.QuarterTurnCount);
			HashName(Hash, Structure.ActiveOpeningSetId);

			HashUInt64(Hash, Structure.InternalConnections.Num());
			for (const FZeroEscapeGeneratedCellConnection& Connection :
				Structure.InternalConnections)
			{
				if (!IsInsideBuilding(Connection.FirstCoordinate, Plan.GridSize, Plan.FloorCount)
					|| !IsInsideBuilding(Connection.SecondCoordinate, Plan.GridSize, Plan.FloorCount))
				{
					return 0;
				}
				HashCoordinate(Hash, Connection.FirstCoordinate);
				HashCoordinate(Hash, Connection.SecondCoordinate);
			}

			HashUInt64(Hash, Structure.Openings.Num());
			for (const FZeroEscapeGeneratedStructureOpening& Opening : Structure.Openings)
			{
				if (Opening.OpeningId.IsNone()
					|| !IsInsideBuilding(Opening.StructureCoordinate, Plan.GridSize, Plan.FloorCount)
					|| !IsInsideBuilding(
						Opening.ConnectedOrdinaryCoordinate, Plan.GridSize, Plan.FloorCount))
				{
					return 0;
				}
				HashName(Hash, Opening.OpeningId);
				HashCoordinate(Hash, Opening.StructureCoordinate);
				HashCoordinate(Hash, Opening.ConnectedOrdinaryCoordinate);
			}

			HashUInt64(Hash, Structure.Landings.Num());
			for (const FZeroEscapeGeneratedStructureLanding& Landing : Structure.Landings)
			{
				if (Landing.LandingId.IsNone()
					|| !IsInsideBuilding(Landing.Coordinate, Plan.GridSize, Plan.FloorCount))
				{
					return 0;
				}
				HashName(Hash, Landing.LandingId);
				HashCoordinate(Hash, Landing.Coordinate);
			}

			ThreeFloorStairwellCount +=
				Structure.Kind == EZeroEscapeStructureKind::ThreeFloorStairwell ? 1 : 0;
			StructureByStableId.Add(Structure.StableStructureId, &Structure);
			PreviousStructureId = Structure.StableStructureId;
		}
		if (ThreeFloorStairwellCount > 1)
		{
			return 0;
		}

		HashUInt64(Hash, Plan.RequiredTwoFloorStairStableIdByLowerFloor.Num());
		TSet<int32> RequiredStairIds;
		for (int32 LowerFloor = 0;
			LowerFloor < Plan.RequiredTwoFloorStairStableIdByLowerFloor.Num();
			++LowerFloor)
		{
			const int32 StableId =
				Plan.RequiredTwoFloorStairStableIdByLowerFloor[LowerFloor];
			const FZeroEscapeGeneratedStructure* const* Structure =
				StructureByStableId.Find(StableId);
			if (Structure == nullptr
				|| (*Structure)->Kind != EZeroEscapeStructureKind::TwoFloorStair
				|| (*Structure)->BaseCoordinate.Z != LowerFloor
				|| RequiredStairIds.Contains(StableId))
			{
				return 0;
			}
			RequiredStairIds.Add(StableId);
			HashUInt64(Hash, StableId);
		}

		HashUInt64(Hash, Plan.Floors.Num());
		for (int32 FloorIndex = 0; FloorIndex < Plan.Floors.Num(); ++FloorIndex)
		{
			const FZeroEscapeGeneratedFloorSummary& Floor = Plan.Floors[FloorIndex];
			if (Floor.FloorIndex != FloorIndex
				|| Floor.RequiredEnterCoordinate.Z != FloorIndex
				|| Floor.RequiredLeaveCoordinate.Z != FloorIndex
				|| !IsInsideBuilding(
					Floor.RequiredEnterCoordinate, Plan.GridSize, Plan.FloorCount)
				|| !IsInsideBuilding(
					Floor.RequiredLeaveCoordinate, Plan.GridSize, Plan.FloorCount)
				|| Floor.OrdinaryWalkableCellCount <= 0
				|| Floor.TotalWalkableCellCount < Floor.OrdinaryWalkableCellCount
				|| Floor.RequiredRouteLengthTiles <= 0
				|| Floor.FarthestRouteLengthTiles < Floor.RequiredRouteLengthTiles
				|| !FMath::IsFinite(Floor.SpatialSeparationRatio)
				|| Floor.SpatialSeparationRatio < 0.0
				|| Floor.SpatialSeparationRatio > 1.0
				|| !FMath::IsFinite(Floor.RouteCoverageRatio)
				|| Floor.RouteCoverageRatio < 0.0
				|| Floor.RouteCoverageRatio > 1.0
				|| Floor.JunctionMetrics.DeadEndCount < 0
				|| Floor.JunctionMetrics.StraightCount < 0
				|| Floor.JunctionMetrics.CornerCount < 0
				|| Floor.JunctionMetrics.TJunctionCount < 0
				|| Floor.JunctionMetrics.CrossJunctionCount < 0
				|| Floor.CycleRank < 0)
			{
				return 0;
			}
			HashUInt64(Hash, Floor.FloorIndex);
			HashCoordinate(Hash, Floor.RequiredEnterCoordinate);
			HashCoordinate(Hash, Floor.RequiredLeaveCoordinate);
			HashUInt64(Hash, Floor.OrdinaryWalkableCellCount);
			HashUInt64(Hash, Floor.TotalWalkableCellCount);
			HashUInt64(Hash, Floor.RequiredRouteLengthTiles);
			HashUInt64(Hash, Floor.FarthestRouteLengthTiles);
			HashDouble(Hash, Floor.SpatialSeparationRatio);
			HashDouble(Hash, Floor.RouteCoverageRatio);
			HashUInt64(Hash, Floor.JunctionMetrics.DeadEndCount);
			HashUInt64(Hash, Floor.JunctionMetrics.StraightCount);
			HashUInt64(Hash, Floor.JunctionMetrics.CornerCount);
			HashUInt64(Hash, Floor.JunctionMetrics.TJunctionCount);
			HashUInt64(Hash, Floor.JunctionMetrics.CrossJunctionCount);
			HashUInt64(Hash, Floor.CycleRank);
		}
		if (Plan.CycleRank < 0
			|| Plan.JunctionMetrics.DeadEndCount < 0
			|| Plan.JunctionMetrics.StraightCount < 0
			|| Plan.JunctionMetrics.CornerCount < 0
			|| Plan.JunctionMetrics.TJunctionCount < 0
			|| Plan.JunctionMetrics.CrossJunctionCount < 0)
		{
			return 0;
		}
		HashUInt64(Hash, Plan.JunctionMetrics.DeadEndCount);
		HashUInt64(Hash, Plan.JunctionMetrics.StraightCount);
		HashUInt64(Hash, Plan.JunctionMetrics.CornerCount);
		HashUInt64(Hash, Plan.JunctionMetrics.TJunctionCount);
		HashUInt64(Hash, Plan.JunctionMetrics.CrossJunctionCount);
		HashUInt64(Hash, Plan.CycleRank);
		return static_cast<int64>(Hash & MAX_int64);
	}

	bool FGenerationCore::IsFiniteUnitScaleTransform(const FTransform& Transform)
	{
		return !Transform.GetLocation().ContainsNaN()
			&& !Transform.GetRotation().ContainsNaN()
			&& Transform.GetRotation().IsNormalized()
			&& !Transform.GetScale3D().ContainsNaN()
			&& Transform.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER);
	}
}
