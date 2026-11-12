// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptApplication.h"
#include "VeroScriptExecutable.h"
#include "VeroScriptProcess.h"
#include "VeroScriptHost.h"
#include "VeroScriptSession.h"
#include "VeroScriptPath.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptBytecodeVmAssembly.h"
#if defined(VEROSCRIPT_APPLICATION_PACKAGES)
#include "VeroScriptApplicationPackages.h"
#endif
#if defined(VEROSCRIPT_WITH_GUI)
#include "VeroScriptGuiTools.h"
#include "VeroScriptGuiServices.h"
#endif
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::Application;
	namespace fs = std::filesystem;
	std::string SelfPath()
	{
		#if defined(_WIN32)
		std::wstring W(32768, L'\0');
		const auto N = GetModuleFileNameW(nullptr, W.data(), static_cast<DWORD>(W.size()));
		if (!N || N >= W.size()) return {};
		W.resize(N);
		return PathUtil::ToUtf8(fs::path(W));
		#elif defined(__linux__)
		std::string S(65536, '\0');
		const auto N = ::readlink("/proc/self/exe", S.data(), S.size());
		if (N <= 0 || static_cast<std::size_t>(N) >= S.size()) return {};
		S.resize(static_cast<std::size_t>(N));
		return S;
		#elif defined(__APPLE__)
		std::uint32_t N = 0;
		_NSGetExecutablePath(nullptr, &N);
		std::string S(N, '\0');
		if (_NSGetExecutablePath(S.data(), &N) != 0) return {};
		S.resize(std::char_traits<char>::length(S.c_str()));
		return PathUtil::ToUtf8(fs::absolute(PathUtil::FromUtf8(S)).lexically_normal());
		#else
		return {};
		#endif
	}
	int Main(const std::vector<std::string>& Args)
	{
		try
		{
			if (Args.size() == 2 && Args[1] == "--component-info")
			{
				std::cout << HostApplicationTargetJson() << '\n';
				return 0;
			}
			auto Services = CreateApplicationServices();
			std::map<std::string, std::string> Backends;
			std::string File, Crypto, Keyring, Runtime, Environment, JitPath, VmPath;
			#if VEROSCRIPT_RUNTIME_HAS_VM && !defined(VEROSCRIPT_APPLICATION_BOOTSTRAP)
			VmPath = BytecodeVm::RuntimeLibraryPath(SelfPath());
			#endif
			const auto ProductVmPath = VmPath;
			bool Check = false, Delegated = false, RequireSignature = false, VmExplicit = false;
			std::vector<std::string> ScriptArgs;
			std::shared_ptr<const IJitBackendFactory> Jit;
			for (std::size_t I = 1; I < Args.size(); ++I)
			{
				const auto& A = Args[I];
				if (A == "--application-delegated")
				{
					if (Delegated)
					{
						throw std::runtime_error("duplicate delegation marker");
					}
					Delegated = true;
					continue;
				}
				if (A == "--check")
				{
					Check = true;
					continue;
				}
				if (A == "--require-signature")
				{
					RequireSignature = true;
					continue;
				}
				if (A == "--")
				{
					ScriptArgs.insert(ScriptArgs.end(), Args.begin() + I + 1, Args.end());
					break;
				}
				#if defined(VEROSCRIPT_WITH_GUI) || defined(VEROSCRIPT_APPLICATION_BOOTSTRAP)
				if (A == "--platform-backend" || A == "--graphics-backend" || A == "--gui-backend" ||
				    A == "--image-backend" || A == "--font-backend" || A == "--text-layout-backend")
				{
					if (++I == Args.size() || Args[I].empty())
					{
						throw std::runtime_error("missing GUI backend option value");
					}
					Backends[A.substr(2, A.size() - 2 - std::string("-backend").size())] = Args[I];
					continue;
				}
				#endif
				if (A == "--application-file" || A == "--crypto-library" || A == "--keyring" || A == "--runtime" || A == "--path" || A == "--application-cache" || A == "--stdlib-path" || A == "--jit-backend" || A == "--vm-backend")
				{
					if (++I == Args.size() || Args[I].empty())
					{
						throw std::runtime_error("missing application option value");
					}
					if (A == "--jit-backend" || A == "--vm-backend")
					{
						const auto& Choice = Args[I];
						if (Choice.find_first_of("/\\.") == std::string::npos && !(A == "--vm-backend" && Choice == "vs-bytecode"))
						{
							Backends[A == "--vm-backend" ? "vm" : "jit"] = Choice;
							if (A == "--jit-backend")
							{
								JitPath.clear();
							}
							else
							{
								VmExplicit = false;
								VmPath = ProductVmPath;
							}
							continue;
						}
						if (A == "--vm-backend" && Choice == "vs-bytecode")
						{
							VmExplicit = !ProductVmPath.empty();
							VmPath = ProductVmPath;
							Backends["vm"] = VmExplicit ? "" : Choice;
							continue;
						}
						Backends[A == "--vm-backend" ? "vm" : "jit"] = "";
						if (A == "--vm-backend")
						{
							VmExplicit = true;
						}
					}
					const auto P = PathUtil::ToUtf8(fs::absolute(PathUtil::FromUtf8(Args[I])).lexically_normal());
					if (A == "--application-file")
					{
						File = P;
					}
					else if (A == "--crypto-library")
					{
						Crypto = P;
					}
					else if (A == "--keyring")
					{
						Keyring = P;
					}
					else if (A == "--runtime")
					{
						Runtime = P;
					}
					else if (A == "--path")
					{
						Environment = P;
					}
					else if (A == "--stdlib-path")
					{
						Services.StandardLibraryPaths.push_back(P);
					}
					else if (A == "--jit-backend")
					{
						JitPath = P;
					}
					else if (A == "--vm-backend")
					{
						VmPath = P;
					}
					else
					{
						Services.CacheRoot = P;
					}
					continue;
				}
				#if !defined(VEROSCRIPT_APPLICATION_BOOTSTRAP)
				if (File.empty())
				{
					File = PathUtil::ToUtf8(fs::absolute(PathUtil::FromUtf8(A)).lexically_normal());
					continue;
				}
				#endif
				ScriptArgs.insert(ScriptArgs.end(), Args.begin() + I, Args.end());
				break;
			}
			if (!Crypto.empty() || !Keyring.empty() || RequireSignature)
			{
				if (Crypto.empty() || Keyring.empty())
				{
					throw std::runtime_error("protection requires an explicit crypto library and trusted keyring");
				}
				FDiagnostic E;
				auto Provider = LoadSodiumCrypto(Crypto, E);
				if (!Provider)
				{
					throw std::runtime_error(E.Message);
				}
				auto Keys = LoadProtectionKeys(Keyring, E);
				if (!Keys)
				{
					throw std::runtime_error(E.Message);
				}
				FTrustPolicy Policy;
				Policy.RequireSignature = RequireSignature;
				Services.Protection = std::make_shared<FProtection>(std::move(Provider), std::move(Keys), std::move(Policy));
			}
			Services.AnalysisOnly = Check;
			#if defined(VEROSCRIPT_WITH_GUI)
			auto GuiService = Gui::CreateCommandLineService();
			auto GuiTools = GuiService->CreateRegistration();
			FCommandLineValues GuiValues;
			for (const auto& Backend : Backends)
			{
				// Execution backend choices are handled by the runtime, not the GUI adapter.
				if (Backend.first == "vm" || Backend.first == "jit")
				{
					continue;
				}
				GuiValues.emplace(Backend.first + "-backend", std::vector<std::string>{Backend.second});
			}
			std::string GuiError;
			FHostBindingRegistry GuiBindings;
			if (!GuiTools.Module.Configure({}, GuiValues, GuiTools.Module.UserData, GuiError) ||
			    !GuiTools.RegisterBindings(GuiBindings, GuiError))
			{
				throw std::runtime_error(GuiError);
			}
			Services.PreparePackage = [&GuiTools](const auto& Package, auto& Error)
			{
				return !Package.HostMetadata.count("veroscript.gui.contract") || GuiTools.UseArtifact(Package, Error);
			};
			#endif
			#if defined(VEROSCRIPT_APPLICATION_PACKAGES)
			std::shared_ptr<const Packages::FInstalledEnvironment> Installed;
			if (!Environment.empty())
			{
				Packages::FEnvironmentInput Input;
				Input.Root = Environment;
				Packages::FDiagnostic E;
				if (!Packages::FPackageManager().Load(Input, Installed, E))
				{
					throw std::runtime_error(E.Code + ": " + E.Message);
				}
			}
			Packages::FCoreEnvironmentOptions Options;
			Options.SelectedBackends = Backends;
			#if defined(VEROSCRIPT_WITH_GUI)
			Options.BaseBindings = &GuiBindings;
			ConfigurePackageApplicationServices(Services, Installed, Options,
			    [&GuiTools](auto Providers, auto Environment, auto SelectBackend)
			{
				Gui::FScriptServices Runtime;
				Runtime.ReadResource = Gui::MakeGuiResourceReader(Environment);
				Runtime.Application = Gui::MakeRegisteredServiceFactories(std::move(Providers), std::move(Environment));
				Runtime.DefaultBackend = std::move(SelectBackend);
				GuiTools.SetRuntimeServices(std::move(Runtime));
			});
			#else
			ConfigurePackageApplicationServices(Services, Installed, Options);
			#endif
			#else
			if (!Environment.empty())
			{
				throw std::runtime_error("this host has no package runtime adapter");
			}
			#endif
			#if defined(VEROSCRIPT_APPLICATION_BOOTSTRAP)
			if (File.empty())
			{
				File = SelfPath();
			}
			#endif
			if (File.empty())
			{
				throw std::runtime_error("usage: veroscript-runtime [options] application.vsapp [-- args...]");
			}
			std::shared_ptr<const FApplication> App;
			FDiagnostic E;
			const auto P = PathUtil::FromUtf8(File);
			auto Extension = PathUtil::ToUtf8(P.extension());
			std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C)
			{
				return static_cast<char>(std::tolower(C));
			});
			if (Extension == ".vsapp" || Extension == ".zip" || fs::is_directory(P) || P.filename() == "veroscript-app.json")
			{
				App = FApplication::Open(P.filename() == "veroscript-app.json" ? PathUtil::ToUtf8(P.parent_path()) : File, Services, E);
			}
			else
			{
				std::vector<std::uint8_t> Bytes;
				if (ReadExecutableApplication(File, Bytes, E))
				{
					App = FApplication::Open(std::move(Bytes), Services, E);
				}
				#if defined(VEROSCRIPT_APPLICATION_BOOTSTRAP)
				else if (fs::exists(P.parent_path() / "veroscript-app.json"))
				{
					File = PathUtil::ToUtf8(P.parent_path());
					App = FApplication::Open(File, Services, E);
				}
				#endif
			}
			if (!App)
			{
				throw std::runtime_error(E.Code + ": " + E.Message);
			}
			std::string RuntimeExe;
			std::shared_ptr<const void> RuntimeLease;
			#if defined(VEROSCRIPT_APPLICATION_BOOTSTRAP)
			std::vector<std::string> RuntimeStdlib;
			if (!App->ResolveRuntime(Runtime, RuntimeExe, RuntimeLease, E, &RuntimeStdlib))
			{
				throw std::runtime_error(E.Code + ": " + E.Message);
			}
			if (RuntimeExe.empty())
			{
				throw std::runtime_error("external Runtime requires --runtime <component-directory>");
			}
			if (Delegated)
			{
				throw std::runtime_error("a bootstrap cannot delegate twice");
			}
			FProcessRequest Process;
			Process.Executable = RuntimeExe;
			Process.WorkingDirectory = PathUtil::ToUtf8(fs::current_path());
			Process.Arguments = {"--application-file", File, "--application-delegated"};
			if (Check)
			{
				Process.Arguments.push_back("--check");
			}
			if (!Crypto.empty())
			{
				Process.Arguments.insert(Process.Arguments.end(), {"--crypto-library", Crypto, "--keyring", Keyring});
			}
			if (RequireSignature)
			{
				Process.Arguments.push_back("--require-signature");
			}
			if (!Environment.empty())
			{
				Process.Arguments.insert(Process.Arguments.end(), {"--path", Environment});
			}
			for (const auto& Backend : Backends)
			{
				if (Backend.second.empty())
				{
					continue;
				}
				Process.Arguments.insert(Process.Arguments.end(), {"--" + Backend.first + "-backend", Backend.second});
			}
			for (const auto& Root : RuntimeStdlib)
			{
				Process.Arguments.insert(Process.Arguments.end(), {"--stdlib-path", Root});
			}
			if (!VmPath.empty())
			{
				Process.Arguments.insert(Process.Arguments.end(), {"--vm-backend", VmPath});
			}
			if (!JitPath.empty())
			{
				Process.Arguments.insert(Process.Arguments.end(), {"--jit-backend", JitPath});
			}
			Process.Arguments.insert(Process.Arguments.end(), {"--application-cache", Services.CacheRoot, "--"});
			Process.Arguments.insert(Process.Arguments.end(), ScriptArgs.begin(), ScriptArgs.end());
			Process.InheritEnvironment = true;
			Process.TimeoutMilliseconds = 0;
			Process.MaxOutputBytes = 0;
			Process.Output = [](std::string_view Text, bool Error)
			{
				auto& Stream = Error ? std::cerr : std::cout;
				Stream.write(Text.data(), static_cast<std::streamsize>(Text.size()));
				Stream.flush();
			};
			int Code;
			if (!RunProcess(Process, Code, E))
			{
				throw std::runtime_error(E.Code + ": " + E.Message);
			}
			return Code;
			#else
			(void)Delegated;
			(void)Runtime;
			FStreamEngineHost Host(std::cout, std::cerr);
			VeroScriptSession Session(Host);
			FEngineConfig Config;
			if (VmExplicit)
			{
				Config.VmBackendPath = VmPath;
			}
			else
			{
				Config.FallbackVmBackendPath = VmPath;
			}
			Config.bClearVmBackend = Backends.count("vm") && Backends.at("vm") == DisabledBackend;
			Config.bDisableJitBackend = Backends.count("jit") && Backends.at("jit") == DisabledBackend;
			std::vector<std::string_view> Views;
			if (!JitPath.empty())
			{
				std::string Error;
				Jit = LoadJitBackend(JitPath, Error);
				if (!Jit)
				{
					throw std::runtime_error(Error);
				}
				Config.JitBackendFactory = Jit;
			}
			for (const auto& A : ScriptArgs)
			{
				Views.emplace_back(A);
			}
			Config.ScriptArgs = Views;
			Config.ModuleEnvironment = Services.Environment;
			Session.ApplyConfig(Config);
			#if defined(VEROSCRIPT_WITH_GUI)
			// The environment adapter borrows GuiBindings until application preparation completes.
			Session.GetBindingRegistry() = GuiBindings;
			#endif
			const auto Loaded = Session.LoadApplication(App);
			if (!Loaded.bSuccess)
			{
				throw std::runtime_error(Loaded.Error.Message);
			}
			for (const auto* Role : {"vm", "jit"})
			{
				const auto Found = Backends.find(Role);
				if (Found == Backends.end() || Found->second.empty() || Found->second == AutomaticBackend || Found->second == DisabledBackend)
				{
					continue;
				}
				const auto Environment = Session.GetModuleEnvironment();
				if (!Environment || (std::string_view(Role) == "vm" ? !Environment->GetDescription().ExecutionBackends.Vm : !Environment->GetDescription().ExecutionBackends.Jit))
				{
					throw std::runtime_error("backend ID requires an available package environment: " + Found->second);
				}
				std::string Error;
				if (!(std::string_view(Role) == "vm" ? Session.SetVmBackendPath(Found->second, Error)
				                                     : Session.SetJitBackendPath(Found->second, Error)))
				{
					throw std::runtime_error(Error);
				}
			}
			return Check ? 0 : Session.Run().ExitCode;
			#endif
		}
		catch (const std::exception& E)
		{
			std::cerr << "VeroScript application: " << E.what() << '\n';
			return 2;
		}
	}
}

#if defined(_WIN32)
int wmain(int Count, wchar_t** Values)
{
	std::vector<std::string> Args;
	for (int I = 0; I < Count; ++I)
	{
		const int N = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Values[I], -1, nullptr, 0, nullptr, nullptr);
		if (N <= 0) return 2;
		std::string A(static_cast<std::size_t>(N), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Values[I], -1, A.data(), N, nullptr, nullptr);
		A.pop_back();
		Args.push_back(std::move(A));
	}
	return Main(Args);
}
#else
int main(int Count, char** Values)
{
	return Main({Values, Values + Count});
}
#endif
