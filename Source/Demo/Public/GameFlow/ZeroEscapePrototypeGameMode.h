// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePrototypeGameMode.h
 * 职责：声明用于角色、HUD 与磁性道具验证的临时可玩测试夹具。
 * 边界：只拥有原型测试物生成，不实现正式逃亡胜负、PCG 或追猎者规则。
 * 状态 Owner：测试物生成开关由本 GameMode 管理；玩家输入与磁力状态分别由专用类管理。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "ZeroEscapePrototypeGameMode.generated.h"

/** 在不修改现有 Level0 资产的前提下启动磁力交互测试场。 */
UCLASS()
class DEMO_API AZeroEscapePrototypeGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** 指定零号逃亡角色、专用 PlayerController 与轻量中心准星 HUD。 */
	AZeroEscapePrototypeGameMode();

protected:
	/** 当前关卡开始后按开关生成一组固定且可复现的磁性测试物体。 */
	virtual void BeginPlay() override;

private:
	/** 创建不同形状与质量的测试案例，用于选取、持有稳定性和投掷手感验收。 */
	void SpawnPrototypeProps();

	/**
	 * 对应 C++ 属性 bSpawnPrototypeProps；初始值：true。
	 * 关闭后不生成临时测试队列，适用于未来已有正式关卡内容时避免重复生成。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "原型")
	bool bSpawnPrototypeProps = true;
};
