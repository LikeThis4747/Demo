// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameInstance.h
 * 职责：跨关卡持有“下一局”的不可变生成请求身份（公开 Seed + 难度）。
 * 边界：只响应菜单等显式调用更新请求，不驱动生成、不管胜负、不做存档或统计。
 * 状态 Owner：本类是待开始一局生成请求的唯一持有者；运行时失败不得改写请求。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeGameInstance.generated.h"

/** 单机会话级 GameInstance：在关卡切换间保留玩家确认的本局请求。 */
UCLASS()
class DEMO_API UZeroEscapeGameInstance final : public UGameInstance
{
	GENERATED_BODY()

public:
	/** 读取当前待开始一局的生成请求（Seed + 难度）。 */
	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Round")
	const FZeroEscapeGenerationRequest& GetPendingRequest() const { return PendingRequest; }

	/** 由主菜单整体写入本局请求；游戏关卡随后读取用于生成。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Round")
	void SetPendingRequest(const FZeroEscapeGenerationRequest& InRequest);

	/** 便利入口：单独设置 Seed，不改动已选难度。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Round")
	void SetPendingSeed(int32 InSeed);

	/** 便利入口：单独设置难度，不改动已选 Seed。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Round")
	void SetPendingDifficulty(EZeroEscapeDifficulty InDifficulty);

private:
	/** 待开始一局的 Seed 与难度；默认值来自 FZeroEscapeGenerationRequest。 */
	UPROPERTY(VisibleAnywhere, Category = "ZeroEscape|Round")
	FZeroEscapeGenerationRequest PendingRequest;
};
