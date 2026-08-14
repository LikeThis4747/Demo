// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file BatteringRamHazardTuningData.h
 * 职责：保存自动周期冲锤的碰撞几何、行程、时序、重冲击预测窗口与来源响应比例。
 * 边界：不引用关卡、网格、角色或 PCG；不定义玩家与追猎者的受击差异。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "BatteringRamHazardTuningData.generated.h"

/** 自动周期冲锤第一版的唯一运行时调参来源。 */
UCLASS(BlueprintType)
class DEMO_API UBatteringRamHazardTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 校验公开参数；失败时返回具体属性和原因，不偷偷钳制运行时数值。 */
	bool IsConfigured(FString& OutError) const;

	/**
	 * ABatteringRamHazard::ApplyGeometry 读取的锤头碰撞盒半尺寸，单位 cm；默认 50/110/100，范围 1~500。
	 * X 决定运动方向厚度，Y/Z 决定走廊覆盖面；增大时命中更稳定，但需要同步增加墙体和地面净空。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|几何",
		meta = (ClampMin = "1.0", ClampMax = "500.0", Units = "cm"))
	FVector RamBodyHalfExtent = FVector(50.0f, 110.0f, 100.0f);

	/**
	 * ABatteringRamHazard::AdvanceLinearPhase 读取的完全伸出距离，单位 cm；默认 450，范围 10~1000。
	 * 调高会覆盖更宽的通道但更容易穿入对面墙体；Actor 局部 +X 是唯一伸出方向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|几何",
		meta = (ClampMin = "10.0", ClampMax = "1000.0", UIMin = "100.0", UIMax = "600.0", Units = "cm"))
	float StrokeDistance = 450.0f;

	/**
	 * ABatteringRamHazard::EnterWaiting 读取的完全缩回安全时间，单位 s；默认 1.8，范围 0.05~10。
	 * 调高会给玩家和 AI 更长通过窗口，调低会增加持续压力。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|时序",
		meta = (ClampMin = "0.05", ClampMax = "10.0", UIMin = "0.5", UIMax = "4.0", Units = "s"))
	float RetractedWaitSeconds = 1.8f;

	/**
	 * ABatteringRamHazard::EnterWarning 读取的静止预警时间，单位 s；默认 0.7，范围 0.05~5。
	 * 调高会提高可读性，调低会增加反应压力；本阶段只控制预警挂点可见性。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|时序",
		meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.2", UIMax = "2.0", Units = "s"))
	float WarningSeconds = 0.7f;

	/**
	 * ABatteringRamHazard::AdvanceLinearPhase 读取的伸出时间，单位 s；默认 0.25，范围 0.05~5。
	 * 调低会提高锤头速度和物理冲击，也会增加高速穿透风险；速度由 StrokeDistance/ExtensionSeconds 唯一决定。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|时序",
		meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.1", UIMax = "1.0", Units = "s"))
	float ExtensionSeconds = 0.25f;

	/**
	 * ABatteringRamHazard::AdvanceLinearPhase 读取的回收时间，单位 s；默认 0.8，范围 0.05~10。
	 * 调高会让安全窗口出现得更慢，调低会让下一轮更快开始；回收阶段不会发送重冲击准备请求。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|时序",
		meta = (ClampMin = "0.05", ClampMax = "10.0", UIMin = "0.2", UIMax = "2.0", Units = "s"))
	float RetractionSeconds = 0.8f;

	/**
	 * ABatteringRamHazard::ApplyGeometry 读取的锤头前方候选距离，单位 cm；默认 350，范围 10~1000。
	 * 调高可更早发现高速接收者但增加短时重叠候选，调低可能让 Prepared 请求来不及生效。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|重冲击预测",
		meta = (ClampMin = "10.0", ClampMax = "1000.0", UIMin = "100.0", UIMax = "600.0", Units = "cm"))
	float PreparationLookAheadDistance = 350.0f;

	/**
	 * ABatteringRamHazard::BuildPreparationRequest 读取的正常帧率最大预计接触时间，单位 s；默认 0.08，范围 0.08~0.5。
	 * 调高会更早切入接收端准备状态；实际 Sweep 还会被本次伸出的剩余时间截断。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|重冲击预测",
		meta = (ClampMin = "0.08", ClampMax = "0.5", UIMin = "0.08", UIMax = "0.25", Units = "s"))
	float MaximumPreparationLeadTime = 0.08f;

	/**
	 * ABatteringRamHazard::BuildPreparationRequest 写入 Heavy 请求的整体线性响应比例；默认 0.60，范围 0~1。
	 * 调低只削减首次真实接触沿冲锤方向新增的全身共同平移速度；局部转动、四肢相对运动和冲锤自身速度保持真实接触结果。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "机关|冲锤|重冲击响应",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.3", UIMax = "1.0"))
	float PhysicalResponseScale = 0.60f;
};
