// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticFractureActor.cpp
 * 职责：实现 Geometry Collection 替身的初始运动、显式解簇、碎片碰撞和双层清理。
 * 边界：Remove On Break 的逐叶时序属于 GC 资产；本 Actor 的 LifeSpan 只处理异常残留。
 * 状态 Owner：本 Actor 不产生攻击 Hit，不参与导航或抓取，不创建常驻 Tick。
 */

#include "Actors/Magnetism/MagneticFractureActor.h"

#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogMagneticFractureActor, Log, All);

namespace UE::ZeroEscape::Magnetism
{
	/** 防止 NaN/Inf 进入 Chaos 初始速度并污染整个 Geometry Collection 求解。 */
	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}
}

/** 固定短命碎片的碰撞、伤害、Removal 和导航边界，Blueprint 不再维护第二份真相。 */
AMagneticFractureActor::AMagneticFractureActor()
{
	PrimaryActorTick.bCanEverTick = false;

	GeometryCollection = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("GeometryCollection"));
	SetRootComponent(GeometryCollection);
	GeometryCollection->SetMobility(EComponentMobility::Movable);
	GeometryCollection->SetSimulatePhysics(true);
	GeometryCollection->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GeometryCollection->SetCollisionObjectType(ECC_PhysicsBody);
	GeometryCollection->SetCollisionResponseToAllChannels(ECR_Ignore);
	GeometryCollection->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	GeometryCollection->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	GeometryCollection->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GeometryCollection->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GeometryCollection->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	GeometryCollection->SetGenerateOverlapEvents(false);
	GeometryCollection->SetNotifyRigidBodyCollision(false);
	GeometryCollection->SetUseCCD(false);
	GeometryCollection->SetCanEverAffectNavigation(false);

	GeometryCollection->bEnableDamageFromCollision = false;
	GeometryCollection->bAllowRemovalOnSleep = false;
	GeometryCollection->bAllowRemovalOnBreak = true;
}

/** 在组件注册和 Chaos 代理创建前写入用户指定的初始线速度与角速度。 */
void AMagneticFractureActor::SetInheritedMotion(
	const FVector& LinearVelocity,
	const FVector& AngularVelocityRadians)
{
	if (!IsValid(GeometryCollection))
	{
		return;
	}

	const bool bLinearValid = UE::ZeroEscape::Magnetism::IsFiniteVector(LinearVelocity);
	const bool bAngularValid = UE::ZeroEscape::Magnetism::IsFiniteVector(AngularVelocityRadians);
	if (!bLinearValid || !bAngularValid)
	{
		UE_LOG(LogMagneticFractureActor, Warning,
			TEXT("%s received non-finite inherited motion; invalid values were replaced with zero."),
			*GetNameSafe(this));
	}

	GeometryCollection->InitialVelocityType =
		EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined;
	GeometryCollection->InitialLinearVelocity = bLinearValid ? LinearVelocity : FVector::ZeroVector;
	GeometryCollection->InitialAngularVelocity = bAngularValid
		? AngularVelocityRadians
		: FVector::ZeroVector;
}

/** RestCollection 缺失时自毁并让调用方保留原物体；有效时立即解簇并启动异常兜底。 */
void AMagneticFractureActor::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(GeometryCollection) || !IsValid(GeometryCollection->GetRestCollection()))
	{
		UE_LOG(LogMagneticFractureActor, Error,
			TEXT("%s has no valid Geometry Collection RestCollection."),
			*GetNameSafe(this));
		Destroy();
		return;
	}

	SetLifeSpan(FMath::Max(SafetyLifetimeSeconds, 6.0f));
	GeometryCollection->CrumbleActiveClusters();
	UE_LOG(LogMagneticFractureActor, Verbose,
		TEXT("%s crumbled active clusters with %.2fs safety lifetime."),
		*GetNameSafe(this),
		GetLifeSpan());
}
