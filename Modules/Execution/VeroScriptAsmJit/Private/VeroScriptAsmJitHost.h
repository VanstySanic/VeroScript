// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitHostServices.h"

namespace VeroScript::AsmJit::JitHost
{
	const FJitHostServices& Services();

	// Every public factory/instance/executable entry binds its own host. The
	// thread-local scope restores a nested host and is never a process-wide binding.
	class FScope final
	{
	public:
		explicit FScope(const FJitHostServices& InServices) noexcept;
		~FScope();
		FScope(const FScope&) = delete;
		FScope& operator=(const FScope&) = delete;

	private:
		const FJitHostServices* Previous = nullptr;
	};
}
