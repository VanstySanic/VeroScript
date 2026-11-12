// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>

namespace VeroScript::Packages
{
	using namespace Detail;
	namespace
	{
		constexpr const char* DefaultOfficialApi = "https://veroscript.org/api";
		const std::map<std::string, EOperation> Commands = {
		    {"install", EOperation::Install}, {"uninstall", EOperation::Uninstall},
		    {"enable", EOperation::Enable}, {"disable", EOperation::Disable},
		    {"list", EOperation::List}, {"show", EOperation::Show},
		    {"search", EOperation::Search}, {"verify", EOperation::Verify},
		    {"pack", EOperation::Pack}, {"publish", EOperation::Publish},
		    {"deploy", EOperation::Deploy}, {"clean", EOperation::Clean}};
		std::string Text(std::string S)
		{
			std::string Result;
			for (std::size_t I = 0; I < S.size(); ++I)
			{
				auto C = static_cast<unsigned char>(S[I]);
				if (C == 0xc2 && I + 1 < S.size() && static_cast<unsigned char>(S[I + 1]) >= 0x80 &&
				    static_cast<unsigned char>(S[I + 1]) <= 0x9f)
				{
					++I;
					continue;
				}
				if (C == 127 || (C < 32 && C != '\n' && C != '\t'))
				{
					continue;
				}
				Result += S[I];
			}
			return Result;
		}
		std::string SafeJson(const std::string& S)
		{
			static const char Hex[] = "0123456789abcdef";
			std::string Result;
			for (std::size_t I = 0; I < S.size(); ++I)
			{
				auto C = static_cast<unsigned char>(S[I]);
				if (C == 0xc2 && I + 1 < S.size())
				{
					auto Next = static_cast<unsigned char>(S[I + 1]);
					if (Next >= 0x80 && Next <= 0x9f)
					{
						Result += "\\u00";
						Result += Hex[Next >> 4];
						Result += Hex[Next & 15];
						++I;
						continue;
					}
				}
				Result += S[I];
			}
			return Result;
		}

