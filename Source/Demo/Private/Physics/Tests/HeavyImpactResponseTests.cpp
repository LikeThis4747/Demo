// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file HeavyImpactResponseTests.cpp
 * 职责：验证重冲击请求与调参资产在进入运行时状态机前拒绝非法输入。
 * 边界：只构造瞬态 World、Actor、组件和 PCA 数据，不依赖项目资产或真实 Chaos 求解。
 */

#include "Physics/HeavyImpactTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Characters/PursuerCharacter.h"
#include "Characters/ZeroEscapeCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/Hazards/BatteringRamHazardTuningData.h"
#include "Data/Hazards/PendulumHazardTuningData.h"
#include "Data/Physics/HeavyImpactTuningData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interfaces/HeavyImpactReceiver.h"
#include "Misc/AutomationTest.h"
#include "PhysicsControlAsset.h"
#include "ReferenceSkeleton.h"

#include <limits>

namespace ZeroEscape::Physics::Tests
{
	namespace HeavyImpactTestsPrivate
	{
		/** 为请求契约提供可 Spawn Actor 的短生命周期世界，并在用例结束时完整销毁。 */
		class FScopedTestWorld
		{
		public:
			FScopedTestWorld()
			{
				World = UWorld::CreateWorld(EWorldType::Game, false);
				if (IsValid(World))
				{
					World->AddToRoot();
				}
			}

			~FScopedTestWorld()
			{
				if (IsValid(World))
				{
					World->DestroyWorld(false);
					World->RemoveFromRoot();
				}
			}

			UWorld* Get() const { return World; }

		private:
			TObjectPtr<UWorld> World = nullptr;
		};

		/** 创建归指定 Actor 所有的瞬态碰撞组件；请求校验只读取其真实 Owner。 */
		UBoxComponent* CreateOwnedBox(AActor* Owner)
		{
			if (!IsValid(Owner))
			{
				return nullptr;
			}

			UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
			Owner->AddInstanceComponent(Box);
			Box->RegisterComponent();
			return Box;
		}

