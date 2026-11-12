// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCliExtensions.h"
#include "VeroScriptPath.h"
#include <algorithm>
#include <filesystem>
#include <ostream>
#include <mutex>
#include <stdexcept>

namespace VeroScript::Cli
{
	struct FCommandLineExtensions::FServiceState
	{
		std::mutex Mutex;
		FCommandLineExtensions* Target = nullptr;
	};

	class FCommandLineExtensions::FService final : public ICommandLineExtensionService
	{
	public:
		bool Register(FCommandLineModule D, std::string& Error) const override
		{
			auto S = State.lock();
			if (!S)
			{
				Error = "CLI invocation has ended";
				return false;
			}
			std::lock_guard<std::mutex> Lock(S->Mutex);
			if (!S->Target)
			{
				Error = "CLI invocation has ended";
				return false;
			}
			return S->Target->Register(std::move(D), Error);
		}

		explicit FService(const std::shared_ptr<FServiceState>& S) : State(S)
		{
		}

	private:
		std::weak_ptr<FServiceState> State;
	};

	namespace
	{
		bool Name(const std::string& S)
		{
			return !S.empty() && S.size() <= 64 && S.front() != '-' && S.back() != '-' &&
			    std::all_of(S.begin(), S.end(), [](char C)
			{
				return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
				    (C >= '0' && C <= '9') || C == '-' || C == '_';
			});
		}
		bool Text(const std::string& S)
		{
			return S.size() <= 8192 && std::none_of(S.begin(), S.end(), [](unsigned char C)
			{
				return C < 32 || C == 127;
			});
		}
		const std::set<std::string> ReservedCommands = {
		    "aot", "bindings", "package", "project", "compiler-package", "install", "uninstall", "enable", "disable",
		    "list", "verify", "clean", "env", "search", "show", "pack", "publish", "deploy"};
		const std::set<std::string> ReservedOptions = {
		    "allow-circular-imports", "allow-project-wide-access", "backend", "bc-cache", "bc-cache-dir",
		    "binding-catalog", "bundle-name", "check", "cli-module", "compiler-config", "compiler-extension",
		    "compiler-instructions", "compiler-timeout", "dap-bind", "dap-port", "debug", "define", "emit-package",
		    "eval", "help", "implicit-import", "import-path", "interactive", "jit-backend", "vm-backend", "lang", "macro",
		    "manifest", "no-circular-imports", "no-compiler-imports", "output", "output-dir", "package-file",
		    "package-version", "predefined-macro", "repl-strict", "stdlib-path", "strip-package-debug", "target",
		    "version", "watch", "watch-once", "path", "root", "allow-hooks", "allow-http", "ca-file",
		    "cache-dir", "credential-env", "credential-scope", "development", "fallback-lang", "frozen",
		    "input", "json", "locked", "lockfile", "mirror", "non-interactive", "offline", "packages-dir",
		    "prerelease", "project", "proxy", "registry-api", "requirement", "secret-env", "skip-hooks",
		    "timeout-ms", "upgrade", "user"};
	}

	bool FCommandLineExtensions::Register(FCommandLineModule D, std::string& Error) const
	{
		Error.clear();
		auto Fail = [&](const std::string& Message)
		{
			Error = Message;
			if (Failure.empty())
			{
				Failure = Error;
			}

			return false;
		};
		if (!Open)
		{
			Error = "CLI registration is closed";
			return false;
		}
		if (!Name(D.Id) || !Text(D.Description) || Modules.count(D.Id) || Modules.size() >= 256)
		{
			return Fail("invalid or duplicate CLI module identity: " + D.Id);
		}

		if (D.Commands.size() + D.Options.size() > 256 || (D.Commands.empty() && D.Options.empty()) ||
		    (!D.Options.empty() && !D.Configure))
		{
			return Fail("invalid CLI module declarations: " + D.Id);
		}

		std::set<std::string> Commands, Options;
		for (const auto& M : Modules)
		{
			for (const auto& C : M.second.Commands)
			{
				Commands.insert(C.Name);
			}

			for (const auto& O : M.second.Options)
			{
				Options.insert(O.Name);
			}
		}
		for (const auto& C : D.Commands)
		{
			if (!Name(C.Name) || !C.Run || !Text(C.Description) || !Text(C.Usage) ||
			    ReservedCommands.count(C.Name) || !Commands.insert(C.Name).second)
			{
				return Fail("invalid, reserved or duplicate CLI command: " + C.Name);
			}
		}

		for (const auto& O : D.Options)
		{
			if (!Name(O.Name) || !Text(O.Description) || !Text(O.ValueName) ||
			    ReservedOptions.count(O.Name) || !Options.insert(O.Name).second)
			{
				return Fail("invalid, reserved or duplicate CLI option: --" + O.Name);
			}
		}

		Modules.emplace(D.Id, std::move(D));
		return true;
	}

