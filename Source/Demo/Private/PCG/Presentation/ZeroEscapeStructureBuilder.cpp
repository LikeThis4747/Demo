// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeStructureBuilder.cpp
 * 职责：展开多层普通格和完整结构配方，并按稳定组提交 HISM。
 * 边界：不读取 Level0、不猜素材路径；隐藏坡面与护栏都来自项目 Presentation DataAsset。
 */

#include "PCG/Presentation/ZeroEscapeStructureBuilder.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Containers/StaticArray.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "PCG/ZeroEscapeGenerationAssets.h"
#include "PCG/ZeroEscapeGenerationCore.h"
#include "UObject/UObjectGlobals.h"

namespace ZeroEscape::LevelGeneration
{
	namespace StructureBuilderPrivate
	{
		/** 一段 300 cm 墙的规范地址；Z 是楼层号，Axis 0/1 分别沿 +X/+Y。 */
		struct FStructureEdgeKey
		{
			FIntVector StartVertex = FIntVector::ZeroValue;
			uint8 Axis = 0;

			bool operator==(const FStructureEdgeKey& Other) const
			{
				return StartVertex == Other.StartVertex && Axis == Other.Axis;
			}
		};

		uint32 GetTypeHash(const FStructureEdgeKey& Edge)
		{
			uint32 Hash = ::GetTypeHash(Edge.StartVertex.X);
			Hash = HashCombineFast(Hash, ::GetTypeHash(Edge.StartVertex.Y));
			Hash = HashCombineFast(Hash, ::GetTypeHash(Edge.StartVertex.Z));
			return HashCombineFast(Hash, ::GetTypeHash(Edge.Axis));
		}

		/** 同一 Mesh 与运行时绑定的实例共享一个 HISM；组数组按首次出现顺序保存。 */
		struct FInstanceGroup
		{
			FName GroupId = NAME_None;
			TObjectPtr<UStaticMesh> StaticMesh = nullptr;
			FName CollisionProfileName = NAME_None;
			EZeroEscapeStructurePieceCollisionRole CollisionRole =
				EZeroEscapeStructurePieceCollisionRole::StandardProfile;
			bool bCanEverAffectNavigation = false;
			bool bHiddenInGame = false;
			bool bCastShadow = true;
			int32 RelatedStableId = INDEX_NONE;
			TArray<FTransform> LocalTransforms;
		};

		bool Fail(
			FStructureBuildResult& Result,
			const int32 RelatedStableId,
			FString Message)
		{
			Result.RelatedStableId = RelatedStableId;
			Result.Error = MoveTemp(Message);
			return false;
		}

		bool IsInsidePlan(const FIntVector Coordinate, const FZeroEscapeGeneratedLevelPlan& Plan)
		{
			return Coordinate.Z >= 0
				&& Coordinate.Z < Plan.FloorCount
				&& Grid::IsInside(FIntPoint(Coordinate.X, Coordinate.Y), Plan.GridSize);
		}

		bool CoordinateLess(const FIntVector A, const FIntVector B)
		{
			if (A.Z != B.Z)
			{
				return A.Z < B.Z;
			}
			return A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
		}

		FVector AddressFloorLocation(
			const FIntVector Coordinate,
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const double FloorTopZCm)
		{
			return FVector(
				static_cast<double>(Coordinate.X) * Plan.LogicalTileSizeCm,
				static_cast<double>(Coordinate.Y) * Plan.LogicalTileSizeCm,
				static_cast<double>(Coordinate.Z) * Plan.FloorHeightCm + FloorTopZCm);
		}

		FName MakeRecipeGroupId(
			const FName DefinitionId,
			const TCHAR* Scope,
			const FName OpeningSetId,
			const FName PieceId)
		{
			return FName(*FString::Printf(
				TEXT("Generated_%s_%s_%s_%s"),
				*DefinitionId.ToString(),
				Scope,
				*OpeningSetId.ToString(),
				*PieceId.ToString()));
		}

		void AddNormalInstance(
			const FName GroupId,
			const FZeroEscapeStructureMeshBinding& Binding,
			const FTransform& CanonicalTransform,
			TArray<FStructurePresentationInstance>& OutInstances)
		{
			if (!IsValid(Binding.StaticMesh))
			{
				return;
			}

			FStructurePresentationInstance& Instance = OutInstances.AddDefaulted_GetRef();
			Instance.GroupId = GroupId;
			Instance.StaticMesh = Binding.StaticMesh;
			Instance.CollisionProfileName = Binding.CollisionProfileName;
			Instance.CollisionRole = EZeroEscapeStructurePieceCollisionRole::StandardProfile;
			Instance.bCanEverAffectNavigation = Binding.bCanEverAffectNavigation;
			Instance.LocalTransform = Binding.PivotCorrection * CanonicalTransform;
		}

