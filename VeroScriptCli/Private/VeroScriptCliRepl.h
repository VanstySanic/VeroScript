// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRun.h"

#include <string_view>
#include <vector>

namespace VeroScript::Cli
{
	int RunRepl(const std::vector<std::string_view>& ImportPaths, bool bStrictExit,
				const std::vector<std::string_view>& Defines = {},
				const std::vector<std::string_view>& StdlibSearchPaths = {},
				EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed,
				const std::vector<FPredefinedMacro>& PredefinedMacros = {},
				const std::vector<std::string_view>& ImplicitImportPaths = {},
				bool bAllowCircularImports = true, bool bAllowProjectWideAccess = false,
				std::shared_ptr<const IJitBackendFactory> JitBackendFactory = {},
				const FScriptCompileExtensionsOptions& ScriptExtensions = {},
				std::shared_ptr<const FModuleEnvironment> ModuleEnvironment = {}, std::shared_ptr<const IVmBackendFactory> VmBackendFactory = {}, std::string VmBackendPath = {});
}
