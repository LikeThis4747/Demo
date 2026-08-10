// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ThrustGuidedHazardLauncher.h
 * 职责：拥有壁挂机关的首个角色锁定、预警、Muzzle 初始瞄准和一次性弹体生成。
 * 边界：不模拟弹体、不持续追踪、不判断受击，不依赖关卡名、Actor 名或组件名查找。
 * 状态 Owner：本 Actor 唯一写入 Armed/Warning/Spent/Disabled、锁定目标和预警 Timer。
 * 轴约定：Muzzle 局部 +X 是发射中心线；弹体自身会把局部 +Z 对齐该世界方向。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"

#include "ThrustGuidedHazardLauncher.generated.h"

class ACharacter;
class AThrustGuidedHazardProjectile;
class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UThrustGuidedHazardTuningData;

/** 项目内部的壁挂发射器阶段，不是 UE 官方状态机类型。 */
enum class EThrustGuidedHazardLauncherPhase : uint8
{
	Armed,
	Warning,
	Spent,
	Disabled
};

/** 锁定首个进入角色、预警并发射一次真实物理弹体的壁挂机关。 */
UCLASS()
class DEMO_API AThrustGuidedHazardLauncher final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建壁挂外壳、可独立摆放的触发锚点、发射口和预警挂点；本 Actor 永不 Tick。 */
	AThrustGuidedHazardLauncher();

	/** 用配置资产或 CDO 默认值预览 TriggerVolume 尺寸，但不覆盖 TriggerAnchor/Muzzle 变换。 */
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	/** 校验配置和单位缩放，绑定首个角色触发并进入 Armed。 */
	virtual void BeginPlay() override;

	/** 清理预警 Timer、重叠委托和锁定目标。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 只设置 TriggerVolume 半尺寸；空间位置和旋转由 TriggerAnchor 唯一决定。 */
	void ApplyTriggerGeometry(const UThrustGuidedHazardTuningData& Tuning);

	/** 锁定首个 ACharacter、关闭后续触发并开始预警。 */
	void EnterWarning(ACharacter& TargetCharacter);

	/** 预警结束后计算完整出生姿态、检查净空，并延迟生成一个弹体。 */
	void FireLockedTarget();

	/** 目标在预警期间失效时恢复可触发状态；不换成第二目标。 */
	void ReturnToArmedAfterCancelledWarning(const TCHAR* Reason);

	/** 返回动态前置目标方向钳制到 Muzzle 前方锥体后的单位向量。 */
	FVector CalculateInitialLaunchDirection(const USceneComponent* TargetComponent) const;

	/** 计算初始方向、完整胶囊出生中心和旋转，并执行出生净空检查。 */
	bool TryBuildSpawnTransform(
		const USceneComponent& TargetComponent,
		FTransform& OutSpawnTransform,
		FString& OutFailureReason) const;

	/** 用弹体作为 PhysicsBody 时的真实通道响应检查整个出生胶囊。 */
	bool IsSpawnPoseClear(
		const FTransform& SpawnTransform,
		FString& OutFailureReason) const;

	/** 清理触发和预警状态并记录明确原因；不会生成隐藏兜底弹体。 */
	void DisableHazard(const FString& Reason);

	/** 第一个 Character 进入 Query-only TriggerVolume 时锁定；其他 Actor 和后续角色被忽略。 */
	UFUNCTION()
	void HandleTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** 固定壁挂基准；Blueprint 在其下装配支架、管线和墙柜。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 固定外壳纯美术挂点，不参与触发、碰撞或瞄准。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> HousingVisualRoot;

	/**
	 * 发射口空间基准；世界位置表示炮管出口平面，局部 +X 是初始瞄准中心线。
	 * 弹体中心沿最终初始方向再前移 ProjectileHalfHeight + SpawnClearanceMargin。
	 * Blueprint/关卡实例可调整其相对变换，以适配正面墙或斜角墙。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Muzzle;

	/**
	 * 触发区空间基准；可独立于 Muzzle 移动和旋转。
	 * 拐角摆位必须把它放到目标已经越过遮挡、Muzzle 到目标存在净空的位置。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> TriggerAnchor;

	/** TriggerAnchor 原点上的全高 Query-only Pawn 触发盒；不是压力板，不阻挡任何对象。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> TriggerVolume;

	/** 纯美术预警挂点；C++ 只控制可见性，不指定灯光、材质、音效或粒子。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "机关|物理制导",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> WarningVisualRoot;

	/** V1 唯一配置来源；缺失或非法时发射器明确进入 Disabled。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "机关|物理制导|配置",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UThrustGuidedHazardTuningData> TuningData;

	/** 延迟生成的弹体 Blueprint/C++ 类；必须继承 AThrustGuidedHazardProjectile。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "机关|物理制导|配置",
		meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AThrustGuidedHazardProjectile> ProjectileClass;

	/** 当前一次性阶段，只由本 Actor 的阶段函数写入。 */
	EThrustGuidedHazardLauncherPhase Phase = EThrustGuidedHazardLauncherPhase::Disabled;

	/** 预警时锁定的首个 Character；只用于生命周期检查和诊断。 */
	TWeakObjectPtr<ACharacter> LockedTargetActor;

	/** 锁定角色的根组件；发射时读取当前位置，弹体推进期继续以弱引用读取。 */
	TWeakObjectPtr<USceneComponent> LockedTargetComponent;

	/** 预警结束的一次性 Timer；Spent/Disabled/EndPlay 必须清理。 */
	FTimerHandle WarningTimerHandle;

	/** 目标失效后同步重开碰撞时，阻止盒内第二名角色在同一调用栈自动接管。 */
	bool bSuppressTriggerOverlap = false;
};
