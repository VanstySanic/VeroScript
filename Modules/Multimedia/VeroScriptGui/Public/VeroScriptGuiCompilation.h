// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"

namespace VeroScript::Gui
{
	/** Parsers are ordinary script functions taking string and returning Type.
	 * They must be visible in the declaring widget's module. Empty parsers disable
	 * the corresponding source. The profile is copied into immutable processors. */
	struct FGuiValueCodec
	{
		std::string Type, ParseLiteral, ParseStyle;
	};
	struct FGuiResourceBinding
	{
		std::string ResourceIdentity, ContextModule, ContextType;
	};
	struct FGuiCompilationConfiguration
	{
		std::vector<FGuiValueCodec> ValueCodecs;
		// Script-less pages accepting an explicitly supplied business object.
		std::vector<FGuiResourceBinding> ResourceBindings;
	};
	/** Adds the GUI annotation definitions and generators to an existing compile configuration.
	 * The configuration
	 * is replaced only after validation succeeds. Published registries remain immutable. */
	bool ConfigureGuiCompilation(FCompileProcessingOptions& Options, std::string& Error);
	bool ConfigureGuiCompilation(FCompileProcessingOptions& Options, const FGuiCompilationConfiguration& Configuration,
								 std::string& Error);
} // namespace VeroScript::Gui
