// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePrototypeGameMode.cpp
 * 职责：实现可回退的运行时磁力测试场，并保持现有关卡包不变。
 * 边界：只负责原型类选择和测试物体生成，不管理玩家输入上下文或正式逃亡流程。
 * 状态 Owner：原型生成开关属于 GameMode；输入模式由 AZeroEscapePlayerController 管理。
 */

#include "GameFlow/ZeroEscapePrototypeGameMode.h"

#include "Actors/Magnetism/MagneticPrototypeProp.h"
#include "Characters/ZeroEscapeCharacter.h"
#include "Engine/World.h"
#include "GameFlow/ZeroEscapePlayerController.h"
#include "UI/ZeroEscapeHUD.h"

/** 设置非空诊断后备；项目角色和磁性道具资产仍由 GameMode 蓝图负责装配。 */
AZeroEscapePrototypeGameMode::AZeroEscapePrototypeGameMode()
{
	// 保证 Pawn 类引用不为空；完整输入、表现和磁力资源仍必须由角色蓝图装配。
	DefaultPawnClass = AZeroEscapeCharacter::StaticClass();

	PlayerControllerClass = AZeroEscapePlayerController::StaticClass();
	HUDClass = AZeroEscapeHUD::StaticClass();

	// 蓝图可替换为带正式网格、材质和单物体参数的磁性道具子类。
	PrototypePropClass = AMagneticPrototypeProp::StaticClass();
}

/** 只启动临时测试物体队列；正式逃亡流程仍留给后续独立实现。 */
void AZeroEscapePrototypeGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (DefaultPawnClass == AZeroEscapeCharacter::StaticClass())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("GameMode 尚未配置 BP_ZeroEscapeCharacter；原生诊断后备不包含输入与磁力 DataAsset 装配。"));
	}

	if (bSpawnPrototypeProps)
	{
		SpawnPrototypeProps();
	}
}

/** 生成四个固定且可复现的物理案例；测试夹具不冒充正式 PCG。 */
void AZeroEscapePrototypeGameMode::SpawnPrototypeProps()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	UClass* ResolvedPropClass = PrototypePropClass.Get();
	if (!IsValid(ResolvedPropClass))
	{
		UE_LOG(LogTemp, Error, TEXT("磁性测试道具类未配置，已跳过原型测试物生成。"));
		return;
	}

	struct FPrototypePropCase
	{
		/** 为默认 PlayerStart 选择的世界坐标，保证测试物体进入初始视野。 */
		FVector Location;

		/** 用于近似箱体、铁板、长条和重块的非均匀 Actor 缩放。 */
		FVector Scale;

		/** 人工指定的测试质量，单位 kg。 */
		float MassKilograms;
	};

	const FPrototypePropCase TestCases[] =
	{
		{FVector(340.0f, -130.0f, 90.0f), FVector(0.65f, 0.65f, 0.65f), 5.0f},
		{FVector(500.0f, 80.0f, 100.0f), FVector(1.4f, 0.22f, 0.9f), 20.0f},
		{FVector(660.0f, -90.0f, 130.0f), FVector(0.45f, 0.45f, 1.45f), 45.0f},
		{FVector(820.0f, 130.0f, 100.0f), FVector(1.0f, 0.75f, 0.75f), 70.0f}
	};

	for (const FPrototypePropCase& TestCase : TestCases)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, TestCase.Location, TestCase.Scale);
		AMagneticPrototypeProp* SpawnedProp = World->SpawnActor<AMagneticPrototypeProp>(
			ResolvedPropClass,
			SpawnTransform);
		if (IsValid(SpawnedProp))
		{
			SpawnedProp->ConfigurePrototype(TestCase.Scale, TestCase.MassKilograms);
		}
	}
}
