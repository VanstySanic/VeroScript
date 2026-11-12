// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecodeVm.h"
#include "VeroScriptRun.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <stdexcept>

namespace VeroScript::BytecodeVm
{
	// Product layout only. Core loaders never use this path implicitly.
	inline std::string RuntimeLibraryPath(std::string_view Executable)
	{
		auto Directory = PathUtil::FromUtf8(Executable).parent_path() / "Vm";
		#if defined(_WIN32)
		Directory /= "VeroScriptVmBytecode.dll";
		#elif defined(__APPLE__)
		Directory /= "libVeroScriptVmBytecode.dylib";
		#else
		Directory /= "libVeroScriptVmBytecode.so";
		#endif
		return PathUtil::ToUtf8(std::filesystem::absolute(Directory).lexically_normal());
	}
	/** Product fallback assembly; the Core defaults remain empty. */
	inline void Configure(FEngineConfig& Config)
	{
		if (!Config.VmBackendFactory && Config.VmBackendPath.empty() && !Config.bClearVmBackend
			&& !Config.FallbackVmBackendFactory && Config.FallbackVmBackendPath.empty())
		{
			Config.FallbackVmBackendFactory = CreateFactory(GetVmHostServices());
		}
	}
	namespace Detail
	{
		template <typename TSession>
		auto ConfigureFallback(TSession& Session, std::shared_ptr<const IVmBackendFactory> Factory,
			std::string& Error, int) -> decltype(Session.SetFallbackVmBackendFactory(Factory, Error))
		{
			return Session.SetFallbackVmBackendFactory(std::move(Factory), Error);
		}
		template <typename TVm>
		bool ConfigureFallback(TVm& Vm, std::shared_ptr<const IVmBackendFactory> Factory,
			std::string& Error, long)
		{
			return Vm.SetVmBackendFactory(std::move(Factory), Error);
		}
	}
	template <typename TSession> void Configure(TSession& Session)
	{
		std::string Error;
		if (!Detail::ConfigureFallback(Session, CreateFactory(GetVmHostServices()), Error, 0))
		{
			throw std::runtime_error(Error);
		}
	}
}
