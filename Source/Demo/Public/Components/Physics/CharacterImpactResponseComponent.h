// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file CharacterImpactResponseComponent.h
 * 职责：统一执行玩家与追猎者的站立轻受击、移动恢复、动画和 Heavy 抢占。
 * 边界：不拥有 Heavy/PCA/倒地状态，不结算伤害，不做 AI 决策，也不从原始冲量自动升级 Heavy。
 * 状态 Owner：本组件独占当前 Light 状态、速度快照、Timer、Montage 与有限 ImpactId 历史。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Physics/CharacterImpactTypes.h"

#include "CharacterImpactResponseComponent.generated.h"

class ACharacter;
class UAnimMontage;
class UAnimSequenceBase;
class UCharacterImpactTuningData;
class UCharacterMovementComponent;
class UHeavyImpactResponseComponent;
class UPhysicalAnimationComponent;
class USkeletalMeshComponent;
enum class EHeavyImpactState : uint8;
struct FHeavyImpactPreparationRequest;

UCLASS(ClassGroup = (Physics))
class DEMO_API UCharacterImpactResponseComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UCharacterImpactResponseComponent();

	/** 由具体角色在 PostInitializeComponents 显式注入；接收者类别不从 DataAsset 反推。 */
	void Configure(
		ACharacter* InCharacter,
		USkeletalMeshComponent* InMesh,
		UCharacterMovementComponent* InMovement,
		UPhysicalAnimationComponent* InPhysicalAnimation,
		EImpactReceiverCategory InReceiverCategory,
		UCharacterImpactTuningData* InTuning,
		UHeavyImpactResponseComponent* InHeavyImpact);

	/** 同步处理一次命中确认后的站立反应；不会排队。 */
	EStandingImpactSubmitResult SubmitImpact(const FStandingImpactRequest& Request);

	bool IsLightActive() const { return ActiveResult != EStandingImpactResult::None; }
	bool IsMovementBlocked() const { return ActiveResult == EStandingImpactResult::Stop; }
	bool IsAttackSuppressed() const { return IsLightActive(); }
	EStandingImpactResult GetActiveResult() const { return ActiveResult; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void HandleHeavyPreContactCaptureRequested(const FHeavyImpactPreparationRequest& Request);
	void HandleHeavyImpactStateChanged(EHeavyImpactState Previous, EHeavyImpactState Current);
	void FinishActiveImpact();
	bool ApplySlowMovement(float SpeedMultiplier, float Strength, float BaselineSpeed);
	bool ApplyStopMovement(float BaselineSpeed);
	void RestoreWalkSpeedIfUncontested();
	void ClearActiveImpact(bool bRestoreWalkSpeed, bool bImmediateMontageStop);
	void ScheduleEndTimer(float EndTimeSeconds);
	void RecordRecentImpactId(const FGuid& ImpactId);
	bool HasSeenImpactId(const FGuid& ImpactId) const;
	UAnimSequenceBase* SelectReactionAnimation(const FVector& WorldDirection) const;
	void PlayReactionAnimation(const FVector& WorldDirection);
	void StopReactionAnimation(bool bImmediate);
	void HandleReactionMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	bool ResolvePhysicalHit(
		const FStandingImpactRequest& Request,
		FName& OutImpulseBody,
		FVector& OutWorldPoint) const;
	void TryApplyPhysicalReaction(const FStandingImpactRequest& Request);
	void StopPhysicalReaction();
	void ReleasePhysicalAnimationConfiguration();

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> Character = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> Movement = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterImpactTuningData> Tuning = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UHeavyImpactResponseComponent> HeavyImpact = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveLightMontage = nullptr;

	UPROPERTY(Transient)
	TArray<FGuid> RecentImpactIds;

	TWeakObjectPtr<AActor> ActiveSourceActor;
	FGuid ActiveImpactId;
	FTimerHandle EndTimerHandle;

	EImpactReceiverCategory ReceiverCategory = EImpactReceiverCategory::Player;
	EStandingImpactResult ActiveResult = EStandingImpactResult::None;
	float ActiveStrength = 0.0f;
	float BaselineMaxWalkSpeed = 0.0f;
	float LastWrittenMaxWalkSpeed = 0.0f;
	float LightWindowStartTimeSeconds = 0.0f;
	float ActiveEndTimeSeconds = 0.0f;
	float PhysicalSessionStartTimeSeconds = 0.0f;
	float LastPhysicalImpactTimeSeconds = 0.0f;
	uint64 PhysicalConfigurationFrame = 0;
	bool bConfigurationReady = false;
	bool bPhysicalReactionReady = false;
	bool bPhysicalReactionActive = false;
};
