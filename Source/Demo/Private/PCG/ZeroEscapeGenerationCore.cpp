// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationCore.cpp
 * 职责：实现稳定快照、流程解析、进度带 Landmark、随机域和规范 Hash。
 * 边界：不生成道路，不执行 WFC，不访问世界；这使玩法流程变化不必改动空间求解器内部状态。
 */

#include "PCG/ZeroEscapeGenerationCore.h"

namespace ZeroEscape::LevelGeneration
{
	namespace
	{
		/** 64 位 FNV-1a 初值；最终清除符号位，使 Blueprint/日志中的 int64 保持非负。 */
		inline constexpr uint64 HashOffset = 1469598103934665603ull;
		inline constexpr uint64 HashPrime = 1099511628211ull;

		/** 按字节混入无符号整数，避免结构体填充和平台内存布局进入 Hash。 */
		void HashUInt64(uint64& Hash, const uint64 Value)
		{
			for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				Hash ^= (Value >> (ByteIndex * 8)) & 0xFFu;
				Hash *= HashPrime;
			}
		}

		/** 按 UTF-16 代码单元混入 FName 文本，不依赖进程内 Name Index。 */
		void HashName(uint64& Hash, const FName Name)
		{
			const FString Text = Name.ToString();
			HashUInt64(Hash, static_cast<uint64>(Text.Len()));
			for (const TCHAR Character : Text)
			{
				HashUInt64(Hash, static_cast<uint16>(Character));
			}
		}

