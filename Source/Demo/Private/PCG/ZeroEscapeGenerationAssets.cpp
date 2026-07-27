// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationAssets.cpp
 * 职责：对 Grid/WFC 策划参数，以及结构与顶灯表现绑定执行 fail-closed 校验。
 * 边界：不加载替代资源、不猜测 Pivot、不修正 DataAsset，也不访问 World。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

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

	// 房间槽需要在左右为 Start/Exit 留出连接空间，并在中线两侧保留外部入口缓冲。
	if (GridSize.X < 14 || GridSize.Y < 10)
	{
		OutError = TEXT("Start/Exit 与中立房间槽要求 GridSize 至少为 14x10。");
		return false;
	}

	// 当前表现层只接受已经测量并验证的 600/300 双层网格。
	if (!FMath::IsNearlyEqual(Route.LogicalTileSizeCm, 600.0)
		|| Route.RoomSizeTiles != 2)
	{
		OutError = TEXT("当前结构契约要求 LogicalTileSizeCm=600 且 RoomSizeTiles=2。");
		return false;
	}

	if (Route.RoomCount < 0
		|| Route.RoomCount > ZeroEscape::GenerationLimits::MaxRoomCount
		|| Route.MaxRequiredRouteLengthTiles <= 0
		|| !FMath::IsFinite(Route.AnchorHeightCm)
		|| Route.AnchorHeightCm < 0.0)
	{
		OutError = TEXT("SharedRouteConstraints 包含非法房间、路线或 Anchor 参数。");
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

	// 中立房间沿 X 轴均匀分布；相邻房间至少留一格，房间外部连接完全交给 WFC。
	const int32 MinRoomX = 4;
	const int32 MaxRoomX = GridSize.X - Route.RoomSizeTiles - 5;
	const int32 RoomSpan = MaxRoomX - MinRoomX;
	const int32 MinimumRoomSpacing = Route.RoomSizeTiles + 1;
	if (Route.RoomCount > 0
		&& (MaxRoomX < MinRoomX
			|| (Route.RoomCount > 1
				&& RoomSpan < (Route.RoomCount - 1) * MinimumRoomSpacing)))
	{
		OutError = TEXT("GridSize.X 无法容纳 RoomCount、2x2 房间与安全间距。");
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

	const int64 FixedNonEmptyCellCount = 2LL
		+ static_cast<int64>(Route.RoomCount)
			* Route.RoomSizeTiles * Route.RoomSizeTiles;
	if (FixedNonEmptyCellCount > Route.MaxWalkableCellCount)
	{
		OutError = FString::Printf(
			TEXT("Start/Exit/Rooms 至少需要 %lld 个非空格，超过 MaxWalkable=%d。"),
			static_cast<long long>(FixedNonEmptyCellCount),
			Route.MaxWalkableCellCount);
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

	// Profile 只声明共享上限；路线长度由完整 WFC 候选的最终 BFS 验收。
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

	// 关闭顶灯时允许灯类为空，确保同一套结构表现可以无代码地回退到“不生成灯”。
	if (bSpawnCeilingLights)
	{
		const UClass* LightActorClass = CeilingLightActorClass.Get();
		if (!IsValid(LightActorClass)
			|| LightActorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutError = TEXT("Presentation.CeilingLightActorClass 必须是有效且非抽象的 Actor 类。");
			return false;
		}

		if (!IsFiniteUnitScaleTransform(CeilingLightCellTransform))
		{
			OutError = TEXT(
				"Presentation.CeilingLightCellTransform 必须是有限 Unit Scale Transform。");
			return false;
		}
	}

	return true;
}
