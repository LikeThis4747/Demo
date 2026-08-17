// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeStructureBuilder.h
 * 职责：把已经通过全局验证的多层 Plan 展开为普通结构与完整结构的 HISM 实例。
 * 边界：不求解布局、不修改 Plan/Hash；只返回固定灯位描述，不生成 Actor，也不拥有回滚生命周期。
 */

#pragma once

#include "CoreMinimal.h"
#include "PCG/ZeroEscapeGenerationAssets.h"

class AActor;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMesh;
class UZeroEscapePresentationProfile;

namespace ZeroEscape::LevelGeneration
{
	/** 一条尚未提交到场景的表现实例；GroupId 只用于稳定批处理和诊断。 */
	struct FStructurePresentationInstance
	{
		FName GroupId = NAME_None;
		TObjectPtr<UStaticMesh> StaticMesh = nullptr;
		FName CollisionProfileName = NAME_None;
		EZeroEscapeStructurePieceCollisionRole CollisionRole =
			EZeroEscapeStructurePieceCollisionRole::StandardProfile;
		bool bCanEverAffectNavigation = false;
		bool bHiddenInGame = false;
		bool bCastShadow = true;
		FTransform LocalTransform = FTransform::Identity;
		int32 RelatedStableId = INDEX_NONE;
	};

	/** 构建结果不拥有 UObject；Generator 把计数并入 Report，并负责销毁已登记组件。 */
	struct FStructureBuildResult
	{
		int32 InstancedMeshCount = 0;
		int32 HismComponentCount = 0;
		int32 RelatedStableId = INDEX_NONE;
		TArray<FTransform> FixedLightLocalTransforms;
		TArray<FTransform> StairGuidancePointLightLocalTransforms;
		FString Error;

		void Reset()
		{
			InstancedMeshCount = 0;
			HismComponentCount = 0;
			RelatedStableId = INDEX_NONE;
			FixedLightLocalTransforms.Reset();
			StairGuidancePointLightLocalTransforms.Reset();
			Error.Reset();
		}
	};

	/** 普通格与完整楼梯/高厅的唯一结构表现构建器。 */
	class FStructureBuilder final
	{
	public:
		/**
		 * 只展开纯描述，不创建组件；测试用同一入口验证多层高度、旋转和隐藏坡面数量。
		 * 失败时 OutInstances 为空，OutResult 保存首个关联结构和原因。
		 */
		static bool Expand(
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const UZeroEscapePresentationProfile& Profile,
			TArray<FStructurePresentationInstance>& OutInstances,
			FStructureBuildResult& OutResult);

		/**
		 * 展开并提交 HISM。每个组件创建后立即加入 InOutRegisteredComponents，
		 * 因而后续任意失败都可由 Generator 的统一事务路径回滚。
		 */
		static bool Build(
			AActor& Owner,
			USceneComponent& GeneratedRoot,
			const FZeroEscapeGeneratedLevelPlan& Plan,
			const UZeroEscapePresentationProfile& Profile,
			TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>&
				InOutRegisteredComponents,
			FStructureBuildResult& OutResult);
	};
}
