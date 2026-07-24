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

bool FZeroEscapeWfcShapeWeights::IsConfigured(FString& OutError) const
{
	OutError.Reset();
	if (EmptyWeight <= 0 || DeadEndWeight <= 0 || StraightWeight <= 0
		|| CornerWeight <= 0 || TJunctionWeight <= 0 || CrossWeight <= 0)
	{
		OutError = TEXT("WfcShapeWeights 的六类权重都必须大于 0。");
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

	const FIntPoint GridSize = SharedRouteConstraints.GridSize;
	if (GridSize.X < ZeroEscape::GenerationLimits::MinGridAxis
		|| GridSize.Y < ZeroEscape::GenerationLimits::MinGridAxis
		|| GridSize.X > ZeroEscape::GenerationLimits::MaxGridAxis
		|| GridSize.Y > ZeroEscape::GenerationLimits::MaxGridAxis
		|| GridSize.X * GridSize.Y > ZeroEscape::GenerationLimits::MaxGridCells)
	{
		OutError = TEXT("SharedRouteConstraints.GridSize 超出运行时安全范围。");
		return false;
	}

	// Grid Solver 的构造性布局需要左右各留起终点/安全边界，并为上下两条 2x2 房间 Lane
	// 保留中部骨架。把 14x10 门槛留在 Profile 边界，可避免合法资产进入随机阶段后才失败。
	if (GridSize.X < 14 || GridSize.Y < 10)
	{
		OutError = TEXT("V3.2 的 2x2 房间布局要求 GridSize 至少为 14x10。");
		return false;
	}

	// V3.2 首版只接受已经测量并验证的 600/300 双层网格，避免再次引入不透明适配层。
	if (!FMath::IsNearlyEqual(SharedRouteConstraints.LogicalTileSizeCm, 600.0)
		|| SharedRouteConstraints.RoomSizeTiles != 2)
	{
		OutError = TEXT("V3.2 要求 LogicalTileSizeCm=600 且 RoomSizeTiles=2。");
		return false;
	}

	if (SharedRouteConstraints.ObjectiveProgressBandCount <= 0
		|| SharedRouteConstraints.OptionalEnvelopeRadius < 0
		|| SharedRouteConstraints.MaxRequiredRouteLengthTiles <= 0
		|| SharedRouteConstraints.MaxRequiredRouteExtraTiles < 0
		|| !FMath::IsFinite(SharedRouteConstraints.GameplayAnchorHeightCm)
		|| SharedRouteConstraints.GameplayAnchorHeightCm < 0.0)
	{
		OutError = TEXT("SharedRouteConstraints 包含非法进度、路线或 Anchor 参数。");
		return false;
	}

	// Objective 房沿 X 轴按进度带离散放置。每个 2x2 房之间至少留一格，左右还要保留
	// Start/Exit 与安全边界；该公式与 Grid Solver 的构造性槽位完全一致。
	const int32 MinRoomX = 4;
	// 同带上房会向右错开一格，确保上下两房各自替代不同的主干边；右侧因此比旧单门布局
	// 多预留一格。该值必须与 Grid Solver 的 MaxRoomBaseX 保持一致。
	const int32 MaxRoomX = GridSize.X - SharedRouteConstraints.RoomSizeTiles - 5;
	const int32 RoomSpan = MaxRoomX - MinRoomX;
	const int32 MinimumBandSpacing = SharedRouteConstraints.RoomSizeTiles + 1;
	if (MaxRoomX < MinRoomX
		|| (SharedRouteConstraints.ObjectiveProgressBandCount > 1
			&& RoomSpan < (SharedRouteConstraints.ObjectiveProgressBandCount - 1)
				* MinimumBandSpacing))
	{
		OutError = TEXT("GridSize.X 无法容纳当前进度带数量、2x2 房间与安全间距。");
		return false;
	}

	// 上下 Lane 紧邻中央主干；仍为地图外圈保留一格封闭边界。
	const int32 CenterY = GridSize.Y / 2;
	const int32 LowerLaneMinY = CenterY - SharedRouteConstraints.RoomSizeTiles;
	const int32 UpperLaneMinY = CenterY + 1;
	if (LowerLaneMinY < 1
		|| UpperLaneMinY + SharedRouteConstraints.RoomSizeTiles > GridSize.Y - 1)
	{
		OutError = TEXT("GridSize.Y 无法容纳紧邻中央主干的上下 2x2 房间 Lane。");
		return false;
	}

	bool SeenDifficulties[3] = {false, false, false};
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

		if (Definition.MaxOptionalSideBranches < 0
			|| Definition.MaxOptionalForwardLinks < 0
			|| Definition.ObjectiveCandidateCount < 0
			|| Definition.ObjectiveCandidateCount > ZeroEscape::GenerationLimits::MaxObjectiveCandidates
			|| Definition.RequiredObjectiveCount < 0
			|| Definition.RequiredObjectiveCount > Definition.ObjectiveCandidateCount)
		{
			OutError = TEXT("DifficultyDefinition 的分支或 K/N 参数非法。");
			return false;
		}

		const int32 RoomSlotCapacity = SharedRouteConstraints.ObjectiveProgressBandCount * 2;
		if (Definition.ObjectiveCandidateCount > RoomSlotCapacity)
		{
			OutError = TEXT("ObjectiveCandidateCount 超过进度带上下房间槽容量。");
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
		StableFlowIds.Add(Flow.StableFlowId);
		bHasEscapeOnly |= Flow.StableFlowId == TEXT("EscapeOnly")
			&& Flow.CompletionRule == EZeroEscapeCompletionRule::EscapeOnly;
	}

	if (!bHasEscapeOnly)
	{
		OutError = TEXT("Flows 必须包含规则为 EscapeOnly 的 StableFlowId=EscapeOnly。");
		return false;
	}

	// 双门房替代一条主干边，收集一个目标相对直走最多增加 2 格。把最坏 K 值与
	// 两个共享路线上限在 Profile 阶段联合验证，正常 Seed 就不会在生成后才因路线预算失败。
	for (const FZeroEscapeDifficultyDefinition& Difficulty : Difficulties)
	{
		for (const FZeroEscapeFlowDefinition& Flow : Flows)
		{
			int32 EffectiveRequiredCount = 0;
			switch (Flow.CompletionRule)
			{
			case EZeroEscapeCompletionRule::EscapeOnly:
				EffectiveRequiredCount = 0;
				break;
			case EZeroEscapeCompletionRule::CollectAll:
				EffectiveRequiredCount = Difficulty.ObjectiveCandidateCount;
				break;
			case EZeroEscapeCompletionRule::CollectKOfN:
				EffectiveRequiredCount = Difficulty.RequiredObjectiveCount;
				break;
			default:
				OutError = TEXT("Flow 使用了未支持的 CompletionRule。");
				return false;
			}

			const int32 ConstructiveExtraTiles = EffectiveRequiredCount * 2;
			const int32 ConstructiveCompletionTiles = GridSize.X - 1 + ConstructiveExtraTiles;
			if (ConstructiveExtraTiles > SharedRouteConstraints.MaxRequiredRouteExtraTiles
				|| ConstructiveCompletionTiles > SharedRouteConstraints.MaxRequiredRouteLengthTiles)
			{
				OutError = FString::Printf(
					TEXT("难度 %d / Flow %s 的构造路线至少需要总长 %d、额外 %d 格，超过共享上限。"),
					static_cast<int32>(Difficulty.Difficulty),
					*Flow.StableFlowId.ToString(),
					ConstructiveCompletionTiles,
					ConstructiveExtraTiles);
				return false;
			}
		}
	}

	return WfcShapeWeights.IsConfigured(OutError);
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
