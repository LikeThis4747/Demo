// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PhysicsControlHitResponseComponent.cpp
 * 职责：把 Manny Physics Body 的真实 Chaos 命中映射为短时局部 Physics Control 反应。
 * 边界：不做全局搜索、伤害判定、AI 状态切换或测试专用输入；所有运行态在 EndPlay 清理。
 */

#include "Components/Physics/PhysicsControlHitResponseComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsControlComponent.h"
#include "PhysicsControlData.h"
#include "PhysicsControlLimbData.h"
#include "Physics/DemoHitTags.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsControlHit, Log, All);

/** 创建事件驱动组件；明确禁用自定义 Tick。 */
UPhysicsControlHitResponseComponent::UPhysicsControlHitResponseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/** 保存 Owner 显式注入的依赖；不触发初始化或资产加载。 */
void UPhysicsControlHitResponseComponent::Configure(
	USkeletalMeshComponent* InControlledMesh,
	UPhysicsControlComponent* InPhysicsControl,
	UPhysicsControlHitTuningData* InTuningData)
{
	ControlledMesh = InControlledMesh;
	PhysicsControl = InPhysicsControl;
	TuningData = InTuningData;
}

/** 验证依赖、建立运行时 Sets，并让 Manny Physics Body 接收真实物理道具命中。 */
void UPhysicsControlHitResponseComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(ControlledMesh) || !IsValid(PhysicsControl) || !IsValid(TuningData))
	{
		UE_LOG(
			LogPhysicsControlHit,
			Error,
			TEXT("%s is missing its mesh, Physics Control component or tuning asset."),
			*GetNameSafe(GetOwner()));
		return;
	}

	if (!InitializePhysicsControl())
	{
		UE_LOG(
			LogPhysicsControlHit,
			Error,
			TEXT("%s could not initialize its local Physics Control limbs."),
			*GetNameSafe(GetOwner()));
		ResetRuntimeSetup();
		return;
	}

	// Kinematic Manny bodies receive prop contacts while the capsule remains the movement authority.
	ControlledMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ControlledMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	ControlledMesh->SetAllBodiesNotifyRigidBodyCollision(true);
	ControlledMesh->OnComponentHit.AddUniqueDynamic(this, &UPhysicsControlHitResponseComponent::HandleMeshHit);

	UE_LOG(
		LogPhysicsControlHit,
		Log,
		TEXT("%s local Physics Control hit reaction is ready."),
		*GetNameSafe(GetOwner()));
}

/** 解绑 Delegate、停止 Timer、恢复动画并销毁本组件创建的运行时 Physics Control 对象。 */
void UPhysicsControlHitResponseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(ControlledMesh))
	{
		ControlledMesh->OnComponentHit.RemoveDynamic(this, &UPhysicsControlHitResponseComponent::HandleMeshHit);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingHitTimer);
	}

	RestoreActiveRegion();
	ResetRuntimeSetup();
	Super::EndPlay(EndPlayReason);
}

