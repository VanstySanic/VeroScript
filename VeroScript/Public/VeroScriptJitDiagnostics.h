// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJit.h"
#include <array>
#include <string>
#include <vector>
namespace VeroScript
{
	const char* JitExecutionKindName(EJitExecutionKind Kind) noexcept;
	const char* JitFallbackReasonName(EJitFallbackReason Reason) noexcept;
	struct FJitFunctionDiagnostic
	{
		std::uint16_t FunctionIndex = 0;
		std::string Name;
		std::string ModulePath;
		// Current cached plan at snapshot time; Fallbacks retains all observed
		// rejection events, including earlier warm-up and budget decisions.
		std::string PlanReason;
		EJitExecutionKind PlannedKind = EJitExecutionKind::Unknown;
		std::size_t Calls = 0;
		std::size_t Returned = 0;
		std::size_t ControlTransfers = 0;
		std::size_t Failures = 0;
		std::uint64_t InclusiveNanoseconds = 0;
		std::uint64_t SelfNanoseconds = 0;
		std::array<std::size_t, static_cast<std::size_t>(EJitExecutionKind::Count)> Executions{};
		std::array<std::size_t, static_cast<std::size_t>(EJitFallbackReason::Count)> Fallbacks{};
	};
	struct FJitDiagnostics
	{
		std::uint64_t ProgramGeneration = 0;
		std::size_t DroppedRecords = 0;
		bool bTimingEnabled = false;
		// Actual Core call boundaries. Inlined operations and raw nested native
		// targets have no separate Core entry; existing direct/loop counters expose
		// those events. Inclusive times overlap and must not be summed.
		std::vector<FJitFunctionDiagnostic> Functions;
	};
}
