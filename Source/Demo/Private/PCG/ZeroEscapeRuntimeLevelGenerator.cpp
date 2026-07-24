// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeLevelGenerator.cpp
 * 职责：在游戏线程同步执行 PCG 快照、纯数据求解、验证与事务式场景实例化。
 * 边界：不在 Construction Script 或 Tick 中生成；第三方素材只通过 Presentation Profile 读取。
 * 状态 Owner：AZeroEscapeRuntimeLevelGenerator；登记数组覆盖当前实例化事务的全部可回滚对象。
 */

#include "PCG/ZeroEscapeRuntimeLevelGenerator.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "PCG/ZeroEscapeGenerationAssets.h"
#include "PCG/ZeroEscapeGenerationCore.h"
#include "PCG/ZeroEscapeLayoutSolver.h"

namespace LevelGen = ZeroEscape::LevelGeneration;

/**
 * Runtime Generator 的专用日志类别。
 * `ZE_PCG_RESULT` 是测试和人工排障的观测出口，不是玩法协议；任何游戏逻辑都必须继续通过
 * State、LastReport、LastPlan 查询函数或 OnGenerationFinished Delegate 取得结构化结果。
 */
DEFINE_LOG_CATEGORY_STATIC(LogZeroEscapePCG, Log, All);

namespace
{
	/**
	 * UENUM 的短枚举名比本地化文本更适合稳定日志，同时保留未知值的数值回退，避免新增枚举值时
	 * 整条结果记录失去可读性。该字符串只用于诊断，不进入 Seed、Hash 或任何生成决策。
	 */
	template <typename TEnum>
	FString GetStableEnumName(const TEnum Value)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		const FString ReflectedName = Enum != nullptr
			? Enum->GetNameStringByValue(static_cast<int64>(Value))
			: FString();
		// GetNameStringByValue 对未知值返回空字符串而不是 nullptr；显式检查空值，才能兑现
		// 上方注释承诺的 Unknown(n) 回退，并让损坏/未来版本枚举仍可从日志定位原始数值。
		return !ReflectedName.IsEmpty()
			? ReflectedName
			: FString::Printf(TEXT("Unknown(%lld)"), static_cast<long long>(Value));
	}

	/**
	 * 最终报告必须保持严格单行，方便 Output Log、MCP 与自动化脚本按一次生成一条记录读取。
	 * Message 仍然只是给人看的补充信息，因此这里只净化换行、制表和引号，不允许调用者解析它。
	 */
	FString MakeSingleLineLogValue(const FString& Value)
	{
		FString Result = Value;
		Result.ReplaceInline(TEXT("\r"), TEXT(" "));
		Result.ReplaceInline(TEXT("\n"), TEXT(" "));
		Result.ReplaceInline(TEXT("\t"), TEXT(" "));
		Result.ReplaceInline(TEXT("\""), TEXT("'"));
		return Result;
	}

	/** 签名逐字段比较，避免只校验算法版本而接受不属于本次请求的布局。 */
	bool AreGenerationSignaturesEqual(
		const FZeroEscapeGenerationSignature& A,
		const FZeroEscapeGenerationSignature& B)
	{
		return A.Seed == B.Seed
			&& A.Difficulty == B.Difficulty
			&& A.FlowProfileId == B.FlowProfileId
			&& A.AlgorithmVersion == B.AlgorithmVersion
			&& A.GenerationProfileVersion == B.GenerationProfileVersion
			&& A.FlowVersion == B.FlowVersion
			&& A.CatalogVersion == B.CatalogVersion
			&& A.PresentationVersion == B.PresentationVersion;
	}

	/** 把实例化检查点失败写成统一、可定位且不会被上层覆盖的报告。 */
	bool FailInstantiation(
		FZeroEscapeGenerationReport& InOutReport,
		const int32 RelatedStableId,
		const TCHAR* Checkpoint)
	{
		InOutReport.Stage = EZeroEscapeGenerationStage::Instantiation;
		InOutReport.Failure = EZeroEscapeGenerationFailure::InstantiationFailed;
		InOutReport.RelatedStableId = RelatedStableId;
		InOutReport.Message = FString::Printf(
			TEXT("PCG 实例化在 %s 失败（StableId=%d）。"),
			Checkpoint,
			RelatedStableId);
		return false;
	}
}

