// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeInputConfig.h
 * 职责：集中声明《零号逃亡》玩家输入所需的 Mapping Context 与 Input Action 资源。
 * 边界：只保存输入资源引用和上下文优先级，不包含磁力手感、按键逻辑或运行时输入状态。
 * 状态 Owner：资产只拥有静态配置；输入上下文生命周期由 AZeroEscapeCharacter 管理。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ZeroEscapeInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/** 描述一个需要由本地玩家启用的输入映射上下文及其覆盖优先级。 */
USTRUCT(BlueprintType)
struct DEMO_API FZeroEscapeInputMappingContextConfig
{
	GENERATED_BODY()

	/**
	 * 对应 C++ 属性 MappingContext；由 AZeroEscapeCharacter::ApplyInputMappingContexts 读取。
	 * 初始值：空，必须在资产中指定；影响：缺失时整份输入配置校验失败，防止只启用半套输入。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入映射")
	TObjectPtr<UInputMappingContext> MappingContext;

	/**
	 * 对应 C++ 属性 Priority；由 Enhanced Input 决定同一按键冲突时的覆盖顺序。
	 * 初始值：0；建议范围：-10~100；调高后本上下文更优先，调低后更容易被其他上下文覆盖。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入映射", meta = (UIMin = "-10", UIMax = "100"))
	int32 Priority = 0;
};

/** 《零号逃亡》输入资源的唯一配置源，与磁力手感 DataAsset 完全解耦。 */
UCLASS(BlueprintType)
class DEMO_API UZeroEscapeInputConfig final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 检查所有必填资源及上下文唯一性；失败时返回可直接定位编辑器配置的中文原因。 */
	bool IsConfigured(FString& OutError) const;

	/**
	 * 对应 C++ 属性 MappingContexts；由 AZeroEscapeCharacter 在占有开始/结束时统一添加和移除。
	 * 初始值：空数组；建议数量：1~8；数量过多会增加冲突排查成本，重复上下文会被校验拒绝。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入|映射上下文", meta = (TitleProperty = "MappingContext"))
	TArray<FZeroEscapeInputMappingContextConfig> MappingContexts;

	/**
	 * 对应 C++ 属性 MoveAction；由 AZeroEscapeCharacter::SetupPlayerInputComponent 绑定移动。
	 * 初始值：空，必须指定 Axis2D 类型动作；缺失时输入功能停用并输出明确错误。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入|动作")
	TObjectPtr<UInputAction> MoveAction;

	/**
	 * 对应 C++ 属性 LookAction；绑定手柄 Axis2D 视角输入。
	 * 初始值：空，必须指定；摇杆灵敏度和死区继续由 Input Action/Mapping Context 配置。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入|动作")
	TObjectPtr<UInputAction> LookAction;

	/**
	 * 对应 C++ 属性 MouseLookAction；绑定鼠标 Axis2D 视角输入。
	 * 初始值：空，必须指定；鼠标倍率与方向修饰器继续由 Mapping Context 配置。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入|动作")
	TObjectPtr<UInputAction> MouseLookAction;

	/**
	 * 对应 C++ 属性 JumpAction；绑定跳跃开始、完成和取消。
	 * 初始值：空，必须指定 Boolean 类型动作；缺失时不会注册跳跃输入。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入|动作")
	TObjectPtr<UInputAction> JumpAction;

	/**
	 * 对应 C++ 属性 MagneticGrabAction；绑定右键按下抓取、松开或取消时放下。
	 * 初始值：空，必须指定 Boolean 类型动作；按键本身应在独立磁力 Mapping Context 中配置。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入|动作")
	TObjectPtr<UInputAction> MagneticGrabAction;

	/**
	 * 对应 C++ 属性 MagneticThrowAction；绑定持有期间的左键投掷请求。
	 * 初始值：空，必须指定 Boolean 类型动作；本轮只使用 Started，蓄力语义后续单独扩展。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入|动作")
	TObjectPtr<UInputAction> MagneticThrowAction;

	/** 持有磁力物时切换普通/爆裂状态的 Boolean 动作；首版由 E 键触发。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "输入|动作")
	TObjectPtr<UInputAction> MagneticExplosionModeAction;
};
