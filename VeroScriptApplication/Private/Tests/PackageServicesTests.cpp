// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptApplicationPackages.h"
#include "VeroScriptPath.h"
#include "json/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::Application;
	namespace fs = std::filesystem;
	using J = nlohmann::json;
	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	void Write(const fs::path& Path, const J& Value)
	{
		fs::create_directories(Path.parent_path());
		std::ofstream File(Path, std::ios::binary);
		File << Value.dump();
		Require(bool(File), "cannot write package fixture");
	}
}

int main(int Count, char** Args)
{
	try
	{
		Require(Count == 3, "expected the native fixture and a test directory");
		const auto Work = fs::weakly_canonical(fs::absolute(PathUtil::FromUtf8(Args[2]))) /
		    ("providers-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		Require(fs::create_directories(Work), "expected a fresh test directory");
		const auto Vendor = Work / "vendor", Project = Work / "project";
		fs::create_directories(Vendor / "native");
		const auto Binary = fs::absolute(PathUtil::FromUtf8(Args[1]));
		fs::copy_file(Binary, Vendor / "native" / Binary.filename());
		const auto Target = Packages::HostTargetContext();
		Write(Vendor / "veroscript-package.json", J{{"format", "veroscript-package"}, {"formatVersion", "0.1.1"}, {"name", "application-provider-fixture"}, {"version", "0.1.1"}, {"target", {{"os", Target.Os}, {"arch", Target.Architecture}, {"environment", Target.Environment}, {"coreBuild", Target.CoreBuild}, {"abis", {{"native-extension", Target.Abis.at("native-extension")}}}}}, {"entries", {{"native", J::array({J{{"id", "native-fixture"}, {"componentVersion", "0.1.1"}, {"path", PathUtil::ToUtf8(fs::path("native") / Binary.filename())}, {"linkage", "dynamic"}, {"roles", J::array({"bindings", "compiler", "provider"})}}})}, {"backends", J::array({J{{"service", "test"}, {"id", "fixture"}, {"module", "native-fixture"}}})}}}});
		Write(Project / "veroscript-project.json", J{{"format", "veroscript-project"}, {"formatVersion", "0.1.1"}, {"name", "application-provider-test"}, {"version", "0.1.1"}, {"services", {{"test", {{"backend", "fixture"}}}}}});
		Packages::FRequest Request;
		Request.Environment.Root = PathUtil::ToUtf8(Project);
		Request.Environment.CacheDirectory = PathUtil::ToUtf8(Work / "cache");
		Request.Arguments = {PathUtil::ToUtf8(Vendor)};
		Request.Options.Offline = true;
		Packages::FPackageManager Manager;
		Packages::FResult Installed;
		Packages::FDiagnostic Diagnostic;
		const auto Executed = Manager.Execute(Request, Installed, Diagnostic);
		Require(Executed && Installed.Environment,
		    "install native provider fixture: " + Diagnostic.Message);
		const auto Check = [&](bool Bundled, Packages::FCoreEnvironmentOptions Options,
		                       const std::string& ServicesJson, const std::string& Expected)
		{
			auto Services = CreateApplicationServices();
			FApplicationBackendSelector Select;
			std::shared_ptr<const FModuleEnvironment> Environment;
			ConfigurePackageApplicationServices(Services, Bundled ? nullptr : Installed.Environment, std::move(Options),
			    [&](auto Providers, auto Published, auto SelectBackend)
			{
				Require(Providers.size() == 1 && Providers.front().BackendId == "fixture",
				    "the selected environment did not publish its native provider");
				Require(bool(Published), "the provider environment is absent");
				Select = std::move(SelectBackend);
			});
			std::string Error;
			const auto Loaded = Bundled
			    ? Services.LoadEnvironment(PathUtil::ToUtf8(Work / "deployed"), false, ServicesJson, Environment, Error)
			    : Services.LoadExternalEnvironment(false, ServicesJson, Environment, Error);
			Require(Loaded, "prepare provider environment: " + Error);
			Require(Select && Select("test") == Expected, "effective application backend choice was lost");
			Require(Select("unknown") == AutomaticBackend, "an unselected role must use automatic policy");
			Environment.reset();
			Services = {};
			Require(Select("test") == Expected, "backend choices did not retain their installed snapshot");
		};
		Check(false, {}, "{}", "fixture");
		Check(false, {}, "{\"test\":\"\"}", "none");
		Check(false, {}, "{\"test\":\"auto\"}", "auto");
		Packages::FCoreEnvironmentOptions Host;
		Host.SelectedBackends["test"] = "";
		Check(false, Host, "{\"test\":\"fixture\"}", "none");
		Host.SelectedBackends["test"] = "fixture";
		Check(false, Host, "{\"test\":\"\"}", "fixture");
		std::string Deployment;
		const auto Deployed = Installed.Environment->Deploy(PathUtil::ToUtf8(Work / "deployed"), {"runtime", "compile"}, {}, Deployment, Diagnostic);
		Require(Deployed, "deploy provider environment: " + Diagnostic.Code + ": " + Diagnostic.Message);
		Check(true, {}, "{}", "fixture");
		Check(true, {}, "{\"test\":\"\"}", "none");
		const auto Stdlib = Work / "stdlib", ExtraStdlib = Work / "extra-stdlib";
		fs::create_directories(Stdlib);
		fs::create_directories(ExtraStdlib);
		Packages::FCoreEnvironmentOptions Search;
		Search.StdlibSearchPaths = {PathUtil::ToUtf8(Stdlib), PathUtil::ToUtf8(Stdlib),
		    PathUtil::ToUtf8(Stdlib / ".." / "stdlib"), PathUtil::ToUtf8(ExtraStdlib),
		    PathUtil::ToUtf8(ExtraStdlib / ".")};
		#if defined(_WIN32)
		Search.StdlibSearchPaths.push_back(PathUtil::ToUtf8(Work / "STDLIB"));
		#endif
		for (const bool Bundled : {false, true})
		{
			for (const bool Compile : {false, true})
			{
				auto Services = CreateApplicationServices();
				std::shared_ptr<const FModuleEnvironment> Environment;
				ConfigurePackageApplicationServices(Services, Bundled ? nullptr : Installed.Environment, Search);
				std::string Error;
				const auto Loaded = Bundled
				    ? Services.LoadEnvironment(PathUtil::ToUtf8(Work / "deployed"), Compile, "{}", Environment, Error)
				    : Services.LoadExternalEnvironment(Compile, "{}", Environment, Error);
				Require(Loaded, "prepare application with repeated standard library roots: " + Error);
				const auto& Description = Environment->GetDescription();
				Require(Description.StdlibSearchPaths.size() == 2 &&
				    PathUtil::Equal(Description.StdlibSearchPaths[0], PathUtil::ToUtf8(Stdlib)) &&
				    PathUtil::Equal(Description.StdlibSearchPaths[1], PathUtil::ToUtf8(ExtraStdlib)),
				    "standard library roots must retain their first-seen search order without aliases");
				std::size_t SourceRootCount = 0;
				for (const auto& Root : Description.SourceRoots)
				{
					SourceRootCount += Root.OwnerId == "package.stdlib";
				}
				Require(SourceRootCount == 2, "standard library aliases created duplicate source ownership");
			}
		}
		Search.StdlibSearchPaths = {PathUtil::ToUtf8(Project)};
		auto Conflicting = CreateApplicationServices();
		ConfigurePackageApplicationServices(Conflicting, Installed.Environment, Search);
		std::shared_ptr<const FModuleEnvironment> Environment;
		std::string Error;
		Require(!Conflicting.LoadExternalEnvironment(false, "{}", Environment, Error) &&
		    Error.find("invalid or duplicate source root") != std::string::npos,
		    "a standard library root must not override the project owner");
		std::cout << "Application providers retain host/application/project backend precedence and bundled defaults\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
