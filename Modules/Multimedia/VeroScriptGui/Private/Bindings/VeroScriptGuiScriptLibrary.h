// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>
#include "VeroScriptRun.h"

namespace VeroScript::Gui
{
	std::string_view GetGuiScriptLibrarySource();
	std::string_view GetGuiCompilationFingerprint();
	const std::string& GetGuiScriptLibraryPath();
	bool IsGuiScriptLibraryPath(const std::string& Path);
	bool ConfigureGuiLibrarySources(FEngineConfig& Config, std::string& Error);
} // namespace VeroScript::Gui