/** 按 DataAsset 顺序创建 limb、ParentSpace Controls、Body Modifiers 和骨骼区域映射。 */
bool UPhysicsControlHitResponseComponent::InitializePhysicsControl()
{
	TArray<FPhysicsControlLimbSetupData> LimbSetups;
	const UEnum* RegionEnum = StaticEnum<EPhysicsControlHitRegion>();

	for (const FPhysicsControlHitRegionSetup& Setup : TuningData->Regions)
	{
		// Head 即使禁用也保留为排除 limb，防止其 Physics Body 被 Torso 吸收。
		if (!Setup.bReactionEnabled && Setup.Region != EPhysicsControlHitRegion::Head)
		{
			continue;
		}

		FPhysicsControlLimbSetupData& LimbSetup = LimbSetups.AddDefaulted_GetRef();
		LimbSetup.LimbName = FName(*RegionEnum->GetNameStringByValue(static_cast<int64>(Setup.Region)));
		LimbSetup.StartBone = Setup.StartBone;
	}

	const TMap<FName, FPhysicsControlLimbBones> LimbBones =
		PhysicsControl->GetLimbBonesFromSkeletalMesh(ControlledMesh, LimbSetups);
	if (LimbBones.Num() != LimbSetups.Num())
	{
		return false;
	}

	FPhysicsControlData ParentControlData;
	ParentControlData.bEnabled = false;
	ParentControlData.LinearStrength = TuningData->LinearStrength;
	ParentControlData.LinearDampingRatio = TuningData->DampingRatio;
	ParentControlData.MaxForce = TuningData->MaximumControlForce;
	ParentControlData.AngularStrength = TuningData->AngularStrength;
	ParentControlData.AngularDampingRatio = TuningData->DampingRatio;
	ParentControlData.MaxTorque = TuningData->MaximumControlTorque;
	ParentControlData.bUseSkeletalAnimation = true;
	ParentControlData.bDisableCollision = true;
	ParentControlData.bOnlyControlChildObject = true;

	// UE5.8 当前签名的最后一个参数是 bUpdateKinematicFromSimulation；关闭可避免恢复目标被模拟姿态污染。
	const FPhysicsControlModifierData BodyModifierData(
		EPhysicsMovementType::Kinematic,
		ECollisionEnabled::QueryAndPhysics,
		TuningData->GravityMultiplier,
		0.0f,
		EPhysicsControlKinematicTargetSpace::OffsetInBoneSpace,
		false);

	TMap<FName, FPhysicsControlLimbBones> ReactiveLimbBones;
	for (const FPhysicsControlHitRegionSetup& Setup : TuningData->Regions)
	{
		if (!Setup.bReactionEnabled)
		{
			continue;
		}

		const FName LimbName(*RegionEnum->GetNameStringByValue(static_cast<int64>(Setup.Region)));
		const FPhysicsControlLimbBones* Bones = LimbBones.Find(LimbName);
		if (!Bones || Bones->BoneNames.IsEmpty())
		{
			return false;
		}
		ReactiveLimbBones.Add(LimbName, *Bones);
	}
	if (ReactiveLimbBones.IsEmpty())
	{
		return false;
	}

	FPhysicsControlNames AllParentControls;
	FPhysicsControlNames AllBodyModifiers;
	const TMap<FName, FPhysicsControlNames> LimbParentControls = PhysicsControl->CreateControlsFromLimbBones(
		AllParentControls,
		ReactiveLimbBones,
		EPhysicsControlType::ParentSpace,
		ParentControlData);
	const TMap<FName, FPhysicsControlNames> LimbBodyModifiers = PhysicsControl->CreateBodyModifiersFromLimbBones(
		AllBodyModifiers,
		ReactiveLimbBones,
		BodyModifierData);

	for (const FPhysicsControlHitRegionSetup& Setup : TuningData->Regions)
	{
		if (!Setup.bReactionEnabled)
		{
			continue;
		}

		const FName LimbName(*RegionEnum->GetNameStringByValue(static_cast<int64>(Setup.Region)));
		const FPhysicsControlLimbBones& Bones = ReactiveLimbBones[LimbName];
		const FPhysicsControlNames* Controls = LimbParentControls.Find(LimbName);
		const FPhysicsControlNames* Modifiers = LimbBodyModifiers.Find(LimbName);
		if (!Controls || Controls->Names.IsEmpty() || !Modifiers || Modifiers->Names.IsEmpty())
		{
			return false;
		}

		FRegionRuntime Runtime;
		Runtime.LimbName = LimbName;
		Runtime.ParentControlSet = FName(*FString::Printf(TEXT("ParentSpace_%s"), *LimbName.ToString()));
		Runtime.ImpulseScale = Setup.ImpulseScale;
		Runtime.BodyBones = Bones.BoneNames;

		for (const FName BodyBone : Runtime.BodyBones)
		{
			BodyBoneToRegion.Add(BodyBone, Setup.Region);
		}
		RegionRuntimes.Add(Setup.Region, MoveTemp(Runtime));
	}

	return true;
}

