// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGameplayHUDWidget.cpp
 * 职责：把现有玩家组件的只读状态转换为右下角 UMG 的进度和文本表现。
 * 边界：不写回生命、充能或磁力状态；所有数值仍由对应组件负责。
 */

#include "UI/ZeroEscapeGameplayHUDWidget.h"

#include "Components/Attributes/HealthComponent.h"
#include "Components/Magnetism/ElectromagneticGrabComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "GameFlow/ZeroEscapeGameState.h"

namespace
{
	constexpr float RefreshIntervalSeconds = 0.1f;
}

/** 初始化首次显示，并以低频 Timer 读取会随时间变化的充能进度。 */
void UZeroEscapeGameplayHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RefreshGameplayState();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RefreshTimer,
			this,
			&UZeroEscapeGameplayHUDWidget::RefreshGameplayState,
			RefreshIntervalSeconds,
			true);
	}
}

/** 清理刷新 Timer，确保 Widget 离开视口后不再读取 Pawn 组件。 */
void UZeroEscapeGameplayHUDWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}

	Super::NativeDestruct();
}

/** 读取当前拥有 Pawn 的生命与磁力状态并更新三个绑定控件。 */
void UZeroEscapeGameplayHUDWidget::RefreshGameplayState()
{
	APawn* PlayerPawn = GetOwningPlayerPawn();
	if (!IsValid(PlayerPawn))
	{
		return;
	}

	if (UHealthComponent* Health = PlayerPawn->FindComponentByClass<UHealthComponent>())
	{
		const float MaxHealth = Health->GetMaxHealth();
		const float HealthPercent = MaxHealth > UE_SMALL_NUMBER
			? FMath::Clamp(Health->GetCurrentHealth() / MaxHealth, 0.0f, 1.0f)
			: 0.0f;
		if (IsValid(HealthBar))
		{
			HealthBar->SetPercent(HealthPercent);
		}
	}

	if (UElectromagneticGrabComponent* MagneticGrab =
		PlayerPawn->FindComponentByClass<UElectromagneticGrabComponent>())
	{
		const int32 AvailableCharges = MagneticGrab->GetAvailableExplosionCharges();
		const int32 MaximumCharges = MagneticGrab->GetMaximumExplosionCharges();

		if (IsValid(ChargesText))
		{
			ChargesText->SetText(FText::FromString(FString::Printf(
				TEXT("%d/%d"), AvailableCharges, MaximumCharges)));
		}

		// 能量条表达"下一次爆裂次数的充能进度"：未满时随时间 0→100%，
		// 到 100% 由组件把次数 +1；已满（3/3）不再需要充能，显示 0。
		if (IsValid(EnergyBar))
		{
			const bool bChargesFull = MaximumCharges > 0 && AvailableCharges >= MaximumCharges;
			const float RechargePercent = bChargesFull
				? 0.0f
				: FMath::Clamp(MagneticGrab->GetExplosionRechargePercent() / 100.0f, 0.0f, 1.0f);
			EnergyBar->SetPercent(RechargePercent);
		}
	}

	RefreshObjectiveState();
}

/** 从 GameState 读取通关目标进度，更新顶部"收集能量团逃往出口 X/Y"计数与配色。 */
void UZeroEscapeGameplayHUDWidget::RefreshObjectiveState()
{
	UWorld* World = GetWorld();
	AGameStateBase* GameStateBase = World ? World->GetGameState() : nullptr;
	AZeroEscapeGameState* ZeroEscapeState = Cast<AZeroEscapeGameState>(GameStateBase);
	if (!IsValid(ZeroEscapeState))
	{
		return;
	}

	const int32 Collected = ZeroEscapeState->GetCollectedEnergyOrbCount();
	const int32 Required = ZeroEscapeState->GetRequiredEnergyOrbCount();
	const bool bRequirementMet = ZeroEscapeState->IsEnergyOrbRequirementMet();

	// 计数：未达标红、达标蓝，直观提示"可以去出口了"。
	if (IsValid(ObjectiveCountText))
	{
		ObjectiveCountText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Collected, Required)));
		ObjectiveCountText->SetColorAndOpacity(FSlateColor(bRequirementMet
			? FLinearColor(0.35f, 0.82f, 1.0f, 1.0f)
			: FLinearColor(0.95f, 0.30f, 0.30f, 1.0f)));
	}

	// 固定强调色：能量团黄、出口蓝。
	if (IsValid(ObjectiveOrbText))
	{
		ObjectiveOrbText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.25f, 1.0f)));
	}
	if (IsValid(ObjectiveExitText))
	{
		ObjectiveExitText->SetColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f)));
	}

	// 仅当局内确实存在光团目标时显示，避免其它模式下出现 0/0。
	if (IsValid(ObjectiveRow))
	{
		ObjectiveRow->SetVisibility(Required > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}
