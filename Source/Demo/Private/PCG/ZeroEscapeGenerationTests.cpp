// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationTests.cpp
 * 职责：保存 PCG 算法快速回归测试；Test 0 冻结 Transform 合成约定，其余测试覆盖
 *       资产契约、确定性、K-of-N progression、Socket、A*、WFC 完整布局和失败原子性。
 * 边界：绝大多数测试只使用瞬态夹具；项目集成烟测只读加载 ZeroEscape 自有 DataAsset，
 *       不创建 UWorld、不 Spawn/HISM，也不执行碰撞、导航或 PIE。它能证明“SFC 配置可被
 *       当前算法消费”，不能替代正常视口中的接缝、可走性、性能和玩家实际走通验收。
 * 状态 Owner：无。
 */

#include "PCG/ZeroEscapeLayoutSolver.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "PCG/ZeroEscapeGenerationAssets.h"
#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace
	{
		// 夹具刻意使用项目逻辑尺寸和瞬态 UObject，不依赖 SFCorridors。这样换素材不会使
		// 算法测试失效；真实 Mesh Bounds、缝隙和碰撞则留给资产/PIE 验证。
		constexpr float PositionTolerance = 0.01f;
		constexpr float DirectionTolerance = 0.0001f;

		bool IsUnitScale(const FTransform& Transform)
		{
			return Transform.GetScale3D().Equals(FVector::OneVector, DirectionTolerance);
		}

		/** 构造符合 Catalog Frame 契约的单层水平 Portal Transform。 */
		FTransform MakePortalTransform(
			const EZeroEscapeCardinalDirection Direction,
			const FVector& Translation)
		{
			double YawDegrees = 0.0;
			switch (Direction)
			{
			case EZeroEscapeCardinalDirection::North:
				YawDegrees = 90.0;
				break;
			case EZeroEscapeCardinalDirection::East:
				YawDegrees = 0.0;
				break;
			case EZeroEscapeCardinalDirection::South:
				YawDegrees = -90.0;
				break;
			case EZeroEscapeCardinalDirection::West:
				YawDegrees = 180.0;
				break;
			default:
				checkNoEntry();
				break;
			}
			return FTransform(FRotator(0.0, YawDegrees, 0.0), Translation, FVector::OneVector);
		}

		/** 构造 1x1 单层模块的测试 Portal。 */
		FZeroEscapeModulePortal MakePortal(
			const int32 StableSocketId,
			const EZeroEscapeCardinalDirection Direction,
			const EZeroEscapeSocketPolicy Policy = EZeroEscapeSocketPolicy::Required,
			const int32 ClosureModuleId = INDEX_NONE)
		{
			FVector Translation(0.0, 0.0, 200.0);
			switch (Direction)
			{
			case EZeroEscapeCardinalDirection::North:
				Translation.Y = 400.0;
				break;
			case EZeroEscapeCardinalDirection::East:
				Translation.X = 400.0;
				break;
			case EZeroEscapeCardinalDirection::South:
				Translation.Y = -400.0;
				break;
			case EZeroEscapeCardinalDirection::West:
				Translation.X = -400.0;
				break;
			default:
				checkNoEntry();
				break;
			}

			FZeroEscapeModulePortal Portal;
			Portal.StableSocketId = StableSocketId;
			Portal.CellOffset = FIntVector::ZeroValue;
			Portal.LocalTransform = MakePortalTransform(Direction, Translation);
			Portal.Direction = Direction;
			Portal.ConnectorTypeId = 1;
			Portal.DisplayType = TEXT("Walk");
			Portal.WidthClass = 1;
			Portal.HeightLayer = 0;
			Portal.Policy = Policy;
			Portal.ClosureModuleId = ClosureModuleId;
			return Portal;
		}

		/** 构造测试 Anchor；所有测试 Anchor 都位于逻辑模块内部。 */
		FZeroEscapeModuleAnchor MakeAnchor(
			const int32 StableAnchorId,
			const EZeroEscapeGameplayAnchorType Type)
		{
			FZeroEscapeModuleAnchor Anchor;
			Anchor.StableAnchorId = StableAnchorId;
			Anchor.Type = Type;
			Anchor.LocalTransform = FTransform(
				FQuat::Identity,
				FVector(0.0, 0.0, 100.0),
				FVector::OneVector);
			return Anchor;
		}

		/** 构造三档难度和三种 Flow 都合法的最小测试 Profile。 */
		void BuildValidProfile(UZeroEscapeLevelGenerationProfile& Profile)
		{
			Profile.ProfileVersion = 1;
			Profile.SharedRouteConstraints = FZeroEscapeSharedRouteConstraints();
			Profile.SharedRouteConstraints.CriticalPathNodeCount = 8;
			Profile.SharedRouteConstraints.MaxLeafOneWayEdgeCount = 2;
			Profile.SharedRouteConstraints.MaxObjectiveCandidateCount = 6;
			Profile.SharedRouteConstraints.MaxProgressionSearchStates = 65536;

			Profile.Difficulties.Reset();
			FZeroEscapeDifficultyDefinition Easy;
			Easy.Difficulty = EZeroEscapeDifficulty::Easy;
			Easy.ShortLeafBranchCount = 1;
			Easy.ForwardRejoinBranchCount = 1;
			Easy.ObjectiveCandidateCount = 3;
			Easy.RequiredObjectiveCount = 2;
			Profile.Difficulties.Add(Easy);

			FZeroEscapeDifficultyDefinition Normal = Easy;
			Normal.Difficulty = EZeroEscapeDifficulty::Normal;
			Normal.ShortLeafBranchCount = 2;
			Normal.ObjectiveCandidateCount = 4;
			Normal.RequiredObjectiveCount = 3;
			Profile.Difficulties.Add(Normal);

			FZeroEscapeDifficultyDefinition Hard = Normal;
			Hard.Difficulty = EZeroEscapeDifficulty::Hard;
			Hard.ForwardRejoinBranchCount = 2;
			Hard.ObjectiveCandidateCount = 5;
			Hard.RequiredObjectiveCount = 4;
			Profile.Difficulties.Add(Hard);

			Profile.Flows.Reset();
			FZeroEscapeFlowDefinition EscapeOnly;
			EscapeOnly.StableFlowId = TEXT("EscapeOnly");
			EscapeOnly.CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
			Profile.Flows.Add(EscapeOnly);

			FZeroEscapeFlowDefinition CollectAll;
			CollectAll.StableFlowId = TEXT("CollectAll");
			CollectAll.CompletionRule = EZeroEscapeCompletionRule::CollectAll;
			CollectAll.AllowedObjectiveRoles = {
				EZeroEscapeTopologyRole::MainPath,
				EZeroEscapeTopologyRole::ShortLeaf,
				EZeroEscapeTopologyRole::ForwardRejoin };
			Profile.Flows.Add(CollectAll);

			FZeroEscapeFlowDefinition CollectKOfN = CollectAll;
			CollectKOfN.StableFlowId = TEXT("CollectKOfN");
			CollectKOfN.CompletionRule = EZeroEscapeCompletionRule::CollectKOfN;
			Profile.Flows.Add(CollectKOfN);
			Profile.SolverBudgets = FZeroEscapeSolverBudgets();
		}

		/** 构造含 Cap、WFC、三种目标角色、Start 和 Exit 的逻辑测试 Catalog。 */
		void BuildValidCatalog(UZeroEscapeModuleCatalog& Catalog)
		{
			const FBox ModuleBounds(
				FVector(-400.0, -400.0, 0.0),
				FVector(400.0, 400.0, 400.0));
			Catalog.CatalogVersion = 1;
			Catalog.CellSize = FVector(1000.0, 1000.0, 500.0);
			Catalog.Modules.Reset();

			FZeroEscapeModuleDefinition Cap;
			Cap.StableModuleId = 100;
			Cap.DisplayName = TEXT("Cap");
			Cap.LayoutPolicy = EZeroEscapeLayoutPolicy::Cap;
			Cap.LocalBounds = ModuleBounds;
			Cap.Portals.Add(MakePortal(0, EZeroEscapeCardinalDirection::North));
			Catalog.Modules.Add(Cap);

			FZeroEscapeModuleDefinition MainPath;
			MainPath.StableModuleId = 200;
			MainPath.DisplayName = TEXT("MainPath");
			MainPath.LayoutPolicy = EZeroEscapeLayoutPolicy::WfcSingleCell;
			MainPath.AllowedRoles = { EZeroEscapeTopologyRole::MainPath };
			MainPath.LocalBounds = ModuleBounds;
			MainPath.Portals.Add(MakePortal(0, EZeroEscapeCardinalDirection::North));
			MainPath.Portals.Add(MakePortal(
				1,
				EZeroEscapeCardinalDirection::South,
				EZeroEscapeSocketPolicy::Sealable,
				100));
			MainPath.GameplayAnchors.Add(MakeAnchor(0, EZeroEscapeGameplayAnchorType::Objective));
			Catalog.Modules.Add(MainPath);

			FZeroEscapeModuleDefinition ShortLeaf;
			ShortLeaf.StableModuleId = 201;
			ShortLeaf.DisplayName = TEXT("ShortLeaf");
			ShortLeaf.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			ShortLeaf.AllowedRoles = { EZeroEscapeTopologyRole::ShortLeaf };
			ShortLeaf.LocalBounds = ModuleBounds;
			ShortLeaf.Portals.Add(MakePortal(0, EZeroEscapeCardinalDirection::East));
			ShortLeaf.GameplayAnchors.Add(MakeAnchor(0, EZeroEscapeGameplayAnchorType::Objective));
			Catalog.Modules.Add(ShortLeaf);

			FZeroEscapeModuleDefinition ForwardRejoin;
			ForwardRejoin.StableModuleId = 202;
			ForwardRejoin.DisplayName = TEXT("ForwardRejoin");
			ForwardRejoin.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			ForwardRejoin.AllowedRoles = { EZeroEscapeTopologyRole::ForwardRejoin };
			ForwardRejoin.LocalBounds = ModuleBounds;
			ForwardRejoin.Portals.Add(MakePortal(0, EZeroEscapeCardinalDirection::East));
			ForwardRejoin.Portals.Add(MakePortal(1, EZeroEscapeCardinalDirection::West));
			ForwardRejoin.GameplayAnchors.Add(MakeAnchor(0, EZeroEscapeGameplayAnchorType::Objective));
			Catalog.Modules.Add(ForwardRejoin);

			FZeroEscapeModuleDefinition Start;
			Start.StableModuleId = 300;
			Start.DisplayName = TEXT("Start");
			Start.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			Start.AllowedRoles = { EZeroEscapeTopologyRole::Start };
			Start.LocalBounds = ModuleBounds;
			Start.Portals.Add(MakePortal(0, EZeroEscapeCardinalDirection::East));
			Start.GameplayAnchors.Add(MakeAnchor(0, EZeroEscapeGameplayAnchorType::PlayerSpawn));
			Catalog.Modules.Add(Start);

			FZeroEscapeModuleDefinition Exit;
			Exit.StableModuleId = 400;
			Exit.DisplayName = TEXT("Exit");
			Exit.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			Exit.AllowedRoles = { EZeroEscapeTopologyRole::Exit };
			Exit.LocalBounds = ModuleBounds;
			Exit.Portals.Add(MakePortal(0, EZeroEscapeCardinalDirection::West));
			Exit.GameplayAnchors.Add(MakeAnchor(0, EZeroEscapeGameplayAnchorType::Exit));
			Catalog.Modules.Add(Exit);
		}

		/** 为 Catalog 的每个结构模块构造不需 Spawn 即可校验的 Actor 表现绑定。 */
		void BuildValidPresentation(
			const UZeroEscapeModuleCatalog& Catalog,
			UZeroEscapePresentationProfile& Presentation)
		{
			Presentation.PresentationVersion = 1;
			Presentation.Bindings.Reset();
			for (const FZeroEscapeModuleDefinition& Module : Catalog.Modules)
			{
				if (Module.LayoutPolicy == EZeroEscapeLayoutPolicy::DecorationOnly)
				{
					continue;
				}

				FZeroEscapePresentationBinding Binding;
				Binding.StableModuleId = Module.StableModuleId;
				Binding.SpawnPolicy = EZeroEscapePresentationSpawnPolicy::Actor;
				Binding.ActorClass = AActor::StaticClass();
				Binding.ActorAssetLocalBounds = FBox(
					FVector(-300.0, -300.0, 0.0),
					FVector(300.0, 300.0, 300.0));
				Presentation.Bindings.Add(Binding);
			}
		}

		FSpatialNode MakeSpatialNode(
			const int32 StableNodeId,
			const EZeroEscapeTopologyRole Role,
			const int32 ProgressIndex)
		{
			FSpatialNode Node;
			Node.StableNodeId = StableNodeId;
			Node.Role = Role;
			Node.ProgressIndex = ProgressIndex;
			return Node;
		}

		FSpatialEdge MakeSpatialEdge(
			const int32 StableEdgeId,
			const int32 StableNodeA,
			const int32 StableNodeB)
		{
			FSpatialEdge Edge;
			Edge.StableEdgeId = StableEdgeId;
			Edge.StableNodeA = StableNodeA;
			Edge.StableNodeB = StableNodeB;
			return Edge;
		}

		/**
		 * 构造一张 Start/Exit 与三个候选目标都可达的无向抽象图。
		 * 失败用例会只移除目标支路或 Exit 边，避免把失败原因混入 K/N 配置错误。
		 */
		void BuildProgressionFixture(
			const EZeroEscapeCompletionRule CompletionRule,
			const int32 RequiredObjectiveCount,
			FAbstractLevelPlan& OutPlan)
		{
			OutPlan = {};
			OutPlan.CompletionRule = CompletionRule;
			OutPlan.RequiredObjectiveCount = RequiredObjectiveCount;
			OutPlan.StartStableNodeId = 0;
			OutPlan.ExitStableNodeId = 3;
			OutPlan.Nodes = {
				MakeSpatialNode(0, EZeroEscapeTopologyRole::Start, 0),
				MakeSpatialNode(1, EZeroEscapeTopologyRole::MainPath, 1),
				MakeSpatialNode(2, EZeroEscapeTopologyRole::MainPath, 2),
				MakeSpatialNode(3, EZeroEscapeTopologyRole::Exit, 3),
				MakeSpatialNode(4, EZeroEscapeTopologyRole::ShortLeaf, 1),
				MakeSpatialNode(5, EZeroEscapeTopologyRole::ShortLeaf, 2) };
			OutPlan.Edges = {
				MakeSpatialEdge(0, 0, 1),
				MakeSpatialEdge(1, 1, 2),
				MakeSpatialEdge(2, 2, 3),
				MakeSpatialEdge(3, 1, 4),
				MakeSpatialEdge(4, 2, 5) };

			if (CompletionRule != EZeroEscapeCompletionRule::EscapeOnly)
			{
				FObjectivePlacement ObjectiveA;
				ObjectiveA.StableObjectiveId = 10;
				ObjectiveA.StableNodeId = 1;
				FObjectivePlacement ObjectiveB;
				ObjectiveB.StableObjectiveId = 20;
				ObjectiveB.StableNodeId = 4;
				FObjectivePlacement ObjectiveC;
				ObjectiveC.StableObjectiveId = 30;
				ObjectiveC.StableNodeId = 5;
				OutPlan.Objectives = { ObjectiveA, ObjectiveB, ObjectiveC };
			}
		}

		/** 直线布局夹具使用格边中点 Portal，确保相邻 1x1 模块的逻辑 Frame 精确重合。 */
		FZeroEscapeModulePortal MakeGridPortal(
			const int32 StableSocketId,
			const EZeroEscapeCardinalDirection Direction,
			const FVector& CellSize)
		{
			FVector Translation(0.0, 0.0, CellSize.Z * 0.5);
			switch (Direction)
			{
			case EZeroEscapeCardinalDirection::North:
				Translation.Y = CellSize.Y * 0.5;
				break;
			case EZeroEscapeCardinalDirection::East:
				Translation.X = CellSize.X * 0.5;
				break;
			case EZeroEscapeCardinalDirection::South:
				Translation.Y = CellSize.Y * -0.5;
				break;
			case EZeroEscapeCardinalDirection::West:
				Translation.X = CellSize.X * -0.5;
				break;
			default:
				checkNoEntry();
				break;
			}

			FZeroEscapeModulePortal Portal;
			Portal.StableSocketId = StableSocketId;
			Portal.CellOffset = FIntVector::ZeroValue;
			Portal.LocalTransform = MakePortalTransform(Direction, Translation);
			Portal.Direction = Direction;
			Portal.ConnectorTypeId = 1;
			Portal.DisplayType = TEXT("Walk");
			Portal.WidthClass = 1;
			Portal.HeightLayer = 0;
			Portal.Policy = EZeroEscapeSocketPolicy::Required;
			return Portal;
		}

		/**
		 * 构造最小完整 Layout：Start -- 水平 WFC 直廊 -- Exit。
		 * 夹具只依赖公开纯值接口，因此既能验证整链确定性，也能验证重复调用的状态隔离。
		 */
		void BuildStraightLayoutFixture(
			FAbstractLevelPlan& OutAbstractPlan,
			FModuleCatalogSnapshot& OutCatalog,
			FLayoutRequest& OutRequest,
			FZeroEscapeSolverBudgets& OutBudgets)
		{
			OutAbstractPlan = {};
			OutAbstractPlan.CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
			OutAbstractPlan.StartStableNodeId = 0;
			OutAbstractPlan.ExitStableNodeId = 1;
			OutAbstractPlan.Nodes = {
				MakeSpatialNode(0, EZeroEscapeTopologyRole::Start, 0),
				MakeSpatialNode(1, EZeroEscapeTopologyRole::Exit, 1) };
			OutAbstractPlan.Edges = { MakeSpatialEdge(0, 0, 1) };

			const FVector CellSize(1000.0, 1000.0, 500.0);
			const FBox ModuleBounds(
				FVector(-450.0, -450.0, 0.0),
				FVector(450.0, 450.0, 450.0));
			OutCatalog = {};
			OutCatalog.CatalogVersion = 1;
			OutCatalog.CellSize = CellSize;

			FModuleSnapshot Start;
			Start.StableModuleId = 10;
			Start.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			Start.AllowedRoles = { EZeroEscapeTopologyRole::Start };
			Start.AllowedQuarterTurnsMask = 0x1;
			Start.LocalBounds = ModuleBounds;
			Start.Portals = { MakeGridPortal(0, EZeroEscapeCardinalDirection::East, CellSize) };
			Start.GameplayAnchors = { MakeAnchor(0, EZeroEscapeGameplayAnchorType::PlayerSpawn) };
			OutCatalog.Modules.Add(Start);

			FModuleSnapshot Exit;
			Exit.StableModuleId = 20;
			Exit.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			Exit.AllowedRoles = { EZeroEscapeTopologyRole::Exit };
			Exit.AllowedQuarterTurnsMask = 0x1;
			Exit.LocalBounds = ModuleBounds;
			Exit.Portals = { MakeGridPortal(0, EZeroEscapeCardinalDirection::West, CellSize) };
			Exit.GameplayAnchors = { MakeAnchor(0, EZeroEscapeGameplayAnchorType::Exit) };
			OutCatalog.Modules.Add(Exit);

			FModuleSnapshot Straight;
			Straight.StableModuleId = 30;
			Straight.LayoutPolicy = EZeroEscapeLayoutPolicy::WfcSingleCell;
			Straight.AllowedRoles = { EZeroEscapeTopologyRole::MainPath };
			Straight.AllowedQuarterTurnsMask = 0x1;
			Straight.LocalBounds = ModuleBounds;
			Straight.Portals = {
				MakeGridPortal(0, EZeroEscapeCardinalDirection::East, CellSize),
				MakeGridPortal(1, EZeroEscapeCardinalDirection::West, CellSize) };
			OutCatalog.Modules.Add(Straight);

			OutRequest = {};
			OutRequest.CellSize = CellSize;
			OutRequest.GridExtent = FIntVector(8, 5, 1);
			OutRequest.AStarStraightStepCost = 10;
			OutRequest.AStarTurnPenalty = 3;
			OutRequest.bRequireEffectiveWfcChoice = false;
			OutRequest.AbstractPlan = &OutAbstractPlan;
			OutRequest.CanonicalAbstractHash = ComputeCanonicalAbstractHash(OutAbstractPlan);
			OutRequest.Signature.Seed = 24680;
			OutRequest.Signature.Difficulty = EZeroEscapeDifficulty::Easy;
			OutRequest.Signature.FlowProfileId = TEXT("EscapeOnly");
			OutRequest.Signature.AlgorithmVersion = GAlgorithmVersion;
			OutRequest.Signature.GenerationProfileVersion = 1;
			OutRequest.Signature.FlowVersion = 1;
			OutRequest.Signature.CatalogVersion = OutCatalog.CatalogVersion;

			OutBudgets = FZeroEscapeSolverBudgets();
			OutBudgets.MaxLayoutAttempts = 1;
		}

		/**
		 * 构造多 Strong Socket 的 T 形布局：Start -> Junction -> Exit，Junction -> ShortLeaf。
		 * Junction 的三个 Required Portal 必须分别映射到三条抽象边；支路使用单格纵向 WFC。
		 */
		void BuildBranchedStrongLayoutFixture(
			FAbstractLevelPlan& OutAbstractPlan,
			FModuleCatalogSnapshot& OutCatalog,
			FLayoutRequest& OutRequest,
			FZeroEscapeSolverBudgets& OutBudgets)
		{
			OutAbstractPlan = {};
			OutAbstractPlan.CompletionRule = EZeroEscapeCompletionRule::EscapeOnly;
			OutAbstractPlan.StartStableNodeId = 0;
			OutAbstractPlan.ExitStableNodeId = 2;
			FSpatialNode Leaf = MakeSpatialNode(3, EZeroEscapeTopologyRole::ShortLeaf, 1);
			Leaf.AnchorProgressIndex = 1;
			OutAbstractPlan.Nodes = {
				MakeSpatialNode(0, EZeroEscapeTopologyRole::Start, 0),
				MakeSpatialNode(1, EZeroEscapeTopologyRole::MainPath, 1),
				MakeSpatialNode(2, EZeroEscapeTopologyRole::Exit, 2),
				Leaf };
			OutAbstractPlan.Edges = {
				MakeSpatialEdge(0, 0, 1),
				MakeSpatialEdge(1, 1, 2),
				MakeSpatialEdge(2, 1, 3) };

			const FVector CellSize(1000.0, 1000.0, 500.0);
			const FBox ModuleBounds(
				FVector(-450.0, -450.0, 0.0),
				FVector(450.0, 450.0, 450.0));
			OutCatalog = {};
			OutCatalog.CatalogVersion = 2;
			OutCatalog.CellSize = CellSize;

			FModuleSnapshot Start;
			Start.StableModuleId = 10;
			Start.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			Start.AllowedRoles = { EZeroEscapeTopologyRole::Start };
			Start.AllowedQuarterTurnsMask = 0x1;
			Start.LocalBounds = ModuleBounds;
			Start.Portals = { MakeGridPortal(0, EZeroEscapeCardinalDirection::East, CellSize) };
			Start.GameplayAnchors = { MakeAnchor(0, EZeroEscapeGameplayAnchorType::PlayerSpawn) };
			OutCatalog.Modules.Add(Start);

			FModuleSnapshot Junction;
			Junction.StableModuleId = 20;
			Junction.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			Junction.AllowedRoles = { EZeroEscapeTopologyRole::MainPath };
			Junction.AllowedQuarterTurnsMask = 0x1;
			Junction.LocalBounds = ModuleBounds;
			Junction.Portals = {
				MakeGridPortal(0, EZeroEscapeCardinalDirection::West, CellSize),
				MakeGridPortal(1, EZeroEscapeCardinalDirection::East, CellSize),
				MakeGridPortal(2, EZeroEscapeCardinalDirection::North, CellSize) };
			OutCatalog.Modules.Add(Junction);

			FModuleSnapshot Exit;
			Exit.StableModuleId = 30;
			Exit.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			Exit.AllowedRoles = { EZeroEscapeTopologyRole::Exit };
			Exit.AllowedQuarterTurnsMask = 0x1;
			Exit.LocalBounds = ModuleBounds;
			Exit.Portals = { MakeGridPortal(0, EZeroEscapeCardinalDirection::West, CellSize) };
			Exit.GameplayAnchors = { MakeAnchor(0, EZeroEscapeGameplayAnchorType::Exit) };
			OutCatalog.Modules.Add(Exit);

			FModuleSnapshot ShortLeaf;
			ShortLeaf.StableModuleId = 40;
			ShortLeaf.LayoutPolicy = EZeroEscapeLayoutPolicy::SocketModule;
			ShortLeaf.AllowedRoles = { EZeroEscapeTopologyRole::ShortLeaf };
			ShortLeaf.AllowedQuarterTurnsMask = 0x1;
			ShortLeaf.LocalBounds = ModuleBounds;
			ShortLeaf.Portals = {
				MakeGridPortal(0, EZeroEscapeCardinalDirection::South, CellSize) };
			OutCatalog.Modules.Add(ShortLeaf);

			FModuleSnapshot Straight;
			Straight.StableModuleId = 50;
			Straight.LayoutPolicy = EZeroEscapeLayoutPolicy::WfcSingleCell;
			Straight.AllowedRoles = { EZeroEscapeTopologyRole::MainPath };
			Straight.AllowedQuarterTurnsMask = 0x3;
			Straight.LocalBounds = ModuleBounds;
			Straight.Portals = {
				MakeGridPortal(0, EZeroEscapeCardinalDirection::East, CellSize),
				MakeGridPortal(1, EZeroEscapeCardinalDirection::West, CellSize) };
			OutCatalog.Modules.Add(Straight);

			OutRequest = {};
			OutRequest.CellSize = CellSize;
			OutRequest.GridExtent = FIntVector(12, 9, 1);
			OutRequest.AStarStraightStepCost = 10;
			OutRequest.AStarTurnPenalty = 3;
			OutRequest.bRequireEffectiveWfcChoice = false;
			OutRequest.AbstractPlan = &OutAbstractPlan;
			OutRequest.CanonicalAbstractHash = ComputeCanonicalAbstractHash(OutAbstractPlan);
			OutRequest.Signature.Seed = 314159;
			OutRequest.Signature.Difficulty = EZeroEscapeDifficulty::Normal;
			OutRequest.Signature.FlowProfileId = TEXT("EscapeOnly");
			OutRequest.Signature.AlgorithmVersion = GAlgorithmVersion;
			OutRequest.Signature.GenerationProfileVersion = 1;
			OutRequest.Signature.FlowVersion = 1;
			OutRequest.Signature.CatalogVersion = OutCatalog.CatalogVersion;

			OutBudgets = FZeroEscapeSolverBudgets();
			OutBudgets.MaxLayoutAttempts = 1;
		}
	}

	/**
	 * 冻结三段 Transform 的乘法约定：Asset Local 经 PivotCorrection 进入 Logical Module，
	 * 再进入 Generator Root。测试同时比较 HISM 局部路径与 Actor 世界路径，并用故意错误的
	 * 乘法顺序证明夹具确实能抓到回归，而不是只验证 Unit Scale。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeTransformCompositionTest,
		"Demo.PCG.Unit.TransformComposition",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeTransformCompositionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		const FTransform TargetPortalInGenerator(
			FRotator(17.0, 61.0, -11.0),
			FVector(730.0, -245.0, 160.0),
			FVector::OneVector);
		const FTransform SourcePortalInModule(
			FRotator(-7.0, 28.0, 19.0),
			FVector(125.0, -40.0, 35.0),
			FVector::OneVector);

		const FTransform ModuleLocalTransform = SolveModuleLocalTransform(
			TargetPortalInGenerator,
			SourcePortalInModule);
		const FTransform AlignedSourcePortal =
			SourcePortalInModule * ModuleLocalTransform;

		TestTrue(
			TEXT("对齐后的 Portal 位置必须一致"),
			AlignedSourcePortal.GetTranslation().Equals(
				TargetPortalInGenerator.GetTranslation(),
				PositionTolerance));
		TestTrue(
			TEXT("对齐后的 Portal Forward 必须相反"),
			FMath::IsNearlyEqual(
				AlignedSourcePortal.GetRotation().GetAxisX()
					.Dot(TargetPortalInGenerator.GetRotation().GetAxisX()),
				-1.0f,
				DirectionTolerance));
		TestTrue(
			TEXT("对齐后的 Portal Up 必须相同"),
			FMath::IsNearlyEqual(
				AlignedSourcePortal.GetRotation().GetAxisZ()
					.Dot(TargetPortalInGenerator.GetRotation().GetAxisZ()),
				1.0f,
				DirectionTolerance));
		TestTrue(TEXT("模块布局 Transform 必须保持 Unit Scale"), IsUnitScale(ModuleLocalTransform));
		TestTrue(TEXT("对齐后的 Portal Transform 必须保持 Unit Scale"), IsUnitScale(AlignedSourcePortal));

		// 第二部分覆盖换素材最容易出错的 Pivot 适配边界：Pivot 只修正表现，
		// 不能改变已经由 Portal 对齐求出的逻辑模块 Transform。
		const FTransform PivotCorrection(
			FRotator(9.0, -23.0, 14.0),
			FVector(31.0, -17.0, 8.0),
			FVector::OneVector);
		const FTransform GeneratedRootWorldTransform(
			FRotator(-13.0, 37.0, 6.0),
			FVector(-420.0, 900.0, 75.0),
			FVector::OneVector);
		const TStaticArray<FVector, 5> TestPoints = {
			FVector::ZeroVector,
			FVector::XAxisVector * 100.0,
			FVector::YAxisVector * 100.0,
			FVector::ZAxisVector * 100.0,
			FVector(11.0, -7.0, 3.0) };

		const FTransform PresentationLocalTransform = MakePresentationLocalTransform(
			PivotCorrection,
			ModuleLocalTransform);
		const FTransform PresentationWorldTransform = MakePresentationWorldTransform(
			PivotCorrection,
			ModuleLocalTransform,
			GeneratedRootWorldTransform);

		for (const FVector& TestPoint : TestPoints)
		{
			const FVector ExpectedWorldPoint = GeneratedRootWorldTransform.TransformPosition(
				ModuleLocalTransform.TransformPosition(
					PivotCorrection.TransformPosition(TestPoint)));
			const FVector HismWorldPoint = GeneratedRootWorldTransform.TransformPosition(
				PresentationLocalTransform.TransformPosition(TestPoint));
			const FVector ActorWorldPoint = PresentationWorldTransform.TransformPosition(TestPoint);

			TestTrue(
				TEXT("HISM 局部 Transform 必须遵循 Pivot -> Module -> Root 顺序"),
				HismWorldPoint.Equals(ExpectedWorldPoint, PositionTolerance));
			TestTrue(
				TEXT("Actor 世界 Transform 必须与 HISM 对同一点得到一致世界坐标"),
				ActorWorldPoint.Equals(HismWorldPoint, PositionTolerance));
		}
		TestTrue(TEXT("表现层局部 Transform 必须保持 Unit Scale"), IsUnitScale(PresentationLocalTransform));
		TestTrue(TEXT("表现层世界 Transform 必须保持 Unit Scale"), IsUnitScale(PresentationWorldTransform));

		const FVector TestPoint = TestPoints[TestPoints.Num() - 1];
		const FVector ExpectedWorldPoint = PresentationWorldTransform.TransformPosition(TestPoint);
		const FVector WrongPivotOrderPoint = GeneratedRootWorldTransform.TransformPosition(
			PivotCorrection.TransformPosition(
				ModuleLocalTransform.TransformPosition(TestPoint)));
		TestFalse(
			TEXT("测试数据必须能识别 Pivot 与 Module 乘法顺序被交换"),
			WrongPivotOrderPoint.Equals(ExpectedWorldPoint, PositionTolerance));
		const FVector WrongRootOrderPoint = ModuleLocalTransform.TransformPosition(
			GeneratedRootWorldTransform.TransformPosition(
				PivotCorrection.TransformPosition(TestPoint)));
		TestFalse(
			TEXT("测试数据必须能识别 Module 与 Root 乘法顺序被交换"),
			WrongRootOrderPoint.Equals(ExpectedWorldPoint, PositionTolerance));

		return true;
	}

	/**
	 * 冻结离散网格的 QuarterTurn 方向、Footprint 和 CellOffset 约定。
	 * 非正方形 3x2 Footprint 能暴露“只旋转方向却忘记重映射占格”的错误；四次旋转回原位
	 * 则验证映射是闭合且不会产生负坐标漂移。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeGridRotationContractTest,
		"Demo.PCG.Unit.GridRotationContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeGridRotationContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		const TStaticArray<EZeroEscapeCardinalDirection, 4> RotatedEast = {
			EZeroEscapeCardinalDirection::East,
			EZeroEscapeCardinalDirection::North,
			EZeroEscapeCardinalDirection::West,
			EZeroEscapeCardinalDirection::South };
		const FIntVector Footprint(3, 2, 1);
		const FIntVector CellOffset(2, 1, 0);
		const TStaticArray<FIntVector, 4> RotatedFootprints = {
			FIntVector(3, 2, 1),
			FIntVector(2, 3, 1),
			FIntVector(3, 2, 1),
			FIntVector(2, 3, 1) };
		const TStaticArray<FIntVector, 4> RotatedOffsets = {
			FIntVector(2, 1, 0),
			FIntVector(0, 2, 0),
			FIntVector(0, 0, 0),
			FIntVector(1, 0, 0) };

		for (uint8 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
		{
			TestTrue(
				TEXT("QuarterTurns 必须按局部 +Z 的 +Yaw 旋转 Direction"),
				RotateDirection(EZeroEscapeCardinalDirection::East, QuarterTurns) == RotatedEast[QuarterTurns]);
			TestTrue(
				TEXT("QuarterTurns 必须确定性旋转 Footprint"),
				RotateFootprint(Footprint, QuarterTurns) == RotatedFootprints[QuarterTurns]);
			TestTrue(
				TEXT("QuarterTurns 必须把 CellOffset 重新映射到非负旋转 Footprint"),
				RotateCellOffset(CellOffset, Footprint, QuarterTurns) == RotatedOffsets[QuarterTurns]);
		}

		for (int32 X = 0; X < Footprint.X; ++X)
		{
			for (int32 Y = 0; Y < Footprint.Y; ++Y)
			{
				const FIntVector Original(X, Y, 0);
				FIntVector Rotated = Original;
				FIntVector CurrentFootprint = Footprint;
				for (int32 RotationIndex = 0; RotationIndex < 4; ++RotationIndex)
				{
					Rotated = RotateCellOffset(Rotated, CurrentFootprint, 1);
					CurrentFootprint = RotateFootprint(CurrentFootprint, 1);
					TestTrue(
						TEXT("旋转后的每个 Cell 必须仍位于旋转后 Footprint 内"),
						Rotated.X >= 0 && Rotated.X < CurrentFootprint.X
							&& Rotated.Y >= 0 && Rotated.Y < CurrentFootprint.Y);
				}
				TestTrue(TEXT("任意 Cell 连续旋转四次必须回到原坐标"), Rotated == Original);
			}
		}

		TestTrue(
			TEXT("Up 绕 Z 轴旋转后必须保持 Up"),
			RotateDirection(EZeroEscapeCardinalDirection::Up, 3) == EZeroEscapeCardinalDirection::Up);
		TestTrue(
			TEXT("Down 绕 Z 轴旋转后必须保持 Down"),
			RotateDirection(EZeroEscapeCardinalDirection::Down, 2) == EZeroEscapeCardinalDirection::Down);

		const TStaticArray<EZeroEscapeCardinalDirection, 6> AllDirections = {
			EZeroEscapeCardinalDirection::North,
			EZeroEscapeCardinalDirection::East,
			EZeroEscapeCardinalDirection::South,
			EZeroEscapeCardinalDirection::West,
			EZeroEscapeCardinalDirection::Up,
			EZeroEscapeCardinalDirection::Down };
		for (const EZeroEscapeCardinalDirection Direction : AllDirections)
		{
			TestTrue(
				TEXT("任意受支持方向取两次 Opposite 必须回到自身"),
				OppositeDirection(OppositeDirection(Direction)) == Direction);
		}

		TestTrue(
			TEXT("North 的反向必须为 South"),
			OppositeDirection(EZeroEscapeCardinalDirection::North) == EZeroEscapeCardinalDirection::South);
		TestTrue(
			TEXT("Up 的反向必须为 Down"),
			OppositeDirection(EZeroEscapeCardinalDirection::Up) == EZeroEscapeCardinalDirection::Down);

		return true;
	}

	/**
	 * 验证 Generation Profile 会在运行前拒绝不可实现的 K/N、目标角色容量和越界预算。
	 * 重点不是测试每个 setter，而是保证 DataAsset 无法绕过实时生成的代码级安全护栏。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeGenerationProfileContractsTest,
		"Demo.PCG.Unit.Assets.ProfileContracts",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeGenerationProfileContractsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		FString Error;
		BuildValidProfile(*Profile);
		TestTrue(TEXT("合法 Profile 必须通过配置校验"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Difficulties[0].RequiredObjectiveCount =
			Profile->Difficulties[0].ObjectiveCandidateCount + 1;
		TestFalse(TEXT("CollectKOfN 的 K>N 必须被拒绝"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->Flows[1].AllowedObjectiveRoles = { EZeroEscapeTopologyRole::ShortLeaf };
		TestFalse(
			TEXT("Flow 只允许容量不足的目标角色时必须被拒绝"),
			Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SolverBudgets.MaxWfcActiveCells =
			ZeroEscape::GenerationLimits::FirstPassMaxWfcActiveCells + 1;
		TestFalse(TEXT("资产不能绕过首版 WFC Active Cell 硬上限"), Profile->IsConfigured(Error));

		BuildValidProfile(*Profile);
		Profile->SolverBudgets.MaxTotalWorkUnits =
			ZeroEscape::GenerationLimits::FirstPassMaxTotalWorkUnits + 1;
		TestFalse(TEXT("总工作预算也必须受具名硬上限约束"), Profile->IsConfigured(Error));

		return true;
	}

	/**
	 * 验证逻辑 Catalog 自己足以描述连接：Portal Frame 必须匹配方向、CellOffset 必须在占格内、
	 * Sealable Portal 必须存在可旋转对齐的 Cap，Start Anchor 必须唯一。测试不依赖 Mesh Socket。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeModuleCatalogContractsTest,
		"Demo.PCG.Unit.Assets.CatalogContracts",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeModuleCatalogContractsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		UZeroEscapeModuleCatalog* Catalog = NewObject<UZeroEscapeModuleCatalog>();
		FString Error;
		BuildValidCatalog(*Catalog);
		TestTrue(TEXT("合法 Catalog 必须通过配置校验"), Catalog->IsConfigured(Error));

		BuildValidCatalog(*Catalog);
		Catalog->Modules[1].Portals[0].Direction = EZeroEscapeCardinalDirection::East;
		TestFalse(TEXT("Portal Direction 与局部 Frame 不一致时必须被拒绝"), Catalog->IsConfigured(Error));

		BuildValidCatalog(*Catalog);
		Catalog->Modules[1].Portals[0].CellOffset = FIntVector(1, 0, 0);
		TestFalse(TEXT("Portal CellOffset 越过 Footprint 时必须被拒绝"), Catalog->IsConfigured(Error));

		BuildValidCatalog(*Catalog);
		Catalog->Modules[0].AllowedQuarterTurnsMask = 0x1;
		Catalog->Modules[0].Portals[0] = MakePortal(0, EZeroEscapeCardinalDirection::East);
		TestFalse(TEXT("Closure Cap 没有可对齐旋转时必须被拒绝"), Catalog->IsConfigured(Error));

		BuildValidCatalog(*Catalog);
		Catalog->Modules[4].GameplayAnchors.Add(
			MakeAnchor(1, EZeroEscapeGameplayAnchorType::PlayerSpawn));
		TestFalse(TEXT("Start 模块存在多个 PlayerSpawn Anchor 时必须被拒绝"), Catalog->IsConfigured(Error));

		return true;
	}

	/**
	 * 验证可替换表现层既完整覆盖结构模块，又不能越过 Catalog 的逻辑 envelope；同时覆盖
	 * Profile/Catalog/Presentation 组合后才知道的 WFC Variant 容量与 Objective Anchor 能力。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapePresentationAndAssetSetContractsTest,
		"Demo.PCG.Unit.Assets.PresentationAndAssetSet",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapePresentationAndAssetSetContractsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		UZeroEscapeModuleCatalog* Catalog = NewObject<UZeroEscapeModuleCatalog>();
		UZeroEscapePresentationProfile* Presentation = NewObject<UZeroEscapePresentationProfile>();
		FString Error;
		BuildValidProfile(*Profile);
		BuildValidCatalog(*Catalog);
		BuildValidPresentation(*Catalog, *Presentation);
		TestTrue(
			TEXT("合法 Profile/Catalog/Presentation 集合必须通过联合校验"),
			ValidateZeroEscapeGenerationAssetSet(*Profile, *Catalog, *Presentation, Error));

		BuildValidPresentation(*Catalog, *Presentation);
		Presentation->Bindings.Pop(EAllowShrinking::No);
		TestFalse(
			TEXT("缺失任意结构模块 Binding 时必须被拒绝"),
			Presentation->IsConfigured(*Catalog, Error));

		BuildValidPresentation(*Catalog, *Presentation);
		Presentation->Bindings[0].ActorAssetLocalBounds = FBox(
			FVector(-600.0, -600.0, 0.0),
			FVector(600.0, 600.0, 300.0));
		TestFalse(
			TEXT("Actor 作者声明 Bounds 越过逻辑模块时必须被拒绝"),
			Presentation->IsConfigured(*Catalog, Error));

		BuildValidPresentation(*Catalog, *Presentation);
		Presentation->Bindings[0].ActorAssetLocalBounds = FBox(
			FVector(-450.0, -450.0, 0.0),
			FVector(450.0, 450.0, 300.0));
		Presentation->Bindings[0].BoundsOverhangAllowanceCm = 50.0;
		TestTrue(
			TEXT("实测且受硬上限保护的表现外壳 Overhang 应通过契约校验"),
			Presentation->IsConfigured(*Catalog, Error));

		Presentation->Bindings[0].BoundsOverhangAllowanceCm =
			ZeroEscape::GenerationLimits::FirstPassMaxPresentationBoundsOverhangCm + 1.0;
		TestFalse(
			TEXT("表现 Overhang 作者值越过首版代码硬上限时必须被拒绝"),
			Presentation->IsConfigured(*Catalog, Error));

		BuildValidPresentation(*Catalog, *Presentation);
		Profile->SolverBudgets.MaxWfcVariants = 3;
		TestFalse(
			TEXT("Catalog Variant 数超过当前 Profile 上限时联合校验必须失败"),
			ValidateZeroEscapeGenerationAssetSet(*Profile, *Catalog, *Presentation, Error));

		BuildValidProfile(*Profile);
		BuildValidCatalog(*Catalog);
		BuildValidPresentation(*Catalog, *Presentation);
		Catalog->Modules[2].GameplayAnchors.Reset();
		TestFalse(
			TEXT("Flow 允许的角色没有 Objective Anchor 模块时联合校验必须失败"),
			ValidateZeroEscapeGenerationAssetSet(*Profile, *Catalog, *Presentation, Error));

		return true;
	}

	/**
	 * 读取首套项目自有运行时资产，并沿 Generator 的同一条纯数据链路完成一次真实求解。
	 *
	 * 这个测试不是把第三方 SFCorridors 写死进算法：逻辑层只加载项目的 Profile/Catalog，
	 * SFC Mesh 只存在于可替换的 Presentation Profile 中。这里刻意固定项目资产路径和首版
	 * 数量，是为了在重命名、漏保存、错误重配 Portal/Bounds 或更换表现层时尽早暴露垂直
	 * 切片已断开；未来正式迁移资产时，应成套更新这些项目路径和相应的版本化期望。
	 *
	 * NullRHI 下不创建关卡 Actor，避免把编辑器 Viewport 能力误当成算法依赖。通过只表示
	 * “序列化资产 -> 快照 -> Signature -> 抽象图 -> Socket、A-star、WFC -> 最终 Plan”可运行且
	 * 可复现；地图 Actor 装配、HISM 实例化和 PIE 仍是独立门禁。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeProjectAssetPipelineSmokeTest,
		"Demo.PCG.Integration.Assets.SFCorridorsPipelineSmoke",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeProjectAssetPipelineSmokeTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		// 使用完整对象路径而不是短名，避免 Asset Registry 搜索顺序或同名资产影响测试。
		// 这三个包都属于 /Game/ZeroEscape，第三方 /Game/Assets/SFCorridors 仍保持只读。
		const UZeroEscapeLevelGenerationProfile* Profile =
			LoadObject<UZeroEscapeLevelGenerationProfile>(
				nullptr,
				TEXT("/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile."
					"DA_LevelGenerationProfile"));
		const UZeroEscapeModuleCatalog* Catalog =
			LoadObject<UZeroEscapeModuleCatalog>(
				nullptr,
				TEXT("/Game/ZeroEscape/Generation/Data/DA_LevelModuleCatalog."
					"DA_LevelModuleCatalog"));
		const UZeroEscapePresentationProfile* Presentation =
			LoadObject<UZeroEscapePresentationProfile>(
				nullptr,
				TEXT("/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SFCorridors."
					"DA_Presentation_SFCorridors"));
		UClass* GeneratorClass = LoadObject<UClass>(
			nullptr,
			TEXT("/Game/ZeroEscape/Generation/BP_ZeroEscapeRuntimeLevelGenerator."
				"BP_ZeroEscapeRuntimeLevelGenerator_C"));

		TestNotNull(TEXT("首版运行时 Generation Profile 必须可从固定项目路径加载"), Profile);
		TestNotNull(TEXT("首版运行时 Module Catalog 必须可从固定项目路径加载"), Catalog);
		TestNotNull(TEXT("首版 SFC Presentation Profile 必须可从固定项目路径加载"), Presentation);
		TestNotNull(TEXT("首版 Runtime Generator Blueprint GeneratedClass 必须可加载"), GeneratorClass);
		if (Profile == nullptr
			|| Catalog == nullptr
			|| Presentation == nullptr
			|| GeneratorClass == nullptr)
		{
			// 资产缺失时立即返回，避免后续解引用产生二次崩溃掩盖真正原因。
			return true;
		}

		TestTrue(
			TEXT("Generator Blueprint 必须继承项目原生 Runtime Generator"),
			GeneratorClass->IsChildOf(AZeroEscapeRuntimeLevelGenerator::StaticClass()));
		const AZeroEscapeRuntimeLevelGenerator* GeneratorCdo =
			Cast<AZeroEscapeRuntimeLevelGenerator>(GeneratorClass->GetDefaultObject());
		TestNotNull(TEXT("Generator Blueprint 必须具有可读取的原生类型 CDO"), GeneratorCdo);
		if (GeneratorCdo == nullptr)
		{
			return true;
		}

		// Generator 的装配属性故意保持 private，普通业务代码不能旁路 Generate 事务修改它们。
		// 集成测试通过 UE 反射只读 CDO，验证一次性作者化脚本确实把三份项目资产和默认请求
		// 保存进 Blueprint；这不会要求 UnrealEd，也不会把属性名当作运行时玩法逻辑依赖。
		auto ReadCdoObjectProperty = [this, GeneratorClass, GeneratorCdo](
			const FName PropertyName,
			const TCHAR* MissingPropertyMessage) -> const UObject*
		{
			const FObjectPropertyBase* Property =
				FindFProperty<FObjectPropertyBase>(GeneratorClass, PropertyName);
			TestNotNull(MissingPropertyMessage, Property);
			return Property != nullptr
				? Property->GetObjectPropertyValue_InContainer(GeneratorCdo)
				: nullptr;
		};

		TestTrue(
			TEXT("Generator CDO 必须引用首版 Generation Profile"),
			ReadCdoObjectProperty(TEXT("GenerationProfile"), TEXT("必须存在 GenerationProfile 反射属性"))
				== Profile);
		TestTrue(
			TEXT("Generator CDO 必须引用首版 Module Catalog"),
			ReadCdoObjectProperty(TEXT("ModuleCatalog"), TEXT("必须存在 ModuleCatalog 反射属性"))
				== Catalog);
		TestTrue(
			TEXT("Generator CDO 必须引用首版 SFC Presentation Profile"),
			ReadCdoObjectProperty(TEXT("PresentationProfile"), TEXT("必须存在 PresentationProfile 反射属性"))
				== Presentation);

		const FEnumProperty* TriggerModeProperty =
			FindFProperty<FEnumProperty>(GeneratorClass, TEXT("TriggerMode"));
		TestNotNull(TEXT("Generator CDO 必须存在 TriggerMode 反射属性"), TriggerModeProperty);
		if (TriggerModeProperty != nullptr)
		{
			const void* TriggerModeValue =
				TriggerModeProperty->ContainerPtrToValuePtr<void>(GeneratorCdo);
			const int64 TriggerMode = TriggerModeProperty->GetUnderlyingProperty()
				->GetSignedIntPropertyValue(TriggerModeValue);
			TestEqual(
				TEXT("烟测 Generator 必须在 BeginPlay 自动生成"),
				TriggerMode,
				static_cast<int64>(EZeroEscapeGenerationTrigger::BeginPlay));
		}

		const FStructProperty* DefaultRequestProperty =
			FindFProperty<FStructProperty>(GeneratorClass, TEXT("DefaultRequest"));
		TestNotNull(TEXT("Generator CDO 必须存在 DefaultRequest 反射属性"), DefaultRequestProperty);
		if (DefaultRequestProperty != nullptr)
		{
			TestTrue(
				TEXT("DefaultRequest 反射类型必须保持 FZeroEscapeGenerationRequest"),
				DefaultRequestProperty->Struct == FZeroEscapeGenerationRequest::StaticStruct());
			const FZeroEscapeGenerationRequest* CdoRequest =
				DefaultRequestProperty->ContainerPtrToValuePtr<FZeroEscapeGenerationRequest>(GeneratorCdo);
			TestTrue(
				TEXT("Generator CDO 必须保存 Seed 12345 / Normal / EscapeOnly"),
				CdoRequest != nullptr
					&& CdoRequest->Seed == 12345
					&& CdoRequest->Difficulty == EZeroEscapeDifficulty::Normal
					&& CdoRequest->FlowProfileId == TEXT("EscapeOnly"));
		}

		// 这些断言冻结“第一套最小可运行内容”，而不是冻结最终玩法规模。首版只有
		// EscapeOnly，三档难度共用短关键路线；复杂分支和 K-of-N 由后续 Profile 扩展。
		TestEqual(TEXT("烟测 ProfileVersion 必须为 1"), Profile->ProfileVersion, 1);
		TestEqual(TEXT("烟测 Profile 必须显式保存三档难度"), Profile->Difficulties.Num(), 3);
		bool bHasEasy = false;
		bool bHasNormal = false;
		bool bHasHard = false;
		bool bAllDifficultyExtensionsDisabled = true;
		for (const FZeroEscapeDifficultyDefinition& Difficulty : Profile->Difficulties)
		{
			bHasEasy |= Difficulty.Difficulty == EZeroEscapeDifficulty::Easy;
			bHasNormal |= Difficulty.Difficulty == EZeroEscapeDifficulty::Normal;
			bHasHard |= Difficulty.Difficulty == EZeroEscapeDifficulty::Hard;
			bAllDifficultyExtensionsDisabled &= Difficulty.ShortLeafBranchCount == 0
				&& Difficulty.ForwardRejoinBranchCount == 0
				&& Difficulty.ObjectiveCandidateCount == 0
				&& Difficulty.RequiredObjectiveCount == 0;
		}
		TestTrue(TEXT("烟测 Profile 必须恰好覆盖 Easy、Normal、Hard"),
			bHasEasy && bHasNormal && bHasHard);
		TestTrue(TEXT("首个 EscapeOnly 烟测暂时不生成分支或目标"),
			bAllDifficultyExtensionsDisabled);
		TestEqual(TEXT("烟测 Profile 当前只启用一个 EscapeOnly Flow"), Profile->Flows.Num(), 1);
		if (Profile->Flows.Num() == 1)
		{
			TestTrue(
				TEXT("烟测 Flow Stable Id 必须为 EscapeOnly"),
				Profile->Flows[0].StableFlowId == TEXT("EscapeOnly"));
			TestTrue(
				TEXT("烟测 Flow CompletionRule 必须为 EscapeOnly"),
				Profile->Flows[0].CompletionRule == EZeroEscapeCompletionRule::EscapeOnly);
			TestEqual(TEXT("烟测 EscapeOnly FlowVersion 必须为 1"),
				Profile->Flows[0].FlowVersion, 1);
		}
		TestTrue(
			TEXT("烟测 Profile 必须保存 8x8 网格、4 节点关键路线与整数 A-star 成本 10/3"),
			Profile->SharedRouteConstraints.GridExtentCells == FIntPoint(8, 8)
				&& Profile->SharedRouteConstraints.CriticalPathNodeCount == 4
				&& Profile->SharedRouteConstraints.AStarStraightStepCost == 10
				&& Profile->SharedRouteConstraints.AStarTurnPenalty == 3);
		TestTrue(TEXT("首个素材烟测允许合法退化为唯一 WFC 结果"),
			!Profile->bRequireEffectiveWfcChoice);
		TestTrue(
			TEXT("烟测 Profile 必须显式保存首版关键实时预算"),
			Profile->SolverBudgets.MaxLayoutAttempts == 3
				&& Profile->SolverBudgets.MaxWfcActiveCells == 256
				&& Profile->SolverBudgets.MaxWfcVariants == 64
				&& Profile->SolverBudgets.MaxTotalWorkUnits == 750000);

		TestEqual(TEXT("烟测 CatalogVersion 必须为 1"), Catalog->CatalogVersion, 1);
		TestEqual(TEXT("烟测 Catalog 必须包含 Cap、Start、Exit 与三个 WFC 模块"),
			Catalog->Modules.Num(), 6);
		TestTrue(
			TEXT("SFC 示例实测后的逻辑 Cell 必须保持 660x660x500 cm"),
			Catalog->CellSize.Equals(FVector(660.0, 660.0, 500.0), 0.01));

		auto FindModuleById = [Catalog](const int32 StableModuleId)
		{
			return Catalog->Modules.FindByPredicate(
				[StableModuleId](const FZeroEscapeModuleDefinition& Module)
				{
					return Module.StableModuleId == StableModuleId;
				});
		};
		const FZeroEscapeModuleDefinition* CapModule = FindModuleById(100);
		const FZeroEscapeModuleDefinition* StartModule = FindModuleById(200);
		const FZeroEscapeModuleDefinition* ExitModule = FindModuleById(201);
		const FZeroEscapeModuleDefinition* PassModule = FindModuleById(300);
		const FZeroEscapeModuleDefinition* LeftTurnModule = FindModuleById(301);
		const FZeroEscapeModuleDefinition* RightTurnModule = FindModuleById(302);
		TestNotNull(TEXT("Catalog 必须存在 StableModuleId=100 的 Cap"), CapModule);
		TestNotNull(TEXT("Catalog 必须存在 StableModuleId=200 的 Start"), StartModule);
		TestNotNull(TEXT("Catalog 必须存在 StableModuleId=201 的 Exit"), ExitModule);
		TestNotNull(TEXT("Catalog 必须存在 StableModuleId=300 的 Pass WFC"), PassModule);
		TestNotNull(TEXT("Catalog 必须存在 StableModuleId=301 的 LeftTurn WFC"), LeftTurnModule);
		TestNotNull(TEXT("Catalog 必须存在 StableModuleId=302 的 RightTurn WFC"), RightTurnModule);
		if (CapModule == nullptr
			|| StartModule == nullptr
			|| ExitModule == nullptr
			|| PassModule == nullptr
			|| LeftTurnModule == nullptr
			|| RightTurnModule == nullptr)
		{
			return true;
		}
		TestTrue(TEXT("模块 100 必须保持 Cap 结构职责"),
			CapModule->LayoutPolicy == EZeroEscapeLayoutPolicy::Cap);
		TestTrue(TEXT("模块 200 必须保持 Start Socket 与唯一 PlayerSpawn Anchor"),
			StartModule->LayoutPolicy == EZeroEscapeLayoutPolicy::SocketModule
				&& StartModule->AllowedRoles.Contains(EZeroEscapeTopologyRole::Start)
				&& StartModule->GameplayAnchors.ContainsByPredicate(
					[](const FZeroEscapeModuleAnchor& Anchor)
					{
						return Anchor.Type == EZeroEscapeGameplayAnchorType::PlayerSpawn;
					}));
		TestTrue(TEXT("模块 201 必须保持 Exit Socket 与唯一 Exit Anchor"),
			ExitModule->LayoutPolicy == EZeroEscapeLayoutPolicy::SocketModule
				&& ExitModule->AllowedRoles.Contains(EZeroEscapeTopologyRole::Exit)
				&& ExitModule->GameplayAnchors.ContainsByPredicate(
					[](const FZeroEscapeModuleAnchor& Anchor)
					{
						return Anchor.Type == EZeroEscapeGameplayAnchorType::Exit;
					}));
		TestTrue(TEXT("模块 300/301/302 必须都是 MainPath 单格 WFC"),
			PassModule->LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell
				&& LeftTurnModule->LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell
				&& RightTurnModule->LayoutPolicy == EZeroEscapeLayoutPolicy::WfcSingleCell
				&& PassModule->AllowedRoles.Contains(EZeroEscapeTopologyRole::MainPath)
				&& LeftTurnModule->AllowedRoles.Contains(EZeroEscapeTopologyRole::MainPath)
				&& RightTurnModule->AllowedRoles.Contains(EZeroEscapeTopologyRole::MainPath));

		TestEqual(TEXT("PresentationVersion 必须为 1"), Presentation->PresentationVersion, 1);
		TestEqual(TEXT("Presentation 必须逐一覆盖六个结构模块"),
			Presentation->Bindings.Num(), 6);
		struct FExpectedPresentationBinding
		{
			int32 StableModuleId;
			const TCHAR* MeshPath;
			double OverhangCm;
			FTransform PivotCorrection;
		};
		const TStaticArray<FExpectedPresentationBinding, 6> ExpectedBindings = {
			FExpectedPresentationBinding{
				100,
				TEXT("/Game/Assets/SFCorridors/Meshes/SM_WallGlass.SM_WallGlass"),
				24.0,
				FTransform(FQuat::Identity, FVector(0.0, -330.0, 0.0), FVector::OneVector) },
			FExpectedPresentationBinding{
				200,
				TEXT("/Game/Assets/SFCorridors/Meshes/SM_Room_Pass.SM_Room_Pass"),
				66.0,
				FTransform::Identity },
			FExpectedPresentationBinding{
				201,
				TEXT("/Game/Assets/SFCorridors/Meshes/SM_Room_Pass.SM_Room_Pass"),
				66.0,
				FTransform::Identity },
			FExpectedPresentationBinding{
				300,
				TEXT("/Game/Assets/SFCorridors/Meshes/SM_Room_Pass.SM_Room_Pass"),
				66.0,
				FTransform::Identity },
			FExpectedPresentationBinding{
				301,
				TEXT("/Game/Assets/SFCorridors/Meshes/SM_Room_LeftTurn.SM_Room_LeftTurn"),
				55.0,
				FTransform::Identity },
			FExpectedPresentationBinding{
				302,
				TEXT("/Game/Assets/SFCorridors/Meshes/SM_Room_RightTurn.SM_Room_RightTurn"),
				46.0,
				FTransform::Identity }
		};
		for (const FExpectedPresentationBinding& Expected : ExpectedBindings)
		{
			const FZeroEscapePresentationBinding* Binding =
				Presentation->Bindings.FindByPredicate(
					[&Expected](const FZeroEscapePresentationBinding& Candidate)
					{
						return Candidate.StableModuleId == Expected.StableModuleId;
					});
			TestNotNull(
				*FString::Printf(TEXT("Presentation 必须存在模块 %d 的 Binding"), Expected.StableModuleId),
				Binding);
			if (Binding == nullptr)
			{
				continue;
			}
			TestTrue(
				*FString::Printf(TEXT("Presentation 模块 %d 必须保持 HISM/BlockAll/不影响导航"),
					Expected.StableModuleId),
				Binding->SpawnPolicy == EZeroEscapePresentationSpawnPolicy::InstancedStaticMesh
					&& Binding->ActorClass == nullptr
					&& Binding->CollisionProfileName == TEXT("BlockAll")
					&& !Binding->bCanEverAffectNavigation);
			TestTrue(
				*FString::Printf(TEXT("Presentation 模块 %d 必须绑定预期 SFC Mesh"),
					Expected.StableModuleId),
				Binding->StaticMesh != nullptr
					&& Binding->StaticMesh->GetPathName() == Expected.MeshPath);
			TestTrue(
				*FString::Printf(TEXT("Presentation 模块 %d 必须保存实测 Overhang"),
					Expected.StableModuleId),
				FMath::IsNearlyEqual(Binding->BoundsOverhangAllowanceCm, Expected.OverhangCm));
			TestTrue(
				*FString::Printf(TEXT("Presentation 模块 %d 必须保存预期 PivotCorrection"),
					Expected.StableModuleId),
				Binding->PivotCorrection.Equals(Expected.PivotCorrection, 0.01));
		}

		FString AssetSetError;
		if (!ValidateZeroEscapeGenerationAssetSet(
			*Profile,
			*Catalog,
			*Presentation,
			AssetSetError))
		{
			AddError(FString::Printf(
				TEXT("首套项目 PCG 资产联合契约失败：%s"),
				*AssetSetError));
			return true;
		}

		// 与 Runtime Generator 保持相同边界：只在这里读 UObject，Solver 以后只接收
		// 稳定排序的值快照。这样 DataAsset 数组编辑顺序不会进入确定性结果。
		FGenerationProfileSnapshot ProfileSnapshot;
		FModuleCatalogSnapshot CatalogSnapshot;
		FZeroEscapeGenerationReport ProfileReport;
		FZeroEscapeGenerationReport CatalogReport;
		if (!BuildGenerationSnapshot(*Profile, ProfileSnapshot, ProfileReport))
		{
			AddError(FString::Printf(
				TEXT("真实 Generation Profile 无法构建快照：%s"),
				*ProfileReport.Message));
			return true;
		}
		if (!BuildCatalogSnapshot(*Catalog, CatalogSnapshot, CatalogReport))
		{
			AddError(FString::Printf(
				TEXT("真实 Module Catalog 无法构建快照：%s"),
				*CatalogReport.Message));
			return true;
		}

		FZeroEscapeGenerationRequest Request;
		Request.Seed = 12345;
		Request.Difficulty = EZeroEscapeDifficulty::Normal;
		Request.FlowProfileId = TEXT("EscapeOnly");

		FZeroEscapeGenerationSignature Signature;
		FZeroEscapeGenerationReport SignatureReport;
		if (!BuildGenerationSignature(
			Request,
			ProfileSnapshot,
			CatalogSnapshot,
			Presentation->PresentationVersion,
			Signature,
			SignatureReport))
		{
			AddError(FString::Printf(
				TEXT("真实资产无法构建 Generation Signature：%s"),
				*SignatureReport.Message));
			return true;
		}

		FAbstractLevelPlan AbstractPlan;
		FZeroEscapeGenerationReport AbstractReport;
		if (!FGenerationCore::BuildAbstractPlan(
			Request,
			ProfileSnapshot,
			AbstractPlan,
			AbstractReport))
		{
			AddError(FString::Printf(
				TEXT("真实 Profile 无法生成 EscapeOnly 抽象图：%s"),
				*AbstractReport.Message));
			return true;
		}

		const int64 AbstractHash = ComputeCanonicalAbstractHash(AbstractPlan);
		if (!TestTrue(TEXT("真实资产生成的抽象图必须具有非零规范 Hash"), AbstractHash != 0))
		{
			return true;
		}

		FLayoutRequest LayoutRequest;
		LayoutRequest.CellSize = CatalogSnapshot.CellSize;
		LayoutRequest.GridExtent = FIntVector(
			ProfileSnapshot.SharedRouteConstraints.GridExtentCells.X,
			ProfileSnapshot.SharedRouteConstraints.GridExtentCells.Y,
			1);
		LayoutRequest.AStarStraightStepCost =
			ProfileSnapshot.SharedRouteConstraints.AStarStraightStepCost;
		LayoutRequest.AStarTurnPenalty =
			ProfileSnapshot.SharedRouteConstraints.AStarTurnPenalty;
		LayoutRequest.bRequireEffectiveWfcChoice = ProfileSnapshot.bRequireEffectiveWfcChoice;
		LayoutRequest.Signature = Signature;
		LayoutRequest.CanonicalAbstractHash = AbstractHash;
		LayoutRequest.AbstractPlan = &AbstractPlan;

		FZeroEscapeGeneratedLevelPlan FirstPlan;
		FZeroEscapeGenerationReport FirstLayoutReport;
		const bool bFirstSolved = FLayoutSolver::Solve(
			LayoutRequest,
			CatalogSnapshot,
			ProfileSnapshot.SolverBudgets,
			Request.Seed,
			FirstPlan,
			FirstLayoutReport);
		if (!bFirstSolved)
		{
			AddError(FString::Printf(
				TEXT("真实 SFC 配置的纯数据布局失败：Stage=%d Failure=%d Message=%s"),
				static_cast<int32>(FirstLayoutReport.Stage),
				static_cast<int32>(FirstLayoutReport.Failure),
				*FirstLayoutReport.Message));
			return true;
		}

		// 成功 Plan 必须完整保留输入身份和可复算 Hash。Start/Exit 的未使用开口各由一个
		// Cap 封闭，因此这里也检查两条 ClosedPortal，防止“算法成功但场景留洞”的假阳性。
		TestTrue(
			TEXT("最终 Plan 必须保留真实请求的完整版本化 Signature"),
			FirstPlan.Signature.Seed == Signature.Seed
				&& FirstPlan.Signature.Difficulty == Signature.Difficulty
				&& FirstPlan.Signature.FlowProfileId == Signature.FlowProfileId
				&& FirstPlan.Signature.AlgorithmVersion == Signature.AlgorithmVersion
				&& FirstPlan.Signature.GenerationProfileVersion == Signature.GenerationProfileVersion
				&& FirstPlan.Signature.FlowVersion == Signature.FlowVersion
				&& FirstPlan.Signature.CatalogVersion == Signature.CatalogVersion
				&& FirstPlan.Signature.PresentationVersion == Signature.PresentationVersion);
		TestEqual(TEXT("最终 Plan 必须保留同一 Abstract Hash"),
			FirstPlan.CanonicalAbstractHash, AbstractHash);
		TestTrue(TEXT("最终 Plan 必须具有非零 Layout Hash"),
			FirstPlan.CanonicalLayoutHash != 0);
		TestEqual(TEXT("最终 Layout Hash 必须能从 Plan 重新计算"),
			ComputeCanonicalLayoutHash(FirstPlan), FirstPlan.CanonicalLayoutHash);
		TestEqual(TEXT("四个关键路线抽象节点必须全部绑定到实际模块"),
			FirstPlan.NodeBindings.Num(), AbstractPlan.Nodes.Num());
		TestEqual(TEXT("三条关键路线边必须全部导出实际 EdgeRoute"),
			FirstPlan.EdgeRoutes.Num(), AbstractPlan.Edges.Num());
		TestEqual(TEXT("Start 与 Exit 的两个 Sealable 开口必须全部封闭"),
			FirstPlan.ClosedPortals.Num(), 2);
		TestTrue(TEXT("最终 Plan 必须同时存在 Start 与 Exit Placement"),
			FirstPlan.StartPlacementId != INDEX_NONE
				&& FirstPlan.ExitPlacementId != INDEX_NONE);
		TestTrue(TEXT("最终 Plan 必须导出 Start 与 Exit Gameplay Anchor"),
			FirstPlan.PlayerSpawnAnchorInstanceId != INDEX_NONE
				&& FirstPlan.ExitAnchorInstanceId != INDEX_NONE);
		TestTrue(TEXT("EscapeOnly 烟测不得伪造 Objective Binding"),
			FirstPlan.ObjectiveBindings.IsEmpty());
		TestTrue(TEXT("真实 SFC 配置必须实际进入 WFC Active Region"),
			FirstLayoutReport.Metrics.WfcActiveCellCount > 0);
		TestTrue(TEXT("布局实际尝试次数必须位于 Profile 的实时预算内"),
			FirstLayoutReport.AttemptsExecuted > 0
				&& FirstLayoutReport.AttemptsExecuted <= Profile->SolverBudgets.MaxLayoutAttempts);

		// 第二次从序列化 UObject 重新构建全部 Snapshot、Signature、AbstractPlan 和
		// LayoutRequest，而不是复用第一次的纯值对象。这样既验证 Seed 复现，也能抓住资产
		// 快照、WFC 支持计数或回溯状态被上一次调用污染的回归。
		FGenerationProfileSnapshot SecondProfileSnapshot;
		FModuleCatalogSnapshot SecondCatalogSnapshot;
		FZeroEscapeGenerationReport SecondProfileReport;
		FZeroEscapeGenerationReport SecondCatalogReport;
		if (!BuildGenerationSnapshot(*Profile, SecondProfileSnapshot, SecondProfileReport))
		{
			AddError(FString::Printf(
				TEXT("第二次真实 Generation Profile 快照失败：%s"),
				*SecondProfileReport.Message));
			return true;
		}
		if (!BuildCatalogSnapshot(*Catalog, SecondCatalogSnapshot, SecondCatalogReport))
		{
			AddError(FString::Printf(
				TEXT("第二次真实 Module Catalog 快照失败：%s"),
				*SecondCatalogReport.Message));
			return true;
		}

		FZeroEscapeGenerationSignature SecondSignature;
		FZeroEscapeGenerationReport SecondSignatureReport;
		if (!BuildGenerationSignature(
			Request,
			SecondProfileSnapshot,
			SecondCatalogSnapshot,
			Presentation->PresentationVersion,
			SecondSignature,
			SecondSignatureReport))
		{
			AddError(FString::Printf(
				TEXT("第二次真实资产 Signature 构建失败：%s"),
				*SecondSignatureReport.Message));
			return true;
		}

		FAbstractLevelPlan SecondAbstractPlan;
		FZeroEscapeGenerationReport SecondAbstractReport;
		if (!FGenerationCore::BuildAbstractPlan(
			Request,
			SecondProfileSnapshot,
			SecondAbstractPlan,
			SecondAbstractReport))
		{
			AddError(FString::Printf(
				TEXT("第二次真实 Profile 抽象图构建失败：%s"),
				*SecondAbstractReport.Message));
			return true;
		}
		const int64 SecondAbstractHash = ComputeCanonicalAbstractHash(SecondAbstractPlan);
		TestEqual(TEXT("完整资产管线同请求必须复现 Abstract Hash"),
			SecondAbstractHash, AbstractHash);
		TestTrue(
			TEXT("完整资产管线同请求必须复现 Signature 全字段"),
			SecondSignature.Seed == Signature.Seed
				&& SecondSignature.Difficulty == Signature.Difficulty
				&& SecondSignature.FlowProfileId == Signature.FlowProfileId
				&& SecondSignature.AlgorithmVersion == Signature.AlgorithmVersion
				&& SecondSignature.GenerationProfileVersion == Signature.GenerationProfileVersion
				&& SecondSignature.FlowVersion == Signature.FlowVersion
				&& SecondSignature.CatalogVersion == Signature.CatalogVersion
				&& SecondSignature.PresentationVersion == Signature.PresentationVersion);

		FLayoutRequest SecondLayoutRequest;
		SecondLayoutRequest.CellSize = SecondCatalogSnapshot.CellSize;
		SecondLayoutRequest.GridExtent = FIntVector(
			SecondProfileSnapshot.SharedRouteConstraints.GridExtentCells.X,
			SecondProfileSnapshot.SharedRouteConstraints.GridExtentCells.Y,
			1);
		SecondLayoutRequest.AStarStraightStepCost =
			SecondProfileSnapshot.SharedRouteConstraints.AStarStraightStepCost;
		SecondLayoutRequest.AStarTurnPenalty =
			SecondProfileSnapshot.SharedRouteConstraints.AStarTurnPenalty;
		SecondLayoutRequest.bRequireEffectiveWfcChoice =
			SecondProfileSnapshot.bRequireEffectiveWfcChoice;
		SecondLayoutRequest.Signature = SecondSignature;
		SecondLayoutRequest.CanonicalAbstractHash = SecondAbstractHash;
		SecondLayoutRequest.AbstractPlan = &SecondAbstractPlan;

		FZeroEscapeGeneratedLevelPlan SecondPlan;
		FZeroEscapeGenerationReport SecondLayoutReport;
		const bool bSecondSolved = FLayoutSolver::Solve(
			SecondLayoutRequest,
			SecondCatalogSnapshot,
			SecondProfileSnapshot.SolverBudgets,
			Request.Seed,
			SecondPlan,
			SecondLayoutReport);
		TestTrue(TEXT("真实 SFC 配置使用同一 Seed 第二次求解仍必须成功"), bSecondSolved);
		if (bSecondSolved)
		{
			TestEqual(TEXT("真实 SFC 配置同 Seed 必须复现 Layout Hash"),
				SecondPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
			TestEqual(TEXT("真实 SFC 配置同 Seed 必须复现模块数量"),
				SecondPlan.Modules.Num(), FirstPlan.Modules.Num());
			TestEqual(TEXT("真实 SFC 配置同 Seed 必须复现 Portal 连接数量"),
				SecondPlan.PortalConnections.Num(), FirstPlan.PortalConnections.Num());
		}

		return true;
	}

	/**
	 * 人工打乱 DataAsset 数组后构建快照，证明确定性依赖 Stable Id/枚举排序而非作者编辑顺序。
	 * 这允许策划整理数组、添加展示分组或重排 SFCorridors 绑定而不改变同 Seed 的算法输入。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeSnapshotStableOrderingTest,
		"Demo.PCG.Unit.Core.SnapshotStableOrdering",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeSnapshotStableOrderingTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		Profile->Difficulties.Swap(0, 2);
		Profile->Flows.Swap(0, 2);
		Profile->Flows[0].AllowedObjectiveRoles.Swap(0, 2);

		FGenerationProfileSnapshot ProfileSnapshot;
		FZeroEscapeGenerationReport Report;
		const bool bBuiltProfile = BuildGenerationSnapshot(*Profile, ProfileSnapshot, Report);
		TestTrue(TEXT("无序 Profile 应能构建纯值快照"), bBuiltProfile);
		if (bBuiltProfile)
		{
			TestEqual(TEXT("快照应保留三个难度"), ProfileSnapshot.Difficulties.Num(), 3);
			TestEqual(TEXT("快照应保留三个流程"), ProfileSnapshot.Flows.Num(), 3);
			if (ProfileSnapshot.Difficulties.Num() == 3)
			{
				TestTrue(TEXT("Difficulty 快照必须按稳定枚举值排序"),
					ProfileSnapshot.Difficulties[0].Difficulty == EZeroEscapeDifficulty::Easy
					&& ProfileSnapshot.Difficulties[1].Difficulty == EZeroEscapeDifficulty::Normal
					&& ProfileSnapshot.Difficulties[2].Difficulty == EZeroEscapeDifficulty::Hard);
			}
			if (ProfileSnapshot.Flows.Num() == 3)
			{
				TestTrue(TEXT("Flow 快照必须按 StableFlowId 词法排序"),
					ProfileSnapshot.Flows[0].StableFlowId == TEXT("CollectAll")
					&& ProfileSnapshot.Flows[1].StableFlowId == TEXT("CollectKOfN")
					&& ProfileSnapshot.Flows[2].StableFlowId == TEXT("EscapeOnly"));
				TestTrue(TEXT("Flow 内部 Role 也必须按稳定枚举值排序"),
					ProfileSnapshot.Flows[1].AllowedObjectiveRoles.Num() == 3
					&& ProfileSnapshot.Flows[1].AllowedObjectiveRoles[0]
						== EZeroEscapeTopologyRole::MainPath
					&& ProfileSnapshot.Flows[1].AllowedObjectiveRoles[1]
						== EZeroEscapeTopologyRole::ShortLeaf
					&& ProfileSnapshot.Flows[1].AllowedObjectiveRoles[2]
						== EZeroEscapeTopologyRole::ForwardRejoin);
			}
		}

		UZeroEscapeModuleCatalog* Catalog = NewObject<UZeroEscapeModuleCatalog>();
		BuildValidCatalog(*Catalog);
		Catalog->Modules.Swap(0, Catalog->Modules.Num() - 1);
		FZeroEscapeModuleDefinition* MainPathModule = Catalog->Modules.FindByPredicate(
			[](const FZeroEscapeModuleDefinition& Module)
			{
				return Module.StableModuleId == 200;
			});
		if (MainPathModule != nullptr)
		{
			MainPathModule->Portals.Swap(0, 1);
		}

		FModuleCatalogSnapshot CatalogSnapshot;
		const bool bBuiltCatalog = BuildCatalogSnapshot(*Catalog, CatalogSnapshot, Report);
		TestTrue(TEXT("无序 Catalog 应能构建纯值快照"), bBuiltCatalog);
		if (bBuiltCatalog)
		{
			bool bModulesSorted = true;
			for (int32 Index = 1; Index < CatalogSnapshot.Modules.Num(); ++Index)
			{
				bModulesSorted &= CatalogSnapshot.Modules[Index - 1].StableModuleId
					< CatalogSnapshot.Modules[Index].StableModuleId;
			}
			TestTrue(TEXT("Module 快照必须按 StableModuleId 排序"), bModulesSorted);

			const FModuleSnapshot* MainPathSnapshot = CatalogSnapshot.Modules.FindByPredicate(
				[](const FModuleSnapshot& Module)
				{
					return Module.StableModuleId == 200;
				});
			TestTrue(TEXT("快照必须保留 MainPath 测试模块"), MainPathSnapshot != nullptr);
			if (MainPathSnapshot != nullptr)
			{
				TestTrue(TEXT("Module 内部 Portal 必须按 StableSocketId 排序"),
					MainPathSnapshot->Portals.Num() == 2
					&& MainPathSnapshot->Portals[0].StableSocketId == 0
					&& MainPathSnapshot->Portals[1].StableSocketId == 1);
			}
		}

		return true;
	}

	/**
	 * 对同一 Request 连续构建两次抽象图并比较规范 Hash 与规模，防止 Core 持有跨调用状态，
	 * 或让容器遍历顺序、非隔离随机流悄悄改变同 Seed 的拓扑。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeAbstractDeterminismTest,
		"Demo.PCG.Unit.Core.AbstractDeterminism",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeAbstractDeterminismTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		FGenerationProfileSnapshot Snapshot;
		FZeroEscapeGenerationReport Report;
		if (!TestTrue(
				TEXT("确定性测试需要先构建合法 Profile Snapshot"),
				BuildGenerationSnapshot(*Profile, Snapshot, Report)))
		{
			return true;
		}

		FZeroEscapeGenerationRequest Request;
		Request.Seed = 8675309;
		Request.Difficulty = EZeroEscapeDifficulty::Normal;
		Request.FlowProfileId = TEXT("EscapeOnly");

		FAbstractLevelPlan FirstPlan;
		FAbstractLevelPlan SecondPlan;
		FZeroEscapeGenerationReport FirstReport;
		FZeroEscapeGenerationReport SecondReport;
		const bool bFirstBuilt = FGenerationCore::BuildAbstractPlan(
			Request, Snapshot, FirstPlan, FirstReport);
		const bool bSecondBuilt = FGenerationCore::BuildAbstractPlan(
			Request, Snapshot, SecondPlan, SecondReport);
		TestTrue(TEXT("第一次相同请求应生成抽象图"), bFirstBuilt);
		TestTrue(TEXT("第二次相同请求应生成抽象图"), bSecondBuilt);
		if (bFirstBuilt && bSecondBuilt)
		{
			const int64 FirstHash = ComputeCanonicalAbstractHash(FirstPlan);
			const int64 SecondHash = ComputeCanonicalAbstractHash(SecondPlan);
			TestTrue(TEXT("合法抽象图的规范 Hash 不应为零"), FirstHash != 0);
			TestEqual(TEXT("同请求重复生成必须得到相同抽象 Hash"), FirstHash, SecondHash);
			TestEqual(TEXT("同请求重复生成必须得到相同 Node 数"),
				FirstPlan.Nodes.Num(), SecondPlan.Nodes.Num());
			TestEqual(TEXT("同请求重复生成必须得到相同 Edge 数"),
				FirstPlan.Edges.Num(), SecondPlan.Edges.Num());
			TestEqual(TEXT("同请求重复生成必须得到相同 Objective 数"),
				FirstPlan.Objectives.Num(), SecondPlan.Objectives.Num());
		}

		return true;
	}

	/**
	 * 回归目标初次随机落在 ShortLeaf、但共享 Required Route 上限不允许该折返的情形。
	 * Core 应执行一次确定性的最小绕行候选回退，而不是让合法 Seed 整体失败；64 个 Seed 同时
	 * 验证 CollectAll 与 CollectKOfN，并明确证明测试集合覆盖了触发回退的初始随机分支。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeObjectiveRouteFallbackTest,
		"Demo.PCG.Unit.Core.ObjectiveRouteFallback",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeObjectiveRouteFallbackTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		UZeroEscapeLevelGenerationProfile* Profile = NewObject<UZeroEscapeLevelGenerationProfile>();
		BuildValidProfile(*Profile);
		Profile->SharedRouteConstraints.CriticalPathNodeCount = 4;
		Profile->SharedRouteConstraints.MaxLeafOneWayEdgeCount = 1;
		Profile->SharedRouteConstraints.MaxRequiredRouteExtraEdgeCount = 0;
		Profile->SharedRouteConstraints.MaxObjectiveCandidateCount = 1;
		for (FZeroEscapeDifficultyDefinition& Definition : Profile->Difficulties)
		{
			Definition.ShortLeafBranchCount = 2;
			Definition.ForwardRejoinBranchCount = 0;
			Definition.ObjectiveCandidateCount = 1;
			Definition.RequiredObjectiveCount = 1;
		}
		for (FZeroEscapeFlowDefinition& Flow : Profile->Flows)
		{
			if (Flow.CompletionRule != EZeroEscapeCompletionRule::EscapeOnly)
			{
				Flow.AllowedObjectiveRoles = {
					EZeroEscapeTopologyRole::MainPath,
					EZeroEscapeTopologyRole::ShortLeaf };
			}
		}

		FGenerationProfileSnapshot Snapshot;
		FZeroEscapeGenerationReport SnapshotReport;
		if (!TestTrue(
				TEXT("目标折返回退测试需要先构建合法 Profile Snapshot"),
				BuildGenerationSnapshot(*Profile, Snapshot, SnapshotReport)))
		{
			return true;
		}

		const TArray<FName> CollectFlowIds = {
			FName(TEXT("CollectAll")),
			FName(TEXT("CollectKOfN")) };
		constexpr int32 SeedCount = 64;
		bool bCoveredInitialShortLeafDraw = false;
		for (const FName FlowId : CollectFlowIds)
		{
			for (int32 Seed = 0; Seed < SeedCount; ++Seed)
			{
				FRandomStream InitialObjectiveRandom = FGenerationCore::MakeRandomStream(
					Seed, GAlgorithmVersion, ERandomDomain::ObjectivePlacement, 0);
				bCoveredInitialShortLeafDraw |= InitialObjectiveRandom.RandHelper(4) % 2 == 1;

				FZeroEscapeGenerationRequest Request;
				Request.Seed = Seed;
				Request.Difficulty = EZeroEscapeDifficulty::Easy;
				Request.FlowProfileId = FlowId;

				FAbstractLevelPlan FirstPlan;
				FAbstractLevelPlan SecondPlan;
				FZeroEscapeGenerationReport FirstReport;
				FZeroEscapeGenerationReport SecondReport;
				const bool bFirstBuilt = FGenerationCore::BuildAbstractPlan(
					Request, Snapshot, FirstPlan, FirstReport);
				const bool bSecondBuilt = FGenerationCore::BuildAbstractPlan(
					Request, Snapshot, SecondPlan, SecondReport);
				if (!bFirstBuilt || !bSecondBuilt)
				{
					AddError(FString::Printf(
						TEXT("Flow=%s Seed=%d 不应因首次随机抽中 ShortLeaf 而失败：%s / %s"),
						*FlowId.ToString(),
						Seed,
						*FirstReport.Message,
						*SecondReport.Message));
					return true;
				}

				if (FirstPlan.Objectives.Num() != 1
					|| SecondPlan.Objectives.Num() != 1
					|| ComputeCanonicalAbstractHash(FirstPlan)
						!= ComputeCanonicalAbstractHash(SecondPlan))
				{
					AddError(FString::Printf(
						TEXT("Flow=%s Seed=%d 的有界目标候选必须稳定复现。"),
						*FlowId.ToString(),
						Seed));
					return true;
				}

				const int32 ObjectiveNodeId = FirstPlan.Objectives[0].StableNodeId;
				const FSpatialNode* ObjectiveNode = FirstPlan.Nodes.FindByPredicate(
					[ObjectiveNodeId](const FSpatialNode& Node)
					{
						return Node.StableNodeId == ObjectiveNodeId;
					});
				if (ObjectiveNode == nullptr
					|| ObjectiveNode->Role != EZeroEscapeTopologyRole::MainPath)
				{
					AddError(FString::Printf(
						TEXT("Flow=%s Seed=%d 在 MaxRequiredRouteExtraEdgeCount=0 时必须选到主路目标。"),
						*FlowId.ToString(),
						Seed));
					return true;
				}
			}
		}
		TestTrue(
			TEXT("回归 Seed 集必须实际覆盖首次抽中 ShortLeaf 的分支"),
			bCoveredInitialShortLeafDraw);

		return true;
	}

	/**
	 * 验证每个随机职责与 Layout Attempt 都由 Master Seed 派生独立子流。
	 * 这样未来在 Topology 增加一次随机抽取，不会连带改变 Objective Placement 或其他阶段。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeRandomDomainIsolationTest,
		"Demo.PCG.Unit.Core.RandomDomainIsolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeRandomDomainIsolationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		FRandomStream ProgressionA = FGenerationCore::MakeRandomStream(
			13579, GAlgorithmVersion, ERandomDomain::Progression, 0);
		FRandomStream ProgressionB = FGenerationCore::MakeRandomStream(
			13579, GAlgorithmVersion, ERandomDomain::Progression, 0);
		FRandomStream Topology = FGenerationCore::MakeRandomStream(
			13579, GAlgorithmVersion, ERandomDomain::Topology, 0);
		FRandomStream NextAttempt = FGenerationCore::MakeRandomStream(
			13579, GAlgorithmVersion, ERandomDomain::Progression, 1);

		bool bSameDomainSequenceMatches = true;
		bool bTopologySequenceDiffers = false;
		bool bAttemptSequenceDiffers = false;
		for (int32 DrawIndex = 0; DrawIndex < 8; ++DrawIndex)
		{
			const uint32 ProgressionValueA = ProgressionA.GetUnsignedInt();
			const uint32 ProgressionValueB = ProgressionB.GetUnsignedInt();
			bSameDomainSequenceMatches &= ProgressionValueA == ProgressionValueB;
			bTopologySequenceDiffers |= ProgressionValueA != Topology.GetUnsignedInt();
			bAttemptSequenceDiffers |= ProgressionValueA != NextAttempt.GetUnsignedInt();
		}

		TestTrue(TEXT("同 Seed/版本/Domain/Attempt 必须复现相同随机序列"),
			bSameDomainSequenceMatches);
		TestTrue(TEXT("不同随机 Domain 必须派生独立子流"), bTopologySequenceDiffers);
		TestTrue(TEXT("不同 Attempt 必须派生独立子流"), bAttemptSequenceDiffers);
		return true;
	}

	/**
	 * 用同一张小图验证 EscapeOnly、CollectAll 和 CollectKOfN 的可达性语义。
	 * 当前产品约束要求所有生成出的候选目标房都可达；K<N 只降低通关所需数量，不能用来
	 * 掩盖一个坏掉、永远到不了的候选房间。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeProgressionRulesTest,
		"Demo.PCG.Unit.Core.ProgressionRules",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeProgressionRulesTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		FZeroEscapeGenerationReport Report;
		FAbstractLevelPlan EscapePlan;
		BuildProgressionFixture(EZeroEscapeCompletionRule::EscapeOnly, 0, EscapePlan);
		TestTrue(TEXT("EscapeOnly 在 Start 到 Exit 连通时应可解"),
			FGenerationCore::ValidateProgression(EscapePlan, 4096, Report));
		FAbstractLevelPlan BrokenEscapePlan = EscapePlan;
		BrokenEscapePlan.Edges.RemoveAt(2);
		TestFalse(TEXT("EscapeOnly 在 Exit 不可达时必须失败"),
			FGenerationCore::ValidateProgression(BrokenEscapePlan, 4096, Report));
		TestTrue(TEXT("EscapeOnly 断路应报告 ProgressionNoSolution"),
			Report.Failure == EZeroEscapeGenerationFailure::ProgressionNoSolution);

		FAbstractLevelPlan CollectAllPlan;
		BuildProgressionFixture(EZeroEscapeCompletionRule::CollectAll, 3, CollectAllPlan);
		TestTrue(TEXT("CollectAll 在三个目标均可达时应可解"),
			FGenerationCore::ValidateProgression(CollectAllPlan, 4096, Report));
		FAbstractLevelPlan BrokenCollectAllPlan = CollectAllPlan;
		BrokenCollectAllPlan.Edges.Pop(EAllowShrinking::No);
		TestFalse(TEXT("CollectAll 存在不可达候选目标时必须失败"),
			FGenerationCore::ValidateProgression(BrokenCollectAllPlan, 4096, Report));
		TestTrue(TEXT("CollectAll 不可达目标应报告 ProgressionNoSolution"),
			Report.Failure == EZeroEscapeGenerationFailure::ProgressionNoSolution);

		FAbstractLevelPlan CollectKOfNPlan;
		BuildProgressionFixture(EZeroEscapeCompletionRule::CollectKOfN, 2, CollectKOfNPlan);
		TestTrue(TEXT("CollectKOfN 在三个候选均可达且可收集两个时应可解"),
			FGenerationCore::ValidateProgression(CollectKOfNPlan, 4096, Report));
		FAbstractLevelPlan BrokenKOfNPlan = CollectKOfNPlan;
		BrokenKOfNPlan.Edges.Pop(EAllowShrinking::No);
		TestFalse(TEXT("K-of-N 也不能用 K 掩盖坏掉的候选目标房"),
			FGenerationCore::ValidateProgression(BrokenKOfNPlan, 4096, Report));
		TestTrue(TEXT("K-of-N 不可达候选应报告 ProgressionNoSolution"),
			Report.Failure == EZeroEscapeGenerationFailure::ProgressionNoSolution);

		return true;
	}

	/**
	 * 对最小 Start--WFC--Exit 完整链做确定性和状态隔离回归：成功输出必须可复现，
	 * 传入带脏数据的 OutPlan 必须被覆盖，预算失败必须原子清空，再次求解必须能恢复。
	 * 这直接保护运行时生成器依赖的“Plan 成功完整、失败为空”事务前提。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeLayoutDeterminismAndStateIsolationTest,
		"Demo.PCG.Unit.Layout.DeterminismAndStateIsolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeLayoutDeterminismAndStateIsolationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		FAbstractLevelPlan AbstractPlan;
		FModuleCatalogSnapshot Catalog;
		FLayoutRequest Request;
		FZeroEscapeSolverBudgets Budgets;
		BuildStraightLayoutFixture(AbstractPlan, Catalog, Request, Budgets);
		TestTrue(TEXT("直线布局夹具必须具有有效抽象 Hash"),
			Request.CanonicalAbstractHash != 0);

		FZeroEscapeGeneratedLevelPlan FirstPlan;
		FZeroEscapeGenerationReport FirstReport;
		const bool bFirstSolved = FLayoutSolver::Solve(
			Request, Catalog, Budgets, Request.Signature.Seed, FirstPlan, FirstReport);
		TestTrue(TEXT("最小 Start-WFC-Exit 夹具应完成全流程布局"), bFirstSolved);
		if (!bFirstSolved)
		{
			AddError(FString::Printf(
				TEXT("最小布局失败：Stage=%d Failure=%d Message=%s"),
				static_cast<int32>(FirstReport.Stage),
				static_cast<int32>(FirstReport.Failure),
				*FirstReport.Message));
			return true;
		}

		TestTrue(TEXT("成功布局必须生成非零 CanonicalLayoutHash"),
			FirstPlan.CanonicalLayoutHash != 0);
		TestTrue(TEXT("成功布局必须导出 Start/Exit 与至少一个 WFC 模块"),
			FirstPlan.StartPlacementId >= 0
			&& FirstPlan.ExitPlacementId >= 0
			&& FirstPlan.Modules.Num() >= 3);

		// 先注入上一次调用可能留下的脏值，验证 Solve 不会把输出参数当增量容器使用。
		FZeroEscapeGeneratedLevelPlan ReusedPlan;
		ReusedPlan.CanonicalLayoutHash = 123;
		ReusedPlan.Modules.AddDefaulted();
		FZeroEscapeGenerationReport ReusedReport;
		const bool bSecondSolved = FLayoutSolver::Solve(
			Request, Catalog, Budgets, Request.Signature.Seed, ReusedPlan, ReusedReport);
		TestTrue(TEXT("复用带脏数据的输出对象仍应成功"), bSecondSolved);
		if (bSecondSolved)
		{
			TestEqual(TEXT("相同布局请求必须得到相同 Layout Hash"),
				ReusedPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
			TestEqual(TEXT("重复求解不能残留额外 Module"),
				ReusedPlan.Modules.Num(), FirstPlan.Modules.Num());
			TestEqual(TEXT("重复求解不能残留额外 PortalConnection"),
				ReusedPlan.PortalConnections.Num(), FirstPlan.PortalConnections.Num());
		}

		// 再强制预算失败，验证失败不是“保留最后一个看似可用 Plan”的软失败。
		FZeroEscapeSolverBudgets ExhaustedBudgets = Budgets;
		ExhaustedBudgets.MaxTotalWorkUnits = 1;
		FZeroEscapeGeneratedLevelPlan FailedPlan = FirstPlan;
		FZeroEscapeGenerationReport FailedReport;
		TestFalse(TEXT("确定性工作预算不足时布局必须失败"),
			FLayoutSolver::Solve(
				Request,
				Catalog,
				ExhaustedBudgets,
				Request.Signature.Seed,
				FailedPlan,
				FailedReport));
		TestTrue(TEXT("失败调用必须原子清空旧 Plan，而不是泄漏上次结果"),
			FailedPlan.Modules.IsEmpty()
			&& FailedPlan.PortalConnections.IsEmpty()
			&& FailedPlan.CanonicalAbstractHash == 0
			&& FailedPlan.CanonicalLayoutHash == 0);
		TestTrue(TEXT("工作预算耗尽应报告 SearchBudgetExceeded"),
			FailedReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded);

		FZeroEscapeGenerationReport RecoveryReport;
		const bool bRecovered = FLayoutSolver::Solve(
			Request, Catalog, Budgets, Request.Signature.Seed, FailedPlan, RecoveryReport);
		TestTrue(TEXT("预算失败后再次合法求解不应残留失败状态"), bRecovered);
		if (bRecovered)
		{
			TestEqual(TEXT("失败后的恢复求解仍必须复现原 Layout Hash"),
				FailedPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
		}

		return true;
	}

	/**
	 * 回归一个 Junction 同时服务三条抽象边的 Strong Socket 场景。它验证每条 EdgeRoute 的
	 * 端点和每一跳都由真实 PortalConnection 支撑，并覆盖确定性复算与共享 A* 尝试预算。
	 * 该测试专门防止贪心路由占用错误 Portal 后留下“Hash 有值但支路没有真实连接”的假成功。
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeStrongSocketBranchRegressionTest,
		"Demo.PCG.Unit.Layout.StrongSocketBranchRegression",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeStrongSocketBranchRegressionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		FAbstractLevelPlan AbstractPlan;
		FModuleCatalogSnapshot Catalog;
		FLayoutRequest Request;
		FZeroEscapeSolverBudgets Budgets;
		BuildBranchedStrongLayoutFixture(AbstractPlan, Catalog, Request, Budgets);
		TestTrue(TEXT("多 Strong Socket 夹具必须具有有效抽象 Hash"),
			Request.CanonicalAbstractHash != 0);

		FZeroEscapeGeneratedLevelPlan FirstPlan;
		FZeroEscapeGenerationReport FirstReport;
		const bool bFirstSolved = FLayoutSolver::Solve(
			Request, Catalog, Budgets, Request.Signature.Seed, FirstPlan, FirstReport);
		TestTrue(TEXT("Start-Junction-Exit 加 ShortLeaf 的完整布局必须可解"), bFirstSolved);
		if (!bFirstSolved)
		{
			AddError(FString::Printf(
				TEXT("Strong Socket 分支布局失败：Stage=%d Failure=%d Message=%s"),
				static_cast<int32>(FirstReport.Stage),
				static_cast<int32>(FirstReport.Failure),
				*FirstReport.Message));
			return true;
		}

		TestEqual(TEXT("四个 Strong 抽象节点必须各自导出 NodeBinding"),
			FirstPlan.NodeBindings.Num(), 4);
		TestEqual(TEXT("三条抽象边必须各自导出 EdgeRoute"),
			FirstPlan.EdgeRoutes.Num(), 3);
		TestTrue(TEXT("完整分支布局必须导出非零 Layout Hash"),
			FirstPlan.CanonicalLayoutHash != 0);

		auto FindPlacementForNode = [](const FZeroEscapeGeneratedLevelPlan& Plan, const int32 NodeId)
		{
			const FZeroEscapeNodePlacementBinding* Binding = Plan.NodeBindings.FindByPredicate(
				[NodeId](const FZeroEscapeNodePlacementBinding& Candidate)
				{
					return Candidate.AbstractNodeId == NodeId;
				});
			return Binding != nullptr ? Binding->StablePlacementId : INDEX_NONE;
		};

		// EdgeRoute 不是仅供显示的路径提示：每一对相邻 Placement 都必须能回查到
		// 同一 AbstractEdge 的实际 PortalConnection，玩法和验证层才能把它当作真实连通图。
		bool bEveryRouteHasCorrectEndpoints = true;
		bool bEveryRouteLinkHasConnection = true;
		for (const FZeroEscapeEdgeRouteBinding& Route : FirstPlan.EdgeRoutes)
		{
			const int32 ExpectedStart = FindPlacementForNode(FirstPlan, Route.FromNodeId);
			const int32 ExpectedEnd = FindPlacementForNode(FirstPlan, Route.ToNodeId);
			bEveryRouteHasCorrectEndpoints &= Route.OrderedStablePlacementIds.Num() >= 2
				&& Route.OrderedStablePlacementIds[0] == ExpectedStart
				&& Route.OrderedStablePlacementIds.Last() == ExpectedEnd;

			for (int32 LinkIndex = 1;
				LinkIndex < Route.OrderedStablePlacementIds.Num();
				++LinkIndex)
			{
				const int32 PlacementA = Route.OrderedStablePlacementIds[LinkIndex - 1];
				const int32 PlacementB = Route.OrderedStablePlacementIds[LinkIndex];
				const bool bHasConnection = FirstPlan.PortalConnections.ContainsByPredicate(
					[&Route, PlacementA, PlacementB](const FZeroEscapePortalConnection& Connection)
					{
						const bool bSamePair =
							(Connection.StablePlacementAId == PlacementA
								&& Connection.StablePlacementBId == PlacementB)
							|| (Connection.StablePlacementAId == PlacementB
								&& Connection.StablePlacementBId == PlacementA);
						return bSamePair && Connection.AbstractEdgeId == Route.AbstractEdgeId;
					});
				bEveryRouteLinkHasConnection &= bHasConnection;
			}
		}
		TestTrue(TEXT("每条 EdgeRoute 必须从其 From NodeBinding 到 To NodeBinding"),
			bEveryRouteHasCorrectEndpoints);
		TestTrue(TEXT("EdgeRoute 的每对相邻 Placement 必须由同一 AbstractEdge 的真实 PortalConnection 连接"),
			bEveryRouteLinkHasConnection);

		const int32 JunctionPlacementId = FindPlacementForNode(FirstPlan, 1);
		const FZeroEscapePlacedModule* JunctionPlacement = FirstPlan.Modules.FindByPredicate(
			[JunctionPlacementId](const FZeroEscapePlacedModule& Module)
			{
				return Module.StablePlacementId == JunctionPlacementId;
			});
		TestTrue(TEXT("三向 Junction Node 必须绑定到预期的 StableModuleId"),
			JunctionPlacement != nullptr && JunctionPlacement->StableModuleId == 20);

		FZeroEscapeGeneratedLevelPlan SecondPlan;
		FZeroEscapeGenerationReport SecondReport;
		const bool bSecondSolved = FLayoutSolver::Solve(
			Request, Catalog, Budgets, Request.Signature.Seed, SecondPlan, SecondReport);
		TestTrue(TEXT("多 Strong Socket 布局第二次求解仍必须成功"), bSecondSolved);
		if (bSecondSolved)
		{
			TestEqual(TEXT("多 Strong Socket 布局同 Seed 必须复现 Layout Hash"),
				SecondPlan.CanonicalLayoutHash, FirstPlan.CanonicalLayoutHash);
			TestEqual(TEXT("多 Strong Socket 布局同 Seed 必须复现连接数量"),
				SecondPlan.PortalConnections.Num(), FirstPlan.PortalConnections.Num());
		}

		// 三条抽象边共享同一个 MaxAStarRouteAttempts 计数；故意只给两次，验证总预算
		// 不会在递归/回溯分支中被各自重置，也验证失败输出保持原子为空。
		FZeroEscapeSolverBudgets RouteLimitedBudgets = Budgets;
		RouteLimitedBudgets.MaxAStarRouteAttempts = 2;
		FZeroEscapeGeneratedLevelPlan LimitedPlan = FirstPlan;
		FZeroEscapeGenerationReport LimitedReport;
		const bool bLimitedSolved = FLayoutSolver::Solve(
			Request,
			Catalog,
			RouteLimitedBudgets,
			Request.Signature.Seed,
			LimitedPlan,
			LimitedReport);
		TestFalse(TEXT("Route Attempt 预算小于抽象 Edge 数时必须结构化失败"), bLimitedSolved);
		TestTrue(TEXT("Route Attempt 预算失败必须报告 SocketLayout/SearchBudgetExceeded"),
			LimitedReport.Stage == EZeroEscapeGenerationStage::SocketLayout
			&& LimitedReport.Failure == EZeroEscapeGenerationFailure::SearchBudgetExceeded);
		TestTrue(TEXT("Route Attempt 预算失败必须原子清空旧 Plan"),
			LimitedPlan.Modules.IsEmpty()
			&& LimitedPlan.EdgeRoutes.IsEmpty()
			&& LimitedPlan.PortalConnections.IsEmpty()
			&& LimitedPlan.CanonicalLayoutHash == 0);

		return true;
	}
}

#endif
