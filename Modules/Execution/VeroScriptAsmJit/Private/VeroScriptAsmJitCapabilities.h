// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitLowering.h"

#include <cstdint>
#include <string>

namespace VeroScript::AsmJit::JitBackend
{
	enum class EArchitecture : std::uint8_t
	{
		X64 = 0,
		A64,
		X86,
	};

	/**

	 * Capabilities shared with the frontend and used before machine-code emission.

	 */
	struct FCapabilities
	{
		EArchitecture Architecture = EArchitecture::X64;
		bool bNativeLeaves = false;
		bool bScalarCfg = false;
		bool bI32Cfg = false;
		bool bF64Cfg = false;
		bool bBoxedCfg = false;
		bool bMixedCfg = false;
	};

	FCapabilities GetCapabilities(EArchitecture Architecture);
	FCapabilities GetHostCapabilities();

	// Validate that a lowering plan is legal for the selected backend contract.
	bool ValidateScalarPlan(
		const JitLowering::FPlan& Plan,
		EArchitecture Architecture,
		std::string* OutError = nullptr);
}
