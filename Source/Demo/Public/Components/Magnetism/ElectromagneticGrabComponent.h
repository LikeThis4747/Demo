// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ElectromagneticGrabComponent.h
 * 职责：实现玩家电磁抓取的选取、曲线吸取、持有、放下、投掷与安全恢复状态机。
 * 边界：Chaos/Physics Handle 负责刚体求解；全局手感只从 UMagneticGrabTuningData 读取；输入由角色转发。
 * 状态 Owner：本组件独占当前持有物、吸取/持有阶段、临时物理覆盖、输入锁与安全计时状态。
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#include "ElectromagneticGrabComponent.generated.h"

class UCameraComponent;
class UMagneticGrabTuningData;
class UMagneticObjectComponent;
class UPhysicsHandleComponent;
class UPrimitiveComponent;

/** 提供宽容准星选取和保留自然碰撞旋转的 Physics Handle 抓取基线。 */
UCLASS(ClassGroup = (Magnetism), meta = (BlueprintSpawnableComponent))
class DEMO_API UElectromagneticGrabComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建仅在持有物体期间启用 Tick 的事件驱动组件。 */
	UElectromagneticGrabComponent();

	/** 接收角色拥有的 Physics Handle 与相机，并校验独立磁力 Tuning DataAsset 后应用 Handle 参数。 */
	void Configure(UPhysicsHandleComponent* InPhysicsHandle, UCameraComponent* InViewCamera);

	/** 玩家首次按下抓取时执行一次宽容候选选取；配置无效或输入锁定时不产生副作用。 */
	void BeginGrabInput();

	/** 玩家松开或取消抓取时放下物体，并解除投掷后的再次抓取锁。 */
	void EndGrabInput();

	/** 释放当前刚体，并按照准星方向、全局基础速度和单物体倍率施加速度变化冲量。 */
	void ThrowHeldObject();

	/** 同时检查组件弱引用与 Physics Handle，确认双方是否指向同一个当前持有刚体。 */
	bool IsHoldingObject() const;

