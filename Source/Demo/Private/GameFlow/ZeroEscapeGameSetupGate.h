// Copyright Epic Games, Inc. All Rights Reserved.

/** 纯值策略：过滤 Generator 的旧操作、重复终态与 EndPlay 后回调。 */

#pragma once

#include "CoreMinimal.h"

namespace ZeroEscape::GameFlow
{
	struct FGameSetupGateSnapshot
	{
		int64 ActiveOperationId = 0;
		int64 LastHandledOperationId = 0;
		bool bTerminal = false;
		bool bEndingPlay = false;
	};

	class FGameSetupGate final
	{
	public:
		static bool AcceptFinalReport(
			const FGameSetupGateSnapshot& State,
			const int64 ReportOperationId)
		{
			return !State.bTerminal
				&& !State.bEndingPlay
				&& State.ActiveOperationId > 0
				&& ReportOperationId == State.ActiveOperationId
				&& ReportOperationId > State.LastHandledOperationId;
		}
	};
}
