// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVmBackend.h"
#include "VeroScriptExecutionServices.h"
#include "VeroScriptJitModule.h"

namespace VeroScript
{
	inline constexpr const char* VmModuleQuerySymbol = "veroscript_vm_query";
	struct FVmModuleDescriptor
	{
		std::uint32_t StructSize = sizeof(FVmModuleDescriptor);
		std::uint32_t CoreVersion = VEROSCRIPT_VERSION_CODE;
		char Abi[512]{};
		char Id[64]{};
		char Version[32]{};
		void* (VEROSCRIPT_JIT_MODULE_CALL *CreateFactory)(const void*, char*, std::uint32_t) = nullptr;
		void (VEROSCRIPT_JIT_MODULE_CALL *DestroyFactory)(void*) = nullptr;
	};
	using FVmModuleQuery = std::uint32_t (VEROSCRIPT_JIT_MODULE_CALL *)(
		std::uint32_t, std::uint32_t, void*);
	// Shared compiler/STL/CRT/product checks plus the actual VM view layouts.
	static inline std::string BuildVmModuleAbi()
	{
		return BuildJitModuleAbi() + ":vm:" + std::to_string(sizeof(FVmExecutionView))
			+ ':' + std::to_string(sizeof(FVmHostServices)) + ':' + std::to_string(sizeof(FVmInvocation))
			+ ':' + std::to_string(sizeof(FVmCallFrame));
	}
}
