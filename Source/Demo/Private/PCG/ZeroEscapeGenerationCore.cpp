// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationCore.cpp
 * 职责：实现单次配置解析、确定性随机域和纯空间 Layout Hash。
 * 边界：不生成道路，不执行 WFC，不访问 World，也不保存任何玩法完成条件。
 */

#include "PCG/ZeroEscapeGenerationCore.h"

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

		OutInput.Rules = Profile.SharedRouteConstraints;
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
			|| Plan.GridSize.X <= 0
			|| Plan.GridSize.Y <= 0
			|| !FMath::IsFinite(Plan.LogicalTileSizeCm)
			|| Plan.LogicalTileSizeCm <= 0.0
			|| Plan.Cells.IsEmpty())
		{
			return 0;
		}

		using GenerationCorePrivate::HashUInt64;
		uint64 Hash = GenerationCorePrivate::HashOffset;
		HashUInt64(Hash, Plan.Signature.Seed);
		HashUInt64(Hash, static_cast<uint8>(Plan.Signature.Difficulty));
		HashUInt64(Hash, Plan.Signature.AlgorithmVersion);
		HashUInt64(Hash, Plan.Signature.GenerationProfileVersion);
		HashUInt64(Hash, Plan.GridSize.X);
		HashUInt64(Hash, Plan.GridSize.Y);
		HashUInt64(Hash, static_cast<uint64>(FMath::RoundToInt64(Plan.LogicalTileSizeCm * 100.0)));
		HashUInt64(Hash, Plan.StartCoordinate.X);
		HashUInt64(Hash, Plan.StartCoordinate.Y);
		HashUInt64(Hash, Plan.ExitCoordinate.X);
		HashUInt64(Hash, Plan.ExitCoordinate.Y);
		HashUInt64(Hash, Plan.Cells.Num());
		HashUInt64(Hash, Plan.Rooms.Num());

		int32 PreviousCellId = INDEX_NONE;
		for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
		{
			if (Cell.StableCellId <= PreviousCellId
				|| !Grid::IsInside(Cell.GridCoordinate, Plan.GridSize)
				|| Cell.OpeningMask == 0
				|| (Cell.OpeningMask & ~Grid::AllOpenEdges) != 0)
			{
				return 0;
			}
			PreviousCellId = Cell.StableCellId;
			HashUInt64(Hash, Cell.StableCellId);
			HashUInt64(Hash, Cell.GridCoordinate.X);
			HashUInt64(Hash, Cell.GridCoordinate.Y);
			HashUInt64(Hash, Cell.OpeningMask);
			HashUInt64(Hash, Cell.RegionId);
			HashUInt64(Hash, static_cast<uint8>(Cell.RegionKind));
		}

		int32 PreviousRegionId = INDEX_NONE;
		for (const FZeroEscapeGeneratedRoom& Room : Plan.Rooms)
		{
			if (Room.RegionId <= PreviousRegionId
				|| !Grid::IsInside(Room.AnchorCoordinate, Plan.GridSize))
			{
				return 0;
			}
			PreviousRegionId = Room.RegionId;
			HashUInt64(Hash, Room.RegionId);
			HashUInt64(Hash, Room.AnchorCoordinate.X);
			HashUInt64(Hash, Room.AnchorCoordinate.Y);
		}
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
