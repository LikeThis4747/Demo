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

	/** 鼠标灵敏度倍率（1.0 为基准），由主菜单设置面板写入。 */
	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Settings")
	float GetMouseSensitivity() const { return MouseSensitivity; }

	/** 设置鼠标灵敏度倍率，限制在 0.1~3.0。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Settings")
	void SetMouseSensitivity(float InValue) { MouseSensitivity = FMath::Clamp(InValue, 0.1f, 3.0f); }

	/** 音乐音量 0~1，由主菜单设置面板写入，BGM 播放时读取。 */
	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Settings")
	float GetMusicVolume() const { return MusicVolume; }

	/** 设置音乐音量，限制在 0~1。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Settings")
	void SetMusicVolume(float InValue) { MusicVolume = FMath::Clamp(InValue, 0.0f, 1.0f); }

	/** 音效音量 0~1，由主菜单设置面板写入。 */
	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Settings")
	float GetSfxVolume() const { return SfxVolume; }

	/** 设置音效音量，限制在 0~1。 */
	UFUNCTION(BlueprintCallable, Category = "ZeroEscape|Settings")
	void SetSfxVolume(float InValue) { SfxVolume = FMath::Clamp(InValue, 0.0f, 1.0f); }

	/**
	 * 任意播音效处统一读取音效音量；新增音效只需把播放音量乘上本函数返回值。
	 * WorldContextObject 传调用方自身（this）即可；取不到 GameInstance 时返回 1.0 不影响播放。
	 */
	UFUNCTION(BlueprintPure, Category = "ZeroEscape|Settings", meta = (WorldContext = "WorldContextObject"))
	static float GetSfxVolumeFor(const UObject* WorldContextObject);

private:
	/** 鼠标灵敏度倍率；会话级设置，不做存档。 */
	UPROPERTY(VisibleAnywhere, Category = "ZeroEscape|Settings")
	float MouseSensitivity = 1.0f;

	/** 音乐音量；会话级设置，不做存档。 */
	UPROPERTY(VisibleAnywhere, Category = "ZeroEscape|Settings")
	float MusicVolume = 1.0f;

	/** 音效音量；会话级设置，不做存档。 */
	UPROPERTY(VisibleAnywhere, Category = "ZeroEscape|Settings")
	float SfxVolume = 1.0f;

	/** 待开始一局的 Seed 与难度；默认值来自 FZeroEscapeGenerationRequest。 */
	UPROPERTY(VisibleAnywhere, Category = "ZeroEscape|Round")
	FZeroEscapeGenerationRequest PendingRequest;
};
