// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeInputConfig.cpp
 * 职责：校验输入 DataAsset 的完整性，避免运行时悄悄启用缺少动作或重复上下文的半套配置。
 * 边界：不加载资源、不修改本地玩家输入栈，也不为缺失资源提供硬编码路径兜底。
 * 状态 Owner：校验只读取资产；运行时上下文仍由 AZeroEscapeCharacter 管理。
 */

#include "Data/Input/ZeroEscapeInputConfig.h"

#include "InputAction.h"
#include "InputMappingContext.h"

/** 校验所有必填输入资源，并把第一个失败原因返回给调用者用于日志定位。 */
bool UZeroEscapeInputConfig::IsConfigured(FString& OutError) const
{
	OutError.Reset();

	if (MappingContexts.IsEmpty())
	{
		OutError = TEXT("输入 DataAsset 未配置任何 Mapping Context。");
		return false;
	}

	TSet<const UInputMappingContext*> UniqueContexts;
	for (int32 Index = 0; Index < MappingContexts.Num(); ++Index)
	{
		const UInputMappingContext* Context = MappingContexts[Index].MappingContext.Get();
		if (!IsValid(Context))
		{
			OutError = FString::Printf(TEXT("输入 DataAsset 的 MappingContexts[%d] 为空。"), Index);
			return false;
		}

		if (UniqueContexts.Contains(Context))
		{
			OutError = FString::Printf(TEXT("输入 DataAsset 重复配置了 Mapping Context：%s。"), *Context->GetName());
			return false;
		}

		UniqueContexts.Add(Context);
	}

	struct FRequiredAction
	{
		/** 编辑器属性名，用于生成可直接定位的错误文本。 */
		const TCHAR* PropertyName;

		/** 当前待校验的 Input Action 资源。 */
		const UInputAction* Action;

		/** 本属性要求的值类型，避免 Axis2D 与 Boolean 动作误装配后在回调中读取错误类型。 */
		EInputActionValueType ExpectedValueType;

		/** 面向编辑器配置者显示的预期类型名称。 */
		const TCHAR* ExpectedTypeName;
	};

	const FRequiredAction RequiredActions[] =
	{
		{TEXT("MoveAction"), MoveAction.Get(), EInputActionValueType::Axis2D, TEXT("Axis2D")},
		{TEXT("LookAction"), LookAction.Get(), EInputActionValueType::Axis2D, TEXT("Axis2D")},
		{TEXT("MouseLookAction"), MouseLookAction.Get(), EInputActionValueType::Axis2D, TEXT("Axis2D")},
		{TEXT("JumpAction"), JumpAction.Get(), EInputActionValueType::Boolean, TEXT("Boolean")},
		{TEXT("MagneticGrabAction"), MagneticGrabAction.Get(), EInputActionValueType::Boolean, TEXT("Boolean")},
		{TEXT("MagneticThrowAction"), MagneticThrowAction.Get(), EInputActionValueType::Boolean, TEXT("Boolean")},
		{TEXT("MagneticExplosionModeAction"), MagneticExplosionModeAction.Get(), EInputActionValueType::Boolean, TEXT("Boolean")}
	};

	for (const FRequiredAction& RequiredAction : RequiredActions)
	{
		if (!IsValid(RequiredAction.Action))
		{
			OutError = FString::Printf(TEXT("输入 DataAsset 的 %s 为空。"), RequiredAction.PropertyName);
			return false;
		}

		if (RequiredAction.Action->ValueType != RequiredAction.ExpectedValueType)
		{
			OutError = FString::Printf(
				TEXT("输入 DataAsset 的 %s 值类型错误，预期 %s。"),
				RequiredAction.PropertyName,
				RequiredAction.ExpectedTypeName);
			return false;
		}
	}

	return true;
}
