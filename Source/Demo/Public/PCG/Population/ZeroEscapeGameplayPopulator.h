// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayPopulator.h
 * 职责：由 GameMode 在角色与出口就绪后显式调用，按 Profile 分层放置机关与物理资源。
 * 边界：只消费生成器的 Ready 纯值快照；不参与空间生成，不结算玩法后果。
 * 状态 Owner：只拥有本局由自己 Spawn 的对象；任一配置、加载或 Spawn 失败会原子清空。
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
	 * 清旧对象后一次规划并依次生成机关层和资源层。密度欠填只告警；
	 * 非法 Plan/Profile、Class 加载或实际 Spawn 失败会清掉本轮全部对象并返回 false。
	 */
	bool Populate(AZeroEscapeRuntimeLevelGenerator& Generator);

	/** GameMode 失败回滚与 EndPlay 共用的幂等清理入口。 */
	void ClearPopulation();

protected:
	/** 结束时清理本局已放置对象。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 唯一 Population Profile；共享装配与三档策划数值都在这里。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UZeroEscapePopulationProfile> PopulationProfile;

	/** 本局已放置对象；失败回滚或结束时统一销毁。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedActors;
};