/** 销毁 Controls/Modifiers 并清空所有由 BeginPlay 建立的运行态。 */
void UPhysicsControlHitResponseComponent::ResetRuntimeSetup()
{
	if (IsValid(PhysicsControl))
	{
		PhysicsControl->DestroyAllControlsAndBodyModifiers();
	}

	RegionRuntimes.Reset();
	BodyBoneToRegion.Reset();
	ActiveRegion.Reset();
	PendingHit = FPendingHit();
}

/** 筛选其他模拟组件的命中，并在同一 Chaos 批次只保留缩放后最强接触。 */
void UPhysicsControlHitResponseComponent::HandleMeshHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	// 只认被标记为攻击性抛射物（玩家投掷/陷阱击飞）的物体，避免追猎者自身撞到普通物理物体误触发受击。
	if (!IsValid(OtherComponent)
		|| !IsValid(OtherActor)
		|| OtherActor == GetOwner()
		|| !OtherActor->ActorHasTag(DemoHitTags::AttackProjectile())
		|| Hit.MyBoneName.IsNone())
	{
		return;
	}

	const EPhysicsControlHitRegion* Region = BodyBoneToRegion.Find(Hit.MyBoneName);
	const FRegionRuntime* Runtime = Region ? RegionRuntimes.Find(*Region) : nullptr;
	if (!Region || !Runtime)
	{
		return;
	}

	const float ScaledImpulse = NormalImpulse.Size() * TuningData->CollisionImpulseScale * Runtime->ImpulseScale;
	const float ReactionMagnitude = FMath::Min(ScaledImpulse, TuningData->MaximumImpulse);
	if (ReactionMagnitude < TuningData->MinimumReactionImpulse || Hit.ImpactNormal.IsNearlyZero())
	{
		return;
	}

	FVector ReactionDirection = NormalImpulse.GetSafeNormal();
	// Chaos 保留总冲量轴，ImpactNormal 用于校正接收组件一侧的方向符号。
	if (FVector::DotProduct(ReactionDirection, Hit.ImpactNormal) < 0.0f)
	{
		ReactionDirection *= -1.0f;
	}
	const FVector ReactionImpulse = ReactionDirection * ReactionMagnitude;

	const bool bHadPendingHit = PendingHit.Score > 0.0f;
	if (ScaledImpulse > PendingHit.Score)
	{
		PendingHit.Region = *Region;
		PendingHit.BodyBone = Hit.MyBoneName;
		PendingHit.ImpactPoint = Hit.ImpactPoint;
		PendingHit.WorldImpulse = ReactionImpulse;
		PendingHit.Score = ScaledImpulse;
	}

	if (!bHadPendingHit)
	{
		PendingHitTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(
			this,
			&UPhysicsControlHitResponseComponent::ProcessPendingHit);
	}
}

