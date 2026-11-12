// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCppFrontend.h"

#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::CppBindGen
{
	struct FBindGenOptions
	{
		bool bEmitApiReference = true;
	};

	struct FBindGenResult
	{
		bool Succeeded() const;

		std::string CatalogJson;

		// Human-readable VeroScript declaration surface. The catalog, not this text,
		// is the authoritative machine-readable binding artifact.
		std::string ApiReference;
		std::vector<CppFrontend::FDiagnostic> Diagnostics;

	};

	struct FBindGenApiReference
	{
		std::string InputPath;
		std::string OutputName;
		std::string Contents;
	};

	struct FBindGenProjectResult
	{
		bool Succeeded() const;

		// The merged catalog remains authoritative and is emitted exactly once.
		std::string CatalogJson;
		std::vector<FBindGenApiReference> ApiReferences;
		std::string ManifestJson;
		std::vector<CppFrontend::FDiagnostic> Diagnostics;

	};

	struct FBindGenProjectLayout
	{
		std::string CatalogPath = "bindings.json";
		// Empty keeps per-input API paths beside the manifest.
		std::string ApiReferenceDirectory;
	};

	FBindGenResult GenerateBindings(
		const CppFrontend::FTranslationUnit& Unit,
		const FBindGenOptions& Options = {});

	FBindGenResult GenerateBindingsFromSource(
		std::string_view Source,
		std::string_view SourcePath = "<cpp-source>",
		const FBindGenOptions& Options = {});

	FBindGenResult GenerateBindingsFromFile(
		std::string_view FilePath,
		const FBindGenOptions& Options = {});

	FBindGenResult GenerateBindingsFromFiles(
		const std::vector<std::string>& FilePaths,
		const FBindGenOptions& Options = {});

	// Generate one merged catalog plus a stable API-reference file per input.
	FBindGenProjectResult GenerateBindingProject(
		const std::vector<std::string>& FilePaths,
		const FBindGenOptions& Options = {},
		const FBindGenProjectLayout& Layout = {});
}
