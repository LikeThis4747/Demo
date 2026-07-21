// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticPrototypeProp.cpp
 * 职责：实现原生磁性物理道具，使基线玩法不依赖蓝图图表逻辑。
 */

#include "Actors/Magnetism/MagneticPrototypeProp.h"

#include "Components/Magnetism/MagneticObjectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

/** 装配使用引擎碰撞的 Chaos 刚体与可复用磁性标记组件。 */
AMagneticPrototypeProp::AMagneticPrototypeProp()
{
	PrimaryActorTick.bCanEverTick = false;

	MagneticBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MagneticBody"));
	SetRootComponent(MagneticBody);
	MagneticBody->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	MagneticBody->SetSimulatePhysics(true);
	MagneticBody->SetLinearDamping(0.15f);
	MagneticBody->SetAngularDamping(0.35f);

	MagneticObject = CreateDefaultSubobject<UMagneticObjectComponent>(TEXT("MagneticObject"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MagneticBody->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PrototypeMaterial(
		TEXT("/Game/ZeroEscape/Interaction/Magnetism/M_MagneticPrototype.M_MagneticPrototype"));
	if (PrototypeMaterial.Succeeded())
	{
		MagneticBody->SetMaterial(0, PrototypeMaterial.Object);
	}
}

/** 允许原型 GameMode 使用同一 Actor 类生成外形不同的铁板、箱体与长条测试体。 */
void AMagneticPrototypeProp::ConfigurePrototype(const FVector& InScale, const float InMassKilograms)
{
	SetActorScale3D(InScale);
	InitialMassKilograms = FMath::Max(1.0f, InMassKilograms);
	ApplyConfiguredMass();
}

/** 确保运行时刚体创建完成后再应用质量覆盖值。 */
void AMagneticPrototypeProp::BeginPlay()
{
	Super::BeginPlay();
	ApplyConfiguredMass();
}

/** 通过引擎质量覆盖接口更新 Chaos，不重复实现质量或惯性积分。 */
void AMagneticPrototypeProp::ApplyConfiguredMass()
{
	if (IsValid(MagneticBody))
	{
		MagneticBody->SetMassOverrideInKg(NAME_None, InitialMassKilograms, true);
	}
}
