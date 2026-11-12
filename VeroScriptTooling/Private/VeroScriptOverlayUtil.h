// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptCompile.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptToolingTypes.h"
#include "VeroScriptPath.h"

#include <unordered_set>
#include <type_traits>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace VeroScript::Tooling
{
	// The loader uses the last overlay for a path. Apply the same precedence
	// before enumerating sources, so stale environment text cannot add references
	// or make a cross-file rename omit the declaration in the current buffer.
	template <class TOverlay>
	std::vector<const TOverlay*> EffectiveSourceOverlayViews(const std::vector<TOverlay>& Overlays)
	{
		std::vector<const TOverlay*> Result;
		Result.reserve(Overlays.size());
		std::unordered_set<std::string> Seen;
		for (auto I = Overlays.rbegin(); I != Overlays.rend(); ++I)
		{
			if (Seen.insert(PathUtil::IdentityKey(I->Path)).second)
			{
				Result.push_back(&*I);
			}
		}

		std::reverse(Result.begin(), Result.end());
		return Result;
	}
	// Keep the public query and debug options aligned with the core's immutable
	// environment. Apply is transactional and retains callback/resource owners.
	template <class TOptions>
	bool ResolveToolingModuleEnvironment(TOptions& Options, std::string& Error)
	{
		if (!Options.ModuleEnvironment ||
			Options.CompileProcessing.ModuleEnvironment == Options.ModuleEnvironment)
		{
			return true;
		}

		FCompileOptions Compile;
		for (const auto& P : Options.ImportPaths)
		{
			Compile.ImportPaths.emplace_back(P);
		}

		for (const auto& P : Options.StdlibSearchPaths)
		{
			Compile.StdlibSearchPaths.emplace_back(P);
		}

		for (const auto& P : Options.ImplicitImportPaths)
		{
			Compile.ImplicitImportPaths.emplace_back(P);
		}

		Compile.Bindings = Options.Bindings;
		Compile.CompileProcessing = Options.CompileProcessing;
		Compile.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		if constexpr (!std::is_same_v<TOptions, FWorkspaceOptions>)
		{
			for (const auto& S : Options.SourceOverlays)
			{
				Compile.SourceOverlays.push_back({S.Path, S.Source});
			}
		}

		if (!Options.ModuleEnvironment->Apply(Compile, Error)) return false;
		auto Effective = Options;
		Effective.ImportPaths.assign(Compile.ImportPaths.begin(), Compile.ImportPaths.end());
		Effective.StdlibSearchPaths.assign(Compile.StdlibSearchPaths.begin(), Compile.StdlibSearchPaths.end());
		Effective.ImplicitImportPaths.assign(Compile.ImplicitImportPaths.begin(), Compile.ImplicitImportPaths.end());
		if constexpr (!std::is_same_v<TOptions, FWorkspaceOptions>)
		{
			Effective.SourceOverlays.clear();
			for (const auto* S : EffectiveSourceOverlayViews(Compile.SourceOverlays))
			{
				Effective.SourceOverlays.push_back({S->Path, S->Source});
			}
		}
		Effective.Bindings = Compile.Bindings;
		Effective.CompileProcessing = std::move(Compile.CompileProcessing);
		Effective.ScriptCompileExtensions = std::move(Compile.ScriptCompileExtensions);
		Options = std::move(Effective);
		return true;
	}
	template <class TResult>
	TResult ToolingModuleEnvironmentFailure(const std::string& Message)
	{
		TResult Result;
		FScriptError Error;
		Error.Message = Message;
		using TDiagnostic = typename decltype(Result.Diagnostics)::value_type;
		if constexpr (std::is_same_v<TDiagnostic, FDiagnostic>)
		{
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<module-environment>"));
		}

		else
		{
			Result.Error = Error;
			Result.Diagnostics.push_back(std::move(Error));
		}
		return Result;
	}
	inline bool IsDefaultCompileProcessing(const FCompileProcessingOptions& Options)
	{
		const FCompileProcessingOptions Default;
		return !Options.ModuleEnvironment && !Options.Registry && !Options.CheckCancelled && !Options.ReadDependency &&
			Options.ImplementationFingerprint.empty() && Options.InitialDependencies.empty() && Options.bCacheable && !Options.PrepareSource &&
			Options.WritableModules.empty() && Options.AnnotationDefinitions.empty() &&
			Options.CommentRetention == Default.CommentRetention && Options.MaxOperations == Default.MaxOperations &&
			Options.MaxSourceBytes == Default.MaxSourceBytes && Options.MaxInputBytes == Default.MaxInputBytes &&
			Options.MaxEvents == Default.MaxEvents && Options.MaxDependencies == Default.MaxDependencies;
	}
	// A VM selection alone does not run processors or alter ordinary-source
	// analysis. Cache eligibility also checks for actual extension roots/imports.
	inline bool IsDefaultScriptCompileExtensionProcessing(const FScriptCompileExtensionsOptions& O)
	{
		const FScriptCompileExtensionsOptions D;
		return O.Roots.empty() && O.AllowedRoots.empty() && O.Configuration.empty() && !O.Bindings && !O.bBindingsDeterministic &&
			O.bAllowSourceImports && !O.ExecutionControl && O.BindingConfiguration.empty() &&
			O.MaxInstructions==D.MaxInstructions && O.TimeoutMilliseconds==D.TimeoutMilliseconds &&
			O.MaxRoots==D.MaxRoots && O.MaxDependencyDepth==D.MaxDependencyDepth && O.MaxRegistrations==D.MaxRegistrations &&
			O.MaxBridgeBytes==D.MaxBridgeBytes && O.MaxBridgeValues==D.MaxBridgeValues;
	}
	// Merging the complete options object must retain an explicit backend too.
	inline bool IsDefaultScriptCompileExtensions(const FScriptCompileExtensionsOptions& O)
	{
		return IsDefaultScriptCompileExtensionProcessing(O) && !O.VmBackendFactory && O.VmBackendPath.empty();
	}
	inline std::vector<VeroScript::FSourceOverlay> ToEngineOverlays(
		const std::vector<FDocumentOverlay>& Overlays)
	{
		std::vector<VeroScript::FSourceOverlay> EngineOverlays;
		EngineOverlays.reserve(Overlays.size());
		for (const FDocumentOverlay* Overlay : EffectiveSourceOverlayViews(Overlays))
		{
			VeroScript::FSourceOverlay EngineOverlay;
			EngineOverlay.Path = Overlay->Path;
			EngineOverlay.Source = Overlay->Source;
			EngineOverlays.push_back(std::move(EngineOverlay));
		}
		return EngineOverlays;
	}

	inline void ApplyDocumentOverlays(
		VeroScript::VeroScriptModuleLoader& Loader,
		const std::vector<FDocumentOverlay>& Overlays)
	{
		for (const FDocumentOverlay& Overlay : Overlays)
		{
			Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
		}
	}

	inline void ApplyEngineOverlays(
		VeroScript::VeroScriptModuleLoader& Loader,
		const std::vector<VeroScript::FSourceOverlay>& Overlays)
	{
		for (const VeroScript::FSourceOverlay& Overlay : Overlays)
		{
			Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
		}
	}

	// CLI/embed `-D` macros (`NAME` or `NAME=value`) for ModuleLoader preprocess.
	inline void ApplyDefines(
		VeroScript::VeroScriptModuleLoader& Loader,
		const std::vector<std::string_view>& Defines)
	{
		if (Defines.empty())
		{
			return;
		}
		std::vector<std::string> Specs;
		Specs.reserve(Defines.size());
		for (const std::string_view Define : Defines)
		{
			Specs.emplace_back(Define);
		}
		Loader.SetDefines(std::move(Specs));
	}

	inline void ApplyPredefinedMacros(
		VeroScript::VeroScriptModuleLoader& Loader,
		const std::vector<VeroScript::FPredefinedMacro>& Macros)
	{
		Loader.SetPredefinedMacros(Macros);
	}

	/**
	 * Keep every tooling parser aligned with the compiler's host type surface.
	 * Generic host aliases (for example TEnumAsByte<T>) are valid type names too;
	 * omitting them makes completion/hover/semantic-token loaders reject otherwise
	 * valid source even though ordinary compilation succeeds.
	 */
	inline void ApplyHostTypeNames(
		VeroScript::VeroScriptModuleLoader& Loader,
		const VeroScript::IHostBindingCatalog* const Bindings)
	{
		if (!Bindings)
		{
			return;
		}
		std::unordered_set<std::string> TypeNames;
		for (const VeroScript::FHostTypeDesc& Type : Bindings->GetTypes())
		{
			if (!Type.Name.empty())
			{
				TypeNames.insert(Type.Name);
			}
		}
		for (const VeroScript::FHostTypeAliasDesc& Alias : Bindings->GetTypeAliases())
		{
			if (!Alias.Name.empty())
			{
				TypeNames.insert(Alias.Name);
			}
		}
		for (const auto& Template : Bindings->GetGenericTypeTemplates())
		{
			if (!Template.Name.empty())
			{
				TypeNames.insert(Template.Name);
			}
		}

		if (!TypeNames.empty())
		{
			Loader.SetHostTypeNames(std::move(TypeNames));
		}
	}

	template <typename TOptions>
	inline void ApplyModuleLoadingOptions(
		VeroScript::VeroScriptModuleLoader& Loader,
		const TOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Error;
			if (!ResolveToolingModuleEnvironment(Effective, Error))
			{
				throw std::invalid_argument(Error);
			}

			ApplyModuleLoadingOptions(Loader, Effective);
			return;
		}
		Loader.SetBuildPreprocessLineMaps(true);
		ApplyHostTypeNames(Loader, Options.Bindings);
		auto Processing = Options.CompileProcessing;
		if (Options.Bindings)
		{
			for (auto Definition : VeroScript::GetAnnotationDefinitions(*Options.Bindings))
			{
				Processing.AnnotationDefinitions.push_back(std::move(Definition));
			}
		}
		Loader.SetCompileProcessing(std::move(Processing));
		Loader.SetScriptCompileExtensions(Options.ScriptCompileExtensions);
		for (const std::string_view StdlibPath : Options.StdlibSearchPaths)
		{
			Loader.AddStdlibSearchRoot(std::string(StdlibPath));
		}
		for (const std::string_view ImportPath : Options.ImportPaths)
		{
			Loader.AddImportPath(std::string(ImportPath));
		}
		for (const std::string_view ImplicitImportPath : Options.ImplicitImportPaths)
		{
			Loader.AddImplicitImportPath(std::string(ImplicitImportPath));
		}
		Loader.SetAllowCircularImports(Options.bAllowCircularImports);
		Loader.SetAllowProjectWideAccess(Options.bAllowProjectWideAccess);
	}
}
