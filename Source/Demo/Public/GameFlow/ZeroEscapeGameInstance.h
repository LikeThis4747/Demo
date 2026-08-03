// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameInstance.h
 * 职责：跨关卡持有“下一局”的生成请求（Seed + 难度）与有限自动重试次数。
 * 边界：只原子更新请求和重试计数，不驱动生成、不管胜负、不做存档或统计。
 * 状态 Owner：本类是待开始一局的请求与跨 World 自动重试次数的唯一持有者。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "PCG/ZeroEscapeGenerationTypes.h"

#include "ZeroEscapeGameInstance.generated.h"

/** 单机会话级 GameInstance：在关卡切换间保留本局请求与有限自动重试次数。 */
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

	/**
	 * 为可恢复的开局失败原子地推进 Seed 与次数；达到上限时不修改任何状态。
	 * 返回的 RetryNumber 从 1 开始，由 GameMode 记录后重载正式游戏关卡。
	 */
	bool TryAdvancePendingRequestForAutomaticRetry(
		int32& OutRetryNumber,
		int32& OutPreviousSeed,
		int32& OutNextSeed);

	static constexpr int32 MaxAutomaticGenerationRetryCount = 3;

private:
	/** 待开始一局的 Seed 与难度；默认值来自 FZeroEscapeGenerationRequest。 */
	UPROPERTY(VisibleAnywhere, Category = "ZeroEscape|Round")
	FZeroEscapeGenerationRequest PendingRequest;

	/** 仅统计当前 PendingRequest 已执行的跨关卡自动重试；任一显式 Setter 都会归零。 */
	UPROPERTY(Transient)
	int32 AutomaticGenerationRetryCount = 0;
};
