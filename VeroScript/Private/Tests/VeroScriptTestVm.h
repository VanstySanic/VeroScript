// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVm.h"
#include "VeroScriptVmBackendLoader.h"
#include "VeroScriptCompile.h"
#include <cstdlib>
#include <stdexcept>

namespace VeroScript::Test
{
	inline std::shared_ptr<const IVmBackendFactory> VmFactory()
	{
		static const auto Factory = []()
		{
			std::string Error;
			const auto* Path = std::getenv("VEROSCRIPT_TEST_VM_BACKEND");
			#ifdef VEROSCRIPT_TEST_VM_LIBRARY
			if (!Path)
			{
				Path = VEROSCRIPT_TEST_VM_LIBRARY;
			}
			#endif
			auto Result = Path ? LoadVmBackend(Path, Error) : nullptr;
			if (!Result)
			{
				throw std::runtime_error(Error.empty() ? "test requires an explicit VM backend" : Error);
			}
			return Result;
		}();
		return Factory;
	}
	inline void Configure(VeroScriptVm& Vm)
	{
		std::string Error;
		if (!Vm.GetVmBackendFactory() && !Vm.SetVmBackendFactory(VmFactory(), Error))
		{
			throw std::runtime_error(Error);
		}
	}
	inline void Configure(FEngineConfig& Config)
	{
		if (!Config.VmBackendFactory && Config.VmBackendPath.empty() && !Config.bClearVmBackend)
		{
			Config.VmBackendFactory = VmFactory();
		}
	}
	inline void Configure(FCompileOptions& Options)
	{
		if (!Options.ScriptCompileExtensions.VmBackendFactory && Options.ScriptCompileExtensions.VmBackendPath.empty())
		{
			Options.ScriptCompileExtensions.VmBackendFactory = VmFactory();
		}
	}
	inline FCompileOptions MakeCompileOptions()
	{
		FCompileOptions Options;
		Configure(Options);
		return Options;
	}
	inline FEngineConfig MakeEngineConfig()
	{
		FEngineConfig Config;
		Configure(Config);
		return Config;
	}
	template <typename TSession>
	void Configure(TSession& Session)
	{
		std::string Error;
		if (!Session.GetVmExecutionDomain()->GetFactory() && !Session.SetVmBackendFactory(VmFactory(), Error))
		{
			throw std::runtime_error(Error);
		}
	}
}
