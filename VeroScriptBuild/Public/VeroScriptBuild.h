// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptApplication.h"
#include "VeroScriptCompile.h"
#include <map>
#include <set>

namespace VeroScript::Build
{
	struct FDiagnostic
	{
		std::string Code, Message, Target, Path, Stage;
	};
	struct FLimits
	{
		Application::FLimits Content;
		std::size_t MaxTargets = 4096, MaxDepth = 256;
		std::uint64_t TimeoutMilliseconds = 300000, StepTimeoutMilliseconds = 60000;
		std::uint64_t ScriptInstructions = 10000000, MaxLogBytes = 16ull << 20;
	};
	struct FArtifact
	{
		std::string Target, Output, Path, Type, Kind, Identity, Platform;
		std::string SignaturePath;
		std::shared_ptr<const void> Owner;
	};
	struct FStepContext
	{
		std::string Target, ProjectRoot, StagingRoot, PlatformJson, OptionsJson;
		std::map<std::string, FArtifact> Inputs, Outputs;
		std::vector<std::string> StandardLibraryPaths;
		FLimits Limits;
		std::function<void()> CheckCancelled;
		std::function<void(const std::string&)> Log;
		// Providers report additional physical inputs before returning success.
		std::function<void(const std::string& AbsolutePath)> TrackInput;
	};
	class IBuildStepProvider
	{
	public:
		virtual ~IBuildStepProvider() = default;
		virtual std::string GetIdentity() const = 0;
		virtual std::string GetVersion() const = 0;
		// Planning must not execute processors, native factories or external tools.
		virtual bool Plan(const std::string& OptionsJson, const std::string& PlatformJson, FDiagnostic&) const = 0;
		virtual bool IsDeterministic() const noexcept
		{
			return false;
		}
		virtual bool Execute(const FStepContext&, FDiagnostic&) const = 0;
	};
	struct FServices
	{
		FCompileOptions Compile;
		// Retain host catalogs and callback state through planning, execution and results.
		std::vector<std::shared_ptr<const void>> Owners;
		// Explicit frozen environment, toolchain and processor configuration identity.
		std::string Identity;
		std::string HostPlatformJson;
		Application::FApplicationServices Application;
		std::shared_ptr<const Application::FProtection> Protection;
		std::map<std::string, std::shared_ptr<const IBuildStepProvider>> Providers;
		std::function<bool(const std::string& Source, const std::string& Output, FDiagnostic&)> Pack;
		std::function<bool(const std::vector<std::string>& Usage, const std::vector<std::string>& Roots,
		    const std::string& Output, FDiagnostic&)>
		    Deploy;
		std::function<std::shared_ptr<const void>(const std::string& Directory, FDiagnostic&)> LeaseDeployment;
		// Explicit compatible prebuilt bootstrap, never inferred from PATH.
		std::string LauncherTemplate;
		bool AllowProcess = false, AllowScript = false;
		std::function<void(const std::string& Target, const std::string& Text)> Log;
	};
	struct FBuildRequest
	{
		// All service-boundary paths are absolute and frozen. No process chdir.
		std::string ProjectRoot, BuildFile, OutputDirectory;
		std::string Profile = "release";
		std::vector<std::string> Targets;
		FLimits Limits;
		std::function<void()> CheckCancelled;
	};
	class FBuildPlan final
	{
	public:
		struct FImpl;
		std::string ToJson() const;
		const std::vector<std::string>& GetOrder() const;

	private:
		friend class FBuilder;
		explicit FBuildPlan(std::shared_ptr<FImpl>);
		std::shared_ptr<FImpl> Impl;
	};
	struct FTargetResult
	{
		std::string Id;
		bool Cached = false;
		std::vector<FArtifact> Artifacts;
	};
	struct FBuildResult
	{
		std::vector<FTargetResult> Targets;
		std::vector<FDiagnostic> Diagnostics;
		std::string ToJson() const;
	};
	class FBuilder final
	{
	public:
		static bool Plan(const FBuildRequest&, const FServices&, std::shared_ptr<const FBuildPlan>&, FDiagnostic&);
		static bool Execute(const FBuildPlan&, FBuildResult&, FDiagnostic&);
	};
}
