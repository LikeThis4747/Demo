// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactTypes.h
 * 职责：定义机关与角色共享的重冲击准备请求、返回结果和运行状态。
 * 边界：只描述接触预测，不施加冲量，也不依赖具体玩家、AI 或机关类型。
 */

#pragma once

#include "CoreMinimal.h"

#include "HeavyImpactTypes.generated.h"

class AActor;
class UPrimitiveComponent;

/** 项目内部的重冲击状态；不是 UE 官方状态机类型。 */
UENUM(BlueprintType)
enum class EHeavyImpactState : uint8
{
	Inactive,
	Prepared,
	Simulating,
	Settling,
	Downed,
	Recovering
};

/** 机关请求角色提前切换到物理身体时的明确结果。 */
UENUM(BlueprintType)
enum class EHeavyImpactPrepareResult : uint8
{
	Accepted,
	Duplicate,
	Busy,
	Invalid
};

/**
 * 机关在真实接触前提交的准备请求。
 * 该请求标识预期接触源与来源期望的整体线性响应比例；局部线性和角向变化仍由随后发生的 Chaos 接触产生。
 */
USTRUCT(BlueprintType)
struct DEMO_API FHeavyImpactPreparationRequest
{
	GENERATED_BODY()

	/** 同一次摆动或冲撞保持相同 ID，用于消除重叠和多刚体回调。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heavy Impact")
	FGuid ImpactId;

	/** 即将发生真实接触的机关 Actor。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heavy Impact")
	TObjectPtr<AActor> SourceActor = nullptr;

	/** 最终会与角色 Mesh 阻挡接触的真实刚体组件，不得传预测 Trigger。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heavy Impact")
	TObjectPtr<UPrimitiveComponent> SourceComponent = nullptr;

	/** 世界空间预测接触点，仅用于诊断预测质量，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heavy Impact")
	FVector PredictedImpactPoint = FVector::ZeroVector;

	/** 世界空间机关线速度；用于诊断并确定来源冲击方向，不会直接转成角色冲量，单位为厘米/秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heavy Impact")
	FVector SourceLinearVelocity = FVector::ZeroVector;

	/**
	 * 来源期望保留的整体线性物理响应比例；1 保留完整 Chaos 接触，0 只保留局部相对运动与角向响应。
	 * 接收组件只缩放该次接触沿 SourceLinearVelocity 新增的共同平移速度，不重放冲量，也不改变各刚体相对速度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heavy Impact",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhysicalResponseScale = 1.0f;

	/** 真实接触提交后由玩家结算的伤害；0 表示本次 Heavy 只保留物理响应。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heavy Impact",
		meta = (ClampMin = "0.0"))
	float Damage = 0.0f;

	/** 从请求时刻到预计接触的秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heavy Impact", meta = (ClampMin = "0.0"))
	float EstimatedTimeToContactSeconds = 0.0f;

	/** 校验请求自身关系和所有浮点输入；不检查接收组件的运行状态。 */
	bool IsStructurallyValid(const AActor* Receiver, FString& OutReason) const;
};

namespace Demo::HeavyImpact
{
	/** 以下均为本项目 Physics Control Asset Profile 名，不是 UE 内置 Profile。 */
	inline const FName ProfileInactive(TEXT("Inactive"));
	inline const FName ProfilePrepared(TEXT("Prepared"));
	inline const FName ProfileFlight(TEXT("Flight"));
	inline const FName ProfileLandingRecovery(TEXT("LandingRecovery"));
	inline const FName ProfileFreeFallback(TEXT("FreeFallback"));
}
