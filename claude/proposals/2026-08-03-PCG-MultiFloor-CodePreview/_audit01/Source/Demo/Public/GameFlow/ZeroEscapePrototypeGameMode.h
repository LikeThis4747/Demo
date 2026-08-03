// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePrototypeGameMode.h
 * 职责：声明用于选择玩家角色、Controller 与 HUD 的临时可玩规则入口。
 * 边界：不生成或装配关卡内容，也不实现正式逃亡胜负、PCG 或追猎者规则。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "ZeroEscapePrototypeGameMode.generated.h"

/** 使用 Level0 已装配内容启动磁力交互测试场。 */
UCLASS()
class DEMO_API AZeroEscapePrototypeGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** 指定零号逃亡角色、专用 PlayerController 与轻量中心准星 HUD。 */
	AZeroEscapePrototypeGameMode();

protected:
	/** 关卡开始后检查角色蓝图是否完成资源装配。 */
	virtual void BeginPlay() override;
};
