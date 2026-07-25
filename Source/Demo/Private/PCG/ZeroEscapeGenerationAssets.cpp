// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationAssets.cpp
 * 职责：对 Grid/WFC 策划参数和结构表现绑定执行 fail-closed 校验。
 * 边界：不加载替代资源、不猜测 Pivot、不修正 DataAsset，也不访问 World。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"

namespace
{
	/** Transform 必须有限且不缩放；结构大小由统一网格决定，禁止在绑定里偷偷拉伸素材。 */
	bool IsFiniteUnitScaleTransform(const FTransform& Transform)
	{
		const FVector Location = Transform.GetLocation();
		const FQuat Rotation = Transform.GetRotation();
		const FVector Scale = Transform.GetScale3D();
		return !Location.ContainsNaN()
			&& !Rotation.ContainsNaN()
			&& !Scale.ContainsNaN()
			&& Rotation.IsNormalized()
			&& Scale.Equals(FVector::OneVector, KINDA_SMALL_NUMBER);
	}

	/** 校验一个已配置绑定；可选空绑定由调用者在进入本函数前跳过。 */
	bool ValidateBinding(
		const FZeroEscapeStructureMeshBinding& Binding,
		const TCHAR* PropertyName,
		FString& OutError)
	{
		if (!IsValid(Binding.StaticMesh))
		{
			OutError = FString::Printf(TEXT("Presentation.%s.StaticMesh 未配置。"), PropertyName);
			return false;
		}

		if (!IsFiniteUnitScaleTransform(Binding.PivotCorrection))
		{
			OutError = FString::Printf(
				TEXT("Presentation.%s.PivotCorrection 必须是有限 Unit Scale Transform。"),
				PropertyName);
			return false;
		}

		FCollisionResponseTemplate ProfileTemplate;
		if (Binding.CollisionProfileName.IsNone()
			|| !UCollisionProfile::Get()->GetProfileTemplate(
				Binding.CollisionProfileName,
				ProfileTemplate))
		{
			OutError = FString::Printf(
				TEXT("Presentation.%s.CollisionProfileName 不是已注册碰撞配置。"),
				PropertyName);
			return false;
		}

		return true;
	}
}

int32 FZeroEscapeWfcShapeWeights::GetWeightForMask(const uint8 OpeningMask) const
{
	if ((OpeningMask & ~ZeroEscape::Grid::AllOpenEdges) != 0)
	{
		return 0;
	}

	const int32 OpenCount = FMath::CountBits(static_cast<uint32>(OpeningMask));
	switch (OpenCount)
	{
	case 0:
		return EmptyWeight;
	case 1:
		return DeadEndWeight;
	case 2:
		// N+S 或 E+W 是直线，其余双开口都是转角。
		return OpeningMask == 0x5 || OpeningMask == 0xA
			? StraightWeight
			: CornerWeight;
	case 3:
		return TJunctionWeight;
	case 4:
		return CrossWeight;
	default:
		return 0;
	}
}

int64 FZeroEscapeWfcShapeWeights::GetTotalNonEmptyVariantWeight() const
{
	/**
	 * 15 个非空 mask 按旋转展开后分别为：4 个单出口、2 个直线、4 个转角、
	 * 4 个 T 字和 1 个十字。使用 int64 计算，避免非法超大配置在校验前发生溢出。
	 */
	return 4LL * DeadEndWeight
		+ 2LL * StraightWeight
		+ 4LL * CornerWeight
		+ 4LL * TJunctionWeight
		+ CrossWeight;
}

bool FZeroEscapeWfcShapeWeights::IsConfigured(FString& OutError) const
{
	OutError.Reset();
	if (EmptyWeight <= 0 || DeadEndWeight <= 0 || StraightWeight <= 0
		|| CornerWeight <= 0 || TJunctionWeight <= 0 || CrossWeight <= 0)
	{
		OutError = TEXT("WfcShapeWeights 的六类权重都必须大于 0。");
		return false;
	}

	const int64 TotalVariantWeight = static_cast<int64>(EmptyWeight)
		+ GetTotalNonEmptyVariantWeight();
	if (TotalVariantWeight > MAX_int32)
	{
		OutError = TEXT("WfcShapeWeights 的 16 个 Variant 总权重超过 int32 加权抽样上限。");
		return false;
	}
	return true;
}

