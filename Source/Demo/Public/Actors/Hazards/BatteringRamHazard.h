// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file BatteringRamHazard.h
 * 职责：拥有自动周期、局部 +X 直线运动、锤头碰撞和每次伸出的重冲击准备通知。
 * 边界：不识别玩家/AI 类型，不决定倒地，不施加补偿冲量，不接入 PCG。
 * 状态 Owner：本 Actor 唯一写入阶段、阶段时间、当前 ImpactId 和本次已通知接收者。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"

#include "BatteringRamHazard.generated.h"

class UBatteringRamHazardTuningData;
class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
struct FHeavyImpactPreparationRequest;

/** 项目内部的冲锤运行阶段，不是 UE 官方状态机类型。 */
enum class EBatteringRamPhase : uint8
{
	Waiting,
	Warning,
	Extending,
	Retracting,
	Disabled
};

/** 自动等待、预警、伸出并缩回的机械冲锤。 */
UCLASS()
class DEMO_API ABatteringRamHazard final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建固定根、运动学锤头、查询预测体积和纯美术挂点；等待阶段不启用 Tick。 */
	ABatteringRamHazard();

	/** 按 DataAsset 在编辑器中预览碰撞尺寸、预测范围和完全缩回姿态。 */
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	/** 校验配置、启用碰撞并从安全等待阶段启动。 */
	virtual void BeginPlay() override;

	/** 仅在伸出或缩回阶段推进线性位置；其他阶段不会启用。 */
	virtual void Tick(float DeltaSeconds) override;

	/** 清理阶段 Timer、ImpactId 和本次通知集合。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 应用锤头盒体和前置预测盒体几何；Actor 原点就是锤头完全缩回中心。 */
	void ApplyGeometry(const UBatteringRamHazardTuningData& Tuning);

	/** 进入完全缩回安全期，并安排下一次预警。 */
	void EnterWaiting();

	/** 显示 WarningVisualRoot，并安排伸出。 */
	void EnterWarning();

	/** 生成本次 ImpactId、通知已有候选并从下一帧开始伸出。 */
	void BeginExtension();

	/** 关闭本次角色通知并开始缩回。 */
	void BeginRetraction();

	/** 完成缩回并重新进入安全等待。 */
	void FinishRetraction();

	/** 按阶段时间计算线性位置；返回 true 表示该阶段已完成。 */
	bool AdvanceLinearPhase(float DeltaSeconds, float DurationSeconds, bool bExtending);

	/** 查询当前预测盒重叠者，计算线性 ETA，并在短窗口内请求准备。 */
	void EvaluatePreparationCandidates(const FVector& PlannedWorldVelocity);

	/** 使用锤头前表面、接收者 Bounds 和相对速度构造一次准备请求。 */
	bool BuildPreparationRequest(
		const AActor& Receiver,
		const FVector& PlannedWorldVelocity,
		FHeavyImpactPreparationRequest& OutRequest);

	/** 清理运行状态、关闭碰撞并输出明确错误；不启用隐藏兜底。 */
	void DisableHazard(const FString& Reason);

	/** 固定 Actor 根；局部 +X 定义伸出方向。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 固定外壳纯美术挂点；Blueprint 在其下装配墙柜、管线等网格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> HousingVisualRoot;

	/** 唯一真实碰撞锤头；不模拟自由物理，由本 Actor 在 PrePhysics 阶段移动。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> RamBody;

	/** 随 RamBody 运动的纯美术挂点；Blueprint 在其下装配冲杆和锤面。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> RamVisualRoot;

	/** 锤头前方 Query-only Pawn 预测盒；不阻挡、不施力且不影响导航。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤|重冲击",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> PreparationVolume;

	/** 固定预警纯美术挂点；C++ 只控制可见性，不指定灯光、材质或音效。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> WarningVisualRoot;

	/** 第一版唯一配置来源；缺失或非法时机关明确停用。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "机关|冲锤|配置",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBatteringRamHazardTuningData> TuningData;

	/** 当前阶段，只由本 Actor 的阶段函数写入。 */
	EBatteringRamPhase Phase = EBatteringRamPhase::Disabled;

	/** 当前伸出或缩回阶段已经推进的秒数。 */
	float MotionElapsedSeconds = 0.0f;

	/** 当前伸出的去重 ID；缩回开始时失效。 */
	FGuid CurrentImpactId;

	/** 本次伸出已经返回 Accepted/Duplicate 的接收者。 */
	TSet<TWeakObjectPtr<AActor>> NotifiedReceiversThisStroke;

	/** 等待和预警共用的单次 Timer；运动阶段不使用 Timer 采样位置。 */
	FTimerHandle PhaseTimerHandle;
};
