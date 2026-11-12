// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"

#include "VeroScriptPreprocessor.h"
#include "VeroScriptAnnotationProcessing.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace VeroScript::Tooling
{
	/**
	 * Optional CLI/host defaults applied before the client sends workspace/didChangeConfiguration.
	 */
	struct FLspStdioRunOptions
	{
		std::vector<std::string> DefaultImportPaths;
		std::vector<std::string> DefaultStdlibSearchPaths;
		std::vector<std::string> DefaultDefines;
		std::vector<VeroScript::FPredefinedMacro> DefaultPredefinedMacros;
		// Ordered catalogs merged for Analyze/Complete/…; duplicate definitions fail.
		std::vector<std::string> DefaultBindingCatalogPaths;
		// Appended to preserve positional aggregate initialization of older callers.
		std::vector<std::string> DefaultImplicitImportPaths;
		bool bDefaultAllowCircularImports = true;
		bool bDefaultAllowProjectWideAccess = false;
		// Initial closed-file indexing roots. initialize.rootUri/workspaceFolders,
		// when supplied by the client, replace these defaults. Appended to preserve
		// positional aggregate initialization of older callers.
		std::vector<std::string> DefaultWorkspaceRoots;
		// Native embedding hosts supply the same frozen registry as compilation.
		// Factories/readers may run on the analysis and protocol workers concurrently;
		// provide thread-safe host snapshots and cooperative cancellation callbacks.
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	/**
	 * Stdio LSP host: Content-Length framing + JSON-RPC over istream/ostream.
	 * Wraps VeroScriptWorkspace: tracks open documents, publishes diagnostics after
	 * open/change, and answers language-feature requests (completion, hover,
	 * definition/declaration/type-definition/implementation, type hierarchy,
	 * document symbols/links/folding/selection ranges, signature help, references,
	 * document highlights, rename, semantic tokens with delta, formatting and safe
	 * source actions, plus workspace symbols with resolve).
	 */
	class VeroScriptLspStdio
	{
	public:
		// 0 on clean shutdown or EOF; exit-before-shutdown is non-zero.
		static int Run(
			std::istream& In,
			std::ostream& Out,
			const FLspStdioRunOptions& Options = {});
	};
}