bool UZeroEscapeLevelGenerationProfile::IsConfigured(FString& OutError) const
{
	OutError.Reset();
	if (ProfileVersion <= 0)
	{
		OutError = TEXT("GenerationProfile.ProfileVersion 必须大于 0。");
		return false;
	}

	const FZeroEscapeSharedRouteConstraints& Route = SharedRouteConstraints;
	const FIntPoint GridSize = Route.GridSize;
	if (GridSize.X < ZeroEscape::GenerationLimits::MinGridAxis
		|| GridSize.Y < ZeroEscape::GenerationLimits::MinGridAxis
		|| GridSize.X > ZeroEscape::GenerationLimits::MaxGridAxis
		|| GridSize.Y > ZeroEscape::GenerationLimits::MaxGridAxis
		|| GridSize.X * GridSize.Y > ZeroEscape::GenerationLimits::MaxGridCells)
	{
		OutError = TEXT("SharedRouteConstraints.GridSize 超出运行时安全范围。");
		return false;
	}
	const int32 GridCellCount = GridSize.X * GridSize.Y;

	// 当前 Landmark 槽位仍需在左右为 Start/Exit 留安全边界，并在上下容纳两条 2x2 房间 Lane。
	// 该检查只验证语义占格容量，不再证明或预刻任何中央主干路线。
	if (GridSize.X < 14 || GridSize.Y < 10)
	{
		OutError = TEXT("V4 的 Start/Exit 与 2x2 Objective 槽位要求 GridSize 至少为 14x10。");
		return false;
	}

	// 首版表现层只接受已经测量并验证的 600/300 双层网格，避免再次引入不透明适配层。
	if (!FMath::IsNearlyEqual(Route.LogicalTileSizeCm, 600.0)
		|| Route.RoomSizeTiles != 2)
	{
		OutError = TEXT("V4 首版要求 LogicalTileSizeCm=600 且 RoomSizeTiles=2。");
		return false;
	}

	if (Route.ObjectiveProgressBandCount <= 0
		|| Route.ObjectiveProgressBandCount > ZeroEscape::GenerationLimits::MaxObjectiveProgressBands
		|| Route.MaxRequiredRouteLengthTiles <= 0
		|| Route.MaxRequiredRouteExtraTiles < 0
		|| !FMath::IsFinite(Route.GameplayAnchorHeightCm)
		|| Route.GameplayAnchorHeightCm < 0.0)
	{
		OutError = TEXT("SharedRouteConstraints 包含非法进度、路线或 Anchor 参数。");
		return false;
	}

	if (Route.MinWalkableCellCount <= 0
		|| Route.MaxWalkableCellCount < Route.MinWalkableCellCount
		|| Route.MaxWalkableCellCount > GridCellCount)
	{
		OutError = TEXT("非空 Cell 数量必须满足 0 < MinWalkable <= MaxWalkable <= GridCells。");
		return false;
	}

	if (Route.MaxConsecutiveStraightTiles <= 0
		|| Route.MaxConsecutiveStraightTiles > FMath::Max(GridSize.X, GridSize.Y)
		|| Route.MaxWfcCandidateAttempts <= 0
		|| Route.MaxWfcBacktrackCount <= 0
		|| Route.MaxWfcSolveAttempts <= 0
		|| Route.MaxWfcSolveAttempts > 16
		|| Route.MaxWfcCandidateAttempts < Route.MaxWfcSolveAttempts
		|| Route.MaxWfcBacktrackCount < Route.MaxWfcSolveAttempts)
	{
		OutError = TEXT(
			"连续轴向贯通上限或 WFC 尝试/候选/回溯预算配置非法；"
			"总候选与总回溯预算必须至少能为每次尝试分配 1 次。");
		return false;
	}

	// Objective 房沿 X 轴按进度带离散放置。每个 2x2 房之间至少留一格，左右还要保留
	// Start/Exit 与安全边界；这些只是 Landmark 占格约束，不再暗含一条固定连接路线。
	const int32 MinRoomX = 4;
	// 同一进度带的上下房在 X 轴错开一格，避免房间投影重叠；该值必须与 Grid Solver
	// 的 Landmark 槽位公式保持一致，但不再承担固定 Gate 或主干边证明。
	const int32 MaxRoomX = GridSize.X - Route.RoomSizeTiles - 5;
	const int32 RoomSpan = MaxRoomX - MinRoomX;
	const int32 MinimumBandSpacing = Route.RoomSizeTiles + 1;
	if (MaxRoomX < MinRoomX
		|| (Route.ObjectiveProgressBandCount > 1
			&& RoomSpan < (Route.ObjectiveProgressBandCount - 1)
				* MinimumBandSpacing))
	{
		OutError = TEXT("GridSize.X 无法容纳当前进度带数量、2x2 房间与安全间距。");
		return false;
	}

	// 上下 Lane 分列在中线两侧，并为地图外圈保留一格封闭边界。
	const int32 CenterY = GridSize.Y / 2;
	const int32 LowerLaneMinY = CenterY - Route.RoomSizeTiles;
	const int32 UpperLaneMinY = CenterY + 1;
	if (LowerLaneMinY < 1
		|| UpperLaneMinY + Route.RoomSizeTiles > GridSize.Y - 1)
	{
		OutError = TEXT("GridSize.Y 无法在中线两侧容纳上下 2x2 房间 Lane 与外圈边界。");
		return false;
	}

	bool SeenDifficulties[3] = {false, false, false};
	int32 SharedEmptyWeight = INDEX_NONE;
	int64 SharedNonEmptyWeight = INDEX_NONE;
	for (const FZeroEscapeDifficultyDefinition& Definition : Difficulties)
	{
		const int32 DifficultyIndex = static_cast<int32>(Definition.Difficulty);
		if (DifficultyIndex < 0 || DifficultyIndex >= UE_ARRAY_COUNT(SeenDifficulties)
			|| SeenDifficulties[DifficultyIndex])
		{
			OutError = TEXT("Difficulties 必须恰好包含三个互不重复的难度。");
			return false;
		}
		SeenDifficulties[DifficultyIndex] = true;

		if (Definition.ObjectiveCandidateCount < 0
			|| Definition.ObjectiveCandidateCount > ZeroEscape::GenerationLimits::MaxObjectiveCandidates
			|| Definition.RequiredObjectiveCount < 0
			|| Definition.RequiredObjectiveCount > Definition.ObjectiveCandidateCount)
		{
			OutError = TEXT("DifficultyDefinition 的 K/N 参数非法。");
			return false;
		}

		const int32 RoomSlotCapacity = Route.ObjectiveProgressBandCount * 2;
		if (Definition.ObjectiveCandidateCount > RoomSlotCapacity)
		{
			OutError = TEXT("ObjectiveCandidateCount 超过进度带上下房间槽容量。");
			return false;
		}

		/**
		 * Start、Exit 各占一格；每个候选 Objective 房固定占 RoomSizeTiles² 个 Required 格。
		 * 这里只验证 MaxWalkable 能容纳所有固定语义格，不假定这些格之间如何连接。
		 */
		const int64 FixedNonEmptyCellCount = 2LL
			+ static_cast<int64>(Definition.ObjectiveCandidateCount)
				* Route.RoomSizeTiles * Route.RoomSizeTiles;
		if (FixedNonEmptyCellCount > Route.MaxWalkableCellCount)
		{
			OutError = FString::Printf(
				TEXT("难度 %d 至少需要 %lld 个 Start/Exit/Objective 非空格，超过 MaxWalkable=%d。"),
				DifficultyIndex,
				static_cast<long long>(FixedNonEmptyCellCount),
				Route.MaxWalkableCellCount);
			return false;
		}

		FString WeightError;
		if (!Definition.WfcShapeWeights.IsConfigured(WeightError))
		{
			OutError = FString::Printf(
				TEXT("难度 %d 的 WFC 权重非法：%s"),
				DifficultyIndex,
				*WeightError);
			return false;
		}

		const int64 NonEmptyWeight =
			Definition.WfcShapeWeights.GetTotalNonEmptyVariantWeight();
		if (SharedEmptyWeight == INDEX_NONE)
		{
			SharedEmptyWeight = Definition.WfcShapeWeights.EmptyWeight;
			SharedNonEmptyWeight = NonEmptyWeight;
		}
		else if (SharedEmptyWeight != Definition.WfcShapeWeights.EmptyWeight
			|| SharedNonEmptyWeight != NonEmptyWeight)
		{
			OutError = TEXT(
				"Easy、Normal、Hard 必须保持相同 EmptyWeight 与非空 Variant 总权重；"
				"难度只能重新分配 DeadEnd/Straight/Corner/T/Cross 的形态比例。");
			return false;
		}
	}

	if (!SeenDifficulties[0] || !SeenDifficulties[1] || !SeenDifficulties[2])
	{
		OutError = TEXT("Difficulties 缺少 Easy、Normal 或 Hard。");
		return false;
	}

	TSet<FName> StableFlowIds;
	bool bHasEscapeOnly = false;
	for (const FZeroEscapeFlowDefinition& Flow : Flows)
	{
		if (Flow.StableFlowId.IsNone() || Flow.FlowVersion <= 0
			|| StableFlowIds.Contains(Flow.StableFlowId))
		{
			OutError = TEXT("Flows 包含空 Id、重复 Id 或非法版本。");
			return false;
		}

		switch (Flow.CompletionRule)
		{
		case EZeroEscapeCompletionRule::EscapeOnly:
		case EZeroEscapeCompletionRule::CollectAll:
		case EZeroEscapeCompletionRule::CollectKOfN:
			break;
		default:
			OutError = TEXT("Flows 包含未支持的 CompletionRule。");
			return false;
		}

		StableFlowIds.Add(Flow.StableFlowId);
		bHasEscapeOnly |= Flow.StableFlowId == TEXT("EscapeOnly")
			&& Flow.CompletionRule == EZeroEscapeCompletionRule::EscapeOnly;
	}

	if (!bHasEscapeOnly)
	{
		OutError = TEXT("Flows 必须包含规则为 EscapeOnly 的 StableFlowId=EscapeOnly。");
		return false;
	}

	// 路线长度由完整 WFC 候选的最终 BFS/K-of-N 验收决定；Profile 不再用固定主干公式证明可解。
	return true;
}

