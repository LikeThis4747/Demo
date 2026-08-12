// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file CharacterImpactResponseTests.cpp
 * Verifies the deterministic data contracts shared by standing light-impact sources and receivers.
 * These tests intentionally avoid Chaos simulation, PIE, and project content assets.
 */

#include "Physics/CharacterImpactTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Data/Physics/CharacterImpactSourceProfile.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace ZeroEscape::Physics::Tests
{
	namespace CharacterImpactTestsPrivate
	{
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
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCharacterImpactSourceProfileContractTest,
		"Demo.Physics.CharacterImpact.SourceProfileContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FCharacterImpactSourceProfileContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;

		UCharacterImpactSourceProfile* Profile = NewObject<UCharacterImpactSourceProfile>();
		if (!TestNotNull(TEXT("Source profile fixture must be created"), Profile))
		{
			return false;
		}

		Profile->PlayerReaction.Result = EStandingImpactResult::Slow;
		Profile->PlayerReaction.DurationSeconds = 0.5f;
		Profile->PlayerReaction.SpeedMultiplier = 0.4f;
		Profile->PursuerReaction.Result = EStandingImpactResult::Stop;
		Profile->PursuerReaction.DurationSeconds = 0.25f;
		Profile->PursuerReaction.SpeedMultiplier = 0.0f;
		Profile->PursuerReaction.bPlayReactionAnimation = true;
		Profile->MinimumPhysicalImpulse = 100.0f;
		Profile->FullStrengthPhysicalImpulse = 300.0f;

		TestTrue(
			TEXT("Player category must select the player mapping"),
			&Profile->GetReaction(EImpactReceiverCategory::Player) == &Profile->PlayerReaction);
		TestTrue(
			TEXT("Pursuer category must select the pursuer mapping"),
			&Profile->GetReaction(EImpactReceiverCategory::Pursuer) == &Profile->PursuerReaction);

		FString Error;
		TestTrue(TEXT("Distinct valid player and pursuer mappings must configure"), Profile->IsConfigured(Error));
		TestTrue(TEXT("A valid source profile must not retain an error"), Error.IsEmpty());

		TestEqual(TEXT("Impulse below the source threshold must normalize to zero"),
			Profile->NormalizePhysicalImpulse(-25.0f), 0.0f);
		TestEqual(TEXT("Impulse at the source threshold must normalize to zero"),
			Profile->NormalizePhysicalImpulse(100.0f), 0.0f);
		TestEqual(TEXT("Impulse halfway through the source range must normalize to one half"),
			Profile->NormalizePhysicalImpulse(200.0f), 0.5f);
		TestEqual(TEXT("Impulse at full strength must normalize to one"),
			Profile->NormalizePhysicalImpulse(300.0f), 1.0f);
		TestEqual(TEXT("Impulse above full strength must remain clamped to one"),
			Profile->NormalizePhysicalImpulse(1000.0f), 1.0f);

		Profile->FullStrengthPhysicalImpulse = Profile->MinimumPhysicalImpulse;
		TestFalse(TEXT("A collapsed physical impulse range must be rejected"), Profile->IsConfigured(Error));
		TestFalse(TEXT("An invalid source profile must provide a diagnostic"), Error.IsEmpty());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCharacterImpactDataValidationContractTest,
		"Demo.Physics.CharacterImpact.DataValidationContract",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

	bool FCharacterImpactDataValidationContractTest::RunTest(const FString& Parameters)
	{
		using namespace CharacterImpactTestsPrivate;
		(void)Parameters;

		FString Error;
		FStandingImpactReactionSpec Reaction;
		TestTrue(TEXT("The canonical None reaction must be valid"),
			Reaction.IsConfigured(TEXT("Reaction"), Error));
		TestTrue(TEXT("A valid None reaction must not retain an error"), Error.IsEmpty());
		Reaction.bApplyPhysicalReaction = true;
		TestFalse(TEXT("None may not request a hidden physical presentation"),
			Reaction.IsConfigured(TEXT("Reaction"), Error));
		Reaction.bApplyPhysicalReaction = false;

		Reaction.DurationSeconds = 0.1f;
		TestFalse(TEXT("None may not carry a gameplay duration"),
			Reaction.IsConfigured(TEXT("Reaction"), Error));
		TestFalse(TEXT("An invalid None reaction must provide a diagnostic"), Error.IsEmpty());

		Reaction.Result = EStandingImpactResult::Slow;
		Reaction.DurationSeconds = 0.5f;
		Reaction.SpeedMultiplier = 0.5f;
		Reaction.bPlayReactionAnimation = false;
		Reaction.bApplyPhysicalReaction = true;
		TestTrue(TEXT("Slow requires a duration and a strict fractional speed multiplier"),
			Reaction.IsConfigured(TEXT("Reaction"), Error));
		Reaction.bPlayReactionAnimation = true;
		TestFalse(TEXT("V1 Slow may not request a full-body reaction animation"),
			Reaction.IsConfigured(TEXT("Reaction"), Error));

		Reaction.Result = EStandingImpactResult::Stop;
		Reaction.DurationSeconds = 0.25f;
		Reaction.SpeedMultiplier = 0.0f;
		Reaction.bPlayReactionAnimation = true;
		Reaction.bApplyPhysicalReaction = true;
		TestTrue(TEXT("Stop may request the optional reaction animation"),
			Reaction.IsConfigured(TEXT("Reaction"), Error));
		Reaction.SpeedMultiplier = 0.1f;
		TestFalse(TEXT("Stop must use a zero speed multiplier"),
			Reaction.IsConfigured(TEXT("Reaction"), Error));
		Reaction.SpeedMultiplier = 0.0f;
		Reaction.DurationSeconds = std::numeric_limits<float>::quiet_NaN();
		TestFalse(TEXT("Reaction data must reject non-finite values"),
			Reaction.IsConfigured(TEXT("Reaction"), Error));

		FScopedTestWorld TestWorld;
		if (!TestNotNull(TEXT("Request fixture must create a transient World"), TestWorld.Get()))
		{
			return false;
		}

		AActor* Receiver = TestWorld.Get()->SpawnActor<AActor>();
		AActor* SourceActor = TestWorld.Get()->SpawnActor<AActor>();
		AActor* OtherActor = TestWorld.Get()->SpawnActor<AActor>();
		UBoxComponent* SourceComponent = CreateOwnedBox(SourceActor);
		UCharacterImpactSourceProfile* Profile = NewObject<UCharacterImpactSourceProfile>();
		if (!TestNotNull(TEXT("Request fixture must create a receiver"), Receiver)
			|| !TestNotNull(TEXT("Request fixture must create a source actor"), SourceActor)
			|| !TestNotNull(TEXT("Request fixture must create a second actor"), OtherActor)
			|| !TestNotNull(TEXT("Request fixture must create an owned source component"), SourceComponent)
			|| !TestNotNull(TEXT("Request fixture must create a source profile"), Profile))
		{
			return false;
		}

		FStandingImpactRequest ValidRequest;
		ValidRequest.ImpactId = FGuid::NewGuid();
		ValidRequest.SourceActor = SourceActor;
		ValidRequest.SourceComponent = SourceComponent;
		ValidRequest.SourceProfile = Profile;
		ValidRequest.WorldDirection = FVector::ForwardVector;
		ValidRequest.ImpactPoint = FVector(100.0f, 20.0f, 50.0f);
		ValidRequest.NormalizedStrength = 0.5f;
		ValidRequest.RawNormalImpulse = FVector::ZeroVector;

		TestTrue(TEXT("A structurally complete trigger-style request must be valid"),
			ValidRequest.IsStructurallyValid(Receiver, Error));
		TestTrue(TEXT("A valid request must not retain an error"), Error.IsEmpty());

		auto ExpectInvalid = [this, Receiver](
			const TCHAR* Description,
			const FStandingImpactRequest& Request)
		{
			FString Reason;
			TestFalse(Description, Request.IsStructurallyValid(Receiver, Reason));
			TestFalse(TEXT("Every invalid request must provide a diagnostic"), Reason.IsEmpty());
		};

		FStandingImpactRequest Request = ValidRequest;
		Request.ImpactId.Invalidate();
		ExpectInvalid(TEXT("An invalid ImpactId must be rejected"), Request);

		Request = ValidRequest;
		Request.SourceProfile = nullptr;
		ExpectInvalid(TEXT("A missing source profile must be rejected"), Request);

		Request = ValidRequest;
		Request.SourceActor = OtherActor;
		ExpectInvalid(TEXT("A source component owned by another actor must be rejected"), Request);

		Request = ValidRequest;
		Request.WorldDirection = FVector::ZeroVector;
		ExpectInvalid(TEXT("A zero response direction must be rejected"), Request);

		Request = ValidRequest;
		Request.ImpactPoint.X = std::numeric_limits<float>::quiet_NaN();
		ExpectInvalid(TEXT("A non-finite impact point must be rejected"), Request);

		Request = ValidRequest;
		Request.RawNormalImpulse.Z = std::numeric_limits<float>::quiet_NaN();
		ExpectInvalid(TEXT("A non-finite raw impulse must be rejected"), Request);

		Request = ValidRequest;
		Request.NormalizedStrength = -0.01f;
		ExpectInvalid(TEXT("A normalized strength below zero must be rejected"), Request);

		Request = ValidRequest;
		Request.NormalizedStrength = 1.01f;
		ExpectInvalid(TEXT("A normalized strength above one must be rejected"), Request);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