AZeroEscapeRuntimeLevelGenerator::AZeroEscapeRuntimeLevelGenerator()
{
	// 整关生成是显式的一次性事务，不需要逐帧更新。GeneratedRoot 统一承载所有模块，
	// 这样移动预览 Actor 或把局部 Anchor 转成世界坐标都只有一个受控变换入口。
	PrimaryActorTick.bCanEverTick = false;
	GeneratedRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GeneratedRoot"));
	GeneratedRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(GeneratedRoot);
}

void AZeroEscapeRuntimeLevelGenerator::BeginPlay()
{
	Super::BeginPlay();

	// Blueprint ReceiveBeginPlay 在 Super 内可能已经显式生成；只从 Idle 自动触发一次。
	if (TriggerMode == EZeroEscapeGenerationTrigger::BeginPlay
		&& State == EZeroEscapeRuntimeGenerationState::Idle)
	{
		Generate();
	}
}

bool AZeroEscapeRuntimeLevelGenerator::Generate()
{
	return GenerateFromRequest(DefaultRequest);
}

bool AZeroEscapeRuntimeLevelGenerator::CanAcceptGenerationRequest() const
{
	// State 不是重入锁：FinishGeneration 会先提交 Ready/Failed，再在锁仍有效时广播完成事件。
	// 将完整的生命周期门禁集中在这里，调用者才能在移动玩家或清理旧场景之前安全预检。
	if (!IsInGameThread())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	return World != nullptr
		&& World->IsGameWorld()
		&& !IsRunningUserConstructionScript()
		&& !bGenerationInProgress
		&& !bEndingPlay;
}

