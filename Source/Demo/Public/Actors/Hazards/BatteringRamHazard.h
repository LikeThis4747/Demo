// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file BatteringRamHazard.h
 * 职责：拥有自动周期、局部 +X 直线运动、锤头碰撞和每次伸出的重冲击准备通知。
 * 边界：不识别玩家/AI 类型，不决定倒地，不施加补偿冲量；PCG 只可在 BeginPlay 前注入初始相位。
 * 状态 Owner：本 Actor 唯一写入阶段、阶段时间、当前 ImpactId 和本次已通知接收者。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"

#include "BatteringRamHazard.generated.h"

class UBatteringRamHazardTuningData;
class UBoxComponent;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
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

	/** 延迟生成专用：在 BeginPlay 前注入 [0,1) 初始相位；失败时保持原同步启动行为。 */
	bool ConfigurePopulationPhase(float InNormalizedPhase01);

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

	/** 让可见伸缩梁与简单盒碰撞共同覆盖墙面到锤头背后的已伸出距离。 */
	void UpdateShaftGeometry(float ExtensionDistance, const UBatteringRamHazardTuningData& Tuning);

	/** 进入完全缩回安全期；非负 Override 只用于 PCG 第一轮启动偏移。 */
	void EnterWaiting(float InitialDelayOverrideSeconds = -1.0f);

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

	/** 随 RamBody 运动的纯美术挂点；Blueprint 只在其下装配锤面，不再挂固定长度冲杆。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> RamVisualRoot;

	/** 固定在墙面一侧、由 C++ 动态拉伸的可见长方形金属梁；自身不拥有碰撞。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ShaftVisualMesh;

	/** 与可见梁同步的简单实心盒；只填充锤头后方，阻止角色或物理身体钻入机构。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|冲锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> ShaftBlocker;

	/** Blueprint 只配置资源；运行时由 C++ 装到动态伸缩梁组件，避免继承组件覆盖丢失。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "机关|冲锤|视觉",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMesh> ShaftVisualAsset;

	/** 可选的伸缩梁材质覆盖；为空时沿用网格默认材质。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "机关|冲锤|视觉",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInterface> ShaftVisualMaterial;

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

	/** 仅由 ConfigurePopulationPhase 在 BeginPlay 前写入；手摆机关保持 false。 */
	bool bHasPopulationPhase = false;

	/** PCG 确定性归一化相位；只映射到第一轮安全启动延迟。 */
	float PopulationNormalizedPhase01 = 0.0f;

	/** 当前伸出的去重 ID；缩回开始时失效。 */
	FGuid CurrentImpactId;

	/** 本次伸出已经返回 Accepted/Duplicate 的接收者。 */
	TSet<TWeakObjectPtr<AActor>> NotifiedReceiversThisStroke;

	/** 等待和预警共用的单次 Timer；运动阶段不使用 Timer 采样位置。 */
	FTimerHandle PhaseTimerHandle;
};
