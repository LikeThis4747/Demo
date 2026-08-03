// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayPopulator.h
 * 职责：由 GameMode 在角色与出口就绪后显式调用，按 Profile 确定性放置玩法对象。
 * 边界：只消费生成器的只读空间查询与本局 Seed；不参与空间生成，不结算玩法后果。
 * 状态 Owner：只拥有本局由自己 Spawn 的对象；任一规则失败会原子清空。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ZeroEscapeGameplayPopulator.generated.h"

class AZeroEscapeRuntimeLevelGenerator;
class UZeroEscapePopulationProfile;

/** 不订阅生成事件；正式开局的原子提交顺序只由 GameMode 编排。 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapeGameplayPopulator final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的放置器。 */
	AZeroEscapeGameplayPopulator();

	/**
	 * 清旧对象后执行全部规则。无候选或按密度计算为零目标时合法跳过；
	 * 配置、加载或实际 Spawn 失败时清掉本次已放对象并返回 false。
	 */
	bool Populate(AZeroEscapeRuntimeLevelGenerator& Generator);

	/** GameMode 失败回滚与 EndPlay 共用的幂等清理入口。 */
	void ClearPopulation();

protected:
	/** 结束时清理本局已放置对象。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 放置规则表；决定在普通候选格放哪些对象、放多少。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UZeroEscapePopulationProfile> PopulationProfile;

	/** 本局已放置对象；失败回滚或结束时统一销毁。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedActors;
};
