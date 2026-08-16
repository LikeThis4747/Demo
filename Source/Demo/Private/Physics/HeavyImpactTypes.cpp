// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactTypes.cpp
 * 职责：校验跨机关和角色边界传递的重冲击准备请求。
 * 边界：不查询世界、不改变碰撞，也不决定角色是否接受请求。
 */

#include "Physics/HeavyImpactTypes.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

/** 拒绝无效对象关系、非有限预测值和负的预计接触时间。 */
bool FHeavyImpactPreparationRequest::IsStructurallyValid(
	const AActor* Receiver,
	FString& OutReason) const
{
	if (!IsValid(Receiver))
	{
		OutReason = TEXT("Receiver is invalid.");
		return false;
	}

	if (!ImpactId.IsValid())
	{
		OutReason = TEXT("ImpactId is invalid.");
		return false;
	}

	if (!IsValid(SourceActor) || !IsValid(SourceComponent))
	{
		OutReason = TEXT("Source actor or component is invalid.");
		return false;
	}

	if (SourceComponent->GetOwner() != SourceActor)
	{
		OutReason = TEXT("SourceComponent does not belong to SourceActor.");
		return false;
	}

	if (SourceActor == Receiver)
	{
		OutReason = TEXT("Receiver cannot prepare for its own component.");
		return false;
	}

	if (PredictedImpactPoint.ContainsNaN() || SourceLinearVelocity.ContainsNaN())
	{
		OutReason = TEXT("Prediction vectors must contain only finite values.");
		return false;
	}

	if (!FMath::IsFinite(EstimatedTimeToContactSeconds)
		|| EstimatedTimeToContactSeconds < 0.0f)
	{
		OutReason = TEXT("Estimated contact time must be finite and non-negative.");
		return false;
	}

	if (!FMath::IsFinite(PhysicalResponseScale)
		|| PhysicalResponseScale < 0.0f
		|| PhysicalResponseScale > 1.0f)
	{
		OutReason = TEXT("Physical response scale must be finite and within 0..1.");
		return false;
	}

	if (!FMath::IsFinite(Damage) || Damage < 0.0f)
	{
		OutReason = TEXT("Damage must be finite and non-negative.");
		return false;
	}

	OutReason.Reset();
	return true;
}
