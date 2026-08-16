// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeExitVolume.h
 * 职责：在 PCG 出口坐标处提供占位外观与"玩家到达"触发器，并广播到达事件。
 * 边界：只检测玩家进入并广播；不裁决胜负、不弹 UI（由 GameState/GameMode 处理）。
 * 状态 Owner：仅拥有自身触发器激活与一次性到达标记。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ZeroEscapeExitVolume.generated.h"

class UStaticMeshComponent;
class USphereComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnExitReached);

/** 出口体积：激活后玩家进入触发器广播 OnExitReached；默认隐藏且无碰撞。 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapeExitVolume final : public AActor
{
	GENERATED_BODY()

public:
	AZeroEscapeExitVolume();

	/** 在指定世界 Transform 处激活出口：定位、显示外观、启用触发器。 */
	void Activate(const FTransform& WorldTransform);

	/** GameMode 真正判胜后确认出口完成并关闭碰撞；未达光团门槛时不得调用。 */
	void ConfirmReached();

	/** 玩家每次重新进入出口触发器时广播；只有 GameMode 确认判胜后才停止触发。 */
	UPROPERTY(BlueprintAssignable, Category = "ZeroEscape|Exit")
	FOnExitReached OnExitReached;

private:
	UFUNCTION()
	void HandleGoalBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** 检测玩家 Pawn 的球形触发区；激活前无碰撞。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ZeroEscape|Exit", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> GoalTrigger;

	/** 占位出口外观；Mesh 由蓝图 Class Defaults 指定，后续替换为正式素材。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ZeroEscape|Exit", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GoalVisual;

	/** 只在 GameMode 确认判胜后置真；门槛不足时保持可再次进入。 */
	bool bReached = false;
};
