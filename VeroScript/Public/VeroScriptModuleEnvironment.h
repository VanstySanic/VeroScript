// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptArtifact.h"
#include "VeroScriptExternalDependency.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptI18n.h"
#include "VeroScriptResources.h"
#include "VeroScriptScriptCompileExtensions.h"
#include "VeroScriptBackendSelection.h"

#include <map>
#include <optional>

namespace VeroScript
{
	struct FCompileOptions;
	struct FModuleScope
	{
		std::string Id;
		std::vector<std::string> DirectDependencies;
		// Explicitly authorized host/standard-library scope, accessible to all.
		bool bHostProvided = false;
	};
	struct FModuleSourceRoot
	{
		std::string Path, OwnerId;
	};
	struct FResolvedModule
	{
		std::string Name, Id, Path, OwnerId, ContentIdentity;
		std::optional<std::string> Source;
		std::vector<FExternalDependency> RequiredDependencies;
		std::shared_ptr<const std::vector<std::uint8_t>> Binary;
	};
	struct FModuleEnvironmentDesc
	{
		// Owners are destroyed after every callback/catalog/provider they protect.
		std::vector<std::shared_ptr<const void>> Owners;
		std::vector<FModuleScope> Scopes;
		std::string EntryOwnerId;
		std::vector<FModuleSourceRoot> SourceRoots;
		std::vector<FResolvedModule> Modules;
		std::vector<std::string> ImportPaths, StdlibSearchPaths, ImplicitImports;
		std::shared_ptr<const IHostBindingCatalog> Bindings;
		FCompileProcessingOptions CompileProcessing;
		FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::vector<FResourceProviderBinding> Resources;
		std::vector<FExternalDependency> AvailableDependencies, RequiredDependencies;
		std::shared_ptr<I18n::FI18nServices> I18nServices;
		I18n::FI18nLimits I18nLimits;
		std::string CompileConfiguration, ExecutionConfiguration;
		// Omitted services use automatic selection; a present empty value disables.
		std::map<std::string, std::string> SelectedBackends;
		std::vector<FNativeServiceProviderDesc> ServiceProviders;
		// Unavailable automatic provider modules remain observable without
		// preventing independent services from being used.
		std::map<std::string, std::vector<FBackendAttempt>> BackendDiagnostics;
		FExecutionBackendResolvers ExecutionBackends;
		bool bStrictSources = true;
		bool bAllowAmbientSearch = false;
		std::uint64_t MaxResourceBytes = 64 * 1024 * 1024;
		std::shared_ptr<const IArtifactReader> ArtifactReader;
		std::shared_ptr<const IArtifactDecoder> ArtifactDecoder;
	};
	class FModuleEnvironment final : public std::enable_shared_from_this<FModuleEnvironment>
	{
	public:
		static std::shared_ptr<const FModuleEnvironment> Create(FModuleEnvironmentDesc, std::string& Error);
		const FModuleEnvironmentDesc& GetDescription() const noexcept
		{
			return Description;
		}
		const std::string& GetCompileFingerprint() const noexcept
		{
			return CompileFingerprint;
		}
		const std::string& GetExecutionFingerprint() const noexcept
		{
			return ExecutionFingerprint;
		}
		const FResolvedModule* FindModule(const std::string& Name) const;
		const FResolvedModule* FindSource(const std::string& Path) const;
		std::string GetSourceOwner(const std::string& Path, bool bDirectory = false) const;
		bool CanAccessOwner(const std::string& FromOwner, const std::string& TargetOwner) const;
		bool AuthorizeSource(const std::string& Path, const std::string& Importer, std::string& Error,
		    bool bImporterIsDirectory = false) const;
		bool ReadResource(const FResourceRequest&, const std::string& RequestingModule,
		    const FResourceWork&, FResourceData&, std::string& Error) const;
		bool ReadResourceDependency(const std::string& Id, const FResourceWork&, std::string& Content) const;
		// Idempotent normalization of the common compile input. Explicit conflicting
		// live catalogs are rejected; prepare a combined catalog before publication.
		bool Apply(FCompileOptions&, std::string& Error) const;

	private:
		explicit FModuleEnvironment(FModuleEnvironmentDesc Desc) : Description(std::move(Desc))
		{
		}
		FModuleEnvironmentDesc Description;
		std::map<std::string, std::size_t> Names, Sources, Scopes, Providers;
		std::vector<std::pair<std::string, std::string>> CanonicalRoots;
		std::string CompileFingerprint, ExecutionFingerprint;
	};
}
