// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptGuiCompilation.h"

namespace VeroScript::Gui
{
	FCompileProcessorDesc MakeGuiMarkupProcessor(std::shared_ptr<const FGuiCompilationConfiguration> Configuration,
												 std::string ConfigurationKey, bool bImportFramework = false);
} // namespace VeroScript::Gui
