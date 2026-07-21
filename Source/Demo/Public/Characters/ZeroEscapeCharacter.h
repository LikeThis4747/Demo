// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeCharacter.h
 * 职责：装配第三人称相机、Physics Handle 与磁力组件，并把 Enhanced Input 意图转发给对应系统。
 * 边界：角色不保存磁力手感参数，不实现选取、持有或投掷算法，也不硬编码输入资源路径。
 * 状态 Owner：输入上下文生命周期由本类管理；磁力交互状态由 UElectromagneticGrabComponent 独占。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "ZeroEscapeCharacter.generated.h"

class UCameraComponent;
class UElectromagneticGrabComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UPhysicsHandleComponent;
class USpringArmComponent;
class UZeroEscapeInputConfig;
struct FInputActionValue;

/** 第三人称玩家角色，只负责组件装配、移动、相机和玩家意图转发。 */
UCLASS()
class DEMO_API AZeroEscapeCharacter final : public ACharacter
{
	GENERATED_BODY()

public:
	/** 创建第三人称过肩相机、Physics Handle 与电磁抓取能力组件。 */
	AZeroEscapeCharacter();

	/** 本地 Pawn 再次可玩时，先清理旧上下文再按输入 DataAsset 重建映射。 */
	virtual void PawnClientRestart() override;

	/** 失去占有前移除本角色添加的上下文、放下物体并清空待处理移动输入。 */
	virtual void UnPossessed() override;

protected:
	/** 所有 C++ 与蓝图默认子对象初始化后，把相机和 Physics Handle 接入磁力组件。 */
	virtual void PostInitializeComponents() override;

	/** 从输入 DataAsset 绑定移动、视角、跳跃、抓取、放下与投掷动作。 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	/** 返回当前本地玩家的 Enhanced Input 子系统；非本地或未占有时返回空。 */
	UEnhancedInputLocalPlayerSubsystem* FindInputSubsystem() const;

	/** 校验输入 DataAsset，并以“先移除、后添加”的幂等方式启用全部 Mapping Context。 */
	void ApplyInputMappingContexts();

	/** 只移除本输入 DataAsset 声明的 Mapping Context，不影响其他系统的输入层。 */
	void RemoveInputMappingContexts();

	/** 把 Axis2D 移动输入转换为仅受相机 Yaw 影响的前后和左右移动。 */
	void Move(const FInputActionValue& Value);

	/** 在移动动作完成或取消时清空本帧尚未消费的输入，避免旧方向残留。 */
	void ClearMoveInput(const FInputActionValue& Value);

	/** 把 Axis2D 视角输入转发为控制器 Yaw 与 Pitch。 */
	void Look(const FInputActionValue& Value);

	/** 将右键按下意图转发给磁力状态 Owner。 */
	void BeginMagneticGrab();

	/** 将右键松开或取消意图转发给磁力状态 Owner，并解除投掷后的输入锁。 */
	void EndMagneticGrab();

	/** 将左键投掷意图转发给磁力状态 Owner。 */
	void ThrowMagneticObject();

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

	/**
	 * 输入资源唯一来源；对应 UZeroEscapeInputConfig，由 PawnClientRestart 与 SetupPlayerInputComponent 读取。
	 * 初始值：空，必须在 BP_ZeroEscapeCharacter 中指定；缺失或非法时输出错误并停用输入，不使用路径兜底。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "输入", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UZeroEscapeInputConfig> InputConfig;
};