	FCommandLineExtensions::FCommandLineExtensions() : ServiceState(std::make_shared<FServiceState>())
	{
		ServiceState->Target = this;
	}

	FCommandLineExtensions::~FCommandLineExtensions()
	{
		{
			std::lock_guard<std::mutex> Lock(ServiceState->Mutex);
			Open = false;
			ServiceState->Target = nullptr;
		}
		Modules.clear();
		Registry.reset();
		Owners.clear();
	}

	bool FCommandLineExtensions::Load(const std::string& Path, const std::string& WorkingDirectory, std::string& Error)
	{
		try
		{
			auto P = std::filesystem::weakly_canonical(PathUtil::FromUtf8(WorkingDirectory) / PathUtil::FromUtf8(Path));
			if (!Paths.insert(PathUtil::ToUtf8(P)).second)
			{
				Error = "duplicate --cli-module path";
				return false;
			}
			auto Module = LoadNativeExtension(PathUtil::ToUtf8(P), Error);
			if (!Module) return false;
			if (!(Module->GetDescriptor().Roles & NativeExtensionRole(ENativeExtensionRole::CommandLine)))
			{
				Error = "--cli-module requires a module declaring the cli role";
				return false;
			}
			if (!Registry)
			{
				Registry = std::make_unique<FNativeExtensionRegistry>();
			}

			FNativeExtensionRegistrationOptions O;
			O.Origin.ProviderId = std::string("cli:") + Module->GetDescriptor().Id;
			O.Services.push_back(Binding());
			O.Services.insert(O.Services.end(), Services.begin(), Services.end());
			if (!Registry->Add(std::move(Module), std::move(O), Error)) return false;
			Owners = Registry->GetOwners();
			if (!Failure.empty())
			{
				Error = Failure;
				return false;
			}
			return true;
		}
		catch (const std::exception& X)
		{
			Error = X.what();
			return false;
		}
	}

	FNativeExtensionServiceBinding FCommandLineExtensions::Binding() const
	{
		FNativeExtensionServiceBinding B;
		B.Id = CommandLineServiceId;
		B.Version = CommandLineServiceVersion;
		// The proxy holds no module/registry owner and safely rejects an expired invocation.
		B.Service = std::make_shared<FService>(ServiceState);
		return B;
	}

	int FCommandLineExtensions::ParseOption(std::string_view Arg, std::string_view Next, bool HasNext, std::string& Error)
	{
		if (Arg.rfind("--", 0) != 0) return 0;
		const auto Equal = Arg.find('=');
		const std::string Key(Arg.substr(2, Equal == Arg.npos ? Arg.size() - 2 : Equal - 2));
		for (const auto& M : Modules)
		{
			for (const auto& O : M.second.Options)
			{
				if (O.Name != Key)
				{
					continue;
				}

				auto& List = Values[M.first][Key];
				if (!O.Repeatable && !List.empty())
				{
					Error = "duplicate CLI option: --" + Key;
					return -1;
				}
				if (!O.RequiresValue && Equal != Arg.npos)
				{
					Error = "CLI flag does not accept a value: --" + Key;
					return -1;
				}
				const bool NextIsOption = Next.rfind("--", 0) == 0 || Next == "-h" || Next == "-?" ||
				    Next == "-e" || Next == "-i" || Next == "-I" || Next.rfind("-D", 0) == 0;
				if (O.RequiresValue && Equal == Arg.npos && (!HasNext || NextIsOption))
				{
					Error = "missing value for --" + Key + "; use --" + Key + "=<value> for an option-like value";
					return -1;
				}
				List.emplace_back(O.RequiresValue ? (Equal == Arg.npos ? Next : Arg.substr(Equal + 1)) : std::string_view{});
				return O.RequiresValue && Equal == Arg.npos ? 2 : 1;
			}
		}

		return 0;
	}

