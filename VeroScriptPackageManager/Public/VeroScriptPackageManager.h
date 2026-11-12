// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptNumerics.h"
#include "VeroScriptTemplate.h"
#include "VeroScriptVmBackend.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Packages
{
	class IHttpTransport;
	class IInstallationProcessor;
	class FInstalledEnvironment;
	struct FClientOptions;

	inline constexpr const char* FormatVersion = "0.1.1";
	enum class EEnvironmentScope
	{
		Project,
		Runtime,
		Sdk
	};

	enum class EOperation
	{
		Install,
		Uninstall,
		Enable,
		Disable,
		Verify,
		List,
		Show,
		Search,
		Pack,
		Publish,
		Deploy,
		Clean
	};

	struct FDiagnostic
	{
		std::string Code, Stage, Message, Package, Source, Field;
		std::vector<std::string> DependencyChain;
		std::string DetailsJson;
	};
	struct FLimits
	{
		std::uint64_t MaxArchiveBytes = 1ull << 30, MaxFileBytes = 1ull << 30;
		std::uint64_t MaxExpandedBytes = 4ull << 30, MaxManifestBytes = 4ull << 20;
		std::uint64_t MaxResponseBytes = 16ull << 20, MaxFiles = 100000, MaxExpansionRatio = 200;
		std::uint64_t MaxStateBytes = 256ull << 20, MaxJournalBytes = 1ull << 30;
		std::uint64_t MaxMetadataBytes = 128ull << 20;
		std::size_t MaxJsonDepth = 64, MaxPackages = 4096, MaxEdges = 32768;
		std::size_t MaxCandidates = 65536, MaxSolverSteps = 100000, MaxDependencyDepth = 128;
		std::uint64_t TimeoutMilliseconds = 120000, HookInstructions = 1000000,
					  HookTimeoutMilliseconds = 2000;
		std::uint64_t MaxDisplayBytes = 65536;
		std::uint64_t HookResourceBytes = 16ull << 20, HookBufferBytes = 64ull << 20;
		FNumericLimits HookNumericLimits;
		// Per hook compilation, including imported source/open library templates.
		// Independent of runtime instruction/numeric limits; zero cache is allowed.
		FConstantEvaluationLimits HookConstantEvaluationLimits;
	};
	struct FTargetContext
	{
		std::string Core, CoreBuild, Os, Architecture, Environment;
		std::map<std::string, std::string> Abis, MinimumVersions;
		std::vector<std::string> Capabilities;
	};
	FTargetContext HostTargetContext();
	// Stable before the surrounding ZIP is built. Use this origin when compiling
	// a .vslib against native code shipped in the same distribution. Invalid
	// canonical names/digests throw std::invalid_argument.
	std::string NativeProviderId(const std::string& Package, const std::string& Module,
								 const std::string& BinarySha256);
	struct FEnvironmentInput
	{
		EEnvironmentScope Scope = EEnvironmentScope::Project;
		// Absolute inputs need no WorkingDirectory. Relative inputs require this
		// explicit absolute base; the library never reads or changes process cwd.
		std::string Root, WorkingDirectory, PackagesDirectory, CacheDirectory, Lockfile;
		FTargetContext Target, Host;
	};
	struct FEnvironmentLayout
	{
		EEnvironmentScope Scope = EEnvironmentScope::Project;
		std::string Root, Management, Requirements, Lockfile, State, Packages, Cache, TargetKey;
		FTargetContext Target, Host;
	};
	bool ResolveEnvironment(const FEnvironmentInput&, FEnvironmentLayout&, FDiagnostic&);
	std::string DefaultCacheRoot();
	// Cooperating readers and publishers retain this same-user, path-bound lease.
	std::shared_ptr<const void> AcquireDeploymentDirectoryLease(const std::string& AbsoluteRoot,
		bool Exclusive, FDiagnostic&, const FClientOptions&);
	// Returns false for a corrupt/mismatched descriptor. No descriptor is a
	// successful empty selection; no component role is inferred from a bin name.
	bool DiscoverComponent(const std::string& Executable, FEnvironmentInput&, bool& Found, FDiagnostic&);
	// Validate a component root using its root/bin deployment descriptors.
	bool DiscoverComponentRoot(const std::string& AbsoluteRoot, FEnvironmentInput&, bool& Found,
							   FDiagnostic&);
	bool DiscoverProject(const std::string& EntryFile, const std::string& Boundary, std::string& Root,
						 FDiagnostic&);

	struct FHttpRequest
	{
		std::string Method = "GET", Url, Body, BodyFile, Proxy, CaFile;
		std::map<std::string, std::string> Headers;
		std::uint64_t MaxBytes = 16ull << 20, TimeoutMilliseconds = 30000;
		bool AllowHttp = false;
		std::function<void()> CheckCancelled;
		std::function<void(std::uint64_t, std::uint64_t)> Progress;
		std::function<void(const char*, std::size_t)> DataSink;
		// Revalidate resolver-supplied Authorization for each normalized request
		// URL, including redirects. Empty means no Authorization. This overrides
		// that header; the transport's origin/path restrictions still apply.
		// Custom transports must enforce this policy on every request leg.
		std::function<std::string(const std::string& Url)> Credential;
	};
	struct FHttpResponse
	{
		unsigned Status = 0;
		std::string Body;
		std::map<std::string, std::string> Headers;
	};
	struct FInstallProcessingContext
	{
		std::string Phase, ManifestJson, ConfigurationJson, GeneratedRoot;
		FTargetContext Host, Target;
		std::function<void()> CheckCancelled;
		I18n::FI18nSnapshot DisplaySnapshot;
		std::function<bool(const std::string& Group, const std::string& Path, FResourceData&, FDiagnostic&)>
			ReadResource;
		std::function<bool(const std::string& Name, I18n::FI18nArgument&, FDiagnostic&)> ReadInput;
	};

	struct FClientOptions
	{
		FLimits Limits;
		bool Offline = false, Locked = false, Upgrade = false, AllowPrerelease = false;
		bool AllowHooks = false, NonInteractive = true, SkipHooks = false, AllowHttp = false;
		bool Development = false;
		std::string Mirror, Proxy, CaFile, OfficialApi;
		// Values are JSON scalars, retaining integer widths. Secret values are
		// operation-only and never written to configuration, logs or receipts.
		std::map<std::string, std::string> Inputs, Secrets;
		std::map<std::string, std::shared_ptr<const IInstallationProcessor>> Features;
		std::vector<std::string> StdlibSearchPaths;
		std::shared_ptr<const IHttpTransport> Transport;
		// Optional native trust service. Called again for cached archives; never
		// serialized into locks or passed to installation scripts.
		std::shared_ptr<const IArtifactDecoder> ArtifactDecoder;
		I18n::FI18nConfiguration DisplayConfiguration;
		std::function<void()> CheckCancelled;
		std::function<void(const std::string& Stage, std::uint64_t Current, std::uint64_t Total)> Progress;
		std::function<void(const std::string&)> Message;
		std::function<bool(const std::string& Name, const std::string& DefinitionJson, std::string& ValueJson,
						   FDiagnostic&)>
			RequestInput;
		// Called only for an exact URL. The default HTTP implementation strips
		// authentication on redirects; callers scope credentials to host/path.
		std::function<std::string(const std::string& Url)> Credential;
		// Install hooks use a separate governed execution domain. Omission uses
		// the PackageManager product's static VM, when that component is built.
		std::shared_ptr<const IVmBackendFactory> VmBackendFactory;
		std::string VmBackendPath;
	};

	struct FInstalledPackage
	{
		std::string Name, Version, SourceId, ArtifactId, Sha256, Path, GeneratedPath, LoadersJson,
			ManifestJson, ConfigurationJson, TextDomain;
		std::vector<std::string> Dependencies, RuntimeDependencies, Usage;
		bool Enabled = true;
	};

	struct FCoreEnvironmentOptions
	{
		struct FStaticExtension
		{
			FNativeExtensionQuery Query = nullptr;
			std::shared_ptr<const void> Owner;
			std::string ContentIdentity;
		};
		std::string EntryRoot, EntryOwnerId = "project";
		std::vector<std::string> StdlibSearchPaths;
		// Empty value explicitly disables the configured selection for that service.
		std::map<std::string, std::string> SelectedBackends;
		const FHostBindingRegistry* BaseBindings = nullptr;
		const FCompileProcessorRegistry* BaseProcessors = nullptr;
		std::vector<FNativeExtensionServiceBinding> Services;
		std::map<std::string, FStaticExtension> StaticExtensions;
		// Instantiate native generic types or add host declarations to a private
		// catalog before the immutable environment is published.
		std::function<bool(FHostBindingRegistry&, std::string&)> PrepareBindings;
		std::shared_ptr<const IArtifactDecoder> ArtifactDecoder;
		// Trusted host policy for installed compile extensions. Roots are derived
		// from installed entries; Core intersects this policy with compile requests.
		FScriptCompileExtensionsOptions ScriptCompileExtensions;
		bool AnalysisOnly = false, RuntimeOnly = false;
		std::function<void()> CheckCancelled;
	};

	struct FRequest
	{
		EOperation Operation = EOperation::Install;
		FEnvironmentInput Environment;
		FClientOptions Options;
		std::vector<std::string> Arguments;
		std::string OutputPath;
		// Deploy only. Explicit roots must already be installed and enabled.
		std::vector<std::string> DeploymentUsage = {"runtime"};
		std::vector<std::string> DeploymentRoots;
		// Install only. Relative file paths use Environment.WorkingDirectory;
		// nested files and local distributions use their containing file's directory.
		std::vector<std::string> RequirementFiles;
	};

	struct FResult
	{
		bool Committed = false;
		std::uint64_t Generation = 0;
		std::string Json;
		std::shared_ptr<const FInstalledEnvironment> Environment;
		std::vector<FDiagnostic> Warnings;
	};

	struct FEnvironmentSelection
	{
		bool ComponentRoot = false;
		std::string Path;
	};

	struct FRunEnvironmentRequest
	{
		std::string StartupDirectory, Executable, EntryFile, WorkspaceBoundary;
		FEnvironmentSelection Selection;
		FClientOptions Options;
	};

	class IHttpTransport
	{
	public:
		virtual ~IHttpTransport() = default;
		virtual bool Send(const FHttpRequest&, FHttpResponse&, FDiagnostic&) const = 0;
		// Custom transports with ambient authentication are private by default.
		virtual bool AllowsPublicMetadataCache() const noexcept
		{
			return false;
		}
	};
	std::shared_ptr<const IHttpTransport> CreateHttpTransport();
	class IInstallationProcessor
	{
	public:
		virtual ~IInstallationProcessor() = default;
		virtual std::string GetVersion() const = 0;
		virtual std::string GetImplementationIdentity() const = 0;
		// Trusted client service: writes only inside the supplied staging root.
		// External system actions need their own explicit transaction contract.
		virtual bool Process(const FInstallProcessingContext&, FDiagnostic&) const = 0;
	};

	class FInstalledEnvironment final
	{
	public:
		struct FImpl;
		const FEnvironmentLayout& GetLayout() const;
		const std::vector<FInstalledPackage>& GetPackages() const;
		std::uint64_t GetGeneration() const;
		const std::string& GetFingerprint() const;
		std::string ToJson() const;
		std::string GetSelectedBackend(std::string_view Service) const;
		// Stable metadata-only order. Disabled packages never contribute candidates.
		std::vector<std::string> GetBackendCandidates(std::string_view Service) const;
		// Metadata-only query; never loads a native library.
		bool HasCommandLineModules() const;
		bool BuildCoreEnvironment(const FCoreEnvironmentOptions&, std::shared_ptr<const FModuleEnvironment>&,
								  FDiagnostic&,
								  std::vector<FNativeServiceProviderDesc>* Providers = nullptr) const;
		bool ResolveBackend(const std::string& Service, const std::string& Id, std::string& Path,
							FDiagnostic&) const;
		// "auto"/omitted selection may succeed without a factory; diagnostics
		// distinguish unavailable optional backends from an explicit disable.
		bool LoadJitBackend(const std::string& Id, std::shared_ptr<const IJitBackendFactory>&,
			FDiagnostic&, FBackendSelectionResult* Selection = nullptr) const;
		bool LoadVmBackend(const std::string& Id, std::shared_ptr<const IVmBackendFactory>&,
			FDiagnostic&, FBackendSelectionResult* Selection = nullptr) const;
		bool DisplaySnapshot(const I18n::FI18nConfiguration&, I18n::FI18nSnapshot&, FDiagnostic&) const;
		bool Deploy(const std::string& NewAbsoluteOutput, const std::vector<std::string>& Usage,
			const std::vector<std::string>& Roots, std::string& SnapshotJson, FDiagnostic&,
			const FClientOptions& = {}) const;
		// Export an installed closure for its exact target without loading native
		// code. The target must match this snapshot, including its Core and ABIs.
		bool DeployForTarget(const FTargetContext& Target, const std::string& NewAbsoluteOutput,
			const std::vector<std::string>& Usage, const std::vector<std::string>& Roots,
			std::string& SnapshotJson, FDiagnostic&, const FClientOptions& = {}) const;

	private:
		friend class FPackageManager;
		explicit FInstalledEnvironment(std::shared_ptr<FImpl>);
		std::shared_ptr<FImpl> Impl;
	};
	// Stateless operations. Environment/caches use OS locks; callbacks are
	// synchronous and borrowed for this call. Published snapshots own leases.
	class FPackageManager final
	{
	public:
		bool Execute(const FRequest&, FResult&, FDiagnostic&) const;
		bool Load(const FEnvironmentInput&, std::shared_ptr<const FInstalledEnvironment>&, FDiagnostic&,
				  const FClientOptions& = {}) const;
		bool LoadDeployment(const std::string& AbsoluteRoot, std::shared_ptr<const FInstalledEnvironment>&,
							FDiagnostic&, const FClientOptions& = {}) const;
		// Explicit host composition only; discovery never merges environments.
		bool Combine(std::shared_ptr<const FInstalledEnvironment> Base,
					 std::shared_ptr<const FInstalledEnvironment> Overlay,
					 std::shared_ptr<const FInstalledEnvironment>&, FDiagnostic&) const;

	private:
		friend class FInstalledEnvironment;
		bool ReadDeployment(const std::string& AbsoluteRoot, const FTargetContext& ExpectedTarget,
			std::shared_ptr<const FInstalledEnvironment>&, FDiagnostic&, const FClientOptions&) const;
	};
	bool ValidateDistribution(std::string_view Json, std::string& CanonicalJson, FDiagnostic&,
							  const FLimits& = {});
	bool VersionSatisfies(std::string_view Version, std::string_view Requirement, bool& Satisfies,
						  FDiagnostic&);
	bool IsPackageCommand(std::string_view);
	// Consumed is 0 for another option, 1 for a flag/inline value, 2 for a
	// following path value. Invalid or repeated selectors fail without mutation.
	bool ParseEnvironmentOption(std::string_view Option, std::string_view Next, FEnvironmentSelection&,
								unsigned& Consumed, FDiagnostic&);
	// Read-only discovery: explicit selection, entry/current project, then the
	// executing component. Never merges, downloads, initializes or runs hooks.
	bool ResolveRunEnvironment(const FRunEnvironmentRequest&, std::shared_ptr<const FInstalledEnvironment>&,
							   FDiagnostic&);
	// argv-free CLI adapter also used by the normal run command. StartupDirectory
	// and executable are supplied once by the CLI, never rediscovered by operations.
	int RunPackageCommand(const std::vector<std::string>& Arguments, const std::string& StartupDirectory,
						  const std::string& Executable, std::ostream& Output, std::ostream& Errors,
		const std::function<std::shared_ptr<const IArtifactDecoder>(const std::string&, FDiagnostic&)>& ProtectionFactory = {});
} // namespace VeroScript::Packages