protected:
	/** 只在持有期间更新安全目标并执行阻挡/误差释放，不做每帧全局 Actor 搜索。 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Owner 或 World 结束时恢复临时物理设置，防止刚体保留错误阻尼或碰撞响应。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 抓取状态只表达玩法锚点阶段；Physics Handle 约束在 Pulling 与 Holding 中始终有效。 */
	enum class EGrabPhase : uint8
	{
		None,
		Pulling,
		Holding
	};

	/** 返回装配引用和 Tuning 资产是否全部通过 Configure 校验。 */
	bool IsConfigurationReady() const;

	/** 在有界 PhysicsBody 重叠结果中，按屏幕轮廓距离、世界距离与道具优先级选出最佳候选。 */
	UPrimitiveComponent* FindBestCandidate(UMagneticObjectComponent*& OutMagneticObject) const;

	/** 在质心开始抓取，快照所有临时覆盖值，并启用持有期间 Tick。 */
	void GrabCandidate(UPrimitiveComponent* CandidateComponent, UMagneticObjectComponent* MagneticObject);

	/** 释放 Handle、恢复物理覆盖，并按需要要求右键先松开再允许下一次抓取。 */
	void ReleaseHeldObject(bool bRequireInputRelease);

	/** 快照并关闭 Handle 目标插值，依据初始安全终点初始化确定性吸取曲线。 */
	void BeginPull(const FVector& StartLocation, const FVector& InitialSafeHoldLocation);

	/** 根据绝对吸取时间、动态安全终点与固定弧线信息返回当前玩法锚点。 */
	FVector CalculatePullTarget(const FVector& SafeHoldLocation) const;

	/** 标记曲线结束并延后一帧恢复 Handle 插值，确保终点先直接写入物理锚点。 */
	void EnterHoldingPhase();

	/** 在正常到位和所有退出路径中恢复抓取前的 Handle 目标插值设置。 */
	void RestoreHandleTargetInterpolation();

	/** 清空本次吸取的阶段、曲线几何与计时，不修改外部物理对象。 */
	void ResetPullState();

	/** 使用角色位置和相机方向计算玩家前侧的持有目标点。 */
	FVector CalculateDesiredHoldLocation() const;

	/** 当目标路径被关卡几何阻挡时缩短目标距离，并返回连续阻挡状态。 */
	FVector ResolveSafeHoldLocation(const FVector& DesiredLocation, bool& bOutObstructed) const;

	/** 从相机沿中心准星射线返回投掷瞄准点；无命中时返回射线终点。 */
	FVector CalculateAimPoint() const;

	/** 向候选表面做一次可见性检测，阻止隔墙抓取。 */
	bool IsCandidateVisible(const UPrimitiveComponent* CandidateComponent, const FVector& CameraLocation) const;

	/**
	 * 磁力手感唯一配置源；对应 UMagneticGrabTuningData，由 Configure 与持有算法读取。
	 * 初始值：空，必须在 BP_ZeroEscapeCharacter 的 ElectromagneticGrab 组件上指定；缺失时明确报错并停用磁力。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力|配置", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMagneticGrabTuningData> TuningData;

	/** 由角色在 PostInitializeComponents 注入；执行 UE 约束与插值，生命周期随角色。 */
	UPROPERTY(Transient)
	TObjectPtr<UPhysicsHandleComponent> PhysicsHandle;

	/** 由角色注入；定义屏幕选取、持有方向与投掷瞄准，生命周期随角色。 */
	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> ViewCamera;

	/** 当前持有刚体的弱引用；由 GrabCandidate 写入、ReleaseHeldObject 清空，避免阻止 Actor 销毁。 */
	TWeakObjectPtr<UPrimitiveComponent> HeldComponent;

	/** 当前道具磁性配置的弱引用；只用于读取单物体投掷倍率，释放时清空。 */
	TWeakObjectPtr<UMagneticObjectComponent> HeldMagneticObject;

	/** 抓取前的角阻尼快照；GrabCandidate 写入，所有 ReleaseHeldObject 路径负责恢复。 */
	float PreviousAngularDamping = 0.0f;

	/** 抓取前对 Pawn 通道的碰撞响应快照；释放时恢复，防止永久改变道具碰撞。 */
	ECollisionResponse PreviousPawnCollisionResponse = ECR_Block;

	/** 抓取前对 Camera 通道的碰撞响应快照；持有期间置为 Ignore 以免持有物挤触玩家相机弹簧臂回缩，释放时恢复。 */
	ECollisionResponse PreviousCameraCollisionResponse = ECR_Block;

	/** 当前玩法锚点阶段；GrabCandidate 进入 Pulling，曲线结束进入 Holding，释放时归零。 */
	EGrabPhase GrabPhase = EGrabPhase::None;

	/** 吸取曲线固定起点，单位 cm；抓取成功时取刚体质心，释放时清零。 */
	FVector PullStartLocation = FVector::ZeroVector;

	/** 吸取曲线固定弧线方向；从世界上方向运动垂直平面投影得到，避免随镜头扭曲。 */
	FVector PullArcDirection = FVector::UpVector;

	/** 当前吸取曲线已经过时间，单位 s；仅在 Pulling Tick 中累加并钳制到总时长。 */
	float PullElapsedSeconds = 0.0f;

	/** 本次吸取曲线总时长，单位 s；由初始直线距离、参考速度和最短时长共同决定。 */
	float PullDurationSeconds = 0.0f;

	/** 本次吸取曲线弧高，单位 cm；由初始距离比例计算并受全局最大弧高限制。 */
	float PullArcHeight = 0.0f;

	/** 进入 Holding 后的持续时间，单位 s；从曲线完成时重新计时，用于延迟误差断开。 */
	float HoldingElapsedSeconds = 0.0f;

	/** 持有目标路径连续受阻时间，单位 s；路径恢复时清零，超过阈值则安全释放。 */
	float ObstructedElapsedSeconds = 0.0f;

	/** 本次吸取前的 Handle 目标插值状态；BeginPull 快照，正常到位或释放时恢复。 */
	bool bPreviousHandleInterpolateTarget = true;

	/** 为 true 表示本组件临时关闭了 Handle 目标插值，所有退出路径都必须恢复。 */
	bool bHandleTargetInterpolationOverridden = false;

	/** 曲线终点帧保持插值关闭；下一次 Holding Tick 开头恢复，避免额外尾部缓动。 */
	bool bRestoreHandleInterpolationNextTick = false;

	/** Configure 的缓存校验结果；只有引用和 Tuning 均合法时为 true。 */
	bool bConfigurationReady = false;

	/** 投掷或自动释放后保持 true，直到收到右键松开，防止长按立即重新抓取。 */
	bool bAwaitingGrabRelease = false;
};
