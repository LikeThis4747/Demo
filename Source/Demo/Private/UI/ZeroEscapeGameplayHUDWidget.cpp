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
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/GameViewportClient.h"
#include "GameFlow/ZeroEscapeGameState.h"

namespace
{
	constexpr float RefreshIntervalSeconds = 0.1f;
}

/** 初始化首次显示，并以低频 Timer 读取会随时间变化的充能进度。 */
void UZeroEscapeGameplayHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 订阅能量团计数变化：顶部目标行事件驱动刷新，无需每帧读 GameState。
	if (UWorld* World = GetWorld())
	{
		if (AZeroEscapeGameState* ZeroEscapeState =
			Cast<AZeroEscapeGameState>(World->GetGameState()))
		{
			ZeroEscapeState->OnEnergyOrbCountChanged.AddDynamic(
				this, &UZeroEscapeGameplayHUDWidget::HandleEnergyOrbCountChanged);
		}
	}

	RefreshGameplayState();
	CenterObjectiveRow();
	ResolveMessageTexts();
	SetExitLockedWarningVisible(false);
	if (IsValid(EscapeStartText))
	{
		EscapeStartText->SetVisibility(ESlateVisibility::Collapsed);
	}

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
		if (AZeroEscapeGameState* ZeroEscapeState =
			Cast<AZeroEscapeGameState>(World->GetGameState()))
		{
			ZeroEscapeState->OnEnergyOrbCountChanged.RemoveDynamic(
				this, &UZeroEscapeGameplayHUDWidget::HandleEnergyOrbCountChanged);
		}
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
	CenterObjectiveRow();

	// 复用现有 0.1s 刷新做红字闪烁，不新增 Timer/Tick。
	if (bExitLockedWarningVisible && IsValid(ExitLockedWarningText))
	{
		const UWorld* World = GetWorld();
		const float BlinkPhase = World ? FMath::Fmod(World->GetTimeSeconds() * 5.0f, 1.0f) : 0.0f;
		ExitLockedWarningText->SetRenderOpacity(BlinkPhase < 0.5f ? 1.0f : 0.25f);
	}

	if (EscapeStartMessageUntilTimeSeconds > 0.0)
	{
		const UWorld* World = GetWorld();
		if (World && World->GetTimeSeconds() >= EscapeStartMessageUntilTimeSeconds)
		{
			EscapeStartMessageUntilTimeSeconds = 0.0;
			if (IsValid(EscapeStartText))
			{
				EscapeStartText->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

/** 从 GameState 读取通关目标进度，更新顶部"收集能量团逃往出口 X/Y"计数与配色；仅在数值变化时写 UI。 */
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

	// 数值未变化则不写 UI，消除重复 SetText/SetColor 的 Slate 无效化开销。
	if (Collected == LastOrbCollected && Required == LastOrbRequired)
	{
		return;
	}
	LastOrbCollected = Collected;
	LastOrbRequired = Required;

	const bool bRequirementMet = ZeroEscapeState->IsEnergyOrbRequirementMet();

	// 计数：未达标红、达标蓝，直观提示"可以去出口了"。
	if (IsValid(ObjectiveCountText))
	{
		ObjectiveCountText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Collected, Required)));
		ObjectiveCountText->SetColorAndOpacity(FSlateColor(bRequirementMet
			? FLinearColor(0.35f, 0.82f, 1.0f, 1.0f)
			: FLinearColor(0.95f, 0.30f, 0.30f, 1.0f)));
	}

	// 固定强调色：能量团黄、出口蓝（资产层已设，此处兜底保证一致）。
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

void UZeroEscapeGameplayHUDWidget::ResolveMessageTexts()
{
	if (!IsValid(ExitLockedWarningText))
	{
		ExitLockedWarningText = Cast<UTextBlock>(GetWidgetFromName(TEXT("ExitLockedWarningText")));
	}
	if (!IsValid(EscapeStartText))
	{
		EscapeStartText = Cast<UTextBlock>(GetWidgetFromName(TEXT("EscapeStartText")));
	}
}

void UZeroEscapeGameplayHUDWidget::SetExitLockedWarningVisible(const bool bVisible)
{
	ResolveMessageTexts();
	if (!IsValid(ExitLockedWarningText))
	{
		bExitLockedWarningVisible = false;
		return;
	}

	if (!bVisible)
	{
		bExitLockedWarningVisible = false;
		ExitLockedWarningText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (bExitLockedWarningVisible)
	{
		return;
	}
	bExitLockedWarningVisible = true;

	ExitLockedWarningText->SetText(FText::FromString(TEXT("能量团数量不足！传送门激活失败！")));
	if (IsValid(ObjectiveOrbText))
	{
		ExitLockedWarningText->SetFont(ObjectiveOrbText->GetFont());
	}
	ExitLockedWarningText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.18f, 0.12f, 1.0f)));
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ExitLockedWarningText->Slot))
	{
		// 顶部目标行下面一行；不是 AHUD Canvas 绘制，只是 UMG 顶层 CanvasPanel 定位。
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetOffsets(FMargin(0.0f, 52.0f, 0.0f, 0.0f));
	}
	ExitLockedWarningText->SetRenderOpacity(1.0f);
	ExitLockedWarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UZeroEscapeGameplayHUDWidget::ShowEscapeStartMessage()
{
	ResolveMessageTexts();
	if (!IsValid(EscapeStartText))
	{
		return;
	}

	EscapeStartText->SetText(FText::FromString(TEXT("开始逃亡！")));
	if (IsValid(ObjectiveOrbText))
	{
		FSlateFontInfo EscapeFont = ObjectiveOrbText->GetFont();
		EscapeFont.Size = FMath::Max(EscapeFont.Size * 2, 40);
		EscapeStartText->SetFont(EscapeFont);
	}
	EscapeStartText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.15f, 1.0f)));
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(EscapeStartText->Slot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.18f, 0.5f, 0.18f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetOffsets(FMargin(0.0f));
	}
	EscapeStartText->SetVisibility(ESlateVisibility::HitTestInvisible);

	if (const UWorld* World = GetWorld())
	{
		EscapeStartMessageUntilTimeSeconds = World->GetTimeSeconds() + 1.5;
	}
}

/** 把顶部目标行按当前视口宽度水平居中；分辨率不变则不重复布局。 */
void UZeroEscapeGameplayHUDWidget::CenterObjectiveRow()
{
	if (!IsValid(ObjectiveRow))
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!IsValid(Viewport))
	{
		return;
	}

	FVector2D ViewportSize;
	Viewport->GetViewportSize(ViewportSize);
	if (FMath::IsNearlyEqual(ViewportSize.X, LastCenteredViewportX, 0.5f))
	{
		return;
	}
	LastCenteredViewportX = ViewportSize.X;

	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ObjectiveRow->Slot);
	if (!IsValid(CanvasSlot))
	{
		return;
	}

	// 锚点锁到顶部中点，alignment(0.5,0) 使面板按自身宽度居中，向下留 24px。
	CanvasSlot->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
	CanvasSlot->SetAlignment(FVector2D(0.5f, 0.0f));
	CanvasSlot->SetAutoSize(true);
	CanvasSlot->SetOffsets(FMargin(0.0f, 24.0f, 0.0f, 0.0f));
}

/** 能量团计数变化：直接刷新顶部目标行（事件驱动，无需每帧轮询）。 */
void UZeroEscapeGameplayHUDWidget::HandleEnergyOrbCountChanged(int32 /*CollectedCount*/, int32 /*RequiredCount*/)
{
	RefreshObjectiveState();
}