/** 处理本批次最强接触，按需切区、施加首个手动冲量，并刷新恢复 Timer。 */
void UPhysicsControlHitResponseComponent::ProcessPendingHit()
{
	PendingHitTimer.Invalidate();
	if (PendingHit.Score <= 0.0f)
	{
		return;
	}

	const FPendingHit Hit = PendingHit;
	PendingHit = FPendingHit();

	const FRegionRuntime* Runtime = RegionRuntimes.Find(Hit.Region);
	if (!Runtime)
	{
		return;
	}

	const bool bRegionWasActive = ActiveRegion.IsSet() && ActiveRegion.GetValue() == Hit.Region;
	if (!bRegionWasActive)
	{
		RestoreActiveRegion();
		ActivateRegion(Hit.Region, *Runtime);
	}

	// 新区域在本次碰撞前仍是 Kinematic，需要补施一次冲量；已活动区域直接使用 Chaos 本身的冲量。
	const bool bAppliedManualImpulse = !bRegionWasActive;
	if (bAppliedManualImpulse)
	{
		ControlledMesh->AddImpulseAtLocation(Hit.WorldImpulse, Hit.ImpactPoint, Hit.BodyBone);
	}

	GetWorld()->GetTimerManager().SetTimer(
		RecoveryTimer,
		this,
		&UPhysicsControlHitResponseComponent::RestoreActiveRegion,
		TuningData->ReactionDuration,
		false);

	UE_LOG(
		LogPhysicsControlHit,
		Log,
		TEXT("%s physics hit: region=%s bone=%s impulse=%s source=%s"),
		*GetNameSafe(GetOwner()),
		*StaticEnum<EPhysicsControlHitRegion>()->GetNameStringByValue(static_cast<int64>(Hit.Region)),
		*Hit.BodyBone.ToString(),
		*Hit.WorldImpulse.ToCompactString(),
		bAppliedManualImpulse ? TEXT("manual") : TEXT("chaos"));

	// 冲量在角色本地空间的投影给出命中方向，广播给 Owner 选受击动画。冲量是"推追猎者的方向"。
	if (OnPhysicsHit.IsBound())
	{
		const FVector LocalImpulse =
			ControlledMesh->GetComponentToWorld().InverseTransformVectorNoScale(Hit.WorldImpulse);
		EPhysicsHitDirection HitDirection = EPhysicsHitDirection::Front;
		if (FMath::Abs(LocalImpulse.Y) > FMath::Abs(LocalImpulse.X))
		{
			HitDirection = LocalImpulse.Y > 0.0f ? EPhysicsHitDirection::Right : EPhysicsHitDirection::Left;
		}
		OnPhysicsHit.Broadcast(HitDirection);
	}
}

/** 将指定区域的 Physics Body 切到 Simulated，设置全物理混合并启用 ParentSpace Controls。 */
void UPhysicsControlHitResponseComponent::ActivateRegion(
	const EPhysicsControlHitRegion Region,
	const FRegionRuntime& Runtime)
{
	ActiveRegion = Region;

	for (const FName BodyBone : Runtime.BodyBones)
	{
		ControlledMesh->SetBodySimulatePhysics(BodyBone, true);
	}

	// UE5.8 的三个 Set API 返回 void；Set 的存在性已在 InitializePhysicsControl 中通过创建结果验证。
	PhysicsControl->SetBodyModifiersInSetMovementType(Runtime.LimbName, EPhysicsMovementType::Simulated);
	PhysicsControl->SetBodyModifiersInSetPhysicsBlendWeight(Runtime.LimbName, 1.0f);
	PhysicsControl->SetControlsInSetEnabled(Runtime.ParentControlSet, true);
}

/** 停止恢复 Timer，关闭当前区域控制与模拟，清零速度并交还动画权威。 */
void UPhysicsControlHitResponseComponent::RestoreActiveRegion()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecoveryTimer);
	}

	if (!ActiveRegion.IsSet())
	{
		return;
	}

	if (!IsValid(ControlledMesh) || !IsValid(PhysicsControl))
	{
		ActiveRegion.Reset();
		return;
	}

	const FRegionRuntime* Runtime = RegionRuntimes.Find(ActiveRegion.GetValue());
	if (!Runtime)
	{
		ActiveRegion.Reset();
		return;
	}

	PhysicsControl->SetControlsInSetEnabled(Runtime->ParentControlSet, false);
	PhysicsControl->SetBodyModifiersInSetPhysicsBlendWeight(Runtime->LimbName, 0.0f);
	PhysicsControl->SetBodyModifiersInSetMovementType(Runtime->LimbName, EPhysicsMovementType::Kinematic);

	for (const FName BodyBone : Runtime->BodyBones)
	{
		ControlledMesh->SetPhysicsLinearVelocity(FVector::ZeroVector, false, BodyBone);
		ControlledMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector, false, BodyBone);
		ControlledMesh->SetBodySimulatePhysics(BodyBone, false);
	}

	ActiveRegion.Reset();
}
