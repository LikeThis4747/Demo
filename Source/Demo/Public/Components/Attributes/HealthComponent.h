// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HealthComponent.h
 * 职责：为所属 Actor 维护当前/最大生命，监听官方 OnTakeAnyDamage 并在受击时扣血与记录。
 * 边界：不做死亡流程、UI、再生、失衡或网络复制；不主动施加伤害，只结算被动受伤。
 * 状态 Owner：唯一拥有 Owner 的生命数值。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "HealthComponent.generated.h"

class AController;
class UDamageType;

/** 事件驱动的最小生命组件：绑定 Owner 受伤委托，扣血并记录，暂不处理死亡后果。 */
UCLASS(ClassGroup = (Attributes), meta = (BlueprintSpawnableComponent))
class DEMO_API UHealthComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建默认关闭 Tick 的生命组件。 */
	UHealthComponent();

protected:
	/** 初始化当前生命并绑定 Owner 的 OnTakeAnyDamage。 */
	virtual void BeginPlay() override;

private:
	/** 官方任意伤害回调：按伤害值扣血、Clamp 并记录；归零仅记录，暂不触发死亡逻辑。 */
	UFUNCTION()
	void HandleTakeAnyDamage(
		AActor* DamagedActor,
		float Damage,
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	/** 最大生命；BeginPlay 时作为当前生命初值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性|生命", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	float MaxHealth = 100.0f;

	/** 当前生命；运行时随受击变化，归零表示应进入死亡（后果暂未实现）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "属性|生命", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 0.0f;
};