		bool AddRecipePiece(
			const FZeroEscapeStructurePresentationPiece& Piece,
			const FName GroupId,
			const FTransform& StructureTransform,
			const int32 StableStructureId,
			TArray<FStructurePresentationInstance>& OutInstances,
			FStructureBuildResult& OutResult)
		{
			const FTransform LocalTransform = Piece.RelativeTransform * StructureTransform;
			if (!IsValid(Piece.StaticMesh)
				|| !FGenerationCore::IsFinitePositiveScaleTransform(LocalTransform))
			{
				return Fail(
					OutResult,
					StableStructureId,
					FString::Printf(
						TEXT("结构表现 Piece %s 的 Mesh 或最终 Transform 非法。"),
						*Piece.PieceId.ToString()));
			}

			FStructurePresentationInstance& Instance = OutInstances.AddDefaulted_GetRef();
			Instance.GroupId = GroupId;
			Instance.StaticMesh = Piece.StaticMesh;
			Instance.CollisionProfileName = Piece.CollisionProfileName;
			Instance.CollisionRole = Piece.CollisionRole;
			Instance.bCanEverAffectNavigation = Piece.bCanEverAffectNavigation;
			Instance.bHiddenInGame = Piece.bHiddenInGame;
			Instance.bCastShadow = Piece.bCastShadow;
			Instance.LocalTransform = LocalTransform;
			Instance.RelatedStableId = StableStructureId;
			return true;
		}

		void AddClosedTileEdge(
			const FIntVector Tile,
			const uint8 Direction,
			TSet<FStructureEdgeKey>& OutEdges)
		{
			const int32 CenterX = Tile.X * 2;
			const int32 CenterY = Tile.Y * 2;
			for (int32 Segment = 0; Segment < 2; ++Segment)
			{
				FStructureEdgeKey Edge;
				Edge.StartVertex.Z = Tile.Z;
				switch (Direction)
				{
				case 0:
					Edge.StartVertex.X = CenterX - 1 + Segment;
					Edge.StartVertex.Y = CenterY + 1;
					Edge.Axis = 0;
					break;
				case 1:
					Edge.StartVertex.X = CenterX + 1;
					Edge.StartVertex.Y = CenterY - 1 + Segment;
					Edge.Axis = 1;
					break;
				case 2:
					Edge.StartVertex.X = CenterX - 1 + Segment;
					Edge.StartVertex.Y = CenterY - 1;
					Edge.Axis = 0;
					break;
				default:
					Edge.StartVertex.X = CenterX - 1;
					Edge.StartVertex.Y = CenterY - 1 + Segment;
					Edge.Axis = 1;
					break;
				}
				OutEdges.Add(Edge);
			}
		}

		void AddEdgeToWallGraph(
			const FStructureEdgeKey& Edge,
			TMap<FIntVector, uint8>& InOutGraph)
		{
			const FIntVector End = Edge.StartVertex
				+ (Edge.Axis == 0 ? FIntVector(1, 0, 0) : FIntVector(0, 1, 0));
			if (Edge.Axis == 0)
			{
				InOutGraph.FindOrAdd(Edge.StartVertex) |= Grid::DirectionBit(1);
				InOutGraph.FindOrAdd(End) |= Grid::DirectionBit(3);
			}
			else
			{
				InOutGraph.FindOrAdd(Edge.StartVertex) |= Grid::DirectionBit(0);
				InOutGraph.FindOrAdd(End) |= Grid::DirectionBit(2);
			}
		}

