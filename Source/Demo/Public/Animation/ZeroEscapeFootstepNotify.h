#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ZeroEscapeFootstepNotify.generated.h"

class USoundAttenuation;
class USoundBase;

/**
 * 脚步声 AnimNotify：触发时在骨骼网格位置随机播放一个脚步声。
 * 随机音高 + 距离衰减资产，避免机枪感并保证远近大小变化。
 * 只负责表现，不携带任何玩法逻辑。
 */
UCLASS()
class DEMO_API UZeroEscapeFootstepNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** 触发时随机选取一个播放；为空则不发声。 */
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	/** 脚步声候选集合，每次触发随机取一个；由动画资产上的 Notify 实例指定。 */
	UPROPERTY(EditAnywhere, Category = "Footstep")
	TArray<TObjectPtr<USoundBase>> FootstepSounds;

	/** 距离衰减资产，控制远近大小变化；不指定则声音无衰减，必须指定。 */
	UPROPERTY(EditAnywhere, Category = "Footstep")
	TObjectPtr<USoundAttenuation> AttenuationSettings;

	/** 音量倍率；脚步原始音量偏小，基准放大 5 倍。 */
	UPROPERTY(EditAnywhere, Category = "Footstep", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 5.0f;

	/** 随机音高下限。 */
	UPROPERTY(EditAnywhere, Category = "Footstep", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float PitchMin = 0.94f;

	/** 随机音高上限。 */
	UPROPERTY(EditAnywhere, Category = "Footstep", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float PitchMax = 1.06f;
};
