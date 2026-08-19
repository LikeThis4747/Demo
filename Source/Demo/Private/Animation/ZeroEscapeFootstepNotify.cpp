#include "Animation/ZeroEscapeFootstepNotify.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFlow/ZeroEscapeGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

void UZeroEscapeFootstepNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp) || FootstepSounds.Num() == 0)
	{
		return;
	}

	USoundBase* ChosenSound = FootstepSounds[FMath::RandRange(0, FootstepSounds.Num() - 1)];
	if (!IsValid(ChosenSound))
	{
		return;
	}

	const float RandomPitch = FMath::FRandRange(PitchMin, PitchMax);
	const float FinalVolume = VolumeMultiplier * UZeroEscapeGameInstance::GetSfxVolumeFor(MeshComp);
	UGameplayStatics::PlaySoundAtLocation(
		MeshComp,
		ChosenSound,
		MeshComp->GetComponentLocation(),
		FinalVolume,
		RandomPitch,
		0.0f,
		AttenuationSettings.Get());
}
