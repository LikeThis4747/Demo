// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file PursuerAttackComponent.h
 * 职责：唯一管理追猎者近战与预判跑跳攻击的阶段、计时器、落点、命中去重、恢复和取消。
 * 边界：不选择目标、不做寻路、不拥有玩家受击状态；AI 只请求攻击，角色移动与既有受击接口负责实际执行。
 * 状态 Owner：本组件独占一次攻击从起手到恢复结束的运行时状态，默认关闭 Tick。
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "PursuerAttackComponent.generated.h"

class APawn;
class APursuerCharacter;
class UAnimMontage;
class UHeavyImpactResponseComponent;
class UPursuerConfig;
class UPrimitiveComponent;
class USphereComponent;
enum class EHeavyImpactState : uint8;
struct FHeavyImpactPreparationRequest;

/** 本项目追猎者攻击内部阶段；不是 UE 官方枚举，不向蓝图暴露。 */
enum class EPursuerAttackPhase : uint8
{
	Idle,
	CloseSwing,
	JumpWindup,
	JumpAirborne,
	ImpactPending,
	PostHitRespite,
	Recovery
};

/** 无 Tick 的追猎者攻击执行器；用 Timer 与 Character::LandedDelegate 驱动完整攻击事务。 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class DEMO_API UPursuerAttackComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建关闭 Tick 的攻击执行器；角色在 PostInitializeComponents 注入配置。 */
	UPursuerAttackComponent();

	/** 注入唯一角色与 DataAsset 配置，并绑定该角色的落地事件；重复调用会先解除旧绑定。 */
	void Configure(APursuerCharacter* InCharacter, const UPursuerConfig* InConfig);

	/** 尝试启动近距离斧击；成功后由配置时刻执行一次前方球形 Sweep。 */
	bool TryStartCloseSwing(APawn* Target);

	/** 尝试启动中距离跑跳下砸；助跑后在离地时只锁定一次预测落点，空中不持续追踪。 */
	bool TryStartJumpSmash(APawn* Target);

	/** 当前是否允许开始新攻击；同时检查阶段、冷却、配置、站立状态和现有受击抑制。 */
	bool CanStartAttack() const;

	/** 当前是否从起手到恢复结束仍占用攻击事务。 */
	bool IsBusy() const { return Phase != EPursuerAttackPhase::Idle; }

	/** 取消当前攻击、清理全部 Timer 并只停止本组件启动的 Montage；已开始的冷却仍保留。 */
	void CancelAttack(float BlendOutSeconds = 0.08f);

	/**
	 * 根据目标当前位置和速度预测水平落点，并把水平距离限制在可达上限内。
	 * 输入/输出均为世界空间 cm、cm/s；保留目标位置的 Z，便于单元测试和运行时复用。
	 */
	static FVector ComputePredictedTargetPoint(
		const FVector& Origin,
		const FVector& TargetLocation,
		const FVector& TargetVelocity,
		float LeadSeconds,
		float MaximumHorizontalDistance);

	/**
	 * 求在固定飞行时间和世界重力下到达目标的初速度。
	 * 成功写入 OutVelocity；时间或数值非法时返回 false，调用方必须中止攻击而不是使用兜底瞬移。
	 */
	static bool CalculateBallisticLaunchVelocity(
		const FVector& Start,
		const FVector& Target,
		float FlightSeconds,
		float GravityZ,
		FVector& OutVelocity);

	/** 组合成功 Heavy 后的水平远离与竖直击飞速度变化，供运行时和无世界测试共用。 */
	static FVector ComputeKnockbackVelocity(
		const FVector& SourceLocation,
		const FVector& TargetLocation,
		const FVector& SourceForward,
		float HorizontalVelocity,
		float UpwardVelocity);

	/** 根据目标中心、攻击方向、半径、速度与 ETA 计算攻击刚体安全起点。 */
	static FVector ComputeImpactBodyStart(
		const FVector& TargetPoint,
		const FVector& AttackDirection,
		float BodyRadius,
		float BodySpeed,
		float ContactEtaSeconds);