		/**
		 * 统一填写不可恢复的纯算法错误，避免上层覆盖首个检查点。
		 * 命名为 FailCore 而非 Fail：本项目多个 .cpp 在 ZeroEscape::LevelGeneration 下各用匿名命名空间
		 * 定义过同名 Fail 助手，Unity(blob) 编译会把它们拼进同一编译单元并注入同一具名命名空间，
		 * 若重名且前缀参数一致会触发 C2668 重载歧义。用文件专属名保证唯一，避免依赖 Unity 分组顺序。
		 */
		bool FailCore(
			FZeroEscapeGenerationReport& Report,
			const EZeroEscapeGenerationStage Stage,
			const EZeroEscapeGenerationFailure Failure,
			const FString& Message,
			const int32 ActualValue = 0,
			const int32 LimitValue = 0)
		{
			Report.Stage = Stage;
			Report.Failure = Failure;
			Report.ActualValue = ActualValue;
			Report.LimitValue = LimitValue;
			Report.Message = Message;
			return false;
		}
	}

	bool FGenerationCore::BuildGenerationSnapshot(
		const UZeroEscapeLevelGenerationProfile& Source,
		FGenerationProfileSnapshot& OutSnapshot,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutSnapshot = FGenerationProfileSnapshot();
		FString Error;
		if (!Source.IsConfigured(Error))
		{
			return FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				Error);
		}

		FGenerationProfileSnapshot Candidate;
		Candidate.ProfileVersion = Source.ProfileVersion;
		Candidate.SharedRouteConstraints = Source.SharedRouteConstraints;
		Candidate.Difficulties = Source.Difficulties;
		Candidate.Flows = Source.Flows;
		Candidate.WfcShapeWeights = Source.WfcShapeWeights;

		Candidate.Difficulties.Sort([](
			const FZeroEscapeDifficultyDefinition& A,
			const FZeroEscapeDifficultyDefinition& B)
		{
			return static_cast<uint8>(A.Difficulty) < static_cast<uint8>(B.Difficulty);
		});
		Candidate.Flows.Sort([](
			const FZeroEscapeFlowDefinition& A,
			const FZeroEscapeFlowDefinition& B)
		{
			return A.StableFlowId.LexicalLess(B.StableFlowId);
		});

		OutSnapshot = MoveTemp(Candidate);
		return true;
	}

	bool FGenerationCore::ResolveProgressionSettings(
		const FZeroEscapeGenerationRequest& Request,
		const FGenerationProfileSnapshot& Profile,
		FResolvedProgressionSettings& OutSettings,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutSettings = FResolvedProgressionSettings();
		const FZeroEscapeDifficultyDefinition* Difficulty = Profile.Difficulties.FindByPredicate(
			[&Request](const FZeroEscapeDifficultyDefinition& Candidate)
			{
				return Candidate.Difficulty == Request.Difficulty;
			});
		const FZeroEscapeFlowDefinition* Flow = Profile.Flows.FindByPredicate(
			[&Request](const FZeroEscapeFlowDefinition& Candidate)
			{
				return Candidate.StableFlowId == Request.FlowProfileId;
			});

		if (Difficulty == nullptr || Flow == nullptr)
		{
			return FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("Request 的 Difficulty 或 FlowProfileId 无法从规范快照解析。"));
		}

		FResolvedProgressionSettings Candidate;
		Candidate.Difficulty = Difficulty->Difficulty;
		Candidate.StableFlowId = Flow->StableFlowId;
		Candidate.FlowVersion = Flow->FlowVersion;
		Candidate.CompletionRule = Flow->CompletionRule;
		Candidate.MaxOptionalSideBranches = Difficulty->MaxOptionalSideBranches;
		Candidate.MaxOptionalForwardLinks = Difficulty->MaxOptionalForwardLinks;

		switch (Flow->CompletionRule)
		{
		case EZeroEscapeCompletionRule::EscapeOnly:
			Candidate.ObjectiveCandidateCount = 0;
			Candidate.RequiredObjectiveCount = 0;
			break;
		case EZeroEscapeCompletionRule::CollectAll:
			Candidate.ObjectiveCandidateCount = Difficulty->ObjectiveCandidateCount;
			Candidate.RequiredObjectiveCount = Difficulty->ObjectiveCandidateCount;
			break;
		case EZeroEscapeCompletionRule::CollectKOfN:
			Candidate.ObjectiveCandidateCount = Difficulty->ObjectiveCandidateCount;
			Candidate.RequiredObjectiveCount = Difficulty->RequiredObjectiveCount;
			break;
		default:
			return FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Progression,
				EZeroEscapeGenerationFailure::InvalidKOfN,
				TEXT("CompletionRule 未被 V3.2 支持。"));
		}

		if (Candidate.ObjectiveCandidateCount > GenerationLimits::MaxObjectiveCandidates)
		{
			return FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Progression,
				EZeroEscapeGenerationFailure::ObjectiveLimitExceeded,
				TEXT("目标候选数量超过代码级安全上限。"),
				Candidate.ObjectiveCandidateCount,
				GenerationLimits::MaxObjectiveCandidates);
		}

		const bool bValidKOfN = Candidate.CompletionRule == EZeroEscapeCompletionRule::EscapeOnly
			? Candidate.ObjectiveCandidateCount == 0 && Candidate.RequiredObjectiveCount == 0
			: Candidate.ObjectiveCandidateCount > 0
				&& Candidate.RequiredObjectiveCount > 0
				&& Candidate.RequiredObjectiveCount <= Candidate.ObjectiveCandidateCount;
		if (!bValidKOfN)
		{
			return FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Progression,
				EZeroEscapeGenerationFailure::InvalidKOfN,
				TEXT("解析后的 K/N 不满足 EscapeOnly 或 0<K<=N 契约。"),
				Candidate.RequiredObjectiveCount,
				Candidate.ObjectiveCandidateCount);
		}

		OutSettings = MoveTemp(Candidate);
		return true;
	}

	bool FGenerationCore::BuildGenerationSignature(
		const FZeroEscapeGenerationRequest& Request,
		const FGenerationProfileSnapshot& Profile,
		const FResolvedProgressionSettings& Settings,
		const int32 PresentationVersion,
		FZeroEscapeGenerationSignature& OutSignature,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutSignature = FZeroEscapeGenerationSignature();
		if (Profile.ProfileVersion <= 0 || Settings.FlowVersion <= 0 || PresentationVersion <= 0
			|| Settings.StableFlowId != Request.FlowProfileId
			|| Settings.Difficulty != Request.Difficulty)
		{
			return FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Configuration,
				EZeroEscapeGenerationFailure::InvalidConfiguration,
				TEXT("Generation Signature 输入未完整解析或与 Request 不一致。"));
		}

		OutSignature.Seed = Request.Seed;
		OutSignature.Difficulty = Request.Difficulty;
		OutSignature.FlowProfileId = Request.FlowProfileId;
		OutSignature.AlgorithmVersion = GAlgorithmVersion;
		OutSignature.GenerationProfileVersion = Profile.ProfileVersion;
		OutSignature.FlowVersion = Settings.FlowVersion;
		OutSignature.PresentationVersion = PresentationVersion;
		return true;
	}

	bool FGenerationCore::BuildProgressionIntent(
		const FZeroEscapeGenerationRequest& Request,
		const FGenerationProfileSnapshot& Profile,
		const FResolvedProgressionSettings& Settings,
		FProgressionIntent& OutIntent,
		FZeroEscapeGenerationReport& OutReport)
	{
		OutIntent = FProgressionIntent();
		const int32 BandCount = Profile.SharedRouteConstraints.ObjectiveProgressBandCount;
		const int32 SlotCapacity = BandCount * 2;
		if (BandCount <= 0 || Settings.ObjectiveCandidateCount > SlotCapacity)
		{
			return FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Progression,
				EZeroEscapeGenerationFailure::CapacityInsufficient,
				TEXT("进度带的上下房间槽不足以承载本局候选目标。"),
				Settings.ObjectiveCandidateCount,
				SlotCapacity);
		}

		struct FRoomSlot
		{
			int32 BandIndex = 0;
			int32 LaneIndex = 0;
		};
		TArray<FRoomSlot> Slots;
		Slots.Reserve(SlotCapacity);
		for (int32 BandIndex = 0; BandIndex < BandCount; ++BandIndex)
		{
			Slots.Add({BandIndex, 0});
			Slots.Add({BandIndex, 1});
		}

		// 只在 Landmark 随机域内洗牌候选槽；WFC 增减抽样不会改变目标所在进度带。
		FRandomStream Random = MakeRandomStream(
			Request.Seed,
			GAlgorithmVersion,
			ERandomDomain::Landmark);
		for (int32 Index = Slots.Num() - 1; Index > 0; --Index)
		{
			Slots.Swap(Index, Random.RandRange(0, Index));
		}
		Slots.SetNum(Settings.ObjectiveCandidateCount, EAllowShrinking::No);
		Slots.Sort([](const FRoomSlot& A, const FRoomSlot& B)
		{
			return A.BandIndex != B.BandIndex
				? A.BandIndex < B.BandIndex
				: A.LaneIndex < B.LaneIndex;
		});

		FProgressionIntent Candidate;
		Candidate.CompletionRule = Settings.CompletionRule;
		Candidate.ObjectiveCandidateCount = Settings.ObjectiveCandidateCount;
		Candidate.RequiredObjectiveCount = Settings.RequiredObjectiveCount;
		Candidate.StartStableLandmarkId = 0;
		Candidate.ExitStableLandmarkId = Settings.ObjectiveCandidateCount + 1;
		Candidate.Landmarks.Reserve(Settings.ObjectiveCandidateCount + 2);
		Candidate.Landmarks.Add({0, EProgressionLandmarkKind::Start, INDEX_NONE, INDEX_NONE, INDEX_NONE});

		for (int32 ObjectiveIndex = 0; ObjectiveIndex < Settings.ObjectiveCandidateCount; ++ObjectiveIndex)
		{
			const FRoomSlot& Slot = Slots[ObjectiveIndex];
			Candidate.Landmarks.Add({
				ObjectiveIndex + 1,
				EProgressionLandmarkKind::Objective,
				Slot.BandIndex,
				Slot.LaneIndex,
				ObjectiveIndex});
		}
		Candidate.Landmarks.Add({
			Candidate.ExitStableLandmarkId,
			EProgressionLandmarkKind::Exit,
			BandCount,
			INDEX_NONE,
			INDEX_NONE});

		if (ComputeCanonicalProgressionHash(Candidate) == 0)
		{
			return FailCore(
				OutReport,
				EZeroEscapeGenerationStage::Progression,
				EZeroEscapeGenerationFailure::SolverInvariantViolation,
				TEXT("构建后的 Progression Intent 未通过规范 Hash 前置校验。"));
		}

		OutIntent = MoveTemp(Candidate);
		return true;
	}

	FRandomStream FGenerationCore::MakeRandomStream(
		const int32 MasterSeed,
		const int32 AlgorithmVersion,
		const ERandomDomain Domain,
		const int32 Salt)
	{
		// SplitMix 风格的整数混合只负责派生 Seed；实际随机序列仍由 UE FRandomStream 提供。
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

	int64 FGenerationCore::ComputeCanonicalProgressionHash(const FProgressionIntent& Intent)
	{
		if (Intent.Landmarks.Num() < 2
			|| Intent.StartStableLandmarkId == INDEX_NONE
			|| Intent.ExitStableLandmarkId == INDEX_NONE
			|| Intent.ObjectiveCandidateCount < 0
			|| Intent.RequiredObjectiveCount < 0
			|| Intent.RequiredObjectiveCount > Intent.ObjectiveCandidateCount)
		{
			return 0;
		}

		uint64 Hash = HashOffset;
		HashUInt64(Hash, static_cast<uint8>(Intent.CompletionRule));
		HashUInt64(Hash, Intent.ObjectiveCandidateCount);
		HashUInt64(Hash, Intent.RequiredObjectiveCount);
		HashUInt64(Hash, Intent.StartStableLandmarkId);
		HashUInt64(Hash, Intent.ExitStableLandmarkId);
		HashUInt64(Hash, Intent.Landmarks.Num());

		int32 PreviousId = INDEX_NONE;
		for (const FProgressionLandmark& Landmark : Intent.Landmarks)
		{
			if (Landmark.StableLandmarkId <= PreviousId)
			{
				return 0;
			}
			PreviousId = Landmark.StableLandmarkId;
			HashUInt64(Hash, Landmark.StableLandmarkId);
			HashUInt64(Hash, static_cast<uint8>(Landmark.Kind));
			HashUInt64(Hash, Landmark.ProgressBandIndex);
			HashUInt64(Hash, Landmark.LaneIndex);
			HashUInt64(Hash, Landmark.StableObjectiveId);
		}
		return static_cast<int64>(Hash & MAX_int64);
	}

	int64 FGenerationCore::ComputeCanonicalLayoutHash(const FZeroEscapeGeneratedLevelPlan& Plan)
	{
		if (Plan.GridSize.X <= 0 || Plan.GridSize.Y <= 0
			|| !FMath::IsFinite(Plan.LogicalTileSizeCm)
			|| Plan.LogicalTileSizeCm <= 0.0
			|| Plan.Cells.IsEmpty())
		{
			return 0;
		}

		uint64 Hash = HashOffset;
		HashUInt64(Hash, static_cast<uint64>(Plan.CanonicalProgressionHash));
		HashUInt64(Hash, Plan.GridSize.X);
		HashUInt64(Hash, Plan.GridSize.Y);
		HashUInt64(Hash, static_cast<uint64>(FMath::RoundToInt64(Plan.LogicalTileSizeCm * 100.0)));
		HashUInt64(Hash, static_cast<uint8>(Plan.CompletionRule));
		HashUInt64(Hash, Plan.ObjectiveCandidateCount);
		HashUInt64(Hash, Plan.RequiredObjectiveCount);
		HashUInt64(Hash, Plan.StartCoordinate.X);
		HashUInt64(Hash, Plan.StartCoordinate.Y);
		HashUInt64(Hash, Plan.ExitCoordinate.X);
		HashUInt64(Hash, Plan.ExitCoordinate.Y);
		HashUInt64(Hash, Plan.PlayerSpawnAnchorInstanceId);
		HashUInt64(Hash, Plan.ExitAnchorInstanceId);
		HashUInt64(Hash, Plan.Cells.Num());
		HashUInt64(Hash, Plan.LandmarkBindings.Num());
		HashUInt64(Hash, Plan.GameplayAnchors.Num());
		HashUInt64(Hash, Plan.ObjectiveBindings.Num());

		int32 PreviousCellId = INDEX_NONE;
		for (const FZeroEscapeCollapsedTile& Cell : Plan.Cells)
		{
			if (Cell.StableCellId <= PreviousCellId
				|| !Grid::IsInside(Cell.GridCoordinate, Plan.GridSize)
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

		for (const FZeroEscapeLandmarkBinding& Binding : Plan.LandmarkBindings)
		{
			HashUInt64(Hash, Binding.StableLandmarkId);
			HashUInt64(Hash, Binding.GridCoordinate.X);
			HashUInt64(Hash, Binding.GridCoordinate.Y);
			HashUInt64(Hash, Binding.RegionId);
		}
		for (const FZeroEscapeGeneratedAnchor& Anchor : Plan.GameplayAnchors)
		{
			HashUInt64(Hash, Anchor.StableAnchorInstanceId);
			HashUInt64(Hash, static_cast<uint8>(Anchor.Type));
			HashUInt64(Hash, Anchor.GridCoordinate.X);
			HashUInt64(Hash, Anchor.GridCoordinate.Y);
			HashUInt64(Hash, Anchor.RegionId);
		}
		for (const FZeroEscapeObjectiveBinding& Binding : Plan.ObjectiveBindings)
		{
			HashUInt64(Hash, Binding.StableObjectiveId);
			HashUInt64(Hash, Binding.GridCoordinate.X);
			HashUInt64(Hash, Binding.GridCoordinate.Y);
			HashUInt64(Hash, Binding.RegionId);
			HashUInt64(Hash, Binding.StableAnchorInstanceId);
		}
		HashName(Hash, Plan.Signature.FlowProfileId);
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
