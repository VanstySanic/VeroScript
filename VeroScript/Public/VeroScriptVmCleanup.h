// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"

#include <cstddef>
#include <utility>

namespace VeroScript::VmDetail
{
	// Bound idle storage shared by the interpreter and JIT to 64 frames and
	// 262,144 local slots per VM. Active frames retain their full language limits.
	inline constexpr bool CanRecycleFrame(std::size_t PooledFrames, std::size_t LocalCapacity) noexcept
	{
		return PooledFrames < 64 && LocalCapacity <= 4096;
	}

	inline void RecordCleanupFailure(bool& bSucceeded, FScriptError& Failure, bool& bHasError, FScriptError& Error)
	{
		// Keep the first runtime fault. Script exceptions are transfers: a later
		// uncaught cleanup exception replaces the earlier provisional diagnostic.
		if (bSucceeded || (Failure.bUncaughtScriptException && bHasError))
		{
			Failure = std::move(Error);
		}
		bSucceeded = false;
		bHasError = false;
		Error = {};
	}

	inline bool FinishCleanup(const bool bSucceeded, const bool bHadPendingError, FScriptError&& PendingError,
							  FScriptError&& Failure, const bool bExitRequested, bool& bHasError, FScriptError& Error)
	{
		FScriptError* Selected = nullptr;
		if (bHadPendingError && (!PendingError.bUncaughtScriptException || bSucceeded))
		{
			Selected = &PendingError;
		}
		else if (!bSucceeded)
		{
			Selected = &Failure;
		}
		if (Selected)
		{
			if (bExitRequested && Selected->bUncaughtScriptException)
			{
				bHasError = false;
				Error = {};
			}
			else
			{
				bHasError = true;
				Error = std::move(*Selected);
			}
		}
		// An explicit exit supersedes pending script exceptions, including ones
		// raised earlier in this cleanup pass. Actual runtime faults still fail.
		return bSucceeded || (bExitRequested && !bHasError);
	}
} // namespace VeroScript::VmDetail
