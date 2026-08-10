// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file CharacterImpactReceiver.h
 * 职责：提供轻受击来源到玩家/追猎者的最小命中后接口。
 * 边界：不替代 Heavy 的接触前准备接口，也不让来源依赖具体角色类。
 */

#pragma once

#include "CoreMinimal.h"
#include "Physics/CharacterImpactTypes.h"
#include "UObject/Interface.h"

#include "CharacterImpactReceiver.generated.h"

UINTERFACE(BlueprintType)
class DEMO_API UCharacterImpactReceiver : public UInterface
{
	GENERATED_BODY()
};

class DEMO_API ICharacterImpactReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Character Impact")
	EStandingImpactSubmitResult SubmitStandingImpact(const FStandingImpactRequest& Request);
};
