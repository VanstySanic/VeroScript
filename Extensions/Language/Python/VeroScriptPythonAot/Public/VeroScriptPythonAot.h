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
	// Source/path/name views may borrow strings in a result being replaced.
	// Separate output parameters must refer to distinct objects.

	struct FPythonAotSourceFile
	{
		std::string RelativePath;
		std::string Contents;
	};

	/**
	 * Standalone Python package bundle plus its deterministic manifest.
	 */
	struct FPythonAotSourceBundle
	{
		std::vector<FPythonAotSourceFile> Files;
		std::string ManifestJson;
	};

	// Emit a standalone Python 3.11-3.15 module from a compiled VeroScript AST.
	bool EmitPythonFromProgram(
		const FProgram& Program,
		std::string& OutPython,
		std::string& OutError);

	// Split generated Python into executable package files and runtime support.
	bool EmitPythonSourceBundleFromProgram(
		const FProgram& Program,
		std::string_view BundleName,
		FPythonAotSourceBundle& OutBundle,
		std::string& OutError);

	// Package already-emitted standalone Python without changing its semantics.
	bool BuildPythonSourceBundle(
		std::string_view GeneratedPython,
		std::string_view BundleName,
		FPythonAotSourceBundle& OutBundle,
		std::string& OutError);

	// Load imports, compile-check the complete source graph, then emit Python.
	bool EmitPythonFromSource(
		std::string_view Source,
		std::string_view SourceLabel,
		std::string& OutPython,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	bool EmitPythonSourceBundleFromSource(
		std::string_view Source,
		std::string_view SourceLabel,
		std::string_view BundleName,
		FPythonAotSourceBundle& OutBundle,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);
	bool EmitPythonSourceBundleFromSource(std::string_view Source,std::string_view SourceLabel,
		std::string_view BundleName,const FCodegenOptions&,FPythonAotSourceBundle&,std::string& Error);
	bool EmitPythonSourceBundleFromFile(std::string_view ScriptPath,std::string_view BundleName,
		const FCodegenOptions&,FPythonAotSourceBundle&,std::string& Error);

	// Read a VeroScript file and write a standalone .py file.
	bool EmitPythonFromFile(
		std::string_view ScriptPath,
		std::string_view OutputPath,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	bool EmitPythonSourceBundleFromFile(
		std::string_view ScriptPath,
		std::string_view BundleName,
		FPythonAotSourceBundle& OutBundle,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	// Stage a complete package, then replace an owned regular directory.
	// Commit moves the old directory aside; failure attempts to restore it.
	bool WritePythonSourceBundle(
		const FPythonAotSourceBundle& Bundle,
		std::string_view OutputDirectory,
		std::string& OutError);

	/**
	 * ICodegenBackend adapter for the standalone Python emitter.
	 */
	class FPythonCodegenBackend final : public ICodegenBackend
	{
	public:
		//~ Begin ICodegenBackend Interface
		std::string_view GetName() const override
		{
			return "python";
		}
		std::string_view GetFileExtension() const override
		{
			return ".py";
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
