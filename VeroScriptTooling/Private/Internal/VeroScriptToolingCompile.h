// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompile.h"
#include "VeroScriptSourceColumns.h"

namespace VeroScript::Tooling::Detail
{
	using FVirtualSourceMap = std::unordered_map<std::string, std::string>;
	// Retain the maps from this compilation, including semantic/preparation failures.
	// Rebuilding them would run potentially stateful source processors a second time.
	FCompileResult CompileToolingSource(std::string_view Source, std::string_view FileLabel,
		const FCompileOptions& Options, FSourceColumnMaps* SourceColumns = nullptr,
		FVirtualSourceMap* VirtualSources = nullptr);
	FCompileResult CompileToolingFile(std::string_view FilePath, const FCompileOptions& Options,
		FSourceColumnMaps* SourceColumns = nullptr, FVirtualSourceMap* VirtualSources = nullptr);
}