		bool ExpandOrdinaryCells(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const UZeroEscapePresentationProfile& Profile,
			TArray<FStructurePresentationInstance>& OutInstances,
			FStructureBuildResult& OutResult)
		{
			if (Plan.FloorCount <= 0
				|| Plan.OrdinaryCells.IsEmpty()
				|| !FMath::IsNearlyEqual(Plan.LogicalTileSizeCm, 600.0)
				|| !FMath::IsNearlyEqual(Profile.StructureUnitSizeCm, 300.0)
				|| !FMath::IsNearlyEqual(
					Plan.LogicalTileSizeCm,
					Profile.StructureUnitSizeCm * 2.0)
				|| !FMath::IsFinite(Plan.FloorHeightCm)
				|| Plan.FloorHeightCm <= 0.0)
			{
				return Fail(
					OutResult,
					INDEX_NONE,
					TEXT("普通结构展开要求有效多层 Plan 与 600:300 结构尺寸。"));
			}

			// 完整结构唯一拥有其 Walkable/Solid/Clearance 保留格周围的结构边。
			// 普通格不得在相邻保留格的一侧再生成一份墙，否则会与 recipe 的封墙或护栏重叠。
			TMap<FIntVector, int32> StructureOwnerByReservedCell;
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				auto AddReservedCells =
					[&Plan, &StructureOwnerByReservedCell, &OutResult, &Structure](
						const TArray<FIntVector>& Cells) -> bool
					{
						for (const FIntVector Cell : Cells)
						{
							if (!IsInsidePlan(Cell, Plan))
							{
								return Fail(
									OutResult,
									Structure.StableStructureId,
									TEXT("完整结构包含越界的保留格。"));
							}
							if (StructureOwnerByReservedCell.Contains(Cell))
							{
								return Fail(
									OutResult,
									Structure.StableStructureId,
									TEXT("完整结构保留格存在重复所有权。"));
							}
							StructureOwnerByReservedCell.Add(
								Cell, Structure.StableStructureId);
						}
						return true;
					};
				if (!AddReservedCells(Structure.WalkableCells)
					|| !AddReservedCells(Structure.SolidCells)
					|| !AddReservedCells(Structure.ClearanceCells))
				{
					OutInstances.Reset();
					return false;
				}
			}

			TSet<FStructureEdgeKey> WallEdges;
			TSet<FIntVector> SeenOrdinaryCells;
			const double Offset = Profile.StructureUnitSizeCm * 0.5;
			for (const FZeroEscapeGeneratedOrdinaryCell& Cell : Plan.OrdinaryCells)
			{
				if (!IsInsidePlan(Cell.Coordinate, Plan)
					|| Cell.OpeningMask == 0
					|| (Cell.OpeningMask & ~Grid::AllOpenEdges) != 0
					|| SeenOrdinaryCells.Contains(Cell.Coordinate)
					|| StructureOwnerByReservedCell.Contains(Cell.Coordinate))
				{
					OutInstances.Reset();
					return Fail(
						OutResult,
						INDEX_NONE,
						TEXT("普通结构展开收到越界或 Empty Cell。"));
				}
				SeenOrdinaryCells.Add(Cell.Coordinate);

				const FVector CellFloor = AddressFloorLocation(
					Cell.Coordinate, Plan, Profile.FloorTopZCm);
				const double CeilingZ =
					static_cast<double>(Cell.Coordinate.Z) * Plan.FloorHeightCm
					+ Profile.FloorTopZCm + Profile.CeilingPivotZCm;
				for (int32 LocalY = 0; LocalY < 2; ++LocalY)
				{
					for (int32 LocalX = 0; LocalX < 2; ++LocalX)
					{
						const FVector FloorLocation(
							CellFloor.X + (LocalX == 0 ? -Offset : Offset),
							CellFloor.Y + (LocalY == 0 ? -Offset : Offset),
							CellFloor.Z);
						AddNormalInstance(
							TEXT("GeneratedFloorHISM"),
							Profile.Floor,
							FTransform(FloorLocation),
							OutInstances);
						AddNormalInstance(
							TEXT("GeneratedCeilingHISM"),
							Profile.Ceiling,
							FTransform(FVector(FloorLocation.X, FloorLocation.Y, CeilingZ)),
							OutInstances);
					}
				}

				for (uint8 Direction = 0; Direction < Grid::DirectionCount; ++Direction)
				{
					if ((Cell.OpeningMask & Grid::DirectionBit(Direction)) == 0)
					{
						const FIntPoint Neighbor2D = Grid::Step(
							FIntPoint(Cell.Coordinate.X, Cell.Coordinate.Y), Direction);
						const FIntVector Neighbor(
							Neighbor2D.X, Neighbor2D.Y, Cell.Coordinate.Z);
						if (!StructureOwnerByReservedCell.Contains(Neighbor))
						{
							AddClosedTileEdge(Cell.Coordinate, Direction, WallEdges);
						}
					}
				}
			}

