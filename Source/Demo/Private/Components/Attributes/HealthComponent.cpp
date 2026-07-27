// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HealthComponent.cpp
 * 职责：绑定 Owner 的 OnTakeAnyDamage，受击时扣减并记录当前生命。
 * 边界：不做死亡流程、UI、再生或网络；不主动发起伤害。
 */

#include "Components/Attributes/HealthComponent.h"

#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogHealth, Log, All);

/** 创建关闭 Tick 的生命组件。 */
UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/** 以最大生命初始化当前生命，并绑定 Owner 的任意伤害事件。 */
void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->OnTakeAnyDamage.AddDynamic(this, &UHealthComponent::HandleTakeAnyDamage);
	}
}

/** 扣血并记录；归零仅记录死亡时机，后果留待后续实现。 */
void UHealthComponent::HandleTakeAnyDamage(
	AActor* DamagedActor,
	float Damage,
	const UDamageType* /*DamageType*/,
	AController* /*InstigatedBy*/,
	AActor* /*DamageCauser*/)
{
	if (Damage <= 0.0f || CurrentHealth <= 0.0f)
	{
		return;
	}

	CurrentHealth = FMath::Clamp(CurrentHealth - Damage, 0.0f, MaxHealth);

	const FString OwnerName = IsValid(DamagedActor) ? DamagedActor->GetName() : GetName();
	UE_LOG(LogHealth, Warning, TEXT("%s 受到 %.1f 伤害，剩余生命 %.1f/%.1f"),
		*OwnerName, Damage, CurrentHealth, MaxHealth);

	if (CurrentHealth <= 0.0f)
	{
		// [临时] 死亡后果（倒地/失败/重开）尚未设计，此处仅记录归零时机。
		UE_LOG(LogHealth, Warning, TEXT("%s 生命归零（死亡逻辑暂未实现）。"), *OwnerName);
	}
}
