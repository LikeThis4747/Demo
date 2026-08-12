// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file CharacterImpactTypes.h
 * 职责：定义站立轻受击的结果、来源规格、一次命中请求与提交结果。
 * 边界：不包含 Heavy、PCA、伤害或具体机关逻辑；Heavy 继续使用独立的接触前准备协议。
 */

#pragma once

#include "CoreMinimal.h"

#include "CharacterImpactTypes.generated.h"

class AActor;
class UCharacterImpactSourceProfile;
class UPrimitiveComponent;

/** 本项目站立轻受击的玩法结果，不是 UE 官方枚举。 */
UENUM(BlueprintType)
enum class EStandingImpactResult : uint8
{
	None,
	Slow,
	Stop
};

/** 当前项目支持的接收者类别；由角色 C++ 固定注入，不由资产反推。 */
UENUM(BlueprintType)
enum class EImpactReceiverCategory : uint8
{
	Player,
	Pursuer
};

/** 来源 Profile 对单类角色声明的站立结果。 */
USTRUCT(BlueprintType)
struct DEMO_API FStandingImpactReactionSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Standing Impact")
	EStandingImpactResult Result = EStandingImpactResult::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Standing Impact",
		meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "s"))
	float DurationSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Standing Impact",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpeedMultiplier = 1.0f;

	/** V1 只允许 Stop 在已有 DefaultSlot 播放全身反应；Slow 保持移动且不播全身动画。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Standing Impact")
	bool bPlayReactionAnimation = false;

	/** 该来源是否允许接收者叠加短暂的局部物理表现；不改变 Slow / Stop 玩法结果。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Standing Impact")
	bool bApplyPhysicalReaction = false;

	bool IsConfigured(const TCHAR* PropertyPrefix, FString& OutError) const;
};

/** 来源在确认一次命中后提交的不可排队请求。 */
USTRUCT(BlueprintType)
struct DEMO_API FStandingImpactRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Standing Impact")
	FGuid ImpactId;

	UPROPERTY(BlueprintReadWrite, Category = "Standing Impact")
	TObjectPtr<AActor> SourceActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Standing Impact")
	TObjectPtr<UPrimitiveComponent> SourceComponent = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Standing Impact")
	TObjectPtr<UCharacterImpactSourceProfile> SourceProfile = nullptr;

	/** 目标身体被推走的世界空间方向，而不是来源飞行方向或命中法线本身。 */
	UPROPERTY(BlueprintReadWrite, Category = "Standing Impact")
	FVector WorldDirection = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Standing Impact")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Standing Impact",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalizedStrength = 1.0f;

	/** 真实刚体来源可附带 Chaos NormalImpulse；触发来源保持零向量。 */
	UPROPERTY(BlueprintReadWrite, Category = "Standing Impact")
	FVector RawNormalImpulse = FVector::ZeroVector;

	bool IsStructurallyValid(const AActor* Receiver, FString& OutError) const;
};

/** 站立请求的同步结果；任何结果都不表示排队或稍后重试。 */
UENUM(BlueprintType)
enum class EStandingImpactSubmitResult : uint8
{
	Applied,
	Ignored,
	Duplicate,
	HeavyBusy,
	Invalid
};

namespace Demo::CharacterImpact
{
	inline const FName DefaultSlot(TEXT("DefaultSlot"));
	inline constexpr int32 RecentImpactHistorySize = 16;
}
