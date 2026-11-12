// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiTools.h"
#include "VeroScriptGuiScript.h"

namespace VeroScript::Gui
{
	inline constexpr const char* GuiModuleMarker = "veroscript.gui.module:";
	inline constexpr const char* GuiSourceMarker = "veroscript.gui.source:";
	inline constexpr const char* GuiBakedMarker = "veroscript.gui.baked:";
	bool BuildGuiArtifact(const FCompileResult& Compilation, FPackageBuildOptions Options,
						  std::vector<std::uint8_t>& Bytes, std::string& Error, const FGuiAssetOptions& Assets = {});
} // namespace VeroScript::Gui
