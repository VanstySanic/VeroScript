// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompiledExecution.h"

namespace VeroScript::AsmJit
{
	class FControlledCodeCache
	{
	public:
		FControlledCodeCache();
		~FControlledCodeCache();
		EJitCompilationResult Compile(const FJitCompileRequest&, FJitCompileResponse&);
		FJitResourceUsage GetResourceUsage() const noexcept;
		FJitStats GetStatistics() const noexcept;
		void ResetStatistics() noexcept;
	private:
		struct FImpl;
		std::shared_ptr<FImpl> Impl;
	};
}
