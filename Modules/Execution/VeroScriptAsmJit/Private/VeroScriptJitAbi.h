// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitIr.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * Calling convention for asmjit native leaf / linear kernels.
 *
 * - x86_64 Windows: Microsoft x64 (default)
 * - x86_64 Unix: kernels are emitted as MS x64; mark helpers/typedefs with ms_abi
 * - AArch64: AAPCS64 (default); emit uses a64::Assembler
 * - x86 32-bit: cdecl; scalar arguments and return buffers are stack-passed
 */
#if defined(VEROSCRIPT_ASMJIT_X64) && VEROSCRIPT_ASMJIT_X64 && defined(__GNUC__) && defined(__x86_64__) && !defined(_WIN32)
#define VEROSCRIPT_JIT_CALL __attribute__((ms_abi))
#else
#define VEROSCRIPT_JIT_CALL
#endif

#ifndef VEROSCRIPT_HAS_ASMJIT
#define VEROSCRIPT_HAS_ASMJIT 0
#endif
#ifndef VEROSCRIPT_ASMJIT_X64
#define VEROSCRIPT_ASMJIT_X64 0
#endif
#ifndef VEROSCRIPT_ASMJIT_A64
#define VEROSCRIPT_ASMJIT_A64 0
#endif
#ifndef VEROSCRIPT_ASMJIT_X86
#define VEROSCRIPT_ASMJIT_X86 0
#endif

namespace VeroScript::AsmJit::JitAbi
{
	enum class ECallAbi : std::uint8_t
	{
		MsX64 = 0,
		Aapcs64,
		CdeclX86,
	};

	enum class ERegisterClass : std::uint8_t
	{
		Gpr = 0,
		Fpr,
		Memory,
	};

	struct FArgumentLocation
	{
		JitIr::EValueType Type = JitIr::EValueType::Unknown;
		ERegisterClass RegisterClass = ERegisterClass::Memory;
		std::uint8_t RegisterIndex = 0;
		std::uint16_t StackOffset = 0;
	};

	struct FCallLayout
	{
		ECallAbi Abi = ECallAbi::MsX64;
		std::vector<FArgumentLocation> Arguments;
		FArgumentLocation Return;
		std::uint16_t StackBytes = 0;
		bool bSupported = false;
	};

	bool BuildScalarCallLayout(
		const JitIr::FSignature& Signature,
		ECallAbi Abi,
		FCallLayout& OutLayout,
		std::string* OutError = nullptr);
}
