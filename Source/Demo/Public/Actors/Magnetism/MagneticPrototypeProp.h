// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file MagneticPrototypeProp.h
 * 职责：声明首个可玩交互测试使用的自包含磁性物理道具。
 * 状态边界：Actor 负责表现与初始质量，Chaos 负责刚体运动，磁性标记组件负责单物体磁力配置。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MagneticPrototypeProp.generated.h"

class UMagneticObjectComponent;
class UStaticMeshComponent;

/** 可复用的最小磁性刚体，后续可由运行时 PCG 系统生成。 */
UCLASS()
class DEMO_API AMagneticPrototypeProp final : public AActor
{
	GENERATED_BODY()

public:
	/** 创建模拟物理的立方体刚体，并挂接磁力交互契约。 */
	AMagneticPrototypeProp();

	/** 为运行时原型测试体设置实例级形状缩放与质量。 */
	void ConfigurePrototype(const FVector& InScale, float InMassKilograms);

protected:
	/** Chaos 创建运行时物理状态后，重新应用配置质量。 */
	virtual void BeginPlay() override;

private:
	/** 刚体有效时，将配置的质量覆盖值应用到当前物理实例。 */
	void ApplyConfiguredMass();

	/** 模拟物理的根刚体，负责渲染、碰撞、旋转并接收 Physics Handle 作用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prototype", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> MagneticBody;

	/** 供电磁抓取组件读取的磁性标记与单物体配置契约。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prototype", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMagneticObjectComponent> MagneticObject;

	/** 物理状态创建后及原型配置变化时应用的初始质量，单位 kg。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prototype", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", Units = "kg"))
	float InitialMassKilograms = 20.0f;
};