	bool FCommandLineExtensions::Configure(const FCommandLineContext& C, std::string& Error)
	{
		Close();
		if (!Failure.empty())
		{
			Error = Failure;
			return false;
		}
		if (Configured) return true;
		try
		{
			for (const auto& M : Modules)
			{
				if (M.second.Configure && !M.second.Configure(C, Values[M.first], M.second.UserData, Error))
				{
					if (Error.empty())
					{
						Error = "CLI module configuration failed: " + M.first;
					}
					return false;
				}
			}

			Configured = true;
			return Failure.empty() || (Error = Failure, false);
		}
		catch (const std::exception& X)
		{
			Error = X.what();
			return false;
		}
		catch (...)
		{
			Error = "CLI module configuration threw an exception";
			return false;
		}
	}

	bool FCommandLineExtensions::RunCommand(const std::string& Name, const std::vector<std::string>& Args,
	    const FCommandLineContext& Context, int& ExitCode, std::string& Error)
	{
		for (const auto& M : Modules)
		{
			for (const auto& C : M.second.Commands)
			{
				if (C.Name != Name)
				{
					continue;
				}

				if (Args.size() == 1 && (Args[0] == "--help" || Args[0] == "-h"))
				{
					*Context.Output << C.Description << '\n'
					                << "Usage: veroscript " << C.Name << ' ' << C.Usage << '\n';
					ExitCode = 0;
					return true;
				}
				if (!Configure(Context, Error))
				{
					ExitCode = 2;
					return true;
				}
				try
				{
					ExitCode = C.Run(Context, Args, C.UserData);
				}
				catch (const std::exception& X)
				{
					Error = X.what();
					ExitCode = 1;
				}
				catch (...)
				{
					Error = "CLI command threw an exception: " + C.Name;
					ExitCode = 1;
				}
				if (ExitCode < 0 || ExitCode > 255)
				{
					Error = "CLI command returned an exit code outside 0..255";
					ExitCode = 1;
				}
				return true;
			}
		}

		return false;
	}

	void FCommandLineExtensions::PrintHelp(std::ostream& Out) const
	{
		if (Modules.empty()) return;
		Out << "\nModule commands and options:\n";
		for (const auto& M : Modules)
		{
			for (const auto& C : M.second.Commands)
			{
				Out << "  " << C.Name << "  " << C.Description << " [" << M.first << "]\n";
			}

			for (const auto& O : M.second.Options)
			{
				Out << "  --" << O.Name << (O.RequiresValue ? " <" + (O.ValueName.empty() ? "value" : O.ValueName) + ">" : "")
				    << "  " << O.Description << " [" << M.first << "]\n";
			}
		}
	}

	bool FCommandLineExtensions::HasCommand(const std::string& Name) const
	{
		for (const auto& M : Modules)
		{
			for (const auto& C : M.second.Commands)
			{
				if (C.Name == Name) return true;
			}
		}

		return false;
	}

	bool FCommandLineExtensions::HasOptions() const
	{
		return !Values.empty();
	}

	const FCommandLineValues& FCommandLineExtensions::GetValues(const std::string& ModuleId) const
	{
		const auto Found = Values.find(ModuleId);
		static const FCommandLineValues Empty;
		return Found == Values.end() ? Empty : Found->second;
	}

	bool FCommandLineExtensions::Validate(std::string& Error) const
	{
		std::lock_guard<std::mutex> Lock(ServiceState->Mutex);
		Error = Failure;
		return Error.empty();
	}

	void FCommandLineExtensions::Close()
	{
		std::lock_guard<std::mutex> Lock(ServiceState->Mutex);
		Open = false;
	}
}