			TArray<FStructureEdgeKey> StableEdges = WallEdges.Array();
			StableEdges.Sort([](const FStructureEdgeKey& A, const FStructureEdgeKey& B)
			{
				if (A.StartVertex != B.StartVertex)
				{
					return CoordinateLess(A.StartVertex, B.StartVertex);
				}
				return A.Axis < B.Axis;
			});

			TMap<FIntVector, uint8> WallGraph;
			for (const FStructureEdgeKey& Edge : StableEdges)
			{
				const double Unit = Profile.StructureUnitSizeCm;
				const double FloorBaseZ =
					Profile.FloorTopZCm
					+ static_cast<double>(Edge.StartVertex.Z) * Plan.FloorHeightCm;
				const FVector WallLocation(
					(Edge.StartVertex.X + (Edge.Axis == 0 ? 0.5 : 0.0)) * Unit,
					(Edge.StartVertex.Y + (Edge.Axis == 1 ? 0.5 : 0.0)) * Unit,
					FloorBaseZ + Profile.WallBaseZCm);
				const double Yaw = Edge.Axis == 0 ? 90.0 : 0.0;
				AddNormalInstance(
					TEXT("GeneratedWallHISM"),
					Profile.Wall,
					FTransform(FRotator(0.0, Yaw, 0.0), WallLocation),
					OutInstances);
				AddNormalInstance(
					TEXT("GeneratedWallTopTrimHISM"),
					Profile.WallTopTrim,
					FTransform(
						FRotator(0.0, Yaw, 0.0),
						FVector(
							WallLocation.X,
							WallLocation.Y,
							FloorBaseZ + Profile.CeilingPivotZCm)),
					OutInstances);
				AddEdgeToWallGraph(Edge, WallGraph);
			}

			TArray<FIntVector> StableVertices;
			WallGraph.GetKeys(StableVertices);
			StableVertices.Sort(CoordinateLess);
			for (const FIntVector Vertex : StableVertices)
			{
				const uint8 IncidentMask = WallGraph.FindChecked(Vertex);
				const int32 Degree = FMath::CountBits(static_cast<uint32>(IncidentMask));
				const bool bStraightThrough = Degree == 2
					&& (IncidentMask == 0x05u || IncidentMask == 0x0Au);
				if (Degree == 1 || Degree >= 3 || (Degree == 2 && !bStraightThrough))
				{
					AddNormalInstance(
						TEXT("GeneratedPillarHISM"),
						Profile.Pillar,
						FTransform(FVector(
							static_cast<double>(Vertex.X) * Profile.StructureUnitSizeCm,
							static_cast<double>(Vertex.Y) * Profile.StructureUnitSizeCm,
							Profile.FloorTopZCm
								+ static_cast<double>(Vertex.Z) * Plan.FloorHeightCm
								+ Profile.WallBaseZCm)),
						OutInstances);
				}
			}
			return true;
		}

