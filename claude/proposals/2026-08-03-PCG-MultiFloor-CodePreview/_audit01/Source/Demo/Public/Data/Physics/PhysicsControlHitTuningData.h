// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PhysicsControlHitTuningData.h
 * 职责：集中配置 Manny 局部 Physics Control 受击的语义区域、骨骼入口、控制强度、冲量阈值与恢复时间。
 * 边界：只保存设计数据，不拥有运行时状态，不引用追猎者、AI、关卡或具体资产路径。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PhysicsControlHitTuningData.generated.h"

/** 局部受击的稳定语义区域；具体骨骼名称由 DataAsset 映射。 */
UENUM(BlueprintType)
enum class EPhysicsControlHitRegion : uint8
{
	Head,
	Torso,
	LeftArm,
	RightArm,
	LeftLeg,
	RightLeg
};

/** 单个语义区域的骨骼入口、启用状态和冲量倍率；数组中子肢体必须排在 Torso 前面。 */
USTRUCT(BlueprintType)
struct DEMO_API FPhysicsControlHitRegionSetup
{
	GENERATED_BODY()

	/** 稳定语义区域；运行时用它选择唯一活动肢体。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit")
	EPhysicsControlHitRegion Region = EPhysicsControlHitRegion::Torso;

	/** 当前骨架中该区域的根骨骼；由 GetLimbBonesFromSkeletalMesh 向子级展开。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit")
	FName StartBone;

	/** 是否允许该区域产生局部物理反应；禁用的 Head 仍可作为 Torso 的排除肢体。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit")
	bool bReactionEnabled = false;

	/** 该区域的无量纲冲量倍率；1 为不缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit", meta = (ClampMin = "0.01", ClampMax = "5.0"))
	float ImpulseScale = 1.0f;
};

/** Physics Control 局部受击的唯一配置源；缺失或非法时功能明确停用。 */
UCLASS(BlueprintType)
class DEMO_API UPhysicsControlHitTuningData final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 从叶到根排序的区域映射，避免父肢体提前吸收子肢体 Physics Body。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Regions")
	TArray<FPhysicsControlHitRegionSetup> Regions;

	/** 拉回动画目标的线性强度；调高会更硬，调低会更松。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Control", meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float LinearStrength = 8.0f;

	/** 拉回动画目标的角向强度；调高会更快恢复旋转。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Control", meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float AngularStrength = 6.0f;

	/** 线性与角向控制的阻尼比；1 为临界阻尼附近。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Control", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float DampingRatio = 1.0f;

	/** 单个控制可施加的最大线性力；调低可限制尖峰。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Control", meta = (ClampMin = "1.0"))
	float MaximumControlForce = 25000.0f;

	/** 单个控制可施加的最大扭矩；调低可限制角向尖峰。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Control", meta = (ClampMin = "1.0"))
	float MaximumControlTorque = 25000.0f;

	/** 活动区域的重力倍率；0 禁用重力，1 使用完整重力。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GravityMultiplier = 0.2f;

	/** 将 Chaos 接触冲量缩放为角色局部反应冲量；无量纲。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CollisionImpulseScale = 0.03f;

	/** 低于该冲量的接触不触发反应，避免轻微接触抖动。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Physics", meta = (ClampMin = "0.0"))
	float MinimumReactionImpulse = 50.0f;

	/** 施加到局部身体的最大冲量，限制高速或重物产生的尖峰。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Physics", meta = (ClampMin = "1.0"))
	float MaximumImpulse = 1200.0f;

	/** 单位秒；每次有效命中刷新 Timer，结束后关闭局部模拟并恢复动画。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics Hit|Recovery", meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
	float ReactionDuration = 0.45f;
};
