// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeEnergyOrb.h
 * 职责：把奖励支线光团的一次性玩家触碰转换为收集请求，并播放短时吸向玩家的表现。
 * 边界：不持有爆裂次数、出口目标或 PCG 数量；蓝图继续负责网格、材质、粒子、灯光和碰撞装配。
 * 状态 Owner：只拥有本 Actor 的已收集标记与短时吸取动画状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

#include "ZeroEscapeEnergyOrb.generated.h"

/** 奖励支线光团的轻量玩法基类；表现组件全部由 BP_ThrowEnergyOrb 装配。 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapeEnergyOrb : public AActor
{
	GENERATED_BODY()

public:
	AZeroEscapeEnergyOrb();

protected:
	/** 只接受正式玩家 Pawn；GameMode 拒绝时保留光团，避免开局尚未初始化就误收集。 */
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;

	/** 清理短时表现 Timer；光团销毁不会留下回调。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BeginCollection(AActor& Collector);
	void UpdateCollectionAnimation();

	/** 从确认收集到抵达玩家的表现时长；不影响玩法结算。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Energy Orb|Presentation",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.05", ClampMax = "1.0", Units = "s"))
	float CollectionAnimationDurationSeconds = 0.3f;

	/** 吸取终点相对玩家 Actor 原点的高度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZeroEscape|Energy Orb|Presentation",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "200.0", Units = "cm"))
	float CollectionTargetHeightCm = 50.0f;

	TWeakObjectPtr<AActor> CollectionTarget;
	FVector CollectionStartLocation = FVector::ZeroVector;
	FVector CollectionStartScale = FVector::OneVector;
	double CollectionStartWorldTimeSeconds = 0.0;
	FTimerHandle CollectionAnimationTimer;
	bool bCollectionCommitted = false;
};
