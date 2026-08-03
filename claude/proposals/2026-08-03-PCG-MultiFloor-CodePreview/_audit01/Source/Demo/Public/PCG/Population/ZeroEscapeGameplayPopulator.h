// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayPopulator.h
 * 职责：在关联生成器完成后，按 Population Profile 确定性地把玩法对象 Actor 撒入生成结果。
 * 边界：只消费生成器的只读空间查询与本局 Seed；不参与空间生成，不结算玩法后果。
 * 状态 Owner：只拥有自己本局已 Spawn 的对象集合，用于重生成/结束时清理。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ZeroEscapeGameplayPopulator.generated.h"

class AZeroEscapeRuntimeLevelGenerator;
class UZeroEscapePopulationProfile;
struct FZeroEscapeGenerationReport;

/** 订阅生成完成事件、把 Population Profile 里的对象确定性放置到关卡的独立放置器。 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapeGameplayPopulator final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的放置器。 */
	AZeroEscapeGameplayPopulator();

protected:
	/** 绑定关联生成器的完成事件；若绑定时已 Ready 立即补放一次。 */
	virtual void BeginPlay() override;

	/** 结束时解绑事件并清理本局已放置对象。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 生成成功后清理旧对象并按规则重新放置；失败则不放置。 */
	UFUNCTION()
	void HandleGenerationFinished(bool bSuccess, const FZeroEscapeGenerationReport& Report);

	/** 销毁本局已 Spawn 的全部对象并清空登记。 */
	void ClearSpawnedActors();

	/** 关联的空间生成器；关卡实例中指定，其完成事件驱动本放置器。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Population", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AZeroEscapeRuntimeLevelGenerator> Generator;

	/** 放置规则表；决定放哪些对象、放到哪类格、放多少。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UZeroEscapePopulationProfile> PopulationProfile;

	/** 本局已放置对象；重生成或结束时统一销毁。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedActors;
};
