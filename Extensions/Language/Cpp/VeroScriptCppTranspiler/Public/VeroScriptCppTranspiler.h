// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCppFrontend.h"

#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::CppTranspiler
{
	struct FAttributeMapping
	{
		// Exact C++ spelling, e.g. "nodiscard" or "vendor::Reflect".
		std::string CppName;
		// VeroScript annotation identifier without the leading '@'.
		std::string AnnotationName;
	};

	struct FTranspileOptions
	{
		bool bValidateGeneratedSource = true;
		// Logical path recorded in source maps and generated-source diagnostics.
		// Empty uses <input>.vs.
		std::string GeneratedSourcePath;
		// Project emitters can supply declarations discovered in sibling inputs.
		std::vector<std::string> AdditionalKnownHostTypes;
		std::vector<std::string> AdditionalKnownEnumTypes;
		std::vector<CppFrontend::FTypeAlias> AdditionalKnownTypeAliases;
		// Explicit opt-in mappings for attributes outside the reserved
		// veroscript:: / vero:: namespaces.
		std::vector<FAttributeMapping> AttributeMappings;
	};

	struct FTranspileResult
	{
		bool Succeeded() const;

		std::string Source;
		std::string SourceMapJson;
		std::vector<CppFrontend::FDiagnostic> Diagnostics;

	};

	/**
	 * One generated source in a project transpilation result.
	 */
	struct FTranspiledFile
	{
		std::string InputPath;
		// Stable relative output name. The CLI places it under --output-dir.
		std::string OutputName;
		std::string Source;
		std::string SourceMapJson;
	};

	struct FTranspileProjectOptions
	{
		bool bValidateGeneratedSources = true;
		std::vector<FAttributeMapping> AttributeMappings;
	};

	struct FTranspileProjectResult
	{
		bool Succeeded() const;

		std::vector<FTranspiledFile> Files;

		// Deterministic machine-readable relationship between inputs and outputs.
		std::string ManifestJson;
		std::vector<CppFrontend::FDiagnostic> Diagnostics;

	};

	FTranspileResult Transpile(
		const CppFrontend::FTranslationUnit& Unit,
		const FTranspileOptions& Options = {});

	FTranspileResult TranspileSource(
		std::string_view Source,
		std::string_view SourcePath = "<cpp-source>",
		const FTranspileOptions& Options = {});

	FTranspileResult TranspileFile(
		std::string_view FilePath,
		const FTranspileOptions& Options = {});

	/**
	 * Transpile a C++ project into one VeroScript source per input file.
	 *
	 * All files are parsed and merged before emission, so type mapping and
	 * duplicate-declaration diagnostics use the complete project surface. The
	 * generated manifest enables a workspace to load the outputs together.
	 */
	FTranspileProjectResult TranspileFiles(
		const std::vector<std::string>& FilePaths,
		const FTranspileProjectOptions& Options = {});
}
