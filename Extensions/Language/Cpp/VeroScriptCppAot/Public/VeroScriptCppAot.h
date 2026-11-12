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

	struct FCppAotSourceFile
	{
		std::string RelativePath;
		std::string Contents;
	};

	/**
	 * A deterministic multi-file source bundle. The entry includes separate runtime
	 * support and program fragments, reconstructing the same translation unit as
	 * EmitCppFromProgram. Keeping one TU preserves templates, initialization order,
	 * and internal-linkage semantics while allowing build systems to cache and
	 * distribute the generated payload independently. ManifestJson declares the
	 * minimum C++ standard and whether the platform thread library is required.
	 */
	struct FCppAotSourceBundle
	{
		std::vector<FCppAotSourceFile> Files;
		std::string ManifestJson;
	};

	/**
	 * CppAot (VeroScriptCppAot): emit a standalone C++ TU from a (possibly multi-module) program.
	 * VeroScript 0.1.1 language surface: free-function overloads, open generics as C++ templates
	 * (functions, user class/struct, and generic interfaces; `where T : I` →
	 * `std::enable_if_t` / `std::is_convertible_v` / `<type_traits>`, C++17), class RC (shared_ptr)
	 * / struct by value, inheritance / interface / virtual, shared / handle / pair / delegate glue, fn/lambda,
	 * ref / default / variadic, scalars / string / wstring / array / map / deque / set / queue / stack,
	 * control flow, using, enum / switch / goto, print / printf / format / formatf / len,
	 * stdlib __* (math/string/io/path),
	 * concurrent workers as OS threads with thread-local script globals/statics;
	 * channels remain the explicit synchronized cross-worker capability. Host symbols
	 * reachable from workers may execute concurrently and must be thread-safe. Runs
	 * compiler semantics before emit.
	 * Runtime std::memory operations and region types are rejected explicitly;
	 * an unused memory.vs/core.vs import remains supported.
	 * Scalar numeric ++/-- evaluate the target once and preserve checked
	 * narrowing and overflow semantics. Overloaded user-type ++/-- are not
	 * supported; use an explicit update method for those types.
	 *
	 * Still not: raw script pointers, managed lambda reference-capture cells, LLVM, GUI.
	 * Failed class construction with an escaped receiver requires VM/Core AOT for managed cleanup.
	 * Standalone output reports a fatal error in that case; successful construction-time retention is supported.
	 *
	 * Prefer FCppCodegenBackend when plugging into ICodegenBackend; free functions remain for CLI.
	 * Source processors use FCodegenOptions::CompileProcessing on that backend.
	 * ScriptCompileExtensions, SourceOverlays and StdlibSearchPaths use the core
	 * loader. EmitFromFile resolves entry overlays; EmitFromSource uses its explicit
	 * source argument for the entry. Compiler scripts run only while generating;
	 * their VM and compiler-only bindings are not retained in the business output.
	 * EmitCppFromProgram can reuse a prepared program returned by VeroScriptCompile;
	 * an unprepared program retaining source processor state requires owned preparation first.
	 */
	bool EmitCppFromProgram(
		const FProgram& Program,
		std::string& OutCpp,
		std::string& OutError);

	bool EmitCppSourceBundleFromProgram(
		const FProgram& Program,
		std::string_view BundleName,
		FCppAotSourceBundle& OutBundle,
		std::string& OutError);

	// Bundle names use ASCII letters, digits, '_' or '-'. Both bundle APIs reject
	// Windows device stems (CON/PRN/AUX/NUL, COM1..COM9, LPT1..LPT9) on every host.
	// Package already-emitted C++ (including optional host stubs) without changing it.
	bool BuildCppSourceBundle(
		std::string_view GeneratedCpp,
		std::string_view BundleName,
		FCppAotSourceBundle& OutBundle,
		std::string& OutError);

	/**
	 * Load via ModuleLoader (resolves `import`), compile-check, then emit.
	 */
	bool EmitCppFromSource(
		std::string_view Source,
		std::string_view SourceLabel,
		std::string& OutCpp,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& HostTypeNames = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	// Read script from disk and transactionally replace OutputPath with emitted C++.
	// ScriptPath and OutputPath must identify different files.
	bool EmitCppFromFile(
		std::string_view ScriptPath,
		std::string_view OutputPath,
		std::string& OutError,
		const std::vector<std::string>& ImportPaths = {},
		const std::vector<std::string>& Defines = {},
		const std::vector<std::string>& ImplicitImportPaths = {},
		bool bAllowCircularImports = true,
		bool bAllowProjectWideAccess = false);

	/**
	 * ICodegenBackend adapter around EmitCppFromSource / EmitCppFromFile.
	 */
	class FCppCodegenBackend final : public ICodegenBackend
	{
	public:
		//~ Begin ICodegenBackend Interface
		std::string_view GetName() const override
		{
			return "cpp";
		}
		std::string_view GetFileExtension() const override
		{
			return ".cpp";
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
