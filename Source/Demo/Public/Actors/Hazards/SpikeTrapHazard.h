// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file SpikeTrapHazard.h
 * 职责：自动循环升降的地刺危险区 Actor；固定格栅地板 + Timeline 驱动刺升降，仅在“伸出”相位对区内 Pawn 施加伤害。
 * 边界：不结算生命/失衡/倒地（交由目标的伤害接收方），不硬编码网格资源，不管理玩家或追猎者状态。
 * 状态 Owner：只拥有自身升降相位、危险窗口与区内 Pawn 集合。
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/TimelineComponent.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"

#include "SpikeTrapHazard.generated.h"

class UBoxComponent;
class UCharacterImpactSourceProfile;
class UCurveFloat;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;

/** 关闭常驻 Tick 的自动循环地刺：格栅地板固定，刺从格栅升出；玩家可在收起窗口通过或起跳越过。 */
UCLASS()
class DEMO_API ASpikeTrapHazard final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建场景根、固定格栅、升降刺网格、只响应 Pawn 的伤害区与升降 Timeline。 */
	ASpikeTrapHazard();

	/** 延迟生成专用：注入 [0,1) 初始相位与同一双地刺共享的命中组；失败时保持原同步启动行为。 */
	bool ConfigurePopulationPhase(
		float InNormalizedPhase01,
		const FGuid& InPopulationImpactGroupId);

protected:
	/** 生成升降曲线、绑定 Timeline 与 Overlap，记录刺基准位并从收起相位启动循环。 */
	virtual void BeginPlay() override;

private:
	/** 进入收起相位；非负 Override 只用于 PCG 第一轮启动偏移。 */
	void EnterHidden(float InitialDelayOverrideSeconds = -1.0f);
	/** 正向播放 Timeline 使刺升起。 */
	void StartRising();
	/** 进入伸出相位：标记危险、对区内 Pawn 结算一次并计时后收起。 */
	void EnterExtended();
	/** 反向播放 Timeline 使刺收起。 */
	void StartLowering();

	/** Timeline 每次更新驱动刺网格的相对高度。 */
	UFUNCTION()
	void HandleRiseProgress(float Alpha);

	/** Timeline 到端点：升起结束转伸出，收起结束转收起相位。 */
	UFUNCTION()
	void HandleTimelineFinished();

	/** Pawn 进入伤害区：登记；若正处危险相位立即结算一次。 */
	UFUNCTION()
	void HandleHurtZoneBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** Pawn 离开伤害区：注销。 */
	UFUNCTION()
	void HandleHurtZoneEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	/** 提交一次站立轻受击；仅玩家额外通过官方 ApplyDamage 结算伤害。 */
	void ProcessDangerousContact(AActor* Target);

	/** 固定的场景根；格栅、刺与伤害区都挂其下，升降只动刺网格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 固定不动的格栅地板；在放置实例中指定 SM_RisingSpikesTopGrate。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GrateMesh;

	/** 升降的刺网格；在放置实例中指定 SM_Spikes，由 Timeline 驱动从格栅升出/沉入。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> SpikeMesh;

	/** 只对 Pawn 产生 Overlap 的固定伤害区；覆盖格栅表面，便于玩家跳跃越过。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> HurtZone;

	/** 驱动升降的官方 Timeline 组件；自带更新，无需开启 Actor Tick。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTimelineComponent> RiseTimeline;

	/** 升降 0→1 曲线；BeginPlay 内部程序化生成，不对外暴露，仅用于防止被 GC。 */
	UPROPERTY()
	TObjectPtr<UCurveFloat> RiseCurve;

	/** 刺从“伸出位”向下沉入的深度（cm）；应不小于刺露出格栅的高度以完全藏入。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	float HideDepth = 80.0f;

	/** 单次升起或收起的时长（秒）；越小刺弹出越快。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true", ClampMin = "0.05"))
	float RiseDuration = 0.4f;

	/** 伸出（危险）相位停留时长（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	float ExtendedDuration = 1.5f;

	/** 收起（安全）相位停留时长（秒）；玩家通过窗口。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	float HiddenDuration = 2.0f;

	/** 命中一次施加的伤害；接收方接入生命系统后生效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|地刺", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	float Damage = 20.0f;

	/** Source-owned mapping from this spike contact to Player/Pursuer light-impact behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard|Spike|Standing Impact",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCharacterImpactSourceProfile> StandingImpactSourceProfile;

	/** Normalized strength submitted with this spike contact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard|Spike|Standing Impact",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float StandingImpactStrength = 1.0f;

	/** 蓝图中对齐好的刺“伸出”相对位置；BeginPlay 记录，作为升降基准。 */
	FVector SpikeBaseLocation = FVector::ZeroVector;

	/** 当前 Timeline 是否正向升起；用于区分 Timeline 结束后的相位切换。 */
	bool bMovingUp = false;

	/** 当前是否处于伤害生效的伸出相位。 */
	bool bIsDangerous = false;

	/** 仅由 ConfigurePopulationPhase 在 BeginPlay 前写入；手摆机关保持 false。 */
	bool bHasPopulationPhase = false;

	/** PCG 确定性归一化相位；只映射到第一轮安全启动延迟。 */
	float PopulationNormalizedPhase01 = 0.0f;

	/** 同一 Population 双地刺共享的运行时组标识；只用于让接收端合并同轮命中。 */
	FGuid PopulationImpactGroupId;

	/** 当前 Population 双地刺已经进入过的危险轮次；同组两个 Actor 以相同序号派生命中 ID。 */
	uint32 PopulationDangerPhaseSequence = 0;

	/** Stable for one extended phase so a receiver can reject repeat overlap callbacks. */
	FGuid ActiveDangerImpactId;

	/** 当前处于伤害区内的 Pawn 集合。 */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> OverlappingPawns;

	/** 当前伸出相位已经结算过的目标；离开再进入也不会重复轻受击或伤害。 */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> ProcessedDangerTargets;

	/** 收起/伸出相位停留的单次计时器。 */
	FTimerHandle PhaseTimerHandle;
};