bool UZeroEscapePresentationProfile::IsConfigured(
	const double LogicalTileSizeCm,
	FString& OutError) const
{
	OutError.Reset();
	if (PresentationVersion <= 0
		|| !FMath::IsFinite(StructureUnitSizeCm)
		|| StructureUnitSizeCm <= 0.0
		|| !FMath::IsNearlyEqual(LogicalTileSizeCm, StructureUnitSizeCm * 2.0)
		|| !FMath::IsFinite(FloorTopZCm)
		|| !FMath::IsFinite(WallBaseZCm)
		|| !FMath::IsFinite(CeilingPivotZCm))
	{
		OutError = TEXT("Presentation 的版本、结构高度或 2:1 尺寸关系非法。");
		return false;
	}

	if (!ValidateBinding(Floor, TEXT("Floor"), OutError)
		|| !ValidateBinding(Ceiling, TEXT("Ceiling"), OutError)
		|| !ValidateBinding(Wall, TEXT("Wall"), OutError))
	{
		return false;
	}

	if (IsValid(WallTopTrim.StaticMesh)
		&& !ValidateBinding(WallTopTrim, TEXT("WallTopTrim"), OutError))
	{
		return false;
	}

	if (IsValid(Pillar.StaticMesh)
		&& !ValidateBinding(Pillar, TEXT("Pillar"), OutError))
	{
		return false;
	}

	return true;
}

bool ValidateZeroEscapeGenerationAssetSet(
	const UZeroEscapeLevelGenerationProfile& Profile,
	const UZeroEscapePresentationProfile& Presentation,
	FString& OutError)
{
	OutError.Reset();
	return Profile.IsConfigured(OutError)
		&& Presentation.IsConfigured(Profile.SharedRouteConstraints.LogicalTileSizeCm, OutError);
}
