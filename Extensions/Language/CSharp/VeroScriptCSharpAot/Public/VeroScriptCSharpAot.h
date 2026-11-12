// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCodegen.h"

#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	struct FProgram;

	struct FCSharpAotSourceFile
	{
		std::string RelativePath;
		std::string Contents;
	};

	/**
	 * Deterministic build-ready C# source bundle. Program emission separates the
	 * runtime support, lowered program, and entry point into partial-class source
	 * files and includes an SDK project plus a machine-readable manifest.
	 */
	struct FCSharpAotSourceBundle
	{
		std::vector<FCSharpAotSourceFile> Files;
		std::string ManifestJson;
	};

	// Emits one self-contained C# compilation unit from a checked VeroScript program.
	bool EmitCSharpFromProgram(
		const FProgram& Program,
		std::string& OutCSharp,
		std::string& OutError);

	bool EmitCSharpSourceBundleFromProgram(
		const FProgram& Program,
		std::string_view BundleName,
		FCSharpAotSourceBundle& OutBundle,
		std::string& OutError);

	// Options-aware overload; accepts explicit or module-environment host catalogs.
	// Conflicting catalogs are rejected before emission.
	bool EmitCSharpSourceBundleFromProgram(
		const FProgram& Program,
		std::string_view BundleName,
		const FCodegenOptions& Options,
		FCSharpAotSourceBundle& OutBundle,
		std::string& OutError);

	// Package already-emitted C# with an SDK project without rewriting it.
	bool BuildCSharpSourceBundle(
		std::string_view GeneratedCSharp,
		std::string_view BundleName,
		FCSharpAotSourceBundle& OutBundle,
		std::string& OutError);

	// Loads imports, applies preprocessing, checks compiler semantics, and emits C#.
	bool EmitCSharpFromSource(
		std::string_view Source,
		std::string_view SourceLabel,
		std::string& OutCSharp,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	bool EmitCSharpSourceBundleFromSource(
		std::string_view Source,
		std::string_view SourceLabel,
		std::string_view BundleName,
		FCSharpAotSourceBundle& OutBundle,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	bool EmitCSharpSourceBundleFromSource(
		std::string_view Source,
		std::string_view SourceLabel,
		std::string_view BundleName,
		const FCodegenOptions& Options,
		FCSharpAotSourceBundle& OutBundle,
		std::string& OutError);

	// Reads a VeroScript entry module and writes the generated C# file.
	bool EmitCSharpFromFile(
		std::string_view ScriptPath,
		std::string_view OutputPath,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	// Reads an entry module and returns a build-ready deterministic source bundle.
	bool EmitCSharpSourceBundleFromFile(
		std::string_view ScriptPath,
		std::string_view BundleName,
		FCSharpAotSourceBundle& OutBundle,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	bool EmitCSharpSourceBundleFromFile(
		std::string_view ScriptPath,
		std::string_view BundleName,
		const FCodegenOptions& Options,
		FCSharpAotSourceBundle& OutBundle,
		std::string& OutError);

	/**
	 * ICodegenBackend adapter for tooling that selects generators by backend id.
	 */
	class FCSharpCodegenBackend final : public ICodegenBackend
	{
	public:
		//~ Begin ICodegenBackend Interface
		std::string_view GetName() const override
		{
			return "csharp";
		}
		std::string_view GetFileExtension() const override
		{
			return ".cs";
		}

		bool EmitFromSource(
			std::string_view Source,
			std::string_view SourceLabel,
			const FCodegenOptions& Options,
			FCodegenResult& OutResult) override;

		bool EmitFromFile(
			std::string_view ScriptPath,
			const FCodegenOptions& Options,
			FCodegenResult& OutResult) override;
		//~ End ICodegenBackend Interface
	};
}
