// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePrototypeGameMode.cpp
 * 职责：选择原型使用的角色、Controller 与 HUD，并检查角色资源装配。
 * 边界：关卡物体由 Level0 持久拥有；本类不生成场景内容或管理玩家输入上下文。
 */

#include "GameFlow/ZeroEscapePrototypeGameMode.h"

#include "Characters/ZeroEscapeCharacter.h"
#include "GameFlow/ZeroEscapePlayerController.h"
#include "UI/ZeroEscapeHUD.h"

/** 设置非空诊断后备；完整角色资源仍由 GameMode 蓝图负责装配。 */
AZeroEscapePrototypeGameMode::AZeroEscapePrototypeGameMode()
{
	// 保证 Pawn 类引用不为空；完整输入、表现和磁力资源仍必须由角色蓝图装配。
	DefaultPawnClass = AZeroEscapeCharacter::StaticClass();

	PlayerControllerClass = AZeroEscapePlayerController::StaticClass();
	HUDClass = AZeroEscapeHUD::StaticClass();
}

/** 只检查角色蓝图装配；测试物体由 Level0 持久保存。 */
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
}