bool AZeroEscapeRuntimeLevelGenerator::GenerateFromRequest(
	const FZeroEscapeGenerationRequest& Request)
{
	// 拒绝时不得先清掉旧场景；公开预检与真正入口共用同一组生命周期条件，避免语义漂移。
	if (!CanAcceptGenerationRequest())
	{
		return false;
	}

	TGuardValue<bool> GeneratingGuard(bGenerationInProgress, true);
	// 新请求采用 replace 语义：一旦被接受，旧 Plan 和旧场景先完整失效。
	// 之后任何阶段失败都停在 Failed 且保持空场景，绝不把新旧两局混合展示。
	ClearGeneratedSceneInternal();
	LastReport = FZeroEscapeGenerationReport();
	State = EZeroEscapeRuntimeGenerationState::Planning;

	FZeroEscapeGenerationReport Report;
	if (!IsValid(GeneratedRoot)
		|| !GeneratedRoot->IsRegistered()
		|| !LevelGen::IsFiniteUnitScaleTransform(GeneratedRoot->GetComponentTransform()))
	{
		Report.Stage = EZeroEscapeGenerationStage::Configuration;
		Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
		Report.Message = TEXT("GeneratedRoot 必须已注册，并具有有限 Unit Scale 世界 Transform。");
		FinishGeneration(false, Report, Request);
		return false;
	}

	if (!IsValid(GenerationProfile)
		|| !IsValid(ModuleCatalog)
		|| !IsValid(PresentationProfile))
	{
		Report.Stage = EZeroEscapeGenerationStage::Configuration;
		Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
		Report.Message = TEXT("GenerationProfile、ModuleCatalog 与 PresentationProfile 必须全部装配。");
		FinishGeneration(false, Report, Request);
		return false;
	}

	FString ConfigurationError;
	// 在产生随机数或创建组件前完成跨资产校验。配置错误不应该消耗 Attempt，
	// 也不应该因为 Seed 不同而表现成偶发的求解失败。
	if (!ValidateZeroEscapeGenerationAssetSet(
			*GenerationProfile,
			*ModuleCatalog,
			*PresentationProfile,
			ConfigurationError))
	{
		Report.Stage = EZeroEscapeGenerationStage::Configuration;
		Report.Failure = EZeroEscapeGenerationFailure::InvalidConfiguration;
		Report.Message = MoveTemp(ConfigurationError);
		FinishGeneration(false, Report, Request);
		return false;
	}

	LevelGen::FGenerationProfileSnapshot ProfileSnapshot;
	LevelGen::FModuleCatalogSnapshot CatalogSnapshot;
	// UObject/DataAsset 只在游戏线程边界读取一次；后续 Core 与 Layout Solver 只消费
	// 稳定排序的纯值快照，避免编辑器对象顺序或运行时资产状态渗入确定性算法。
	if (!LevelGen::BuildGenerationSnapshot(*GenerationProfile, ProfileSnapshot, Report)
		|| !LevelGen::BuildCatalogSnapshot(*ModuleCatalog, CatalogSnapshot, Report))
	{
		FinishGeneration(false, Report, Request);
		return false;
	}

	FZeroEscapeGenerationSignature Signature;
	// Signature 把请求与所有会影响结果的版本绑定。PresentationVersion 只标识完整运行，
	// 不改变抽象/布局 Hash，因此单纯换皮不会伪装成拓扑算法变化。
	if (!LevelGen::BuildGenerationSignature(
			Request,
			ProfileSnapshot,
			CatalogSnapshot,
			PresentationProfile->PresentationVersion,
			Signature,
			Report))
	{
		FinishGeneration(false, Report, Request);
		return false;
	}

	const double AbstractStartSeconds = FPlatformTime::Seconds();
	LevelGen::FAbstractLevelPlan AbstractPlan;
	// 第一阶段只回答“玩家应经过哪些节点、目标放在哪里”，不接触格子或具体 Mesh。
	// 未来通关规则变化优先停留在这里和 Profile，而不是改写 WFC 模块状态。
	if (!LevelGen::FGenerationCore::BuildAbstractPlan(
			Request,
			ProfileSnapshot,
			AbstractPlan,
			Report))
	{
		Report.Metrics.AbstractMilliseconds +=
			(FPlatformTime::Seconds() - AbstractStartSeconds) * 1000.0;
		FinishGeneration(false, Report, Request);
		return false;
	}
	Report.Metrics.AbstractMilliseconds +=
		(FPlatformTime::Seconds() - AbstractStartSeconds) * 1000.0;

	LevelGen::FLayoutRequest LayoutRequest;
	// 第二阶段把抽象意图映射到统一测量得到的逻辑网格。所有成本和边界来自快照，
	// 没有 SFCorridors 路径、Mesh 名或猜测性的 660cm 常量。
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
	LayoutRequest.CanonicalAbstractHash = LevelGen::ComputeCanonicalAbstractHash(AbstractPlan);
	LayoutRequest.AbstractPlan = &AbstractPlan;
	if (LayoutRequest.CanonicalAbstractHash == 0)
	{
		Report.Stage = EZeroEscapeGenerationStage::Progression;
		Report.Failure = EZeroEscapeGenerationFailure::SolverInvariantViolation;
		Report.Message = TEXT("规范抽象 Hash 构建失败。");
		FinishGeneration(false, Report, Request);
		return false;
	}

	const double AbstractMilliseconds = Report.Metrics.AbstractMilliseconds;
	FZeroEscapeGenerationReport LayoutReport;
	FZeroEscapeGeneratedLevelPlan Plan;
	// Solve 在纯数据中完成 Socket 放置、A*、WFC、封口和全局验证。
	// 在它成功前，世界里仍然没有任何本次请求的场景对象。
	if (!LevelGen::FLayoutSolver::Solve(
			LayoutRequest,
			CatalogSnapshot,
			ProfileSnapshot.SolverBudgets,
			Request.Seed,
			Plan,
			LayoutReport))
	{
		LayoutReport.Metrics.AbstractMilliseconds += AbstractMilliseconds;
		Report = MoveTemp(LayoutReport);
		FinishGeneration(false, Report, Request);
		return false;
	}
	LayoutReport.Metrics.AbstractMilliseconds += AbstractMilliseconds;
	Report = MoveTemp(LayoutReport);

	State = EZeroEscapeRuntimeGenerationState::Validating;
	// 把 Solver 输出视为不可信边界：再次核对请求签名，并重新计算规范 Hash。
	// 这能捕获遗漏字段、陈旧缓存或内部状态污染，而不是带着错误 Plan 进入实例化。
	if (!AreGenerationSignaturesEqual(Plan.Signature, Signature)
		|| Plan.CanonicalAbstractHash != LayoutRequest.CanonicalAbstractHash
		|| Plan.CanonicalLayoutHash == 0
		|| LevelGen::ComputeCanonicalLayoutHash(Plan) != Plan.CanonicalLayoutHash)
	{
		Report.Stage = EZeroEscapeGenerationStage::GlobalValidation;
		Report.Failure = EZeroEscapeGenerationFailure::SolverInvariantViolation;
		Report.Message = TEXT("Layout 输出没有保留本次请求的完整 Signature 或规范 Hash。");
		FinishGeneration(false, Report, Request);
		return false;
	}

	const double InstantiationStartSeconds = FPlatformTime::Seconds();
	State = EZeroEscapeRuntimeGenerationState::Instantiating;
	// 只有完整 Plan 通过验证才允许触碰场景。Instantiate 会立即登记每个新组件，
	// 所以任一检查点失败后 ClearGeneratedSceneInternal 都能回滚全部已创建对象。
	if (!InstantiateValidatedPlan(Plan, Report))
	{
		Report.Metrics.InstantiationMilliseconds +=
			(FPlatformTime::Seconds() - InstantiationStartSeconds) * 1000.0;
		ClearGeneratedSceneInternal();
		FinishGeneration(false, Report, Request);
		return false;
	}
	if (bEndingPlay)
	{
		// EndPlay 可能由组件注册相关回调间接触发。此时不提交 Plan，也不广播完成事件。
		ClearGeneratedSceneInternal();
		return false;
	}
	Report.Metrics.InstantiationMilliseconds +=
		(FPlatformTime::Seconds() - InstantiationStartSeconds) * 1000.0;

	LastPlan = MoveTemp(Plan);
	Report.Stage = EZeroEscapeGenerationStage::Instantiation;
	Report.Failure = EZeroEscapeGenerationFailure::None;
	Report.Message = FString::Printf(
		TEXT("PCG 生成成功：%d 个模块，%d 个玩法 Anchor。"),
		LastPlan.Modules.Num(),
		LastPlan.GameplayAnchors.Num());
	FinishGeneration(true, Report, Request);
	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::InstantiateValidatedPlan(
	const FZeroEscapeGeneratedLevelPlan& Plan,
	FZeroEscapeGenerationReport& InOutReport)
{
	// StagedComponents 是“尚未公开给导航”的本次提交集合；成员数组则是回滚账本。
	// 两者用途不同：前者控制提交顺序，后者保证失败和 EndPlay 都能完整清理。
	TArray<UHierarchicalInstancedStaticMeshComponent*> StagedComponents;
	TArray<bool> StagedNavigationFlags;
	TArray<const FZeroEscapePresentationBinding*> SortedBindings;
	SortedBindings.Reserve(PresentationProfile->Bindings.Num());
	for (const FZeroEscapePresentationBinding& Binding : PresentationProfile->Bindings)
	{
		SortedBindings.Add(&Binding);
	}
	SortedBindings.Sort(
		[](const FZeroEscapePresentationBinding& A, const FZeroEscapePresentationBinding& B)
		{
			return A.StableModuleId < B.StableModuleId;
		});
	// 绑定按 StableModuleId 排序后再创建组件，避免 DataAsset 数组重排影响对象创建顺序、
	// 调试输出或之后可能增加的确定性观测。

	int32 InstantiatedPlacementCount = 0;
	for (const FZeroEscapePresentationBinding* BindingPtr : SortedBindings)
	{
		if (BindingPtr == nullptr)
		{
			return FailInstantiation(InOutReport, INDEX_NONE, TEXT("NullBinding"));
		}
		const FZeroEscapePresentationBinding& Binding = *BindingPtr;

		TArray<const FZeroEscapePlacedModule*> MatchingModules;
		// 同一表现绑定的所有 Placement 合并到一个 HISM，减少组件与 Draw Call 数量。
		// Plan.Modules 已经是完整事实来源，表现层不再筛选或随机替换结构模块。
		for (const FZeroEscapePlacedModule& Module : Plan.Modules)
		{
			if (Module.StableModuleId == Binding.StableModuleId)
			{
				MatchingModules.Add(&Module);
			}
		}
		if (MatchingModules.IsEmpty())
		{
			continue;
		}

		switch (Binding.SpawnPolicy)
		{
		case EZeroEscapePresentationSpawnPolicy::InstancedStaticMesh:
		{
			UHierarchicalInstancedStaticMeshComponent* Component =
				NewObject<UHierarchicalInstancedStaticMeshComponent>(
					this,
					UHierarchicalInstancedStaticMeshComponent::StaticClass(),
					NAME_None,
					RF_Transient);
			if (Component == nullptr)
			{
				return FailInstantiation(InOutReport, Binding.StableModuleId, TEXT("NewObject.HISM"));
			}

			// NewObject 成功后立刻登记到回滚账本；从这一行起任何 return false 都能销毁它。
			GeneratedHismComponents.Add(Component);
			StagedComponents.Add(Component);
			StagedNavigationFlags.Add(Binding.bCanEverAffectNavigation);
			AddInstanceComponent(Component);
			Component->SetCanEverAffectNavigation(false);
			Component->SetupAttachment(GeneratedRoot);
			Component->SetRelativeTransform(FTransform::Identity);
			Component->SetMobility(
				GeneratedRoot->GetMobility() == EComponentMobility::Static
					? EComponentMobility::Static
					: EComponentMobility::Movable);
			Component->SetStaticMesh(Binding.StaticMesh);
			if (Component->GetStaticMesh() != Binding.StaticMesh)
			{
				return FailInstantiation(InOutReport, Binding.StableModuleId, TEXT("SetStaticMesh.HISM"));
			}

			Component->SetCollisionProfileName(Binding.CollisionProfileName, false);

			TArray<FTransform> LocalTransforms;
			LocalTransforms.Reserve(MatchingModules.Num());
			for (const FZeroEscapePlacedModule* Module : MatchingModules)
			{
				// PivotCorrection 只把 Asset Local 适配到 Logical Module Local；再与模块变换合成。
				// HISM 实例保持在 GeneratedRoot 局部空间，Actor 世界变换只由父组件统一施加一次。
				LocalTransforms.Add(LevelGen::MakePresentationLocalTransform(
					Binding.PivotCorrection,
					Module->LocalTransform));
			}
			Component->PreAllocateInstancesMemory(LocalTransforms.Num());
			const TArray<int32> AddedIndices = Component->AddInstances(
				LocalTransforms,
				true,
				false,
				false);
			if (AddedIndices.Num() != LocalTransforms.Num()
				|| Component->GetInstanceCount() != LocalTransforms.Num())
			{
				return FailInstantiation(InOutReport, Binding.StableModuleId, TEXT("AddInstances.HISM"));
			}
			InstantiatedPlacementCount += MatchingModules.Num();
			break;
		}

		case EZeroEscapePresentationSpawnPolicy::Actor:
			// 首个可验证版本仅承诺纯 HISM 事务。Actor 的 Construction Script/BeginPlay
			// 会在半场景中产生不可回滚副作用，待引入受控包装基类与显式 Activate 后再开放。
			return FailInstantiation(
				InOutReport,
				Binding.StableModuleId,
				TEXT("ActorPolicyNotEnabled"));

		default:
			return FailInstantiation(InOutReport, Binding.StableModuleId, TEXT("UnknownSpawnPolicy"));
		}
	}

	if (InstantiatedPlacementCount != Plan.Modules.Num())
	{
		// 覆盖数必须精确相等；少绑定会形成不可走的洞，多计数则说明 Stable Id/分组不变量失效。
		return FailInstantiation(InOutReport, INDEX_NONE, TEXT("PresentationCoverage"));
	}

	// 所有组件先完成纯内存配置，再统一注册；注册完成前始终禁止导航影响，
	// 避免求解/配置失败把半张地图暴露给导航系统。
	for (UHierarchicalInstancedStaticMeshComponent* Component : StagedComponents)
	{
		if (!IsValid(Component))
		{
			return FailInstantiation(InOutReport, INDEX_NONE, TEXT("StagedHISM.Invalid"));
		}
		Component->RegisterComponent();
		if (!Component->IsRegistered()
			|| Component->GetAttachParent() != GeneratedRoot)
		{
			return FailInstantiation(InOutReport, INDEX_NONE, TEXT("Register.HISM"));
		}
	}
	for (int32 Index = 0; Index < StagedComponents.Num(); ++Index)
	{
		// 只有全部组件注册成功后才恢复作者配置的导航影响。导航系统因此只会看到完整地图，
		// 不会在半事务状态下为部分走廊重建数据。
		StagedComponents[Index]->SetCanEverAffectNavigation(StagedNavigationFlags[Index]);
	}
	return true;
}

bool AZeroEscapeRuntimeLevelGenerator::ClearGeneratedScene()
{
	// Clear 与 Generate 使用同一线程/世界/构造脚本边界，保证销毁不会和生成或对象构造交错。
	if (!IsInGameThread())
	{
		return false;
	}
	UWorld* World = GetWorld();
	if (World == nullptr
		|| !World->IsGameWorld()
		|| IsRunningUserConstructionScript())
	{
		return false;
	}
	if (bGenerationInProgress || bEndingPlay)
	{
		return false;
	}

	TGuardValue<bool> ClearingGuard(bGenerationInProgress, true);
	ClearGeneratedSceneInternal();
	return true;
}

void AZeroEscapeRuntimeLevelGenerator::ClearGeneratedSceneInternal()
{
	// 先让公开查询失效并转移追踪数组；销毁回调即使反向访问，也看不到半清理成员状态。
	State = EZeroEscapeRuntimeGenerationState::Idle;
	LastPlan = FZeroEscapeGeneratedLevelPlan();
	TArray<TObjectPtr<AActor>> ActorsToDestroy = MoveTemp(GeneratedActors);
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ComponentsToDestroy =
		MoveTemp(GeneratedHismComponents);

	for (int32 Index = ActorsToDestroy.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = ActorsToDestroy[Index];
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
		{
			Actor->Destroy();
		}
	}

	for (int32 Index = ComponentsToDestroy.Num() - 1; Index >= 0; --Index)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = ComponentsToDestroy[Index];
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
}

void AZeroEscapeRuntimeLevelGenerator::FinishGeneration(
	const bool bSuccess,
	const FZeroEscapeGenerationReport& Report,
	const FZeroEscapeGenerationRequest& Request)
{
	if (bEndingPlay)
	{
		return;
	}
	LastReport = Report;
	State = bSuccess
		? EZeroEscapeRuntimeGenerationState::Ready
		: EZeroEscapeRuntimeGenerationState::Failed;

	// 先提交 State、LastReport 和（成功时）LastPlan，再生成日志。这样日志中的 HasStart/HasExit、
	// HISM 实例数和 Hash 描述的是 Delegate 监听者即将看到的同一个终态，而不是中间快照。
	int32 ValidHismComponentCount = 0;
	int32 HismInstanceCount = 0;
	for (const UHierarchicalInstancedStaticMeshComponent* Component : GeneratedHismComponents)
	{
		if (IsValid(Component))
		{
			++ValidHismComponentCount;
			HismInstanceCount += Component->GetInstanceCount();
		}
	}

	FTransform StartWorldTransform = FTransform::Identity;
	FTransform ExitWorldTransform = FTransform::Identity;
	const bool bHasStart = GetGeneratedStartWorldTransform(StartWorldTransform);
	const bool bHasExit = GetGeneratedExitWorldTransform(ExitWorldTransform);
	const UWorld* World = GetWorld();
	const FString WorldPackage = World != nullptr && World->GetOutermost() != nullptr
		? World->GetOutermost()->GetName()
		: TEXT("None");
	const FString SingleLineMessage = MakeSingleLineLogValue(LastReport.Message);

	// Schema=1 冻结字段名与“一次对外提交的终态恰好一条结果”的约定。EndPlay 中止沿用既有
	// 生命周期契约：既不提交终态、不广播 Delegate，也不伪造完成日志。正常结果能证明生成事务
	// 在指定 World 中到达终态，但不能代替材质、视觉接缝、碰撞、导航、性能或玩家走通验收。
	UE_LOG(
		LogZeroEscapePCG,
		Display,
		TEXT("ZE_PCG_RESULT Schema=1 Success=%d State=%s IsGameWorld=%d IsPIE=%d WorldPackage=\"%s\" Seed=%d Difficulty=%s Flow=\"%s\" Stage=%s Failure=%s Attempts=%d AbstractHash=%lld LayoutHash=%lld Modules=%d PortalConnections=%d GameplayAnchors=%d Objectives=%d HISMComponents=%d HISMInstances=%d HasStart=%d HasExit=%d WfcActiveCells=%d WfcVariants=%d WfcObservations=%d WfcContradictions=%d WfcBacktracks=%d EffectiveWFC=%d AbstractMs=%.3f SocketMs=%.3f AStarMs=%.3f WfcMs=%.3f ValidationMs=%.3f InstantiationMs=%.3f RelatedStableId=%d Actual=%d Limit=%d Message=\"%s\""),
		bSuccess ? 1 : 0,
		*GetStableEnumName(State),
		World != nullptr && World->IsGameWorld() ? 1 : 0,
		World != nullptr && World->WorldType == EWorldType::PIE ? 1 : 0,
		*WorldPackage,
		Request.Seed,
		*GetStableEnumName(Request.Difficulty),
		*Request.FlowProfileId.ToString(),
		*GetStableEnumName(LastReport.Stage),
		*GetStableEnumName(LastReport.Failure),
		LastReport.AttemptsExecuted,
		static_cast<long long>(LastPlan.CanonicalAbstractHash),
		static_cast<long long>(LastPlan.CanonicalLayoutHash),
		LastPlan.Modules.Num(),
		LastPlan.PortalConnections.Num(),
		LastPlan.GameplayAnchors.Num(),
		LastPlan.ObjectiveBindings.Num(),
		ValidHismComponentCount,
		HismInstanceCount,
		bHasStart ? 1 : 0,
		bHasExit ? 1 : 0,
		LastReport.Metrics.WfcActiveCellCount,
		LastReport.Metrics.WfcVariantCount,
		LastReport.Metrics.WfcObservationCount,
		LastReport.Metrics.WfcContradictionCount,
		LastReport.Metrics.WfcBacktrackCount,
		LastReport.Metrics.bHadEffectiveWfcChoice ? 1 : 0,
		LastReport.Metrics.AbstractMilliseconds,
		LastReport.Metrics.SocketMilliseconds,
		LastReport.Metrics.AStarMilliseconds,
		LastReport.Metrics.WfcMilliseconds,
		LastReport.Metrics.ValidationMilliseconds,
		LastReport.Metrics.InstantiationMilliseconds,
		LastReport.RelatedStableId,
		LastReport.ActualValue,
		LastReport.LimitValue,
		*SingleLineMessage);

	// 广播发生时 bGenerationInProgress 仍由外层 TGuardValue 保持为 true。
	// 监听者可以读取报告和 Anchor，但不能在回调里递归 Generate/Clear 破坏当前终态。
	OnGenerationFinished.Broadcast(bSuccess, LastReport);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedAnchorWorldTransform(
	const int32 StableAnchorInstanceId,
	const EZeroEscapeGameplayAnchorType ExpectedType,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	if (State != EZeroEscapeRuntimeGenerationState::Ready
		|| StableAnchorInstanceId < 0
		|| !IsValid(GeneratedRoot))
	{
		return false;
	}

	for (const FZeroEscapeGeneratedAnchor& Anchor : LastPlan.GameplayAnchors)
	{
		if (Anchor.StableAnchorInstanceId == StableAnchorInstanceId
			&& Anchor.Type == ExpectedType)
		{
			// UE 的相对变换组合约定由自动化 Test 0 冻结；不能交换乘法顺序。
			OutTransform = Anchor.LocalTransform * GeneratedRoot->GetComponentTransform();
			return true;
		}
	}
	return false;
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedStartWorldTransform(FTransform& OutTransform) const
{
	return GetGeneratedAnchorWorldTransform(
		LastPlan.PlayerSpawnAnchorInstanceId,
		EZeroEscapeGameplayAnchorType::PlayerSpawn,
		OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedExitWorldTransform(FTransform& OutTransform) const
{
	return GetGeneratedAnchorWorldTransform(
		LastPlan.ExitAnchorInstanceId,
		EZeroEscapeGameplayAnchorType::Exit,
		OutTransform);
}

bool AZeroEscapeRuntimeLevelGenerator::GetGeneratedObjectiveWorldTransforms(
	TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Reset();
	if (State != EZeroEscapeRuntimeGenerationState::Ready || !IsValid(GeneratedRoot))
	{
		return false;
	}

	const FTransform RootWorldTransform = GeneratedRoot->GetComponentTransform();
	// Finalize 已按 StableAnchorInstanceId 排序 GameplayAnchors，因此这里自然返回稳定顺序，
	// 玩法层无需依赖 UObject 名称或场景遍历顺序。
	for (const FZeroEscapeGeneratedAnchor& Anchor : LastPlan.GameplayAnchors)
	{
		if (Anchor.Type == EZeroEscapeGameplayAnchorType::Objective)
		{
			OutTransforms.Add(Anchor.LocalTransform * RootWorldTransform);
		}
	}
	return true;
}

void AZeroEscapeRuntimeLevelGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 先永久关闭本生命周期的入口，再清理对象，最后交给父类结束。
	// 即使 DestroyComponent/DestroyActor 触发外部回调，也不能复活生成器或发出完成广播。
	bEndingPlay = true;
	bGenerationInProgress = true;
	ClearGeneratedSceneInternal();
	Super::EndPlay(EndPlayReason);
}
