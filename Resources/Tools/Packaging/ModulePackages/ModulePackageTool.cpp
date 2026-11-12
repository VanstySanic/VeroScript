// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPackageManager.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptVmBackendLoader.h"
#include "VeroScriptJitBackendLoader.h"
#if VEROSCRIPT_PACKAGE_TOOL_VM
#include "VeroScriptBytecodeVm.h"
#endif
#include "VeroScriptPath.h"
#include "json/json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
	using J = nlohmann::json;
	namespace P = VeroScript::Packages;
	namespace Fs = std::filesystem;
	std::string Absolute(const char* Path)
	{
		return VeroScript::PathUtil::ToUtf8(Fs::absolute(VeroScript::PathUtil::FromUtf8(Path)).lexically_normal());
	}
	J Target()
	{
		const auto T = P::HostTargetContext();
		return {{"core", T.Core},
		    {"coreBuild", T.CoreBuild},
		    {"os", T.Os},
		    {"arch", T.Architecture},
		    {"environment", T.Environment},
		    {"abis", T.Abis}};
	}
	void Describe(
	    const char* Library, const char* Package, const char* Hash, const char* Catalog, const char* Dependency)
	{
		std::string Error;
		std::shared_ptr<const VeroScript::FLoadedNativeExtension> Preload;
		if (Dependency)
		{
			Preload = VeroScript::LoadNativeExtension(Absolute(Dependency), Error);
			if (!Preload)
			{
				throw std::runtime_error(Error);
			}
		}
		auto Module = VeroScript::LoadNativeExtension(Absolute(Library), Error);
		if (!Module)
		{
			throw std::runtime_error(Error);
		}
		const auto& D = Module->GetDescriptor();
		VeroScript::FNativeExtensionRegistry Registry;
		if (Preload)
		{
			VeroScript::FNativeExtensionRegistrationOptions DependencyRegistration;
			DependencyRegistration.Origin.ProviderId = "package-catalog-dependency";
			if (!Registry.Add(Preload, std::move(DependencyRegistration), Error))
			{
				throw std::runtime_error(Error);
			}
		}
		VeroScript::FNativeExtensionRegistrationOptions Registration;
		Registration.Origin.ProviderId = P::NativeProviderId(Package, D.Id, Hash);
		// A CLI forwarding adapter needs invocation-specific host services. Its
		// descriptor and empty binding catalog can be audited without creating it.
		if ((D.Roles != VeroScript::NativeExtensionRole(VeroScript::ENativeExtensionRole::CommandLine) &&
		        !Registry.Add(Module, std::move(Registration), Error)) ||
		    !VeroScript::SaveBindingCatalogFile(*Registry.GetBindings(), Absolute(Catalog), Error))
		{
			throw std::runtime_error(Error);
		}
		if (Preload)
		{
			std::ifstream Input(Fs::u8path(Absolute(Catalog)));
			J Bindings;
			Input >> Bindings;
			Input.close();
			for (auto& Section : Bindings.items())
			{
				if (!Section.value().is_array())
				{
					continue;
				}
				auto& Entries = Section.value();
				Entries.erase(std::remove_if(Entries.begin(),
				                  Entries.end(),
				                  [](const J& Entry)
				{
					return Entry.value("origin", J::object()).value("providerId", "") ==
					    "package-catalog-dependency";
				}),
				    Entries.end());
			}
			std::ofstream Output(Fs::u8path(Absolute(Catalog)));
			Output << Bindings.dump(2) << '\n';
			if (!Output)
			{
				throw std::runtime_error("Cannot write module catalog");
			}
		}
		J Roles = J::array();
		for (const auto& Role : {std::pair{1u, "bindings"}, {2u, "compiler"}, {4u, "provider"}, {8u, "cli"}})
		{
			if (D.Roles & Role.first)
			{
				Roles.push_back(Role.second);
			}
		}
		J Services = J::array();
		for (const auto& Provider : Registry.GetProviders())
		{
			Services.push_back({{"service", Provider.ServiceId}, {"id", Provider.BackendId}, {"module", D.Id}});
		}
		std::cout << J{{"target", Target()},
		                 {"id", D.Id},
		                 {"componentVersion", D.Version},
		                 {"roles", Roles},
		                 {"backends", Services},
		                 {"abi", D.Abi},
		                 {"coreBuild", D.CoreBuild}}
		                 .dump(2)
		          << '\n';
	}
	void Run(const char* Project, const char* Source, bool Analysis, bool ProbeProviders = false, bool ProbeJit = false)
	{
		P::FEnvironmentInput Input;
		Input.Root = Absolute(Project);
		Input.WorkingDirectory = Input.Root;
		Input.CacheDirectory = VeroScript::PathUtil::ToUtf8(VeroScript::PathUtil::FromUtf8(Input.Root) / ".test-cache");
		P::FDiagnostic Diagnostic;
		P::FPackageManager Manager;
		std::shared_ptr<const P::FInstalledEnvironment> Installed;
		P::FClientOptions Client;
		Client.Offline = true;
		if (!Manager.Load(Input, Installed, Diagnostic, Client))
		{
			throw std::runtime_error(Diagnostic.Code + ": " + Diagnostic.Message);
		}
		P::FCoreEnvironmentOptions Options;
		Options.EntryRoot = Input.Root;
		Options.AnalysisOnly = Analysis;
		Options.StdlibSearchPaths = {VEROSCRIPT_PACKAGE_TOOL_STDLIB};
		std::shared_ptr<const VeroScript::FModuleEnvironment> Environment;
		std::vector<VeroScript::FNativeServiceProviderDesc> Providers;
		if (!Installed->BuildCoreEnvironment(Options, Environment, Diagnostic, &Providers))
		{
			throw std::runtime_error(Diagnostic.Code + ": " + Diagnostic.Message + " " + Diagnostic.Field);
		}
		if (ProbeProviders)
		{
			for (const auto& Provider : Providers)
			{
				VeroScript::FNativeServiceCreateContext Context;
				Context.Environment = Environment;
				std::string Error;
				auto Instance = VeroScript::CreateNativeService(Provider, Context, Error);
				if (!Instance)
				{
					throw std::runtime_error(Error);
				}
				std::cout << "provider " << Provider.ServiceId << '/' << Provider.BackendId << '\n';
			}
		}
		const auto Path = Absolute(Source);
		std::ifstream File(Fs::u8path(Path), std::ios::binary);
		if (!File)
		{
			throw std::runtime_error("Missing consumer source");
		}
		const std::string Text(std::istreambuf_iterator<char>{File}, {});
		VeroScript::FCompileOptions Compile;
		#if VEROSCRIPT_PACKAGE_TOOL_VM
		Compile.ScriptCompileExtensions.VmBackendFactory =
		    VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
		#endif
		Compile.ModuleEnvironment = Environment;
		Compile.StdlibSearchPaths.assign(Options.StdlibSearchPaths.begin(), Options.StdlibSearchPaths.end());
		auto Code = VeroScript::VeroScriptCompile::CompileSource(Text, Path, Compile);
		if (!Code.bSuccess)
		{
			throw std::runtime_error(Code.Error.Message);
		}
		if (!Analysis)
		{
			VeroScript::VeroScriptVm Vm(std::cout);
			#if VEROSCRIPT_PACKAGE_TOOL_VM
			std::string VmError;
			if (!Vm.SetVmBackendFactory(Compile.ScriptCompileExtensions.VmBackendFactory, VmError))
			{
				throw std::runtime_error(VmError);
			}
			#endif
			if (ProbeJit)
			{
				std::shared_ptr<const VeroScript::IJitBackendFactory> Factory;
				if (!Installed->LoadJitBackend("asmjit", Factory, Diagnostic))
				{
					throw std::runtime_error(Diagnostic.Code + ": " + Diagnostic.Message);
				}
				std::string Error;
				if (!Vm.SetJitBackendFactory(std::move(Factory), Error))
				{
					throw std::runtime_error(Error);
				}
				Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
			}
			std::string Error;
			if (!Vm.SetModuleEnvironment(Environment, Error))
			{
				throw std::runtime_error(Error);
			}
			if (!Vm.Execute(*Code.Module, VeroScript::ERunMode::Script) || Vm.GetProcessExitCode() != 0)
			{
				throw std::runtime_error(Vm.GetError().Message.empty() ? "Consumer failed" : Vm.GetError().Message);
			}
			if (ProbeJit)
			{
				const auto Stats = Vm.GetJitStats();
				if (!Stats.CompiledEntries || !Stats.Dispatches)
				{
					throw std::runtime_error("Installed JIT did not compile and dispatch the consumer");
				}
				std::cout << "installed jit compiled=" << Stats.CompiledEntries << " dispatched=" << Stats.Dispatches << '\n';
			}
		}
		std::cout << (Analysis ? "Installed package analysis passed\n" : "Installed package execution passed\n");
	}
} // namespace
int RunMain(int Argc, char** Argv)
{
	try
	{
		if (Argc == 2 && std::string_view(Argv[1]) == "target")
		{
			std::cout << Target().dump() << '\n';
			return 0;
		}
		if (Argc == 3 && std::string_view(Argv[1]) == "describe-jit")
		{
			std::string Error;
			auto Factory = VeroScript::LoadJitBackend(Absolute(Argv[2]), Error);
			if (!Factory)
			{
				throw std::runtime_error(Error);
			}
			const auto& D = Factory->GetDescription();
			std::cout << J{{"target", Target()}, {"id", D.Id}, {"nativeCode", D.bNativeCode}}.dump() << '\n';
			return 0;
		}
		if (Argc == 3 && std::string_view(Argv[1]) == "describe-vm")
		{
			std::string Error;
			auto Factory = VeroScript::LoadVmBackend(Absolute(Argv[2]), Error);
			if (!Factory)
			{
				throw std::runtime_error(Error);
			}
			const auto& D = Factory->GetDescription();
			std::cout << J{{"target", Target()}, {"id", D.Id}, {"version", D.Version},
			                 {"implementationId", D.ImplementationId}, {"capabilities", D.Capabilities}}
			                 .dump()
			          << '\n';
			return 0;
		}
		if ((Argc == 6 || Argc == 7) && std::string_view(Argv[1]) == "describe")
		{
			Describe(Argv[2], Argv[3], Argv[4], Argv[5], Argc == 7 ? Argv[6] : nullptr);
			return 0;
		}
		if ((Argc == 4 || Argc == 5) && std::string_view(Argv[1]) == "run")
		{
			if (Argc == 5 && std::string_view(Argv[4]) != "--analysis" && std::string_view(Argv[4]) != "--providers" && std::string_view(Argv[4]) != "--jit")
			{
				throw std::runtime_error("Unknown option");
			}
			Run(Argv[2], Argv[3], Argc == 5 && std::string_view(Argv[4]) == "--analysis",
			    Argc == 5 && std::string_view(Argv[4]) == "--providers", Argc == 5 && std::string_view(Argv[4]) == "--jit");
			return 0;
		}
		if (Argc >= 3 && std::string_view(Argv[1]) == "command")
		{
			return P::RunPackageCommand({Argv + 2, Argv + Argc},
			    VeroScript::PathUtil::ToUtf8(Fs::current_path()),
			    Absolute(Argv[0]),
			    std::cout,
			    std::cerr);
		}
		throw std::runtime_error("Expected describe <library> <package> <sha256> <catalog>, run <project> <source> "
		                         "[--analysis], or command <package command>");
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}

#if defined(_WIN32)
int wmain(int Argc, wchar_t** Argv)
{
	try
	{
		std::vector<std::string> Arguments;
		for (int Index = 0; Index < Argc; ++Index)
		{
			Arguments.push_back(VeroScript::PathUtil::ToUtf8(Fs::path(Argv[Index])));
		}
		std::vector<char*> Pointers;
		for (auto& Argument : Arguments)
		{
			Pointers.push_back(Argument.data());
		}
		return RunMain(Argc, Pointers.data());
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
#else
int main(int Argc, char** Argv)
{
	return RunMain(Argc, Argv);
}
#endif
