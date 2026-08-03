// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameInstance.h
 * 职责：跨关卡持有"下一局"的生成请求（Seed + 难度），供主菜单写入、游戏关卡读取。
 * 边界：只存本局请求参数，不驱动生成、不管胜负、不做存档或统计。
 * 状态 Owner：本类是待开始一局的 Seed/难度的唯一持有者。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeGameInstance.generated.h"

/** 单机会话级 GameInstance：在关卡切换间保留本局 Seed 与难度。 */
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
	void SetPendingRequest(const FZeroEscapeGenerationRequest& InRequest) { PendingRequest = InRequest; }

	/** 便利入口：单独设置 Seed，不改动已选难度。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Round")
	void SetPendingSeed(int32 InSeed) { PendingRequest.Seed = InSeed; }

	/** 便利入口：单独设置难度，不改动已选 Seed。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Round")
	void SetPendingDifficulty(EZeroEscapeDifficulty InDifficulty) { PendingRequest.Difficulty = InDifficulty; }

private:
	/** 待开始一局的 Seed 与难度；默认值来自 FZeroEscapeGenerationRequest。 */
	UPROPERTY(VisibleAnywhere, Category = "ZeroEscape|Round")
	FZeroEscapeGenerationRequest PendingRequest;
};