		/** 构造不依赖磁盘资产、但能通过 PCA/骨骼前置校验的调参夹具。 */
		struct FTuningFixture
		{
			FTuningFixture()
			{
				Tuning = NewObject<UHeavyImpactTuningData>();
				PhysicsControlAsset = NewObject<UPhysicsControlAsset>();
				SkeletalMesh = NewObject<USkeletalMesh>();
				Skeleton = NewObject<USkeleton>();
				MeshComponent = NewObject<USkeletalMeshComponent>();
				FaceUpAnimation = NewObject<UAnimSequence>();
				FaceDownAnimation = NewObject<UAnimSequence>();

				SkeletalMesh->SetSkeleton(Skeleton);
				FaceUpAnimation->SetSkeleton(Skeleton);
				FaceDownAnimation->SetSkeleton(Skeleton);
				{
					FReferenceSkeletonModifier SkeletonModifier(
						SkeletalMesh->GetRefSkeleton(), Skeleton);
					SkeletonModifier.Add(
						FMeshBoneInfo(TEXT("pelvis"), TEXT("pelvis"), INDEX_NONE),
						FTransform::Identity);
					SkeletonModifier.Add(
						FMeshBoneInfo(TEXT("spine_01"), TEXT("spine_01"), 0),
						FTransform::Identity);
					SkeletonModifier.Add(
						FMeshBoneInfo(TEXT("head"), TEXT("head"), 1),
						FTransform::Identity);
					SkeletonModifier.Add(
						FMeshBoneInfo(TEXT("upperarm_l"), TEXT("upperarm_l"), 1),
						FTransform::Identity);
					SkeletonModifier.Add(
						FMeshBoneInfo(TEXT("upperarm_r"), TEXT("upperarm_r"), 1),
						FTransform::Identity);
					SkeletonModifier.Add(
						FMeshBoneInfo(TEXT("thigh_l"), TEXT("thigh_l"), 0),
						FTransform::Identity);
					SkeletonModifier.Add(
						FMeshBoneInfo(TEXT("thigh_r"), TEXT("thigh_r"), 0),
						FTransform::Identity);
				}
				MeshComponent->SetSkeletalMeshAsset(SkeletalMesh);

				const auto AddLimb = [this](
					const FName LimbName,
					const FName StartBone,
					const bool bIncludeParentBone)
				{
					FPhysicsControlLimbSetupData Limb;
					Limb.LimbName = LimbName;
					Limb.StartBone = StartBone;
					Limb.bIncludeParentBone = bIncludeParentBone;
					Limb.bCreateWorldSpaceControls = false;
					Limb.bCreateParentSpaceControls = true;
					Limb.bCreateBodyModifiers = true;
					PhysicsControlAsset->CharacterSetupData.LimbSetupData.Add(Limb);
				};
				AddLimb(TEXT("Head"), TEXT("head"), false);
				AddLimb(TEXT("ArmLeft"), TEXT("upperarm_l"), false);
				AddLimb(TEXT("ArmRight"), TEXT("upperarm_r"), false);
				AddLimb(TEXT("LegLeft"), TEXT("thigh_l"), false);
				AddLimb(TEXT("LegRight"), TEXT("thigh_r"), false);
				AddLimb(TEXT("Spine"), TEXT("spine_01"), true);
				PhysicsControlAsset->CharacterSetupData.DefaultParentSpaceControlData.bEnabled = false;
				PhysicsControlAsset->CharacterSetupData.DefaultBodyModifierData.MovementType =
					EPhysicsMovementType::Kinematic;
				PhysicsControlAsset->CharacterSetupData.DefaultBodyModifierData.CollisionType =
					ECollisionEnabled::QueryOnly;
				PhysicsControlAsset->CharacterSetupData.DefaultBodyModifierData.GravityMultiplier = 1.0f;
				PhysicsControlAsset->CharacterSetupData.DefaultBodyModifierData.PhysicsBlendWeight = 0.0f;
				PhysicsControlAsset->CharacterSetupData.DefaultBodyModifierData.bEnableCCD = false;

				const auto AddProfile = [this](
					const FName ProfileName,
					const bool bControlEnabled,
					const EPhysicsMovementType MovementType,
					const ECollisionEnabled::Type CollisionType,
					const float GravityMultiplier,
					const float BlendWeight,
					const bool bEnableCCD)
				{
					FPhysicsControlSparseData ControlData;
					ControlData.bEnabled = bControlEnabled;
					ControlData.LinearStrength = 0.0f;
					ControlData.LinearExtraDamping = 0.0f;
					ControlData.AngularStrength = bControlEnabled ? 4.0f : 0.0f;
					ControlData.MaxTorque = bControlEnabled ? 25000.0f : 0.0f;
					ControlData.bUseSkeletalAnimation = true;

					FPhysicsControlModifierSparseData ModifierData(
						MovementType,
						CollisionType,
						GravityMultiplier,
						BlendWeight,
						EPhysicsControlKinematicTargetSpace::OffsetInBoneSpace,
						true,
						bEnableCCD);

					FPhysicsControlControlAndModifierUpdates Profile;
					Profile.ControlUpdates.Add(
						FPhysicsControlNamedControlParameters(TEXT("ParentSpace"), ControlData));
					Profile.ModifierUpdates.Add(
						FPhysicsControlNamedModifierParameters(TEXT("All"), ModifierData));
					PhysicsControlAsset->Profiles.Add(ProfileName, MoveTemp(Profile));
				};

				AddProfile(
					Demo::HeavyImpact::ProfileInactive,
					false,
					EPhysicsMovementType::Kinematic,
					ECollisionEnabled::QueryOnly,
					1.0f,
					0.0f,
					false);
				AddProfile(
					Demo::HeavyImpact::ProfilePrepared,
					true,
					EPhysicsMovementType::Simulated,
					ECollisionEnabled::QueryAndPhysics,
					0.0f,
					1.0f,
					true);
				AddProfile(
					Demo::HeavyImpact::ProfileFlight,
					true,
					EPhysicsMovementType::Simulated,
					ECollisionEnabled::QueryAndPhysics,
					1.0f,
					1.0f,
					true);
				AddProfile(
					Demo::HeavyImpact::ProfileLandingRecovery,
					true,
					EPhysicsMovementType::Simulated,
					ECollisionEnabled::QueryAndPhysics,
					1.0f,
					1.0f,
					true);
				AddProfile(
					Demo::HeavyImpact::ProfileFreeFallback,
					false,
					EPhysicsMovementType::Simulated,
					ECollisionEnabled::QueryAndPhysics,
					1.0f,
					1.0f,
					true);

				Tuning->PhysicsControlAsset = PhysicsControlAsset;
				Tuning->GetUpFaceUpAnimation = FaceUpAnimation;
				Tuning->GetUpFaceDownAnimation = FaceDownAnimation;
			}

