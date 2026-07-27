// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapePrototypeRoundFlow.h
 * 职责：在运行时 PCG 完成后放置当前玩家、唯一追猎者和出口球，并记录首版通关结果。
 * 边界：只消费 Generator 的只读空间结果；不修改 WFC、不撒放陷阱、不接管追猎者行为或正式 UI。
 * 状态 Owner：本类只拥有本局追猎者、当前玩家引用、出口激活状态与一次性通关标记。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapePrototypeRoundFlow.generated.h"

class APawn;
class APursuerCharacter;
class AZeroEscapeRuntimeLevelGenerator;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;

/** 单机原型的一局最小流程：生成地图、放置两个角色、启用出口并记录通关。 */
UCLASS(Blueprintable)
class DEMO_API AZeroEscapePrototypeRoundFlow final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的流程 Actor；出口在完整放置成功前保持隐藏且无碰撞。 */
	AZeroEscapePrototypeRoundFlow();

protected:
	/** 绑定 Generator 与出口事件；Generator 仍为 Idle 时发起首局生成。 */
	virtual void BeginPlay() override;

	/** 解绑事件并销毁本类生成的追猎者；不销毁 GameMode 创建的玩家。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Generator 提交终态后的唯一入口；成功时重新建立本局角色与出口位置。 */
	UFUNCTION()
	void HandleGenerationFinished(bool bSuccess, const FZeroEscapeGenerationReport& Report);

	/** 仅当前玩家首次进入激活出口时记录一条通关日志。 */
	UFUNCTION()
	void HandleGoalBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** 查询三个位置、移动玩家、生成追猎者并启用出口；任一步失败都保持未激活。 */
	bool ActivateRound();

	/** 从走廊候选中选择二维距离至少达到配置值、且额外距离最小的玩家位置。 */
	bool FindPlayerSpawnTransform(
		const FTransform& PursuerStartTransform,
		FTransform& OutPlayerTransform) const;

	/** 销毁旧追猎者并禁用出口；不销毁或重生当前玩家。 */
	void ResetRoundState();

	/** 只承载出口组件；Actor 初始 Transform 不参与生成规则。 */
	UPROPERTY(VisibleAnywhere, Category = "Round Flow")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 靠近即通关的球形触发区，只有完整放置成功后才启用。 */
	UPROPERTY(VisibleAnywhere, Category = "Round Flow")
	TObjectPtr<USphereComponent> GoalTrigger;

	/** 临时出口球外观；由关卡实例指定普通球体 Mesh。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Round Flow", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GoalVisual;

	/** 关卡实例显式指定的空间 Generator；不按 Actor 名称搜索。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Round Flow", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AZeroEscapeRuntimeLevelGenerator> Generator;

	/** 本局唯一追猎者类；关卡实例绑定现有 BP_Pursuer。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Round Flow", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<APursuerCharacter> PursuerClass;

	/** 玩家与追猎者 Start 的最小二维距离；1200 cm 等于当前两个 600 cm 逻辑格。 */
	UPROPERTY(EditInstanceOnly, Category = "Round Flow", meta = (ClampMin = "600.0", Units = "cm"))
	double PlayerStartSeparationCm = 1200.0;

	/** 本类在当前局生成的唯一追猎者；重生成与 EndPlay 时销毁。 */
	UPROPERTY(Transient)
	TObjectPtr<APursuerCharacter> SpawnedPursuer;

	/** GameMode 创建并拥有的当前玩家；本类只负责移动与出口身份判断。 */
	UPROPERTY(Transient)
	TObjectPtr<APawn> ActivePlayer;

	/** 完整放置成功后为 true；通关时立即清零以防重复记录。 */
	bool bRoundActive = false;
};
