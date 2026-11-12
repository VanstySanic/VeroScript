// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBuild.h"
#include "VeroScriptPath.h"
#include "json/json.hpp"
#include <filesystem>
#include <stdexcept>

namespace VeroScript::Build::Detail
{
	using J = nlohmann::json;
	namespace fs = std::filesystem;

	// Preserve the native spellings included in frozen build plans.
	inline std::string NativePathText(const fs::path& Path)
	{
		#if defined(__cpp_char8_t)
		const auto Bytes = Path.u8string();
		return std::string(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
		#else
		return Path.u8string();
		#endif
	}

	struct FFailure : std::runtime_error
	{
		FFailure(std::string Code, std::string Message, std::string Path = {});
		FDiagnostic Diagnostic;
	};
	[[noreturn]] void Fail(const std::string&, const std::string&, const std::string& = {});
	void Require(bool, const Application::FDiagnostic&);
	J Parse(std::string_view, const FLimits&);
	void Keys(const J&, std::initializer_list<const char*>, const std::string&);
	void Id(const std::string&);
	void Safe(const std::string&);
	bool Under(const fs::path&, const fs::path&);
	std::string PhysicalIdentity(const fs::path&);
	fs::path TreePath(const fs::path&);
	void RemoveOwnedTree(const fs::path&, const fs::path& OwnedRoot);
	fs::path Absolute(const std::string&, const fs::path&);
	void Physical(const fs::path&, bool Missing = false);
	std::string Read(const fs::path&, const FLimits&);
	void Write(const fs::path&, std::string_view);
	std::string Fingerprint(const fs::path&, const FLimits&);
	void Snapshot(const fs::path&, std::map<std::string, std::string>&, const FLimits&);
	std::vector<Application::FContentFile> Tree(const fs::path&, const FLimits&);
	void Copy(const fs::path&, const fs::path&, const FLimits&);
	J ValidateConfiguration(std::string_view, const FBuildRequest&);
	std::vector<J> ArtifactReferences(const J& Target);
	std::set<std::string> References(const J&);
	Application::FProtectionOptions Protection(const J&, bool Content);
	void RunBuiltinStep(const std::string&, const FStepContext&, const FServices&);
}
namespace VeroScript::Build
{
	struct FBuildPlan::FImpl
	{
		FBuildRequest Request;
		FServices Services;
		Detail::J Configuration, Targets, Platform;
		std::string PlatformId, OutputRoot, Identity;
		std::vector<std::string> Order;
		std::map<std::string, std::string> Inputs;
		std::map<std::string, std::string> ProviderIdentities;
		std::map<std::string, std::string> ProviderVersions;
		std::map<std::string, std::map<std::string, FArtifact>> Outputs;
		std::vector<std::string> ImportPaths, StdlibPaths, Defines, ImplicitImports;
	};
}
