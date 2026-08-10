// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeCharacter.h
 * 职责：装配第三人称相机、磁力与重冲击组件，并把 Enhanced Input 和机关接口转发给对应系统。
 * 边界：角色不实现磁力或物理受击算法，不直接施加冲量，也不硬编码输入与物理资源路径。
 * 状态 Owner：输入上下文由本类管理；磁力与重冲击状态分别由两个专用组件独占。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CharacterImpactReceiver.h"
#include "Interfaces/HeavyImpactReceiver.h"

#include "ZeroEscapeCharacter.generated.h"

class UCameraComponent;
class UElectromagneticGrabComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UCharacterImpactResponseComponent;
class UCharacterImpactTuningData;
class UHeavyImpactResponseComponent;
class UHeavyImpactTuningData;
class UHealthComponent;
class UPhysicsHandleComponent;
class UPhysicsControlComponent;
class USpringArmComponent;
class UZeroEscapeInputConfig;
struct FInputActionValue;

/** 第三人称玩家角色，只负责组件装配、移动、相机和玩家意图转发。 */
UCLASS()
class DEMO_API AZeroEscapeCharacter final
	: public ACharacter
	, public IHeavyImpactReceiver
	, public ICharacterImpactReceiver
{
	GENERATED_BODY()

public:
	/** 创建第三人称过肩相机、Physics Handle 与电磁抓取能力组件。 */
	AZeroEscapeCharacter();

	/** 重冲击预测与真实接触统一使用角色 Skeletal Mesh，而不是外层移动 Capsule。 */
	virtual UPrimitiveComponent* GetHeavyImpactPredictionPrimitive_Implementation() const override;

	/** 把机关的重冲击准备请求转发给唯一共享响应组件。 */
	virtual EHeavyImpactPrepareResult PrepareForHeavyImpact_Implementation(
		const FHeavyImpactPreparationRequest& Request) override;

	/** 把命中后的站立轻受击请求转发给角色共享协调组件。 */
	virtual EStandingImpactSubmitResult SubmitStandingImpact_Implementation(
		const FStandingImpactRequest& Request) override;

	/** 本地 Pawn 再次可玩时，按输入 DataAsset 幂等应用本角色拥有的映射。 */
	virtual void PawnClientRestart() override;

	/** 失去占有前移除本角色添加的上下文、放下物体并停止跳跃。 */
	virtual void UnPossessed() override;

protected:
	/** 所有 C++ 与蓝图默认子对象初始化后，把相机和 Physics Handle 接入磁力组件。 */
	virtual void PostInitializeComponents() override;

	/** 从输入 DataAsset 绑定移动、视角、跳跃、抓取、放下与投掷动作。 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	/** 仅在重冲击组件空闲时允许移动、跳跃、抓取与投掷等身体输入。 */
	bool CanAcceptBodyInput() const;

	/** 返回当前本地玩家的 Enhanced Input 子系统；非本地或未占有时返回空。 */
	UEnhancedInputLocalPlayerSubsystem* FindInputSubsystem() const;

	/** 校验输入 DataAsset，并以“先移除、后添加”的幂等方式启用全部 Mapping Context。 */
	void ApplyInputMappingContexts();

	/** 只移除本输入 DataAsset 声明的 Mapping Context，不影响其他系统的输入层。 */
	void RemoveInputMappingContexts();

	/** 把 Axis2D 移动输入转换为仅受相机 Yaw 影响的前后和左右移动。 */
	void Move(const FInputActionValue& Value);

	/** 把 Axis2D 视角输入转发为控制器 Yaw 与 Pitch。 */
	void Look(const FInputActionValue& Value);

	/** 仅在身体输入可用时把跳跃意图转发给 ACharacter。 */
	void TryJump();

	/** 将右键按下意图转发给磁力状态 Owner。 */
	void BeginMagneticGrab();

	/** 将右键松开或取消意图转发给磁力状态 Owner，并解除投掷后的输入锁。 */
	void EndMagneticGrab();

	/** 将左键投掷意图转发给磁力状态 Owner。 */
	void ThrowMagneticObject();

	/** 真实重物接触提交后中断正在进行的磁力吸取或持有；空手时不产生副作用。 */
	void HandleHeavyImpactCommitted(const FHeavyImpactPreparationRequest& Request);

	/**
	 * 第三人称弹簧臂；负责相机距离与碰撞回缩。
	 * 初始值：长度 300 cm、SocketOffset=(0,55,65) cm；最终过肩构图可直接在角色蓝图组件上调整。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "相机", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** 玩家视角相机；由移动朝向、屏幕选取和投掷瞄准共同读取，生命周期随角色。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "相机", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	/** UE Physics Handle 组件；只执行 Chaos 刚体约束与插值，玩法策略由磁力组件决定。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "磁力", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsHandleComponent> PhysicsHandle;

	/** 磁力选取、持有、放下、投掷与安全恢复的唯一运行时状态 Owner。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "磁力", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UElectromagneticGrabComponent> ElectromagneticGrab;

	/** 玩家生命组件；监听 OnTakeAnyDamage 结算受伤，供地刺等通过 ApplyDamage 的伤害源作用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "属性", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHealthComponent> HealthComponent;

	/** 官方 Physics Control 求解组件；只由重冲击响应组件创建和驱动运行时记录。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "重冲击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsControlComponent> PhysicsControl;

	/** 玩家重冲击准备、真实接触、飞行和倒地状态的唯一运行时 Owner。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "重冲击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHeavyImpactResponseComponent> HeavyImpactResponse;

	/** 玩家站立轻受击、速度恢复与 Heavy 抢占的运行时 Owner。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "轻受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCharacterImpactResponseComponent> CharacterImpactResponse;

	/** 玩家独立的重冲击 PCA 与判稳参数；必须在 BP_ZeroEscapeCharacter 类默认值中指定。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "重冲击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHeavyImpactTuningData> HeavyImpactTuningData;

	/** 玩家轻受击动画和时序参数；动画可暂时为空，必须在角色蓝图类默认值中指定资产。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "轻受击", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCharacterImpactTuningData> CharacterImpactTuningData;

	/**
	 * 输入资源唯一来源；对应 UZeroEscapeInputConfig，由 PawnClientRestart 与 SetupPlayerInputComponent 读取。
	 * 初始值：空，必须在 BP_ZeroEscapeCharacter 中指定；缺失或非法时输出错误并停用输入，不使用路径兜底。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "输入", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UZeroEscapeInputConfig> InputConfig;
};
