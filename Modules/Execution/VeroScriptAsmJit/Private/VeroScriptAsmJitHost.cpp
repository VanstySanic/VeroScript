// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitHost.h"

#include <stdexcept>

namespace VeroScript::AsmJit::JitHost
{
	namespace
	{
		thread_local const FJitHostServices* ActiveServices = nullptr;
	}

	const FJitHostServices& Services()
	{
		if (ActiveServices) return *ActiveServices;
		#if VEROSCRIPT_ASMJIT_DYNAMIC
		throw std::logic_error("AsmJit operation requires its owning host service scope");
		#else
		// Standalone compiler-plan tests and source embedders use their linked core.
		return GetJitHostServices();
		#endif
	}

	FScope::FScope(const FJitHostServices& InServices) noexcept
		: Previous(ActiveServices)
	{
		ActiveServices = &InServices;
	}

	FScope::~FScope()
	{
		ActiveServices = Previous;
	}
}