		bool ExpandCompleteStructures(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const UZeroEscapePresentationProfile& Profile,
			TArray<FStructurePresentationInstance>& OutInstances,
			FStructureBuildResult& OutResult)
		{
			for (const FZeroEscapeGeneratedStructure& Structure : Plan.Structures)
			{
				if (Structure.StableStructureId < 0
					|| Structure.DefinitionId.IsNone()
					|| Structure.ActiveOpeningSetId.IsNone()
					|| Structure.QuarterTurnCount < 0
					|| Structure.QuarterTurnCount > 3
					|| !IsInsidePlan(Structure.BaseCoordinate, Plan))
				{
					OutInstances.Reset();
					return Fail(
						OutResult,
						Structure.StableStructureId,
						TEXT("完整结构的 ID、基准地址、旋转或开口组合非法。"));
				}

				const FZeroEscapeStructurePresentationRecipe* Recipe =
					Profile.FindStructureRecipe(Structure.DefinitionId);
				if (Recipe == nullptr || Recipe->Kind != Structure.Kind)
				{
					OutInstances.Reset();
					return Fail(
						OutResult,
						Structure.StableStructureId,
						FString::Printf(
							TEXT("DefinitionId=%s 缺少同类型表现配方。"),
							*Structure.DefinitionId.ToString()));
				}

				const FZeroEscapeStructureOpeningSetPresentation* OpeningSet =
					Recipe->OpeningSets.FindByPredicate(
						[&Structure](const FZeroEscapeStructureOpeningSetPresentation& Candidate)
						{
							return Candidate.OpeningSetId == Structure.ActiveOpeningSetId;
						});
				if (OpeningSet == nullptr)
				{
					OutInstances.Reset();
					return Fail(
						OutResult,
						Structure.StableStructureId,
						FString::Printf(
							TEXT("表现配方 %s 不支持开口组合 %s。"),
							*Structure.DefinitionId.ToString(),
							*Structure.ActiveOpeningSetId.ToString()));
				}

				const FVector BaseLocation = AddressFloorLocation(
					Structure.BaseCoordinate, Plan, Profile.FloorTopZCm);
				const FTransform StructureTransform(
					FRotator(0.0, Structure.QuarterTurnCount * -90.0, 0.0),
					BaseLocation);

				for (const FZeroEscapeStructurePresentationPiece& Piece : Recipe->CommonPieces)
				{
					if (!AddRecipePiece(
							Piece,
							MakeRecipeGroupId(
								Recipe->DefinitionId, TEXT("Common"), NAME_None, Piece.PieceId),
							StructureTransform,
							Structure.StableStructureId,
							OutInstances,
							OutResult))
					{
						OutInstances.Reset();
						return false;
					}
				}

				for (const FZeroEscapeStructurePresentationPiece& Piece : OpeningSet->Pieces)
				{
					if (!AddRecipePiece(
							Piece,
							MakeRecipeGroupId(
								Recipe->DefinitionId,
								TEXT("Opening"),
								OpeningSet->OpeningSetId,
								Piece.PieceId),
							StructureTransform,
							Structure.StableStructureId,
							OutInstances,
							OutResult))
					{
						OutInstances.Reset();
						return false;
					}
				}

				for (const FZeroEscapeStructurePresentationPiece& Piece : Recipe->NavigationRampPieces)
				{
					if (!AddRecipePiece(
							Piece,
							MakeRecipeGroupId(
								Recipe->DefinitionId, TEXT("NavRamp"), NAME_None, Piece.PieceId),
							StructureTransform,
							Structure.StableStructureId,
							OutInstances,
							OutResult))
					{
						OutInstances.Reset();
						return false;
					}
				}
			}
			return true;
		}

