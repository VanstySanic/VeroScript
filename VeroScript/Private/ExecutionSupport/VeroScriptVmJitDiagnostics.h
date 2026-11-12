// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVm.h"
namespace VeroScript
{
	void MarkFunctionDiagnosticOutcome(FJitDiagnosticState *State, std::uint64_t Token,
		bool Failed, bool ControlTransfer) noexcept;
	template <typename TFrames>
	void MarkDiscardedFunctionDiagnostics(FJitDiagnosticState *State, const TFrames &Frames,
		std::size_t Keep, bool Failed, bool ControlTransfer) noexcept
	{
		if (!State) return;
		for (std::size_t Index = Frames.size(); Index > Keep; --Index)
		{
			MarkFunctionDiagnosticOutcome(State, Frames[Index - 1].DiagnosticToken, Failed, ControlTransfer);
		}
	}
	// A materialized VM frame takes over its token; synchronous native/scalar
	// entries close it here. The disabled case performs no allocation or timing.
	struct FJitDiagnosticScope
	{
		FJitDiagnosticScope(VeroScriptVm &InVm, std::uint16_t Function, EJitExecutionKind Kind) noexcept
			: Vm(InVm), Token(InVm.JitPolicy.Diagnostics.MaxFunctions ? Vm.BeginFunctionDiagnostic(Function, Kind) : 0)
		{
		}
		~FJitDiagnosticScope()
		{
			if (Token)
			{
				Vm.EndFunctionDiagnostic(Token, Entered);
			}
		}
		std::uint64_t Release() noexcept
		{
			const auto Result = Token;
			Token = 0;
			return Result;
		}
		VeroScriptVm &Vm;
		std::uint64_t Token = 0;
		bool Entered = true;
	};
} // namespace VeroScript