protected:
	/** 组件结束时解除落地委托并清理 Timer，避免世界销毁后的悬挂回调。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 验证目标、地面状态和当前攻击可用性。 */
	bool CanStartAgainst(const APawn* Target) const;

	/** 同步加载并播放指定 Montage；只有播放成功才允许建立攻击事务。 */
	bool PlayAttackMontage(const TSoftObjectPtr<UAnimMontage>& MontageAsset, float PlayRate);

	/** 建立一次攻击的共享状态与冷却；近战停步，跑跳前段保留既有追击助跑。 */
	void BeginAttack(EPursuerAttackPhase NewPhase, APawn* Target, const FVector& FacingPoint);

	/** 把角色水平朝向对准世界点，不改变 Pitch/Roll。 */
	void FacePoint(const FVector& WorldPoint) const;

	/** 近战命中时刻：执行一次 Sweep，随后无论命中或落空都进入恢复。 */
	void ExecuteCloseHit();

	/** 跑跳离地时刻：先锁定一次预测落点，再计算抛物线初速度并调用 CharacterMovement 的正常 Launch。 */
	void LaunchJump();

	/** 角色真正从 Falling 落地时结算一次范围命中；其他阶段的普通落地事件直接忽略。 */
	UFUNCTION()
	void HandleCharacterLanded(const FHitResult& Hit);

	/** 判断活动目标是否出现在近战前方球形 Sweep 中。 */
	bool IsActiveTargetInCloseSweep(FVector& OutImpactOrigin) const;

	/** 判断活动目标是否位于落地点范围查询内。 */
	bool IsActiveTargetInLandingRadius(const FVector& ImpactOrigin) const;

	/** 做一次 Visibility 遮挡检测，防止斧击或落地范围隔墙命中玩家。 */
	bool HasClearLineToTarget(const FVector& ImpactOrigin, const APawn* Target) const;

	/**
	 * 既有宽容查询通过后，提前把目标切到 Heavy Prepared，并发射短时隐藏刚体。
	 * 只有该刚体产生真实 Chaos 接触并由接收端提交后，才结算伤害和额外击飞。
	 */
	bool ArmHeavyStrike(
		APawn* Target,
		const FVector& ImpactOrigin,
		float Damage,
		float BodyRadius,
		float HorizontalVelocity,
		float UpwardVelocity,
		float MissRecoverySeconds);

	/** 目标 Heavy 确认同一 ImpactId 与真实来源刚体后广播；下一帧完成伤害、加成冲量和喘息。 */
	void HandleHeavyImpactCommitted(const FHeavyImpactPreparationRequest& Request);

	/** 目标 Heavy 状态变化监听；只有完整恢复到 Inactive 后才开始额外喘息。 */
	void HandleTargetHeavyStateChanged(EHeavyImpactState Previous, EHeavyImpactState Current);

	/** 在 Heavy 广播后的下一帧关闭攻击刚体并提交一次伤害与速度变化。 */
	void FinalizeCommittedHeavyHit();

	/** 攻击刚体未在准备窗口内真实命中时，按原攻击类型进入普通落空恢复。 */
	void HandleHeavyStrikeMiss();

	/** 成功命中后停住追猎者，等待玩家起身；异常超时只释放 AI，不重复伤害。 */
	void BeginPostHitRespite();

	/** 玩家 Heavy 已恢复，开始配置的额外喘息时间。 */
	void BeginPostHitGrace();

	/** 喘息或保险超时结束，释放攻击事务。 */
	void FinishPostHitRespite();

	/** 把隐藏攻击球恢复为无碰撞、非模拟并重新挂回追猎者。 */
	void DeactivateAttackImpactBody();

	/** 解除目标 Heavy 的原生委托；取消、落空、完成和 EndPlay 都走此路径。 */
	void UnbindTargetHeavyImpact();

	/** 从命中/落空转入恢复，并在配置时长后释放攻击事务。 */
	void BeginRecovery(float RecoverySeconds);

	/** 正常完成当前攻击，停止剩余 Montage 并回到 Idle；不清除已开始的冷却。 */
	void FinishAttack();

	/** 跑跳未收到落地事件时的保险清理；不在空中补结算命中。 */
	void HandleAttackTimeout();

	/** 清理本组件拥有的动作、恢复和保险 Timer。 */
	void ClearAttackTimers();

	/** PostInitializeComponents 注入的追猎者；失效后所有 Timer 回调安全退出。 */
	TWeakObjectPtr<APursuerCharacter> Character;

	/** 追猎者 DataAsset 的只读弱引用；是全部攻击调参的唯一运行时来源。 */
	TWeakObjectPtr<const UPursuerConfig> Config;

	/** 本次攻击起手时选定的 Pawn；跑跳只追踪其最终范围命中资格，不更新锁定落点。 */
	TWeakObjectPtr<APawn> ActiveTarget;

	/** 当前 Heavy 事务的目标响应组件；按类型查找并只用于委托和状态读取。 */
	TWeakObjectPtr<UHeavyImpactResponseComponent> TargetHeavyImpact;

	/** 角色装配的持久隐藏刚体；组件只切换它的短时物理状态，不动态创建对象。 */
	TWeakObjectPtr<USphereComponent> AttackImpactBody;

	/** 仅记录本组件成功启动的 Montage，取消时不会误停受击或起身 Montage。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveMontage = nullptr;

	/** 本次跑跳在离地时确定的世界空间落点；Launch 之后保持不变。 */
	FVector LockedImpactPoint = FVector::ZeroVector;

	/** 本次攻击唯一 Heavy Impact Id；准备、真实提交与去重必须复用。 */
	FGuid ActiveImpactId;

	/** Heavy 真实提交后下一帧写入的伤害，避免在预测阶段提前扣血。 */
	float PendingDamage = 0.0f;

	/** Heavy 真实提交后施加给目标物理 Mesh 的额外速度变化，强化斧击反馈。 */
	FVector PendingKnockbackVelocity = FVector::ZeroVector;

	/** Heavy 未真实接触时回到近战或下砸各自恢复时间。 */
	float PendingMissRecoverySeconds = 0.0f;

	/** 防止同一求解接触的重复 Heavy 广播或计时器重复结算。 */
	bool bHeavyCommitQueued = false;

	/** 当前攻击阶段；只有本组件的开始、落地、恢复、取消路径可写。 */
	EPursuerAttackPhase Phase = EPursuerAttackPhase::Idle;

	/** 世界时间秒；攻击起手写入，冷却期间 AI 可继续追击但组件拒绝新攻击。 */
	double NextAttackAllowedWorldTime = 0.0;

	/** 起手后的近战命中或跑跳离地 Timer。 */
	FTimerHandle ActionTimerHandle;

	/** 命中或落空后的恢复 Timer。 */
	FTimerHandle RecoveryTimerHandle;

	/** 防止异常 Montage/落地状态永久占用攻击的保险 Timer。 */
	FTimerHandle TimeoutTimerHandle;

	/** Heavy 提交广播后延迟到下一帧完成伤害与额外击飞。 */
	FTimerHandle CommitFinalizeTimerHandle;
};
