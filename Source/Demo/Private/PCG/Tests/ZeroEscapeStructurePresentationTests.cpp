// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeStructurePresentationTests.cpp
 * 职责：验证完整结构配方、共享边唯一所有权、顺时针旋转和非零地板高度基准。
 * 边界：只展开纯描述，不创建 World、HISM、临时导航坡面或一次性测试 Actor。
 */

#include "PCG/Presentation/ZeroEscapeStructureBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "PCG/ZeroEscapeGenerationAssets.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace
	{
		UStaticMesh* NewTestMesh()
		{
			return NewObject<UStaticMesh>(GetTransientPackage());
		}

		void ConfigureBinding(
			FZeroEscapeStructureMeshBinding& Binding,
			UStaticMesh* Mesh)
		{
			Binding.StaticMesh = Mesh;
			Binding.CollisionProfileName = TEXT("BlockAll");
			Binding.bCanEverAffectNavigation = true;
		}

		void ConfigureBaseProfile(
			UZeroEscapePresentationProfile& Profile,
			UStaticMesh* Mesh)
		{
			Profile.StructureUnitSizeCm = 300.0;
			Profile.FloorTopZCm = 37.0;
			Profile.WallBaseZCm = 5.0;
			Profile.CeilingPivotZCm = 305.0;
			Profile.bSpawnCeilingLights = false;
			ConfigureBinding(Profile.Floor, Mesh);
			ConfigureBinding(Profile.Ceiling, Mesh);
			ConfigureBinding(Profile.Wall, Mesh);
			Profile.WallTopTrim.StaticMesh = nullptr;
			Profile.Pillar.StaticMesh = Mesh;
			Profile.Pillar.CollisionProfileName = TEXT("BlockAll");
		}

		FZeroEscapeStructurePresentationPiece MakePiece(
			const FName PieceId,
			UStaticMesh* Mesh,
			const FTransform& RelativeTransform = FTransform::Identity)
		{
			FZeroEscapeStructurePresentationPiece Piece;
			Piece.PieceId = PieceId;
			Piece.StaticMesh = Mesh;
			Piece.RelativeTransform = RelativeTransform;
			Piece.CollisionProfileName = TEXT("BlockAll");
			Piece.CollisionRole = EZeroEscapeStructurePieceCollisionRole::StandardProfile;
			Piece.bCanEverAffectNavigation = true;
			return Piece;
		}

		FZeroEscapeStructurePresentationRecipe MakeHighRecipe(
			UStaticMesh* Mesh,
			const FTransform& PieceTransform = FTransform::Identity)
		{
			FZeroEscapeStructurePresentationRecipe Recipe;
			Recipe.DefinitionId = TEXT("High");
			Recipe.Kind = EZeroEscapeStructureKind::HighCeilingRoom;
			Recipe.CommonPieces.Add(MakePiece(TEXT("BoundaryOwner"), Mesh, PieceTransform));
			FZeroEscapeStructureOpeningSetPresentation& OpeningSet =
				Recipe.OpeningSets.AddDefaulted_GetRef();
			OpeningSet.OpeningSetId = TEXT("Default");
			return Recipe;
		}

		FZeroEscapeGeneratedLevelPlan MakePlan()
		{
			FZeroEscapeGeneratedLevelPlan Plan;
			Plan.FloorCount = 1;
			Plan.GridSize = FIntPoint(4, 4);
			Plan.LogicalTileSizeCm = 600.0;
			Plan.FloorHeightCm = 450.0;
			Plan.AnchorHeightCm = 100.0;
			return Plan;
		}

		void AddOrdinary(
			FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntVector Coordinate,
			const uint8 OpeningMask)
		{
			FZeroEscapeGeneratedOrdinaryCell& Cell =
				Plan.OrdinaryCells.AddDefaulted_GetRef();
			Cell.Coordinate = Coordinate;
			Cell.OpeningMask = OpeningMask;
		}

		void AddHighStructure(
			FZeroEscapeGeneratedLevelPlan& Plan,
			const FIntVector ReservedCell,
			const bool bUseClearance)
		{
			FZeroEscapeGeneratedStructure& Structure =
				Plan.Structures.AddDefaulted_GetRef();
			Structure.StableStructureId = 0;
			Structure.DefinitionId = TEXT("High");
			Structure.Kind = EZeroEscapeStructureKind::HighCeilingRoom;
			Structure.BaseCoordinate = ReservedCell;
			Structure.ActiveOpeningSetId = TEXT("Default");
			if (bUseClearance)
			{
				Structure.ClearanceCells.Add(ReservedCell);
			}
			else
			{
				Structure.SolidCells.Add(ReservedCell);
			}
		}

		int32 CountGroup(
			const TArray<FStructurePresentationInstance>& Instances,
			const FName GroupId)
		{
			int32 Count = 0;
			for (const FStructurePresentationInstance& Instance : Instances)
			{
				if (Instance.GroupId == GroupId)
				{
					++Count;
				}
			}
			return Count;
		}

		int32 CountDistinctGroups(
			const TArray<FStructurePresentationInstance>& Instances)
		{
			TSet<FName> GroupIds;
			for (const FStructurePresentationInstance& Instance : Instances)
			{
				GroupIds.Add(Instance.GroupId);
			}
			return GroupIds.Num();
		}

		const FStructurePresentationInstance* FindGroup(
			const TArray<FStructurePresentationInstance>& Instances,
			const FName GroupId)
		{
			return Instances.FindByPredicate(
				[GroupId](const FStructurePresentationInstance& Instance)
				{
					return Instance.GroupId == GroupId;
				});
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeStructureEdgeOwnershipTest,
		"Demo.PCG.Presentation.StructureEdgeOwnership",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeStructureEdgeOwnershipTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UStaticMesh* Mesh = NewTestMesh();
		UZeroEscapePresentationProfile* Profile =
			NewObject<UZeroEscapePresentationProfile>();
		ConfigureBaseProfile(*Profile, Mesh);
		Profile->StructureRecipes.Add(MakeHighRecipe(Mesh));
		TArray<FStructurePresentationInstance> Instances;
		FStructureBuildResult Result;

		FZeroEscapeGeneratedLevelPlan OpenPortalPlan = MakePlan();
		AddOrdinary(
			OpenPortalPlan,
			FIntVector(1, 1, 0),
			Grid::DirectionBit(1));
		AddHighStructure(OpenPortalPlan, FIntVector(2, 1, 0), false);
		TestTrue(
			TEXT("开放结构口应可展开"),
			FStructureBuilder::Expand(OpenPortalPlan, *Profile, Instances, Result));
		TestEqual(
			TEXT("开放口方向不生成普通墙，其他三边共六段"),
			CountGroup(Instances, TEXT("GeneratedWallHISM")), 6);
		TestEqual(
			TEXT("同 GroupId 的展开结果必须收敛为五个 HISM 批次"),
			CountDistinctGroups(Instances), 5);

		FZeroEscapeGeneratedLevelPlan SealedStructurePlan = MakePlan();
		AddOrdinary(
			SealedStructurePlan,
			FIntVector(1, 1, 0),
			Grid::DirectionBit(0));
		AddHighStructure(SealedStructurePlan, FIntVector(2, 1, 0), false);
		TestTrue(
			TEXT("结构封闭边应可展开"),
			FStructureBuilder::Expand(SealedStructurePlan, *Profile, Instances, Result));
		TestEqual(
			TEXT("相邻结构保留格的封闭边不由普通格重复生成"),
			CountGroup(Instances, TEXT("GeneratedWallHISM")), 4);
		TestEqual(
			TEXT("封闭结构边只由完整结构 recipe 生成一份"),
			CountGroup(Instances, TEXT("Generated_High_Common_None_BoundaryOwner")), 1);

		FZeroEscapeGeneratedLevelPlan OrdinaryPairPlan = MakePlan();
		AddOrdinary(OrdinaryPairPlan, FIntVector(1, 1, 0), Grid::DirectionBit(0));
		AddOrdinary(OrdinaryPairPlan, FIntVector(2, 1, 0), Grid::DirectionBit(0));
		TestTrue(
			TEXT("相邻普通格应可展开"),
			FStructureBuilder::Expand(OrdinaryPairPlan, *Profile, Instances, Result));
		TestEqual(
			TEXT("普通格共享封闭边仍按 300cm 段去重"),
			CountGroup(Instances, TEXT("GeneratedWallHISM")), 10);

		FZeroEscapeGeneratedLevelPlan ClearancePlan = MakePlan();
		AddOrdinary(ClearancePlan, FIntVector(1, 1, 0), Grid::DirectionBit(0));
		AddHighStructure(ClearancePlan, FIntVector(2, 1, 0), true);
		TestTrue(
			TEXT("高厅净空边应可展开"),
			FStructureBuilder::Expand(ClearancePlan, *Profile, Instances, Result));
		TestEqual(
			TEXT("高厅 Clearance 相邻边由高厅 recipe 唯一负责"),
			CountGroup(Instances, TEXT("GeneratedWallHISM")), 4);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeStructureRotationAndHeightTest,
		"Demo.PCG.Presentation.StructureRotationAndHeight",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeStructureRotationAndHeightTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UStaticMesh* Mesh = NewTestMesh();
		UZeroEscapePresentationProfile* Profile =
			NewObject<UZeroEscapePresentationProfile>();
		ConfigureBaseProfile(*Profile, Mesh);
		Profile->StructureRecipes.Add(MakeHighRecipe(
			Mesh, FTransform(FVector(100.0, 0.0, 10.0))));

		const FVector ExpectedOffsets[4] = {
			FVector(100.0, 0.0, 10.0),
			FVector(0.0, -100.0, 10.0),
			FVector(-100.0, 0.0, 10.0),
			FVector(0.0, 100.0, 10.0)};
		for (uint8 QuarterTurn = 0; QuarterTurn < 4; ++QuarterTurn)
		{
			FZeroEscapeGeneratedLevelPlan Plan = MakePlan();
			AddOrdinary(Plan, FIntVector(0, 0, 0), Grid::DirectionBit(0));
			AddHighStructure(Plan, FIntVector(2, 2, 0), false);
			Plan.Structures[0].QuarterTurnCount = QuarterTurn;
			TArray<FStructurePresentationInstance> Instances;
			FStructureBuildResult Result;
			TestTrue(
				TEXT("四种顺时针旋转都应展开"),
				FStructureBuilder::Expand(Plan, *Profile, Instances, Result));
			const FStructurePresentationInstance* Marker = FindGroup(
				Instances, TEXT("Generated_High_Common_None_BoundaryOwner"));
			TestNotNull(TEXT("完整结构标记 Piece 必须存在"), Marker);
			if (Marker != nullptr)
			{
				const FVector Expected = FVector(1200.0, 1200.0, Profile->FloorTopZCm)
					+ ExpectedOffsets[QuarterTurn];
				TestTrue(
					TEXT("QuarterTurnCount 必须映射为 UE 负 Yaw 的顺时针旋转"),
					Marker->LocalTransform.GetLocation().Equals(Expected, 0.1));
			}
			const FStructurePresentationInstance* Floor =
				FindGroup(Instances, TEXT("GeneratedFloorHISM"));
			const FStructurePresentationInstance* Ceiling =
				FindGroup(Instances, TEXT("GeneratedCeilingHISM"));
			const FStructurePresentationInstance* Wall =
				FindGroup(Instances, TEXT("GeneratedWallHISM"));
			const FStructurePresentationInstance* Pillar =
				FindGroup(Instances, TEXT("GeneratedPillarHISM"));
			TestNotNull(TEXT("普通地板必须存在"), Floor);
			TestNotNull(TEXT("普通天花板必须存在"), Ceiling);
			TestNotNull(TEXT("普通墙必须存在"), Wall);
			TestNotNull(TEXT("普通柱必须存在"), Pillar);
			if (Floor && Ceiling && Wall && Pillar)
			{
				TestTrue(TEXT("地板使用 FloorTopZCm"),
					FMath::IsNearlyEqual(Floor->LocalTransform.GetLocation().Z, 37.0));
				TestTrue(TEXT("天花板使用同一地板顶面基准"),
					FMath::IsNearlyEqual(Ceiling->LocalTransform.GetLocation().Z, 342.0));
				TestTrue(TEXT("墙使用同一地板顶面基准"),
					FMath::IsNearlyEqual(Wall->LocalTransform.GetLocation().Z, 42.0));
				TestTrue(TEXT("柱使用同一地板顶面基准"),
					FMath::IsNearlyEqual(Pillar->LocalTransform.GetLocation().Z, 42.0));
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeStructurePresentationCrossContractTest,
		"Demo.PCG.Presentation.CrossAssetContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeStructurePresentationCrossContractTest::RunTest(
		const FString& Parameters)
	{
		(void)Parameters;
		UStaticMesh* Mesh = NewTestMesh();
		UZeroEscapeLevelGenerationProfile* Generation =
			NewObject<UZeroEscapeLevelGenerationProfile>();
		UZeroEscapePresentationProfile* Presentation =
			NewObject<UZeroEscapePresentationProfile>();
		ConfigureBaseProfile(*Presentation, Mesh);
		Presentation->StructureRecipes.Add(MakeHighRecipe(Mesh));

		FZeroEscapeStructureDefinition& Definition =
			Generation->StructureDefinitions.AddDefaulted_GetRef();
		Definition.DefinitionId = TEXT("High");
		Definition.Kind = EZeroEscapeStructureKind::HighCeilingRoom;
		FZeroEscapeStructureOpeningSetDefinition& LogicalOpening =
			Definition.AllowedOpeningSets.AddDefaulted_GetRef();
		LogicalOpening.SetId = TEXT("Default");
		FString Error;
		TestTrue(
			TEXT("逻辑定义与表现 recipe 一一对应时合同通过"),
			ValidateZeroEscapeStructurePresentationBindings(
				*Generation, *Presentation, Error));

		Presentation->StructureRecipes[0].OpeningSets[0].OpeningSetId = TEXT("OrphanSet");
		TestFalse(
			TEXT("表现包含逻辑层没有的开口组合时必须失败"),
			ValidateZeroEscapeStructurePresentationBindings(
				*Generation, *Presentation, Error));
		Presentation->StructureRecipes[0].OpeningSets[0].OpeningSetId = TEXT("Default");
		Presentation->StructureRecipes[0].Kind = EZeroEscapeStructureKind::TwoFloorStair;
		TestFalse(
			TEXT("同 DefinitionId 但 Kind 不一致时必须失败"),
			ValidateZeroEscapeStructurePresentationBindings(
				*Generation, *Presentation, Error));
		TestFalse(
			TEXT("双层楼梯缺少两块隐藏坡面时自身配置也必须失败"),
			Presentation->IsConfigured(600.0, Error));
		return true;
	}
}

#endif
