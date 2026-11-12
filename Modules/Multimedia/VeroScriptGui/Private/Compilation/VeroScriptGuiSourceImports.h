// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRun.h"

namespace VeroScript::Gui
{
	struct FGuiCompilationConfiguration;
	bool ConfigureGuiSourceImports(FEngineConfig& Config, const FGuiCompilationConfiguration& Compilation,
		std::string& Error);
	bool RemoveGuiSourceImports(FCompileProcessingOptions& Options, std::string& Error);
} // namespace VeroScript::Gui