		J Diagnostic(const FDiagnostic& E)
		{
			return {{"code", E.Code},
			    {"stage", E.Stage},
			    {"message", E.Message},
			    {"package", E.Package},
			    {"source", E.Source},
			    {"field", E.Field},
			    {"dependencyChain", E.DependencyChain}};
		}
		FEnvironmentInput SelectEnvironment(const FEnvironmentSelection& Selection,
		    const std::string& Startup, const std::string& Executable)
		{
			FEnvironmentInput Selected;
			Selected.WorkingDirectory = NativePathText(Absolute(Startup, {}));
			Selected.Root = Selection.Path.empty()
			    ? Selected.WorkingDirectory
			    : NativePathText(Absolute(Selection.Path, Selected.WorkingDirectory));
			if (Selection.ComponentRoot)
			{
				FDiagnostic E;
				bool Found = false;
				auto ComponentPath = Selected.Root;
				bool Ok = Selection.Path.empty() ? DiscoverComponent(Executable, Selected, Found, E)
				                                 : DiscoverComponentRoot(ComponentPath, Selected, Found, E);
				if (!Ok)
				{
					throw FFailure(E.Code, E.Message, E.Field);
				}
				if (!Found)
				{
					Fail("component-missing", "--root requires a verified Runtime or SDK component");
				}
				Selected.WorkingDirectory = NativePathText(Absolute(Startup, {}));
			}
			return Selected;
		}
		bool MaybeLoad(FPackageManager& M, FEnvironmentInput I, const FClientOptions& O,
		    std::shared_ptr<const FInstalledEnvironment>& Out, FDiagnostic& E)
		{
			FEnvironmentLayout L;
			if (!ResolveEnvironment(I, L, E))
			{
				return false;
			}
			if (!fs::exists(U8Path(L.State)))
			{
				if (fs::exists(U8Path(L.Management) / "transaction.json"))
				{
					Fail("recovery-required", "installation transaction needs writer recovery");
				}
				if (fs::exists(U8Path(L.Requirements)))
				{
					auto P =
					    Project(Parse(Read(U8Path(L.Requirements), O.Limits.MaxManifestBytes), O.Limits));
					if (!P.value("dependencies", J::object()).empty())
					{
						Fail("environment-uninstalled",
						    "declared packages are not installed; run install explicitly");
					}
				}
				Out.reset();
				return true;
			}
			return M.Load(I, Out, E, O);
		}
	} // namespace
	bool IsPackageCommand(std::string_view S)
	{
		return S == "env" || Commands.count(std::string(S)) != 0;
	}
	bool ParseEnvironmentOption(std::string_view Option, std::string_view Next,
	    FEnvironmentSelection& Selection, unsigned& Consumed, FDiagnostic& E)
	{
		Consumed = 0;
		E = {};
		try
		{
			if (Option == "--root" || Option.rfind("--root=", 0) == 0)
			{
				if (Option != "--root")
				{
					Fail("option-value", "--root accepts no value; use --path for a directory");
				}
				if (Selection.ComponentRoot)
				{
					Fail("environment-selector", "duplicate --root selector");
				}
				Selection.ComponentRoot = true;
				Consumed = 1;
			}
			else if (Option == "--path" || Option.rfind("--path=", 0) == 0)
			{
				if (!Selection.Path.empty())
				{
					Fail("environment-selector", "duplicate --path selector");
				}
				auto Path = Option == "--path" ? Next : Option.substr(7);
				if (Path.empty() || (Option == "--path" && Path.front() == '-'))
				{
					Fail("option-value", "--path requires a directory");
				}
				Selection.Path = std::string(Path);
				Consumed = Option == "--path" ? 2 : 1;
			}
			else if (Option == "--project" || Option.rfind("--project=", 0) == 0 || Option == "--user" ||
			    Option.rfind("--user=", 0) == 0)
			{
				Fail("unknown-option", "unknown environment option", std::string(Option));
			}
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			return false;
		}
	}
	bool ResolveRunEnvironment(const FRunEnvironmentRequest& R,
	    std::shared_ptr<const FInstalledEnvironment>& Out, FDiagnostic& E)
	{
		try
		{
			Out.reset();
			E = {};
			FPackageManager M;
			if (R.Selection.ComponentRoot || !R.Selection.Path.empty())
			{
				return MaybeLoad(M, SelectEnvironment(R.Selection, R.StartupDirectory, R.Executable),
				    R.Options, Out, E);
			}
			auto Startup = Absolute(R.StartupDirectory, {});
			auto Directory =
			    R.EntryFile.empty() ? Startup : Absolute(R.EntryFile, NativePathText(Startup)).parent_path();
			auto ProjectRoot = FindProjectRoot(Directory, R.WorkspaceBoundary);
			FEnvironmentInput Selected;
			Selected.WorkingDirectory = NativePathText(Startup);
			if (!ProjectRoot.empty())
			{
				Selected.Root = ProjectRoot;
				return MaybeLoad(M, Selected, R.Options, Out, E);
			}
			bool Found = false;
			if (!DiscoverComponent(R.Executable, Selected, Found, E))
			{
				return false;
			}
			if (!Found)
			{
				return true;
			}
			Selected.WorkingDirectory = NativePathText(Startup);
			return MaybeLoad(M, Selected, R.Options, Out, E);
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			Out.reset();
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "environment-selection";
			E.Message = X.what();
			Out.reset();
			return false;
		}
	}
	int RunPackageCommand(const std::vector<std::string>& Args, const std::string& Startup,
	    const std::string& Executable, std::ostream& Output, std::ostream& Errors,
	    const std::function<std::shared_ptr<const IArtifactDecoder>(const std::string&, FDiagnostic&)>& ProtectionFactory)
	{
		bool Json = false;
		try
		{
			if (Args.empty() || !IsPackageCommand(Args.front()))
			{
				Fail("command", "unknown package command");
			}
			FRequest Q;
			Q.Environment.WorkingDirectory = Startup;
			Q.Environment.Root = Startup;
			Q.Options.NonInteractive = true;
			Q.Options.OfficialApi = DefaultOfficialApi;
			FEnvironmentSelection Selection;
			bool End = false, Env = Args.front() == "env";
			std::string CredentialEnv, CredentialScope;
			std::string ProtectionOptions;
			if (!Env)
			{
				Q.Operation = Commands.at(Args.front());
			}
			for (std::size_t I = 1; I < Args.size(); ++I)
			{
				const auto& A = Args[I];
				if (!End && A == "--")
				{
					End = true;
					continue;
				}
				if (!End && A.rfind("-r", 0) == 0)
				{
					if (Env || Q.Operation != EOperation::Install)
					{
						Fail("argument-conflict", "requirements files are only valid for install");
					}
					auto File = A.substr(2);
					if (File.empty())
					{
						if (I + 1 == Args.size() || Args[I + 1].empty() || Args[I + 1].front() == '-')
						{
							Fail("option-value", "-r requires a file path");
						}
						File = Args[++I];
					}
					Q.RequirementFiles.push_back(std::move(File));
					continue;
				}
				if (End || A.rfind("--", 0) != 0)
				{
					Q.Arguments.push_back(A);
					continue;
				}
				auto Eq = A.find('=');
				auto Key = A.substr(0, Eq);
				auto Value = Eq == std::string::npos ? std::string{} : A.substr(Eq + 1);
				const auto Get = [&]()
				{
					if (Eq != std::string::npos)
					{
						if (Value.empty())
						{
							Fail("option-value", "empty option value", Key);
						}
						return Value;
					}
					if (I + 1 == Args.size() || Args[I + 1].rfind("--", 0) == 0)
					{
						Fail("option-value", "option requires a value", Key);
					}
					return Args[++I];
				};
				const auto Flag = [&]()
				{
					if (Eq != std::string::npos)
					{
						Fail("option-value", "flag accepts no value", Key);
					}
				};
				unsigned Consumed = 0;
				FDiagnostic SelectionError;
				if (!ParseEnvironmentOption(
				        A, I + 1 < Args.size() ? std::string_view(Args[I + 1]) : std::string_view{},
				        Selection, Consumed, SelectionError))
				{
					throw FFailure(SelectionError.Code, SelectionError.Message, SelectionError.Field);
				}
				if (Consumed)
				{
					I += Consumed - 1;
					continue;
				}
				if (Key == "--requirement")
				{
					if (Env || Q.Operation != EOperation::Install)
					{
						Fail("argument-conflict", "requirements files are only valid for install");
					}
					auto File = Get();
					if (File.empty() || (Eq == std::string::npos && File.front() == '-'))
					{
						Fail("option-value", "--requirement requires a file path");
					}
					Q.RequirementFiles.push_back(std::move(File));
				}
				else if (Key == "--json")
				{
					Flag();
					Json = true;
				}
				else if (Key == "--offline")
				{
					Flag();
					Q.Options.Offline = true;
				}
				else if (Key == "--locked" || Key == "--frozen")
				{
					Flag();
					Q.Options.Locked = true;
				}
				else if (Key == "--upgrade")
				{
					Flag();
					Q.Options.Upgrade = true;
				}
				else if (Key == "--prerelease")
				{
					Flag();
					Q.Options.AllowPrerelease = true;
				}
				else if (Key == "--development")
				{
					Flag();
					Q.Options.Development = true;
				}
				else if (Key == "--allow-hooks")
				{
					Flag();
					Q.Options.AllowHooks = true;
				}
				else if (Key == "--skip-hooks")
				{
					Flag();
					Q.Options.SkipHooks = true;
				}
				else if (Key == "--allow-http")
				{
					Flag();
					Q.Options.AllowHttp = true;
				}
				else if (Key == "--non-interactive")
				{
					Flag();
				}
				else if (Key == "--packages-dir")
				{
					Q.Environment.PackagesDirectory = Get();
				}
				else if (Key == "--cache-dir")
				{
					Q.Environment.CacheDirectory = Get();
				}
				else if (Key == "--lockfile")
				{
					Q.Environment.Lockfile = Get();
				}
				else if (Key == "--output")
				{
					Q.OutputPath = Get();
				}
				else if (Key == "--mirror")
				{
					Q.Options.Mirror = Get();
				}
				else if (Key == "--application-options")
				{
					if (!ProtectionOptions.empty())
					{
						Fail("duplicate-option", "duplicate protection options file");
					}
					ProtectionOptions = NativePathText(Absolute(Get(), Startup));
				}
				else if (Key == "--proxy")
				{
					Q.Options.Proxy = Get();
				}
				else if (Key == "--ca-file")
				{
					Q.Options.CaFile = NativePathText(Absolute(Get(), Startup));
				}
				else if (Key == "--credential-env")
				{
					CredentialEnv = Get();
				}
				else if (Key == "--credential-scope")
				{
					CredentialScope = Get();
				}
				else if (Key == "--timeout-ms")
				{
					auto V = Get();
					std::uint64_t N = 0;
					auto P = std::from_chars(V.data(), V.data() + V.size(), N);
					if (P.ec != std::errc{} || P.ptr != V.data() + V.size() || N > std::uint64_t(INT64_MAX))
					{
						Fail("option-value", "invalid timeout");
					}
					Q.Options.Limits.TimeoutMilliseconds = N;
				}
				else if (Key == "--registry-api")
				{
					Q.Options.OfficialApi = Get();
				}
				else if (Key == "--stdlib-path")
				{
					Q.Options.StdlibSearchPaths.push_back(NativePathText(Absolute(Get(), Startup)));
				}
				else if (Key == "--lang")
				{
					Q.Options.DisplayConfiguration.Language = Get();
				}
				else if (Key == "--fallback-lang")
				{
					Q.Options.DisplayConfiguration.FallbackLanguages.push_back(Get());
				}
				else if (Key == "--target")
				{
					Q.Environment.Target = ReadTarget(Parse(
					    Read(Absolute(Get(), Startup), Q.Options.Limits.MaxManifestBytes), Q.Options.Limits));
				}
				else if (Key == "--input" || Key == "--secret-env")
				{
					auto S = Get();
					auto Sep = S.find('=');
					if (Sep == std::string::npos || !Sep)
					{
						Fail("option-value", "input expects name=value", Key);
					}
					auto Name = S.substr(0, Sep), V = S.substr(Sep + 1);
					if (Key == "--input")
					{
						Q.Options.Inputs[Name] = V;
					}
					else
					{
						const char* Secret = std::getenv(V.c_str());
						if (!Secret)
						{
							Fail("secret-environment", "secret environment variable is absent", Name);
						}
						Q.Options.Secrets[Name] = J(std::string(Secret)).dump();
					}
				}
				else if (Key == "--help")
				{
					Output
					    << "veroscript "
					       "<install|uninstall|enable|disable|list|show|search|verify|pack|publish|deploy|"
					       "clean|env> [items]\n  --root; --path <directory>; --json; --offline; --locked; "
					       "--upgrade\n  --packages-dir=<path>; --cache-dir=<path>; --lockfile=<path>\n  "
					       "-r <file>; --requirement=<file> (install only, repeatable)\n  "
					       "--registry-api=<https-url>; --mirror=<https-url>; --proxy=<url>; "
					       "--ca-file=<file>\n  --credential-env=<name>; --credential-scope=<url>; "
					       "--timeout-ms=<n>\n  --allow-hooks; --skip-hooks (uninstall only); "
					       "--input=name=<json>; --secret-env=name=ENV\n  --output=<path>; --lang=<tag>; "
					       "--stdlib-path=<directory>; --target=<target-json>\n"
					    << "  --application-options=<file>  Explicit distribution trust service options\n"
					    << "  Default official API: " << DefaultOfficialApi << '\n';
					return 0;
				}
				else
				{
					Fail("unknown-option", "unknown package option", Key);
				}
			}
			if (!ProtectionOptions.empty())
			{
				if (!ProtectionFactory)
				{
					Fail("protection-service", "this command host does not provide application protection services");
				}
				FDiagnostic E;
				Q.Options.ArtifactDecoder = ProtectionFactory(ProtectionOptions, E);
				if (!Q.Options.ArtifactDecoder)
				{
					Fail("protection-service", E.Message.empty() ? "no artifact trust service was configured" : E.Message);
				}
			}
			if (!CredentialEnv.empty())
			{
				if (CredentialScope.empty())
				{
					CredentialScope = Q.Options.OfficialApi;
				}
				if ((CredentialScope.rfind("https://", 0) != 0 &&
				        !(Q.Options.AllowHttp && CredentialScope.rfind("http://", 0) == 0)) ||
				    CredentialScope.find_first_of("?#@") != std::string::npos)
				{
					Fail("credential-scope", "credentials require an explicit trusted API scope");
				}
				while (!CredentialScope.empty() && CredentialScope.back() == '/')
				{
					CredentialScope.pop_back();
				}
				const char* Value = std::getenv(CredentialEnv.c_str());
				if (!Value)
				{
					Fail("credential-environment", "registry credential environment variable is absent");
				}
				std::string Token = Value;
				Q.Options.Credential = [Scope = CredentialScope,
				                           Token = std::move(Token)](const std::string& Url)
				{
					return Url == Scope || Url.rfind(Scope + "/", 0) == 0 || Url.rfind(Scope + "?", 0) == 0
					    ? Token
					    : std::string{};
				};
			}
			else if (!CredentialScope.empty())
			{
				Fail("credential-scope", "--credential-scope requires --credential-env");
			}
			auto Selected = SelectEnvironment(Selection, Startup, Executable);
			Selected.CacheDirectory = Q.Environment.CacheDirectory;
			Selected.PackagesDirectory = Q.Environment.PackagesDirectory;
			Selected.Lockfile = Q.Environment.Lockfile;
			if (!Q.Environment.Target.Core.empty())
			{
				Selected.Target = Q.Environment.Target;
			}
			Q.Environment = std::move(Selected);
			if (Q.Options.StdlibSearchPaths.empty())
			{
				auto Bin = Absolute(Executable, {}).parent_path();
				for (auto P : {Bin / "stdlib", Bin.parent_path() / U8Path("assets/stdlib")})
				{
					if (fs::is_directory(P))
					{
						Q.Options.StdlibSearchPaths.push_back(NativePathText(P));
					}
				}
			}
			Q.Options.Message = [&](const std::string& S)
			{
				(Json ? Errors : Output) << Text(S) << '\n';
			};
			if (Env)
			{
				if (!Q.Arguments.empty())
				{
					Fail("arguments", "env accepts no positional arguments");
				}
				FEnvironmentLayout L;
				FDiagnostic E;
				if (!ResolveEnvironment(Q.Environment, L, E))
				{
					throw FFailure(E.Code, E.Message);
				}
				FWork Work{Q.Options};
				ConfigureStorage(L, Q.Environment, LoadRequirements(L, Work, true), LoadState(L, Work));
				Output << J{{"formatVersion", FormatVersion},
				              {"scope", L.Scope == EEnvironmentScope::Project ? "project" : L.Scope == EEnvironmentScope::Sdk ? "sdk"
				                                                                                                              : "runtime"},
				              {"root", L.Root},
				              {"requirements", L.Requirements},
				              {"lockfile", L.Lockfile},
				              {"packagesDirectory", L.Packages},
				              {"cache", L.Cache},
				              {"target", TargetJson(L.Target)}}
				              .dump(2)
				       << '\n';
				return 0;
			}
			FPackageManager M;
			FResult R;
			FDiagnostic E;
			if (!M.Execute(Q, R, E))
			{
				if (Json)
				{
					Errors << J{{"formatVersion", FormatVersion}, {"error", Diagnostic(E)}}.dump() << '\n';
				}
				else
				{
					Errors << "VeroScript packages [" << Text(E.Code) << "] " << Text(E.Message)
					       << (E.Field.empty() ? "" : " (" + Text(E.Field) + ")") << '\n';
				}
				return 1;
			}
			Output << SafeJson(R.Json) << '\n';
			for (const auto& W : R.Warnings)
			{
				Errors << "VeroScript packages [" << Text(W.Code) << "] " << Text(W.Message) << '\n';
			}
			return Output ? 0 : 1;
		}
		catch (const FFailure& X)
		{
			if (Json)
			{
				Errors << J{{"formatVersion", FormatVersion}, {"error", Diagnostic(X.Diagnostic)}}.dump()
				       << '\n';
			}
			else
			{
				Errors << "VeroScript packages [" << Text(X.Diagnostic.Code) << "] " << Text(X.what())
				       << '\n';
			}
			return 2;
		}
		catch (const std::exception& X)
		{
			Errors << "VeroScript packages: " << Text(X.what()) << '\n';
			return 1;
		}
	}
} // namespace VeroScript::Packages
