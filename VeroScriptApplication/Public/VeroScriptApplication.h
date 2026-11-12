// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptArchive.h"
#include "VeroScriptProtection.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPackage.h"
#include "VeroScriptCompile.h"

namespace VeroScript::Application
{
	struct FApplicationServices
	{
		std::shared_ptr<const FProtection> Protection;
		std::shared_ptr<const FModuleEnvironment> Environment;
		std::string CacheRoot;
		std::vector<std::string> StandardLibraryPaths;
		bool AnalysisOnly = false;
		std::function<bool(const std::string& TargetJson, std::string& Error)> ValidateTarget;
		std::function<bool(const std::string& RequirementsJson, const FModuleEnvironment*, std::string&)> ValidateRequirements;
		std::set<std::string> Capabilities;
		std::function<bool(const std::string& DeploymentRoot, bool Compile,
			const std::string& ServicesJson, std::shared_ptr<const FModuleEnvironment>&,
			std::string& Error)> LoadEnvironment;
		std::function<bool(bool Compile, const std::string& ServicesJson,
			std::shared_ptr<const FModuleEnvironment>&, std::string& Error)> LoadExternalEnvironment;
		// Configure host callbacks from compiled metadata after the environment is ready.
		// This prepares bindings/resources only; it must not execute the entry program.
		std::function<bool(const FPackageData&, std::string& Error)> PreparePackage;
	};
	struct FPreparedApplication
	{
		FCompileResult CompileSource(const FCompileOptions& HostOptions = {}) const;

		std::shared_ptr<const void> Owner;
		std::shared_ptr<const FModuleEnvironment> Environment;
		std::unique_ptr<FPackageData> Package;
		std::string Source, LogicalPath;
		// Owned, portable compilation defaults saved by source applications.
		std::vector<std::string> Defines, ImplicitImports;
		std::vector<FPredefinedMacro> PredefinedMacros;
		bool AllowCircularImports = true, AllowProjectWideAccess = false;
	};
	std::string HostApplicationTargetJson();
	bool ValidateApplicationTarget(std::string_view TargetJson, FDiagnostic&, const FLimits& = {});
	std::string DefaultApplicationCacheRoot();
	FApplicationServices CreateApplicationServices();
	// Strict, non-secret host options. Paths must already be absolute.
	bool ConfigureApplicationServices(std::string_view OptionsJson, FApplicationServices&, FDiagnostic&);
	// Directory snapshots and publishers cooperate on the assembler-owned .vero-lease.
	std::shared_ptr<const void> AcquireApplicationDirectoryLease(const std::string& AbsoluteRoot,
		bool Exclusive, FDiagnostic&);
	class FApplication final : public std::enable_shared_from_this<FApplication>
	{
	public:
		struct FImpl;
		static std::shared_ptr<const FApplication> Open(const std::string& AbsolutePath,
			const FApplicationServices&, FDiagnostic&, const FLimits& = {});
		static std::shared_ptr<const FApplication> Open(std::vector<std::uint8_t>,
			const FApplicationServices&, FDiagnostic&, const FLimits& = {});
		static std::shared_ptr<const FApplication> Open(FArtifactData,
			const FApplicationServices&, FDiagnostic&, const FLimits& = {});
		~FApplication();
		const std::string& GetDescriptorJson() const noexcept;
		const std::string& GetIdentity() const noexcept;
		FByteView GetContainerBytes() const noexcept;
		std::string GetId() const;
		std::string GetEntryPath() const;
		bool IsSourceEntry() const;
		const std::vector<FContentEntry>& GetEntries() const noexcept;
		bool Read(const std::string& RelativePath, FArtifactData&, FDiagnostic&,
			const FArtifactWork& = {}, const std::string& Kind = "resource") const;
		// Returns integrity-checked stored bytes, without decrypting protected data.
		bool ReadStored(const std::string& RelativePath, FArtifactData&, FDiagnostic&,
			const FArtifactWork& = {}) const;
		bool Export(std::vector<FContentFile>&, FDiagnostic&) const;
		bool Materialize(const std::string& RelativeRoot, std::string& PhysicalRoot,
			std::shared_ptr<const void>& Lease, FDiagnostic&) const;
		bool Prepare(FPreparedApplication&, FDiagnostic&) const;
		// Metadata/image inspection does not load providers, processors or environments.
		bool ReadEntryPackage(FPackageData&, FDiagnostic&) const;
		bool ResolveRuntime(const std::string& ExplicitAbsoluteComponent, std::string& Executable,
			std::shared_ptr<const void>& Lease, FDiagnostic&, std::vector<std::string>* StandardLibraryPaths = nullptr) const;
	private:
		static std::shared_ptr<const FApplication> Initialize(std::shared_ptr<FImpl>,
			const FApplicationServices&, FDiagnostic&, const FLimits&);
		explicit FApplication(std::shared_ptr<FImpl>);
		std::shared_ptr<FImpl> Impl;
	};

	bool ValidateApplicationDescriptor(std::string_view DescriptorJson, std::string& CanonicalJson,
		FDiagnostic&, const FLimits& = {});
	bool AssembleApplication(std::string_view DescriptorJson, const std::vector<FContentFile>& Files,
		std::vector<FContentFile>& SealedFiles, FDiagnostic&, const FLimits& = {},
		const FProtectionOptions& = {}, const FProtection* = nullptr);
	bool BuildApplication(const FBytecodeModule&, const FPackageBuildOptions&,
		const std::string& ProductVersion, const std::vector<FContentFile>& Resources,
		std::vector<FContentFile>& SealedFiles, FDiagnostic&, const FLimits& = {},
		const FProtectionOptions& = {}, const FProtection* = nullptr);
	// Wrap an already baked Core application image without rebuilding its metadata or code.
	bool BuildApplication(std::vector<std::uint8_t> Image, const std::string& ProductVersion,
		const std::vector<FContentFile>& Resources, std::vector<FContentFile>& SealedFiles,
		FDiagnostic&, const FLimits& = {}, const FProtectionOptions& = {}, const FProtection* = nullptr);
}
