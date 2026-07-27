// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file SpikeTrapHazard.cpp
 * 职责：装配固定格栅 + 升降刺 + 伤害区与 Timeline，按相位循环让刺从格栅升出并在伸出相位用官方 ApplyDamage 结算命中。
 * 边界：不结算生命/失衡/倒地，不加载或硬编码网格资源，不管理玩家或追猎者状态。
 */

#include "Actors/Hazards/SpikeTrapHazard.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpikeTrap, Log, All);

/** 以固定场景根挂载固定格栅、升降刺、只响应 Pawn 的伤害区与升降 Timeline；关闭常驻 Tick。 */
ASpikeTrapHazard::ASpikeTrapHazard()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	// 格栅地板固定不动；刺网格运行时升降，必须为 Movable。父子 Mobility 需一致，故统一 Movable。
	GrateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrateMesh"));
	GrateMesh->SetMobility(EComponentMobility::Movable);
	GrateMesh->SetupAttachment(SceneRoot);

	SpikeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpikeMesh"));
	SpikeMesh->SetMobility(EComponentMobility::Movable);
	SpikeMesh->SetupAttachment(SceneRoot);

	HurtZone = CreateDefaultSubobject<UBoxComponent>(TEXT("HurtZone"));
	HurtZone->SetMobility(EComponentMobility::Movable);
	HurtZone->SetupAttachment(SceneRoot);
	// 按格栅 224x224 的占位默认；放置后按实际微调，使地面走过必触发、跳跃可越过。
	HurtZone->SetBoxExtent(FVector(112.0f, 112.0f, 40.0f));
	HurtZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HurtZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	HurtZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	HurtZone->SetGenerateOverlapEvents(true);

	RiseTimeline = CreateDefaultSubobject<UTimelineComponent>(TEXT("RiseTimeline"));
}

/** 生成升降曲线、绑定 Timeline 委托与 Overlap，记录刺伸出基准位，初始收起并启动循环。 */
void ASpikeTrapHazard::BeginPlay()
{
	Super::BeginPlay();

	// 以蓝图中摆好的刺相对位置为“伸出位”基准，升降在此基础上做 Z 偏移，不依赖网格自带坐标。
	SpikeBaseLocation = SpikeMesh->GetRelativeLocation();

	// 程序化生成 0→1 平滑升降曲线；升降手感简单，不需要外部曲线资产。
	RiseCurve = NewObject<UCurveFloat>(this, TEXT("RuntimeRiseCurve"));
	RiseCurve->FloatCurve.AddKey(0.0f, 0.0f);
	RiseCurve->FloatCurve.AddKey(1.0f, 1.0f);
	for (auto KeyIt = RiseCurve->FloatCurve.GetKeyHandleIterator(); KeyIt; ++KeyIt)
	{
		RiseCurve->FloatCurve.SetKeyInterpMode(*KeyIt, RCIM_Cubic);
		RiseCurve->FloatCurve.SetKeyTangentMode(*KeyIt, RCTM_Auto);
	}

	FOnTimelineFloat ProgressDelegate;
	ProgressDelegate.BindUFunction(this, FName(TEXT("HandleRiseProgress")));
	RiseTimeline->AddInterpFloat(RiseCurve, ProgressDelegate);

	FOnTimelineEvent FinishedDelegate;
	FinishedDelegate.BindUFunction(this, FName(TEXT("HandleTimelineFinished")));
	RiseTimeline->SetTimelineFinishedFunc(FinishedDelegate);

	// 曲线时间跨度为 1s，用 PlayRate 折算成期望的升降时长。
	RiseTimeline->SetPlayRate(1.0f / FMath::Max(RiseDuration, 0.05f));

	HurtZone->OnComponentBeginOverlap.AddDynamic(this, &ASpikeTrapHazard::HandleHurtZoneBeginOverlap);
	HurtZone->OnComponentEndOverlap.AddDynamic(this, &ASpikeTrapHazard::HandleHurtZoneEndOverlap);

	// 初始把刺沉入格栅下方并启动循环。
	SpikeMesh->SetRelativeLocation(SpikeBaseLocation - FVector(0.0f, 0.0f, HideDepth));
	EnterHidden();
}

/** 收起相位：解除危险，计时后升起。 */
void ASpikeTrapHazard::EnterHidden()
{
	bIsDangerous = false;
	GetWorldTimerManager().SetTimer(
		PhaseTimerHandle, this, &ASpikeTrapHazard::StartRising, FMath::Max(HiddenDuration, 0.01f), false);
}

/** 正向播放使刺升起。 */
void ASpikeTrapHazard::StartRising()
{
	bMovingUp = true;
	RiseTimeline->PlayFromStart();
}

/** 伸出相位：标记危险，对区内已有 Pawn 结算一次，计时后收起。 */
void ASpikeTrapHazard::EnterExtended()
{
	bIsDangerous = true;
	for (const TObjectPtr<AActor>& Pawn : OverlappingPawns)
	{
		ApplyDamageTo(Pawn);
	}
	GetWorldTimerManager().SetTimer(
		PhaseTimerHandle, this, &ASpikeTrapHazard::StartLowering, FMath::Max(ExtendedDuration, 0.01f), false);
}

/** 反向播放使刺收起。 */
void ASpikeTrapHazard::StartLowering()
{
	bMovingUp = false;
	RiseTimeline->ReverseFromEnd();
}

/** 用曲线值在“沉入位”与“伸出位”之间插值刺网格高度。 */
void ASpikeTrapHazard::HandleRiseProgress(float Alpha)
{
	const float OffsetZ = FMath::Lerp(-HideDepth, 0.0f, Alpha);
	SpikeMesh->SetRelativeLocation(SpikeBaseLocation + FVector(0.0f, 0.0f, OffsetZ));
}

/** 升起结束进入伸出相位；收起结束进入收起相位。 */
void ASpikeTrapHazard::HandleTimelineFinished()
{
	if (bMovingUp)
	{
		EnterExtended();
	}
	else
	{
		EnterHidden();
	}
}

/** 登记进入者；若正处危险相位立即结算一次。 */
void ASpikeTrapHazard::HandleHurtZoneBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!IsValid(OtherActor))
	{
		return;
	}

	OverlappingPawns.Add(OtherActor);
	if (bIsDangerous)
	{
		ApplyDamageTo(OtherActor);
	}
}

/** 注销离开者。 */
void ASpikeTrapHazard::HandleHurtZoneEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/)
{
	if (!IsValid(OtherActor))
	{
		return;
	}

	OverlappingPawns.Remove(OtherActor);
}

/** 官方 ApplyDamage 结算；接收方接入生命系统后生效，当前仅记录命中时机。 */
void ASpikeTrapHazard::ApplyDamageTo(AActor* Target)
{
	if (!IsValid(Target))
	{
		return;
	}

	UGameplayStatics::ApplyDamage(Target, Damage, GetInstigatorController(), this, UDamageType::StaticClass());

	// [临时-A] 生命/结算系统尚未接入：ApplyDamage 暂无接收方，仅记录伸出相位命中时机以验证机关闭环。
	UE_LOG(LogSpikeTrap, Warning, TEXT("%s 地刺伸出命中 %s（Damage=%.1f，待 HealthComponent 接入后结算）。"),
		*GetName(), *Target->GetName(), Damage);
}