		bool IsSameBinding(
			const FInstanceGroup& Group,
			const FStructurePresentationInstance& Instance)
		{
			return Group.StaticMesh == Instance.StaticMesh
				&& Group.CollisionProfileName == Instance.CollisionProfileName
				&& Group.CollisionRole == Instance.CollisionRole
				&& Group.bCanEverAffectNavigation == Instance.bCanEverAffectNavigation
				&& Group.bHiddenInGame == Instance.bHiddenInGame
				&& Group.bCastShadow == Instance.bCastShadow;
		}
	}

	bool FStructureBuilder::Expand(
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const UZeroEscapePresentationProfile& Profile,
		TArray<FStructurePresentationInstance>& OutInstances,
		FStructureBuildResult& OutResult)
	{
		OutInstances.Reset();
		OutResult.Reset();
		if (!StructureBuilderPrivate::ExpandOrdinaryCells(
				Plan, Profile, OutInstances, OutResult)
			|| !StructureBuilderPrivate::ExpandCompleteStructures(
				Plan, Profile, OutInstances, OutResult))
		{
			OutInstances.Reset();
			return false;
		}

		for (const FStructurePresentationInstance& Instance : OutInstances)
		{
			if (Instance.GroupId.IsNone()
				|| !IsValid(Instance.StaticMesh)
				|| Instance.CollisionProfileName.IsNone()
				|| !FGenerationCore::IsFinitePositiveScaleTransform(
					Instance.LocalTransform))
			{
				OutInstances.Reset();
				return StructureBuilderPrivate::Fail(
					OutResult,
					Instance.RelatedStableId,
					TEXT("结构展开产生了非法表现实例。"));
			}
		}
		return true;
	}

	bool FStructureBuilder::Build(
		AActor& Owner,
		USceneComponent& GeneratedRoot,
		const FZeroEscapeGeneratedLevelPlan& Plan,
		const UZeroEscapePresentationProfile& Profile,
		TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>&
			InOutRegisteredComponents,
		FStructureBuildResult& OutResult)
	{
		using namespace StructureBuilderPrivate;
		TArray<FStructurePresentationInstance> Instances;
		if (!Expand(Plan, Profile, Instances, OutResult))
		{
			return false;
		}

		TArray<FInstanceGroup> Groups;
		for (const FStructurePresentationInstance& Instance : Instances)
		{
			FInstanceGroup* Group = Groups.FindByPredicate(
				[&Instance](const FInstanceGroup& Candidate)
				{
					return IsSameBinding(Candidate, Instance);
				});
			if (Group == nullptr)
			{
				const bool bConflictingGroupId = Groups.ContainsByPredicate(
					[&Instance](const FInstanceGroup& Candidate)
					{
						return Candidate.GroupId == Instance.GroupId;
					});
				if (bConflictingGroupId)
				{
					return Fail(
						OutResult,
						Instance.RelatedStableId,
						FString::Printf(
							TEXT("表现 GroupId=%s 被配置成不同 Mesh 或碰撞属性。"),
							*Instance.GroupId.ToString()));
				}

				Group = &Groups.AddDefaulted_GetRef();
				Group->GroupId = Instance.GroupId;
				Group->StaticMesh = Instance.StaticMesh;
				Group->CollisionProfileName = Instance.CollisionProfileName;
				Group->CollisionRole = Instance.CollisionRole;
				Group->bCanEverAffectNavigation = Instance.bCanEverAffectNavigation;
				Group->bHiddenInGame = Instance.bHiddenInGame;
				Group->bCastShadow = Instance.bCastShadow;
				Group->RelatedStableId = Instance.RelatedStableId;
			}
			Group->LocalTransforms.Add(Instance.LocalTransform);
		}

		for (const FInstanceGroup& Group : Groups)
		{
			UHierarchicalInstancedStaticMeshComponent* Component =
				NewObject<UHierarchicalInstancedStaticMeshComponent>(
					&Owner,
					MakeUniqueObjectName(
						&Owner,
						UHierarchicalInstancedStaticMeshComponent::StaticClass(),
						Group.GroupId));
			if (!IsValid(Component))
			{
				return Fail(
					OutResult,
					Group.RelatedStableId,
					FString::Printf(TEXT("创建 HISM GroupId=%s 失败。"), *Group.GroupId.ToString()));
			}

			InOutRegisteredComponents.Add(Component);
			Component->SetupAttachment(&GeneratedRoot);
			Component->SetMobility(EComponentMobility::Static);
			Component->SetStaticMesh(Group.StaticMesh);
			Component->SetCollisionProfileName(Group.CollisionProfileName);
			Component->SetCastShadow(Group.bCastShadow);
			Component->SetVisibility(!Group.bHiddenInGame, true);
			Component->SetHiddenInGame(Group.bHiddenInGame, true);
			switch (Group.CollisionRole)
			{
			case EZeroEscapeStructurePieceCollisionRole::StandardProfile:
				Component->SetCanEverAffectNavigation(Group.bCanEverAffectNavigation);
				break;
			case EZeroEscapeStructurePieceCollisionRole::VisibleStairMesh:
				Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				Component->SetCanEverAffectNavigation(false);
				break;
			case EZeroEscapeStructurePieceCollisionRole::HiddenNavigationRamp:
				Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Component->SetCollisionObjectType(ECC_WorldStatic);
				Component->SetCollisionResponseToAllChannels(ECR_Ignore);
				Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
				Component->SetCanEverAffectNavigation(true);
				Component->SetVisibility(false, true);
				Component->SetHiddenInGame(true, true);
				Component->SetCastShadow(false);
				Component->SetRenderInMainPass(false);
				Component->SetRenderInDepthPass(false);
				Component->SetVisibleInRayTracing(false);
				Component->SetHiddenInSceneCapture(true);
				Component->bVisibleInReflectionCaptures = false;
				Component->bVisibleInRealTimeSkyCaptures = false;
				Component->bVisibleInReflections = false;
				break;
			default:
				return Fail(
					OutResult,
					Group.RelatedStableId,
					TEXT("结构 Piece 使用了未知碰撞职责。"));
			}
			Component->RegisterComponent();

			const TArray<int32> AddedInstanceIndices =
				Component->AddInstances(Group.LocalTransforms, true, false, true);
			if (AddedInstanceIndices.Num() != Group.LocalTransforms.Num())
			{
				return Fail(
					OutResult,
					Group.RelatedStableId,
					FString::Printf(
						TEXT("HISM GroupId=%s 批量提交数量不一致：输入=%d，返回=%d。"),
						*Group.GroupId.ToString(),
						Group.LocalTransforms.Num(),
						AddedInstanceIndices.Num()));
			}
			OutResult.InstancedMeshCount += AddedInstanceIndices.Num();
			++OutResult.HismComponentCount;
		}
		return true;
	}
}
