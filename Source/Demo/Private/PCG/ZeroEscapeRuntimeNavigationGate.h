// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeRuntimeNavigationGate.h
 * 职责：用纯值快照过滤当前单次正式生成流程中的导航完成事件与超时。
 * 边界：不创建测试几何、不请求导航重建、不持有 World；Generator 仍拥有全部运行时状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

namespace ZeroEscape::LevelGeneration
{
	struct FRuntimeNavigationGateSnapshot
	{
		int64 OperationId = 0;
		FObjectKey ExpectedNavigationDataKey;
		bool bWaiting = false;
		bool bGeometryRegistrationStarted = false;
		bool bGeometrySubmitted = false;
		bool bObservedNavigationBuild = false;
		bool bReceivedTargetCompletion = false;
		bool bTerminal = false;
	};

	class FRuntimeNavigationGate final
	{
	public:
		/**
		 * UE 的完成事件不携带 PCG OperationId；编号只防旧 Timer/重复终态回调，
		 * 事件归属还依赖单局只接受一次、目标 NavData 与“全部几何已提交”的边界。
		 */
		/** 目标导航数据的事件只有在本次全部几何提交后才有效。 */
		static bool AcceptCompletion(
			const FRuntimeNavigationGateSnapshot& State,
			int64 CallbackOperationId,
			const FObjectKey& ReceivedNavigationDataKey)
		{
			return State.bWaiting
				&& !State.bTerminal
				&& State.OperationId > 0
				&& CallbackOperationId == State.OperationId
				&& State.bGeometryRegistrationStarted
				&& State.bGeometrySubmitted
				&& ReceivedNavigationDataKey == State.ExpectedNavigationDataKey;
		}

		/** 目标已经完成后，任意 NavData 的后续完成事件都应再次检查全局构建是否静止。 */
		static bool ShouldRetryAfterAnyCompletion(
			const FRuntimeNavigationGateSnapshot& State,
			int64 CallbackOperationId)
		{
			return State.bWaiting
				&& !State.bTerminal
				&& State.OperationId > 0
				&& CallbackOperationId == State.OperationId
				&& State.bGeometrySubmitted
				&& State.bReceivedTargetCompletion;
		}

		/** 仅当前操作、目标完成事件已到达且导航已静止时进入路径验收。 */
		static bool CanValidate(
			const FRuntimeNavigationGateSnapshot& State,
			int64 CallbackOperationId,
			bool bNavigationStillBuilding)
		{
			return State.bWaiting
				&& !State.bTerminal
				&& State.OperationId > 0
				&& CallbackOperationId == State.OperationId
				&& State.bGeometrySubmitted
				&& State.bObservedNavigationBuild
				&& State.bReceivedTargetCompletion
				&& !bNavigationStillBuilding;
		}

		/** 超时 Timer 只能终止创建它的当前等待操作，旧 Timer 与重复 Timer 都无效。 */
		static bool AcceptTimeout(
			const FRuntimeNavigationGateSnapshot& State,
			int64 CallbackOperationId)
		{
			return State.bWaiting
				&& !State.bTerminal
				&& State.OperationId > 0
				&& CallbackOperationId == State.OperationId;
		}
	};
}
