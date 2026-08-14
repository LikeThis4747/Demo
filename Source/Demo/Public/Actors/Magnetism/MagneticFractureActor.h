// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticFractureActor.h
 * 职责：声明只承担短命 Geometry Collection 破碎表现的运行时替身。
 * 边界：不包含磁性资格、抓取、伤害、Light/Heavy 或可重复使用资源逻辑。
 * 状态 Owner：本 Actor 只拥有继承运动、显式解簇和自身 LifeSpan 清理。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MagneticFractureActor.generated.h"

class UGeometryCollectionComponent;

/** 命中后替换完整磁力物的短命碎片表现 Actor。 */
UCLASS()
class DEMO_API AMagneticFractureActor final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建无 Tick 的 Geometry Collection 根组件，并在 C++ 固定碎片碰撞与清理契约。 */
	AMagneticFractureActor();

	/** deferred spawn 完成前注入完整刚体的世界线速度和弧度制角速度。 */
	void SetInheritedMotion(
		const FVector& LinearVelocity,
		const FVector& AngularVelocityRadians,
		float SeparationMultiplier = 1.0f);

protected:
	/** 校验 RestCollection，启动兜底 LifeSpan，并显式解开当前活动簇。 */
	virtual void BeginPlay() override;

private:
	/**
	 * Geometry Collection 唯一根组件；Blueprint 只负责指定 RestCollection。
	 * 碰撞固定为 Block WorldStatic/WorldDynamic，Ignore Pawn/Camera/PhysicsBody。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGeometryCollectionComponent> GeometryCollection;

	/**
	 * Actor 异常兜底寿命，单位 s；默认且最小运行值为 6。
	 * 必须大于当前 Remove On Break 最长 3 秒，正常清理由资产叶子数据完成。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true", ClampMin = "6.0", UIMin = "6.0", Units = "s"))
	float SafetyLifetimeSeconds = 6.0f;

	/**
	 * 解簇后附加给碎片的径向速度改变量，单位 cm/s；初始值 350。
	 * 该值只负责把相邻碎片推开，整体前进速度仍来自命中当帧冻结的继承运动。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
	float FragmentSeparationSpeed = 350.0f;

	/**
	 * 径向速度场半径相对 Geometry Collection 包围球的倍率；初始值 1.25。
	 * 大于 1 可覆盖边缘碎片，线性衰减仍会让中心附近碎片获得更明显的分离速度。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "磁力物|破碎", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", UIMin = "1.0"))
	float FragmentSeparationRadiusScale = 1.25f;

	/** deferred spawn 前由单次投掷事务写入；普通破碎保持 1，不修改 Blueprint 默认值。 */
	float FragmentSeparationMultiplier = 1.0f;
};
