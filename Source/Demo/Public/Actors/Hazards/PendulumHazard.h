// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PendulumHazard.h
 * 职责：创建世界约束的球形物理摆锤，按初始角度自由释放，并在中线穿越时仅补回缺失能量。
 * 边界：不按碰撞物类别改写冲量，只经共享接口请求接收者提前准备，不加载美术资产，不接入 PCG。
 * 状态 Owner：拥有自身中线侧别、半摆 ImpactId、预测候选、补能 Timer 与物理组件生命周期。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"

#include "PendulumHazard.generated.h"

class UPendulumHazardTuningData;
class UPhysicsConstraintComponent;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
struct FHeavyImpactPreparationRequest;

/** 真实 Chaos 刚体自由摆动、以有限中线补能抵消阻尼损耗的常驻摆锤机关。 */
UCLASS()
class DEMO_API APendulumHazard final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建地面根、世界约束、物理锤头、独立预测球与三个纯美术挂点；Actor Tick 永久关闭。 */
	APendulumHazard();

	/** 根据 DataAsset 在编辑器里预览初始释放姿态；不建立约束或启动物理。 */
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	/** 校验配置，以最低点建立约束，再设置一次初始释放姿态并启动中线检测。 */
	virtual void BeginPlay() override;

	/** 清除内部采样 Timer；物理组件由 Actor 生命周期正常销毁。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 设置支点、锤头中心、球半径和美术挂点；bPreviewReleasePose 只用于编辑器预览。 */
	void ApplyGeometry(const UPendulumHazardTuningData& Tuning, bool bPreviewReleasePose);

	/** 在物理状态创建后写入质量、阻尼和 CCD。 */
	void ApplyPhysicsProperties(const UPendulumHazardTuningData& Tuning);

	/** 锁定线性自由度、配置主/副轴硬限位并明确关闭全部驱动。 */
	void ConfigureConstraint(const UPendulumHazardTuningData& Tuning);

	/** 仅在 BeginPlay 初始化时绕世界约束 X 轴瞬移到目标摆幅，然后由重力自然释放。 */
	void SetInitialReleasePose(const UPendulumHazardTuningData& Tuning);

	/** 以低成本采样主轴角度和预测候选；跨中线时更新半摆 ID，并至多请求一次补能。 */
	void EvaluateEnergyAssist();

	/** 对仍在预测球内、且本半摆尚未接受的接口接收者，反复计算接近速度和预计接触时间。 */
	void EvaluatePreparationCandidates();

	/** 使用 Bob 与接收者碰撞表面的相对运动，生成只描述预期真实接触的接口请求。 */
	bool BuildPreparationRequest(
		const AActor& Receiver,
		FHeavyImpactPreparationRequest& OutRequest);

	/** 中线穿越后开始下一半摆：生成新 ID，并允许每个接收者重新接受一次通知。 */
	void BeginNewSwingPass();

	/** 预测球进入事件只登记实现共享接口的 Actor；实际请求统一在 60 Hz Timer 中完成。 */
	UFUNCTION()
	void HandlePreparationVolumeBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** Actor 的最后一个组件离开预测球后移除候选；已通知记录保留到下一半摆。 */
	UFUNCTION()
	void HandlePreparationVolumeEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	/** 当前切向速度低于目标速度时，沿当前运动方向施加有限普通冲量；绝不刹车。 */
	void AssistAtCenterCrossing();

	/** 用世界重力、摆长、目标摆幅和球体转动惯量近似计算最低点目标切向速度。 */
	float CalculateTargetCenterSpeed() const;

	/** 配置非法或运行前提不成立时明确报错，并关闭锤头碰撞和模拟。 */
	void DisableHazard(const FString& Reason);

	/** Actor 地面放置基准；所有几何参数均以此为原点。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|摆锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 顶部支架的纯美术挂点；蓝图可在其下装配网格，不参与物理。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|摆锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> AnchorVisualRoot;

	/** 从世界连接到 BobBody 的物理约束；组件 X 轴定义主摆轴。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|摆锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsConstraintComponent> PhysicsConstraint;

	/** 唯一真实模拟和碰撞的球形锤头；承载质量、阻尼、CCD 与补能冲量。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|摆锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> BobBody;

	/** 随 BobBody 运动的独立 Query-only 球；只 Overlap Pawn，不阻挡、不施力且不影响导航。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|摆锤|重冲击",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> PreparationVolume;

	/** 锤头纯美术挂点；保持在 BobBody 中心并随刚体运动，不猜测未知网格尺寸或自动缩放。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|摆锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> BobVisualRoot;

	/** 摆杆纯美术挂点；保持在锤头至支点的中点并随刚体旋转，不猜测未知网格轴向或自动缩放。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|摆锤",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> RodVisualRoot;

	/** 唯一调参资产；由 BP_PendulumHazard 类默认值指定，BeginPlay 后只读。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "机关|摆锤|配置",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPendulumHazardTuningData> TuningData;

	/** 最近一次确认位于中线哪一侧：-1 / +1；0 表示尚未取得稳定侧别。 */
	int8 LastObservedSide = 0;

	/** 当前半摆共用的请求 ID；玩家和 AI 以同一 ID 分别、独立地接受。 */
	FGuid CurrentSwingImpactId;

	/** 当前仍与预测球重叠、且实现 IHeavyImpactReceiver 的 Actor。 */
	TSet<TWeakObjectPtr<AActor>> PreparationCandidates;

	/** 本半摆已返回 Accepted/Duplicate 的接收者；Invalid/Busy 不写入，以允许稍后重试。 */
	TSet<TWeakObjectPtr<AActor>> NotifiedReceiversThisSwing;

	/** 60 Hz 中线穿越采样 Timer；不是物理子步，也不连续施力。 */
	FTimerHandle EnergyAssistTimerHandle;
};
