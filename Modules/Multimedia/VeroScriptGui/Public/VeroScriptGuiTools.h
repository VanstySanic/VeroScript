// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiCompilation.h"
#include "VeroScriptCompile.h"
#include "VeroScriptPackage.h"
#include "VeroScriptGuiScript.h"
#include "VeroScriptGuiCommandLine.h"

namespace VeroScript::Gui
{
	struct FToolOptions
	{
		FEngineConfig Engine;
		FGuiCompilationConfiguration Compilation;
		FScriptServices Runtime;
		bool bHostOwnsCommandRegistration = false;
	};
	std::shared_ptr<const ICommandLineService> CreateCommandLineService(FToolOptions Options = {});
	/** Shared offline tasks. No script initializers, windows, devices or GUI objects are executed. */
	FCompileResult CompileGuiSource(std::string_view Source, std::string_view FileLabel,
									const FCompileOptions& Options = {},
									const FGuiCompilationConfiguration& Configuration = {});
	/** A .vswml entry is imported by a generated, inert main module; .vs uses its own entry point. */
	FCompileResult CompileGuiFile(const std::string& Path, const FCompileOptions& Options = {},
								  const FGuiCompilationConfiguration& Configuration = {});
	struct FGuiAssetOptions
	{
		// Absolute source file paths. References in VS/ML/CSS remain relative to their owning module.
		std::vector<std::string> Resources;
		FResourceReader ReadResource;
	};
	/** Bake a Core application image for embedding. gui-compile wraps it in the standard .vsapp container.
	 * ML factories, code and styles are in bytecode; listed images/fonts are embedded as bytes. */
	bool BakeGuiApplication(const FCompileResult& Compilation, FPackageBuildOptions Options,
							std::vector<std::uint8_t>& Bytes, std::string& Error);
	bool BakeGuiApplication(const FCompileResult& Compilation, FPackageBuildOptions Options,
							const FGuiAssetOptions& Assets, std::vector<std::uint8_t>& Bytes, std::string& Error);
	/** Reconstructs portable resource bases from the artifact, without original source files. */
	bool ConfigureGuiArtifactServices(const FPackageData& Package, FScriptServices& Services, std::string& Error);
	bool RegisterGuiArtifactBindings(FHostBindingRegistry& Registry, const FPackageData& Package,
									 FScriptServices Services, std::string& Error);
} // namespace VeroScript::Gui
