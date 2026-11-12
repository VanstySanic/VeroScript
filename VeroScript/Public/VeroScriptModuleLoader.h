// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptPath.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VeroScript
{
	struct FImportDecl;
	struct FProgram;
	struct FPackageData;
	class FModuleEnvironment;

	/**
	 * Resolves import paths, loads modules, and merges type declarations.
	 */
	class VeroScriptModuleLoader
	{
	public:
		struct FLoadedFileStamp
		{
			std::string Path;
			std::filesystem::file_time_type Modified{};
			std::uintmax_t Size = 0;
			bool bValid = false;
		};

		explicit VeroScriptModuleLoader(std::string_view MainFilePath,
			std::shared_ptr<const FModuleEnvironment> Environment = {});
		~VeroScriptModuleLoader();

		void AddImportPath(std::string Path);
		// Add an ordinary implicit import. Its completed public macro interface is
		// seeded into consumers; implicit providers establish their own interfaces
		// through explicit imports before consumer preprocessing begins.
		void AddImplicitImportPath(std::string Path);
		void SetAllowCircularImports(bool bAllow)
		{
			bAllowCircularImports = bAllow;
		}
		void SetAllowProjectWideAccess(bool bAllow)
		{
			bAllowProjectWideAccess = bAllow;
		}
		// Prepend a stdlib search root before built-in executable/cwd/source paths.
		void AddStdlibSearchRoot(std::string Path);
		// CLI/embed `-D` macros (`NAME` or `NAME=value`) for preprocess.
		void SetDefines(std::vector<std::string> Defines);
		void SetPredefinedMacros(std::vector<FPredefinedMacro> Macros);
		// Overlay path contents so LoadModule uses memory instead of disk.
		void SetSourceOverlay(std::string Path, std::string Source);
		void ClearSourceOverlays();
		void SetBuildPreprocessLineMaps(bool bEnable)
		{
			bBuildPreprocessLineMaps = bEnable;
		}
		const FPreprocessLineMap *GetPreprocessLineMap(std::string_view Path) const;
		/**
		 * Host binding type names treated as user types during parse
		 * (`Counter c = Counter(1);`).
		 */
		void SetHostTypeNames(std::unordered_set<std::string> TypeNames);
		void SetCompileProcessing(FCompileProcessingOptions Options)
		{
			ConfiguredProcessingOptions = Options;
			ProcessingOptions = std::move(Options);
		}
		void SetScriptCompileExtensions(FScriptCompileExtensionsOptions Options)
		{
			ScriptExtensionOptions = std::move(Options);
		}
		void ClearHostTypeNames();
		// Resolve without parsing, using the same overlays and search order as imports.
		bool ResolveImportPath(
			const std::string& ImportPath,
			const std::string& FromDirectory,
			std::string& OutResolvedPath,
			std::string_view ImporterModulePath = {}) const;
		// Each load starts fresh with the current configuration. Previously returned
		// programs retain their source/path storage and remain valid across loads.
		std::unique_ptr<FProgram> LoadMainModule();
		std::unique_ptr<FProgram> LoadSourceModule(
			std::string_view Source,
			std::string_view FileLabel);
		static std::vector<std::string> ResolveLoadedModulePaths(
			std::string_view MainFilePath,
			const std::vector<std::string_view>& ImportPaths = {},
			const std::vector<std::string_view>& Defines = {},
			const std::vector<FPredefinedMacro>& PredefinedMacros = {},
			const std::vector<std::string_view>& ImplicitImportPaths = {});
		std::uint64_t GetSourceFingerprint() const
		{
			return SourceFingerprint;
		}
		std::uint64_t GetModuleResolutionFingerprint() const;
		const std::unordered_set<std::string>& GetLoadedModulePaths() const
		{
			return LoadedPaths;
		}
		/**
		 * Metadata captured before physical source/package reads; overlays are absent.
		 * Borrowed until the next load or loader destruction. Consumers must reject
		 * invalid stamps and recheck metadata before reuse; this is not a byte hash.
		 */
		const std::vector<FLoadedFileStamp>& GetLoadedFileStamps() const
		{
			return LoadedFileStamps;
		}
		bool HasError() const
		{
			return bHasError;
		}
		const FScriptError& GetError() const
		{
			return Error;
		}
		const std::vector<FScriptError>& GetDiagnostics() const
		{
			return Diagnostics;
		}

	private:
		struct FPreparedModule;

		void ResetLoadState();
		bool PrepareScriptExtensions(std::string_view Source, std::string_view Label);

		void RecordLoadedFileStamp(const std::string& Path);

		void InitSearchPaths();

		// Merge a `localization/` catalog beside a stdlib root or module file.
		void MergeSiblingLocalization(const std::string& DirectoryOrFile);

		// Fold normalized `-D` list into SourceFingerprint once per load (not per module).
		void FoldPreprocessorDefinesIntoFingerprint();

		FPreparedModule* PrepareModuleSource(
			const std::string& ResolvedPath,
			std::string Source);

		void CollectImplicitForwardTypes(const FPreparedModule& Module);
		std::shared_ptr<FPackageData> PreparePackageInterface(
			const std::string &Path, const FSourceLocation *Location = nullptr);
		bool ProveEmptyMacroClosure(const std::string &Path);
		bool ApplyImplicitMacros(FPreparedModule& Module, FPreprocessorSession& Preprocessor);

		bool PrepareImplicitModules(
			const std::string* EntrySource = nullptr,
			const std::string* EntryPath = nullptr);

		bool PrepareReachableModuleGraph(
			const std::string& ResolvedPath,
			const std::string& RequestedPath,
			bool bIsMainModule,
			const std::string* SourceOverride = nullptr,
			const FSourceLocation* ImportLocation = nullptr);

		bool IsImplicitModule(const std::string& ResolvedPath) const;

		bool IsStdlibModulePath(const std::string& ResolvedPath) const;

		void AppendImplicitImports(
			const std::string& ImporterPath,
			std::vector<FImportDecl>& Imports) const;

		// Intern a path so token/AST FileLabel views remain valid for this loader's lifetime.
		const std::string& InternPath(std::string Path);

		bool LoadModule(
			const std::string& ResolvedPath,
			const std::string& RequestedPath,
			bool bIsMainModule,
			const FSourceLocation* ImportLocation = nullptr);

		bool LoadPackageModule(
			const std::string& ResolvedPath,
			const std::string& RequestedPath,
			const FSourceLocation* ImportLocation);

		bool LoadModuleFromSource(
			const std::string& ResolvedPath,
			const std::string& RequestedPath,
			std::string_view Source,
			const FSourceLocation* ImportLocation = nullptr);

		// Exact or path-suffix match against overlay keys. Returns overlay map key if found.
		const std::string* FindOverlayKey(const std::string& ResolvedPath) const;

		const std::string* FindOverlaySource(const std::string& ResolvedPath) const;

		void MergeProgram(FProgram&& ImportedProgram);

		static std::string GetDirectory(std::string_view FilePath);

		std::string MainFilePath;
		std::shared_ptr<const FModuleEnvironment> ModuleEnvironment;
		FCompileProcessingOptions ProcessingOptions;
		FCompileProcessingOptions ConfiguredProcessingOptions;
		FScriptCompileExtensionsOptions ScriptExtensionOptions;
		std::shared_ptr<FCompileProcessingSession> ProcessingSession;
		std::string MainDirectory;
		std::vector<std::string> CliImportPaths;
		std::vector<std::string> ImplicitImportPaths;
		std::vector<std::string> ResolvedImplicitModulePaths;
		std::vector<std::string> EnvImportPaths;
		std::vector<std::string> StdlibSearchRoots;
		std::vector<std::string> PreprocessorDefines;
		std::vector<FPredefinedMacro> PredefinedMacros;
		std::shared_ptr<FPreprocessBudget> PreprocessBudget;
		std::unordered_set<std::string> MergedLocalizationDirectories;
		std::shared_ptr<std::unordered_set<std::string>> InternedPaths;
		std::unordered_set<std::string> LoadedPaths;
		std::vector<FLoadedFileStamp> LoadedFileStamps;
		std::unordered_set<std::string> LoadedIdentities;
		std::unordered_set<std::string> LoadingIdentities;
		std::unordered_map<std::string, std::string> IdentityToDisplayPath;

		// Physical package path identity -> portable facade used by embedded generic
		// template import graphs.
		std::unordered_map<std::string, std::string> LoadedPackageFacades;

		// Portable template-module path -> canonical content fingerprint. Nested
		// and diamond package graphs may surface the same module more than once.
		std::unordered_map<std::string, std::uint64_t>
			LoadedPackageTemplateFingerprints;
		std::unordered_map<std::string, std::string> SourceOverlays;
		std::unordered_map<std::string, std::shared_ptr<FPreparedModule>> PreparedModules;
		std::unordered_map<std::string, std::shared_ptr<FPackageData>> PreparedPackages;
		std::unordered_set<std::string> ProvenEmptyMacroModules;
		bool bBuildPreprocessLineMaps = false;
		std::unordered_set<std::string> ImplicitModuleIdentities;
		std::unordered_set<std::string> PreparingGraphIdentities;
		std::unordered_set<std::string> PreparedGraphIdentities;
		std::unordered_set<std::string> ImplicitForwardTypeNames;
		std::unordered_set<std::string> ImplicitForwardInterfaceNames;
		std::unordered_set<std::string> HostTypeNames;
		std::unique_ptr<FProgram> MergedProgram;
		std::uint64_t SourceFingerprint = 14695981039346656037ull;
		bool bDefinesFoldedIntoFingerprint = false;
		bool bModuleGraphPrepared = false;
		bool bImplicitMacroInterfacesReady = false;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		bool bHasError = false;
		FScriptError Error;
		std::vector<FScriptError> Diagnostics;

	};
}