			TObjectPtr<UHeavyImpactTuningData> Tuning = nullptr;
			TObjectPtr<UPhysicsControlAsset> PhysicsControlAsset = nullptr;
			TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;
			TObjectPtr<USkeleton> Skeleton = nullptr;
			TObjectPtr<USkeletalMeshComponent> MeshComponent = nullptr;
			TObjectPtr<UAnimSequence> FaceUpAnimation = nullptr;
			TObjectPtr<UAnimSequence> FaceDownAnimation = nullptr;
		};
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FHeavyImpactRequestContractTest,
		"Demo.Physics.HeavyImpact.RequestContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FHeavyImpactRequestContractTest::RunTest(const FString& Parameters)
	{
		using namespace HeavyImpactTestsPrivate;
		(void)Parameters;

		FScopedTestWorld TestWorld;
		if (!TestNotNull(TEXT("请求契约夹具必须创建瞬态 World"), TestWorld.Get()))
		{
			return false;
		}

		AActor* Receiver = TestWorld.Get()->SpawnActor<AActor>();
		AActor* SourceActor = TestWorld.Get()->SpawnActor<AActor>();
		AActor* OtherActor = TestWorld.Get()->SpawnActor<AActor>();
		UBoxComponent* SourceComponent = CreateOwnedBox(SourceActor);
		UBoxComponent* ReceiverComponent = CreateOwnedBox(Receiver);
		if (!TestNotNull(TEXT("请求契约夹具必须创建 Receiver"), Receiver)
			|| !TestNotNull(TEXT("请求契约夹具必须创建 SourceActor"), SourceActor)
			|| !TestNotNull(TEXT("请求契约夹具必须创建 OtherActor"), OtherActor)
			|| !TestNotNull(TEXT("请求契约夹具必须创建 SourceComponent"), SourceComponent)
			|| !TestNotNull(TEXT("请求契约夹具必须创建 ReceiverComponent"), ReceiverComponent))
		{
			return false;
		}

		FHeavyImpactPreparationRequest ValidRequest;
		ValidRequest.ImpactId = FGuid::NewGuid();
		ValidRequest.SourceActor = SourceActor;
		ValidRequest.SourceComponent = SourceComponent;
		ValidRequest.PredictedImpactPoint = FVector(100.0f, 20.0f, 50.0f);
		ValidRequest.SourceLinearVelocity = FVector(800.0f, 0.0f, 0.0f);
		ValidRequest.EstimatedTimeToContactSeconds = 0.05f;

		auto ExpectInvalid = [this, Receiver](
			const TCHAR* Description,
			const FHeavyImpactPreparationRequest& Request)
		{
			FString Reason;
			TestFalse(Description, Request.IsStructurallyValid(Receiver, Reason));
			TestFalse(TEXT("非法请求必须返回可诊断原因"), Reason.IsEmpty());
		};

		FHeavyImpactPreparationRequest Request = ValidRequest;
		Request.ImpactId.Invalidate();
		ExpectInvalid(TEXT("无效 ImpactId 必须被拒绝"), Request);

		Request = ValidRequest;
		Request.SourceActor = nullptr;
		ExpectInvalid(TEXT("空 SourceActor 必须被拒绝"), Request);

		Request = ValidRequest;
		Request.SourceComponent = nullptr;
		ExpectInvalid(TEXT("空 SourceComponent 必须被拒绝"), Request);

		Request = ValidRequest;
		Request.SourceActor = OtherActor;
		ExpectInvalid(TEXT("SourceComponent Owner 不匹配必须被拒绝"), Request);

		Request = ValidRequest;
		Request.SourceActor = Receiver;
		Request.SourceComponent = ReceiverComponent;
		ExpectInvalid(TEXT("接收者使用自身组件撞自己必须被拒绝"), Request);

		Request = ValidRequest;
		Request.PredictedImpactPoint.X = std::numeric_limits<float>::quiet_NaN();
		ExpectInvalid(TEXT("预测接触点包含 NaN 必须被拒绝"), Request);

		Request = ValidRequest;
		Request.SourceLinearVelocity.Y = std::numeric_limits<float>::infinity();
		ExpectInvalid(TEXT("源速度包含 Inf 必须被拒绝"), Request);

		Request = ValidRequest;
		Request.EstimatedTimeToContactSeconds = std::numeric_limits<float>::quiet_NaN();
		ExpectInvalid(TEXT("预计接触时间为 NaN 必须被拒绝"), Request);

		Request = ValidRequest;
		Request.EstimatedTimeToContactSeconds = -0.01f;
		ExpectInvalid(TEXT("预计接触时间为负数必须被拒绝"), Request);

		FString ValidReason;
		TestTrue(TEXT("对象关系与预测值都合法的请求必须通过"),
			ValidRequest.IsStructurallyValid(Receiver, ValidReason));
		TestTrue(TEXT("合法请求不得残留失败原因"), ValidReason.IsEmpty());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FHeavyImpactReceiverPrimitiveContractTest,
		"Demo.Physics.HeavyImpact.ReceiverPrimitiveContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FHeavyImpactReceiverPrimitiveContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		AZeroEscapeCharacter* Player = GetMutableDefault<AZeroEscapeCharacter>();
		APursuerCharacter* Pursuer = GetMutableDefault<APursuerCharacter>();
		if (!TestNotNull(TEXT("必须取得玩家 Receiver CDO"), Player)
			|| !TestNotNull(TEXT("必须取得追猎者 Receiver CDO"), Pursuer))
		{
			return false;
		}

		const auto VerifyMeshAuthority = [this](ACharacter* Receiver, const TCHAR* Label)
		{
			UPrimitiveComponent* PredictionPrimitive =
				IHeavyImpactReceiver::Execute_GetHeavyImpactPredictionPrimitive(Receiver);
			TestNotNull(FString::Printf(TEXT("%s 必须提供预测组件"), Label), PredictionPrimitive);
			TestTrue(
				FString::Printf(TEXT("%s 必须用 Skeletal Mesh 预测真实接触"), Label),
				PredictionPrimitive == Receiver->GetMesh());
			TestTrue(
				FString::Printf(TEXT("%s 预测组件必须归自身所有"), Label),
				IsValid(PredictionPrimitive) && PredictionPrimitive->GetOwner() == Receiver);
		};

		VerifyMeshAuthority(Player, TEXT("玩家"));
		VerifyMeshAuthority(Pursuer, TEXT("追猎者"));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FHeavyImpactTuningContractTest,
		"Demo.Physics.HeavyImpact.TuningContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FHeavyImpactTuningContractTest::RunTest(const FString& Parameters)
	{
		using namespace HeavyImpactTestsPrivate;
		(void)Parameters;

		FTuningFixture Fixture;
		FText Error;
		TestEqual(TEXT("Prepared Strength 默认倍率必须稳定"),
			Fixture.Tuning->PreparedControl.AngularStrengthMultiplier, 2.0f);
		TestEqual(TEXT("Prepared Damping 默认倍率必须稳定"),
			Fixture.Tuning->PreparedControl.AngularDampingRatioMultiplier, 1.25f);
		TestEqual(TEXT("Prepared Torque 默认倍率必须稳定"),
			Fixture.Tuning->PreparedControl.MaxTorqueMultiplier, 3.0f);
		TestEqual(TEXT("Flight Strength 默认倍率必须稳定"),
			Fixture.Tuning->FlightControl.AngularStrengthMultiplier, 1.75f);
		TestEqual(TEXT("Flight Damping 默认倍率必须稳定"),
			Fixture.Tuning->FlightControl.AngularDampingRatioMultiplier, 1.0f);
		TestEqual(TEXT("Flight Torque 默认倍率必须稳定"),
			Fixture.Tuning->FlightControl.MaxTorqueMultiplier, 3.0f);
		TestEqual(TEXT("Landing Strength 默认倍率必须稳定"),
			Fixture.Tuning->LandingControl.AngularStrengthMultiplier, 2.25f);
		TestEqual(TEXT("Landing Damping 默认倍率必须稳定"),
			Fixture.Tuning->LandingControl.AngularDampingRatioMultiplier, 1.35f);
		TestEqual(TEXT("Landing Torque 默认倍率必须稳定"),
			Fixture.Tuning->LandingControl.MaxTorqueMultiplier, 3.5f);
		TestEqual(TEXT("Downed reimpact threshold must reject residual contacts by default"),
			Fixture.Tuning->MinimumDownedReimpactImpulse, 1000.0f);
		TestEqual(TEXT("PhysicsBody contact release must preserve a short real-contact window"),
			Fixture.Tuning->PhysicsBodyReleaseDelaySeconds, 0.15f);
		TestEqual(TEXT("Player-class default same-source protection must remain explicit"),
			Fixture.Tuning->SameSourceProtectionSeconds, 0.75f);
		TestEqual(TEXT("共享接收端准备上限必须保留现有追猎者攻击的 0.10s ETA"),
			Fixture.Tuning->MaximumPreparationSeconds, 0.18f);
		TestEqual(TEXT("起身空间重试间隔必须足够短"),
			Fixture.Tuning->RecoveryRetrySeconds, 0.20f);
		TestEqual(TEXT("安全站位阻塞必须有 3 秒截止"),
			Fixture.Tuning->MaximumRecoveryBlockedSeconds, 3.0f);
		TestEqual(TEXT("Snapshot 到起身动画的默认淡入必须稳定"),
			Fixture.Tuning->RecoverySnapshotBlendSeconds, 0.30f);
		if (!TestTrue(TEXT("瞬态调参夹具的完整基线必须有效"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error)))
		{
			AddError(FString::Printf(TEXT("瞬态基线失败原因：%s"), *Error.ToString()));
			return false;
		}

		FPhysicsControlControlAndModifierUpdates& FlightProfile =
			Fixture.PhysicsControlAsset->Profiles.FindChecked(Demo::HeavyImpact::ProfileFlight);
		FlightProfile.ControlUpdates[0].Data.LinearStrength = 1.0f;
		TestFalse(TEXT("Flight Profile 不得用线性驱动钉住角色位移"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		FlightProfile.ControlUpdates[0].Data.LinearStrength = 0.0f;
		FlightProfile.ControlUpdates.Add(
			FPhysicsControlNamedControlParameters(TEXT("Spine"), FPhysicsControlSparseData()));
		TestFalse(TEXT("Profile 不得在 ParentSpace 后追加单 Limb 覆盖"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		FlightProfile.ControlUpdates.RemoveAt(1);

		FPhysicsControlControlAndModifierUpdates& FreeProfile =
			Fixture.PhysicsControlAsset->Profiles.FindChecked(Demo::HeavyImpact::ProfileFreeFallback);
		FreeProfile.ControlUpdates[0].Data.bEnabled = true;
		TestFalse(TEXT("FreeFallback Profile 必须显式关闭姿态控制"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		FreeProfile.ControlUpdates[0].Data.bEnabled = false;

		Fixture.PhysicsControlAsset->AdditionalSets.ControlSetUpdates.AddDefaulted();
		TestFalse(TEXT("PCA 不得通过 Additional Set 引入校验外权威"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.PhysicsControlAsset->AdditionalSets.ControlSetUpdates.Reset();

		Swap(
			Fixture.PhysicsControlAsset->CharacterSetupData.LimbSetupData[0],
			Fixture.PhysicsControlAsset->CharacterSetupData.LimbSetupData[1]);
		TestFalse(TEXT("PCA Limb 必须保持叶到根的固定顺序"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Swap(
			Fixture.PhysicsControlAsset->CharacterSetupData.LimbSetupData[0],
			Fixture.PhysicsControlAsset->CharacterSetupData.LimbSetupData[1]);

		Fixture.Tuning->MaximumRecoveryBlockedSeconds =
			Fixture.Tuning->RecoveryRetrySeconds - 0.01f;
		TestFalse(TEXT("起身阻塞截止不得短于一次重试间隔"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->MaximumRecoveryBlockedSeconds = 3.0f;
		Fixture.Tuning->GroundProbeDistance = std::numeric_limits<float>::quiet_NaN();
		TestFalse(TEXT("任一调参阈值为 NaN 必须被拒绝"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));

		Fixture.Tuning->GroundProbeDistance = 120.0f;
		Fixture.Tuning->MaximumRecoveryBlockedSeconds = std::numeric_limits<float>::infinity();
		TestFalse(TEXT("任一调参阈值为 Inf 必须被拒绝"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));

		Fixture.Tuning->MaximumRecoveryBlockedSeconds = 3.0f;
		Fixture.Tuning->FlightControl.MaxTorqueMultiplier =
			std::numeric_limits<float>::quiet_NaN();
		TestFalse(TEXT("任一阶段控制倍率为 NaN 必须被拒绝"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));

		Fixture.Tuning->FlightControl.MaxTorqueMultiplier = 3.0f;
		Fixture.Tuning->LandingControl.AngularStrengthMultiplier = 0.0f;
		TestFalse(TEXT("阶段控制倍率为零必须被拒绝"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FHeavyImpactRecoveryTuningContractTest,
		"Demo.Physics.HeavyImpact.RecoveryTuningContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FHeavyImpactRecoveryTuningContractTest::RunTest(const FString& Parameters)
	{
		using namespace HeavyImpactTestsPrivate;
		(void)Parameters;

		FTuningFixture Fixture;
		FText Error;
		TestEqual(TEXT("Snapshot recovery blend default must remain stable"),
			Fixture.Tuning->RecoverySnapshotBlendSeconds, 0.30f);
		TestEqual(TEXT("Recovery retry default must remain bounded"),
			Fixture.Tuning->RecoveryRetrySeconds, 0.20f);
		TestEqual(TEXT("Recovery blocked deadline must remain bounded"),
			Fixture.Tuning->MaximumRecoveryBlockedSeconds, 3.0f);
		TestTrue(TEXT("Recovery tuning fixture must begin valid"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));

		Fixture.Tuning->RecoverySnapshotBlendSeconds =
			std::numeric_limits<float>::quiet_NaN();
		TestFalse(TEXT("Snapshot recovery blend must reject NaN"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->RecoverySnapshotBlendSeconds = 0.30f;

		Fixture.Tuning->FaceUpAnimationStartTimeSeconds = 0.1f;
		TestFalse(TEXT("FaceUp Montage start time may not exceed its sequence length"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->FaceUpAnimationStartTimeSeconds = 0.0f;

		UAnimSequenceBase* SavedFaceUpAnimation =
			Fixture.Tuning->GetUpFaceUpAnimation.Get();
		Fixture.Tuning->GetUpFaceUpAnimation = nullptr;
		TestFalse(TEXT("FaceUp recovery animation must be assigned"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->GetUpFaceUpAnimation = SavedFaceUpAnimation;

		USkeleton* OtherSkeleton = NewObject<USkeleton>();
		Fixture.FaceUpAnimation->SetSkeleton(OtherSkeleton);
		TestFalse(TEXT("Recovery animation Skeleton must match the runtime Mesh Skeleton"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.FaceUpAnimation->SetSkeleton(Fixture.Skeleton);

		Fixture.FaceUpAnimation->bEnableRootMotion = true;
		TestFalse(TEXT("Recovery animation Root Motion must remain disabled"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.FaceUpAnimation->bEnableRootMotion = false;

		Fixture.Tuning->MaxRecoveryHorizontalAdjustmentCm = 60.1f;
		TestFalse(TEXT("Recovery placement may never exceed the 60 cm hard bound"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->MaxRecoveryHorizontalAdjustmentCm = 60.0f;

		Fixture.Tuning->RecoverySearchStepCm = 61.0f;
		TestFalse(TEXT("Recovery search step must stay inside the hard placement bound"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->RecoverySearchStepCm = 20.0f;

		Fixture.Tuning->MinimumDownedReimpactImpulse = 0.0f;
		TestFalse(TEXT("Downed reimpact threshold must remain positive"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->MinimumDownedReimpactImpulse = 1000.0f;

		Fixture.Tuning->PhysicsBodyReleaseDelaySeconds =
			Fixture.Tuning->MinimumSimulationSeconds + 0.01f;
		TestFalse(TEXT("PhysicsBody release must occur before stability-based recovery can begin"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->PhysicsBodyReleaseDelaySeconds = 0.15f;

		Fixture.Tuning->SameSourceProtectionSeconds = 0.0f;
		TestFalse(TEXT("Same-source protection duration must remain positive"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		Fixture.Tuning->SameSourceProtectionSeconds = 0.75f;

		Fixture.Tuning->RecoveryRetrySeconds =
			std::numeric_limits<float>::quiet_NaN();
		TestFalse(TEXT("Recovery timing must reject NaN"),
			Fixture.Tuning->Validate(Fixture.MeshComponent, Error));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FPendulumHeavyImpactPredictionContractTest,
		"Demo.Physics.HeavyImpact.PendulumPredictionContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FPendulumHeavyImpactPredictionContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		UPendulumHazardTuningData* Tuning = NewObject<UPendulumHazardTuningData>();
		FString Error;
		if (!TestTrue(TEXT("摆锤默认预测参数必须覆盖 0.08 秒锤头轨迹和 60Hz 采样余量"), Tuning->IsConfigured(Error)))
		{
			AddError(FString::Printf(TEXT("摆锤默认参数失败原因：%s"), *Error));
			return false;
		}
		TestEqual(TEXT("摆锤只允许 0.08 秒短预测窗口"),
			Tuning->MaximumPreparationLeadTime, 0.08f);
		const UBatteringRamHazardTuningData* RamTuning =
			NewObject<UBatteringRamHazardTuningData>();
		TestEqual(TEXT("冲锤只允许 0.08 秒短预测窗口"),
			RamTuning->MaximumPreparationLeadTime, 0.08f);

		Tuning->PreparationLookAheadDistance = 10.0f;
		TestFalse(TEXT("预测距离不足以覆盖锤头短窗口轨迹和采样余量时必须拒绝"),
			Tuning->IsConfigured(Error));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
