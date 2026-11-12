// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitCapabilities.h"

#include "VeroScriptJitAbi.h"

namespace VeroScript::AsmJit::JitBackend
{
	namespace
	{
		void SetError(std::string* const OutError, const char* const Message)
		{
			if (OutError)
			{
				*OutError = Message;
			}
		}
	}

	FCapabilities GetCapabilities(const EArchitecture Architecture)
	{
		FCapabilities Result;
		Result.Architecture = Architecture;
		if (Architecture == EArchitecture::X86)
		{
			// The 32-bit backend currently emits scalar leaves. Full CFG lowering is
			// intentionally disabled until its cdecl frame emitter is available.
			Result.bNativeLeaves = true;
			return Result;
		}
		Result.bNativeLeaves = true;
		Result.bScalarCfg = true;
		Result.bI32Cfg = true;
		// AArch64 supports homogeneous I32/U32/bool/F64 CFGs, including helper-backed
		// calls, globals, methods, and scalar fields. Mixed scalar remains x64-only.
		Result.bF64Cfg = true;
		Result.bBoxedCfg = false;
		Result.bMixedCfg = Architecture == EArchitecture::X64;
		return Result;
	}

	FCapabilities GetHostCapabilities()
	{
		#if defined(VEROSCRIPT_ASMJIT_A64) && VEROSCRIPT_ASMJIT_A64
		return GetCapabilities(EArchitecture::A64);
		#elif defined(VEROSCRIPT_ASMJIT_X64) && VEROSCRIPT_ASMJIT_X64
		return GetCapabilities(EArchitecture::X64);
		#elif defined(VEROSCRIPT_ASMJIT_X86) && VEROSCRIPT_ASMJIT_X86
		return GetCapabilities(EArchitecture::X86);
		#else
		FCapabilities Result;
		Result.bNativeLeaves = false;
		Result.bScalarCfg = false;
		Result.bI32Cfg = false;
		Result.bF64Cfg = false;
		Result.bBoxedCfg = false;
		Result.bMixedCfg = false;
		return Result;
		#endif
	}

	bool ValidateScalarPlan(
		const JitLowering::FPlan& Plan,
		const EArchitecture Architecture,
		std::string* const OutError)
	{
		const FCapabilities Capabilities = GetCapabilities(Architecture);
		if (!Capabilities.bScalarCfg)
		{
			SetError(OutError, "backend does not provide scalar CFG lowering");
			return false;
		}
		if (!Plan.bScalarEligible || Plan.bHasRuntimeFallback)
		{
			SetError(OutError, "lowering plan contains runtime fallback operations");
			return false;
		}
		if (Plan.bRequiresVmOwnership)
		{
			SetError(OutError, "lowering plan requires VM-managed value ownership");
			return false;
		}
		if (Plan.bHasBoxedValues || Plan.MaxBoxedStackSlots != 0)
		{
			SetError(OutError, "lowering plan requires boxed/object value storage");
			return false;
		}
		JitAbi::FCallLayout Layout;
		if (!JitAbi::BuildScalarCallLayout(
				Plan.Signature,
				Architecture == EArchitecture::A64 ? JitAbi::ECallAbi::Aapcs64
				: Architecture == EArchitecture::X86 ? JitAbi::ECallAbi::CdeclX86
				: JitAbi::ECallAbi::MsX64,
				Layout,
				OutError))
		{
			return false;
		}
		for (const JitLowering::FBlock& Block : Plan.Blocks)
		{
			for (const JitLowering::FInstruction& Instruction : Block.Instructions)
			{
				const auto Type = Instruction.ResultType;
				if (Type != JitIr::EValueType::Void
					&& Type != JitIr::EValueType::I32
					&& Type != JitIr::EValueType::U32
					&& Type != JitIr::EValueType::F64
					&& Type != JitIr::EValueType::Bool)
				{
					SetError(OutError, "scalar plan contains a non-scalar value type");
					return false;
				}
			}
		}
		return true;
	}
}
