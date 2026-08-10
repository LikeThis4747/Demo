// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticObjectComponent.h
 * 职责：声明磁性道具身份，并独占一次正式投掷的攻击碰撞快照、Hit、Tag、Timer 与恢复。
 * 边界：不保存玩家全局抓取手感、不制造投掷冲量，也不决定玩家/追猎者的反应结果。
 * 状态 Owner：本组件只拥有单个道具的磁性配置与正式投掷事务；Physics Handle 持有状态仍属于 Grab 组件。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/TimerHandle.h"

#include "MagneticObjectComponent.generated.h"

class AActor;
class UPrimitiveComponent;
class UCharacterImpactSourceProfile;

/** 给 Actor 添加可磁吸标记，并提供选取优先级与单物体投掷倍率。 */
UCLASS(ClassGroup = (Magnetism), meta = (BlueprintSpawnableComponent))
class DEMO_API UMagneticObjectComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的纯配置组件；磁性资格只在玩家输入触发的选取中读取。 */
	UMagneticObjectComponent();

	/** 检查磁性标记、刚体模拟状态和全局质量上限，决定候选组件是否可抓取。 */
	bool CanGrab(const UPrimitiveComponent* CandidateComponent, float MaxAllowedMass) const;

	/** 释放 Physics Handle 后、施加投掷冲量前，用精确 Primitive 建立一次可回滚的 Light 命中事务。 */
	bool ArmThrownImpact(
		UPrimitiveComponent* ThrownPrimitive,
		AActor* Thrower,
		float ActiveDurationSeconds);

	/** 幂等结束当前投掷事务并精确恢复该 Primitive 的全部碰撞快照。 */
	void DisarmThrownImpact();

	bool IsThrownImpactArmed() const { return ArmedPrimitive.IsValid(); }

	/**
	 * 对应 C++ 属性 bMagnetizable；初始值：true。
	 * 关闭后该 Actor 不进入磁力候选，但不会修改碰撞或 Chaos 模拟状态。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体")
	bool bMagnetizable = true;

	/**
	 * 对应 C++ 属性 SelectionPriority，由 FindBestCandidate 处理重叠候选，无单位。
	 * 初始值：1；编辑范围：0~10。调高更容易在重叠轮廓中胜出，调低则让其他道具优先。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0"))
	float SelectionPriority = 1.0f;

	/**
	 * 对应 C++ 属性 ThrowSpeedMultiplier，由 ThrowHeldObject 乘到全局 ThrowSpeed，无单位。
	 * 初始值：1；编辑范围：0.1~3。调高让特殊轻型道具飞得更快，调低可表现重型或吸能道具。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体", meta = (ClampMin = "0.1", ClampMax = "3.0", UIMin = "0.1", UIMax = "3.0"))
	float ThrowSpeedMultiplier = 1.0f;

	/** 本类磁力物作为正式投掷物时使用的站立轻受击来源映射。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁性物体|轻受击")
	TObjectPtr<UCharacterImpactSourceProfile> StandingImpactSourceProfile = nullptr;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleArmedPrimitiveHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	void HandleDeferredDisarm();

	struct FThrownImpactCollisionSnapshot
	{
		bool bValid = false;
		FName CollisionProfileName = NAME_None;
		TEnumAsByte<ECollisionEnabled::Type> CollisionEnabled = ECollisionEnabled::NoCollision;
		TEnumAsByte<ECollisionChannel> ObjectType = ECC_PhysicsBody;
		FCollisionResponseContainer Responses;
		bool bNotifyRigidBodyCollision = false;
		bool bUseCCD = false;
		bool bOwnerHadAttackProjectileTag = false;

		void Reset() { *this = FThrownImpactCollisionSnapshot(); }
	};

	TWeakObjectPtr<UPrimitiveComponent> ArmedPrimitive;
	TWeakObjectPtr<AActor> ActiveThrower;
	FGuid ActiveImpactId;
	FThrownImpactCollisionSnapshot CollisionSnapshot;
	FTimerHandle ActiveDurationTimerHandle;
	FTimerHandle DeferredDisarmTimerHandle;
	bool bImpactConsumed = false;
};
