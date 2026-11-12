// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptCompile.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptVm.h"
#if VEROSCRIPT_PACKAGE_MANAGER_DEFAULT_VM
#include "VeroScriptBytecodeVm.h"
#endif
#include <algorithm>
#include <atomic>
#include <exception>
#include <sstream>
#include <thread>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		void CheckInput(const J& V, const J& D, const std::string& Name)
		{
			auto T = D.at("type").get<std::string>();
			bool Good = T == "string" ? V.is_string()
			    : T == "boolean"      ? V.is_boolean()
			    : T == "integer"
			    ? V.is_number_integer()
			    : V.is_string() && std::find(D.at("choices").begin(), D.at("choices").end(), V) != D.at("choices").end();
			if (!Good)
			{
				Fail("input-value", "input has the wrong type or is outside its enum choices", Name);
			}
		}
		I18n::FI18nArgument Argument(const J& V)
		{
			if (V.is_string())
			{
				return I18n::FI18nArgument(V.get<std::string>());
			}
			if (V.is_boolean())
			{
				return I18n::FI18nArgument(V.get<bool>());
			}
			if (V.is_number_unsigned())
			{
				return I18n::FI18nArgument(V.get<std::uint64_t>());
			}
			if (V.is_number_integer())
			{
				return I18n::FI18nArgument(V.get<std::int64_t>());
			}
			if (V.is_number_float())
			{
				return I18n::FI18nArgument(V.get<double>());
			}
			Fail("input-type", "input cannot be represented as a typed argument");
		}
		std::string InputText(const J& V)
		{
			return V.is_string() ? V.get<std::string>() : V.dump();
		}
	} // namespace
	J PrepareInputs(const FCandidate& C, const J& Config, const FClientOptions& O, FWork& W, J& Secrets,
	    const I18n::FI18nSnapshot* Display)
	{
		auto Inputs = C.Manifest.value("install", J::object()).value("inputs", J::object());
		J Values = Config;
		Secrets = J::object();
		for (auto I = Inputs.begin(); I != Inputs.end(); ++I)
		{
			W.Check();
			bool Secret = I.value().value("sensitive", false);
			auto& Destination = Secret ? Secrets : Values;
			auto& Explicit = Secret ? O.Secrets : O.Inputs;
			auto Qualified = C.Manifest.at("name").get<std::string>() + "." + I.key();
			auto Found = Explicit.find(C.Manifest.at("name").get<std::string>() + "@" +
			    C.Manifest.at("version").get<std::string>() + "." + I.key());
			if (Found == Explicit.end())
			{
				Found = Explicit.find(Qualified);
			}
			if (Found == Explicit.end())
			{
				Found = Explicit.find(I.key());
			}
			if (Secret && Config.contains(I.key()))
			{
				Fail("secret-configuration", "sensitive inputs cannot be persisted in package configuration",
				    I.key());
			}
			if (Found != Explicit.end())
			{
				try
				{
					Destination[I.key()] = Parse(Found->second, O.Limits);
				}
				catch (...)
				{
					if (Secret)
					{
						Fail("secret-input", "sensitive input is not valid JSON", Qualified);
					}
					throw;
				}
			}
			else if (!Destination.contains(I.key()) && I.value().contains("default"))
			{
				Destination[I.key()] = I.value()["default"];
			}
			if (!Destination.contains(I.key()) && !O.NonInteractive && O.RequestInput)
			{
				std::string Value;
				FDiagnostic E;
				auto Definition = I.value();
				if (Display)
				{
					FInstalledPackage P;
					P.Name = C.Manifest.at("name");
					P.Version = C.Manifest.at("version");
					P.ManifestJson = C.Manifest.dump();
					P.TextDomain = C.OwnerId;
					Definition["description"] = Render(P, "inputs." + I.key() + ".description", *Display, W);
				}
				if (!O.RequestInput(Qualified, Definition.dump(), Value, E))
				{
					FFailure X(E.Code.empty() ? "input-required" : E.Code,
					    E.Message.empty() ? "input was not supplied" : E.Message);
					throw X;
				}
				try
				{
					Destination[I.key()] = Parse(Value, O.Limits);
				}
				catch (...)
				{
					if (Secret)
					{
						Fail("secret-input", "sensitive input is not valid JSON", Qualified);
					}
					throw;
				}
			}
			if (!Destination.contains(I.key()))
			{
				if (I.value().value("required", false))
				{
					Fail("input-required", "required installation input is missing", Qualified);
				}
				continue;
			}
			CheckInput(Destination[I.key()], I.value(), Qualified);
		}
		return Values;
	}
	void InstallActions(const FCandidate& C, const fs::path& Root, const J& Inputs, FWork& W)
	{
		auto Install = C.Manifest.value("install", J::object());
		auto Definitions = Install.value("inputs", J::object());
		for (const auto& A : Install.value("actions", J::array()))
		{
			W.Check();
			std::string Rel = A.at("destination");
			CheckPath(Rel);
			auto Output = Root / U8Path(Rel);
			if (fs::exists(Output))
			{
				Fail("action-output", "install action would overwrite an existing generated file", Rel);
			}
			auto Source = C.Content / U8Path(A.at("source").get<std::string>());
			auto Text = Read(Source, W.Options.Limits.MaxFileBytes, &W);
			if (A.at("type") == "template")
			{
				I18n::FI18nArguments Args;
				I18n::FI18nError Error;
				for (auto I = Inputs.begin(); I != Inputs.end(); ++I)
				{
					if (!Definitions.value(I.key(), J::object()).value("sensitive", false))
					{
						if (!Args.Set(I.key(), Argument(I.value()), Error))
						{
							Fail("template-input", Error.Diagnostic.Message);
						}
					}
				}
				std::string Result;
				I18n::FI18nWork Work{[](void* P, std::uint64_t)
				{
					static_cast<FWork*>(P)->Check();
					return true;
				},
				    &W, W.Options.Limits.MaxFileBytes};
				if (!I18n::TryFormatNamed(Text, Args, Result, Error, Work))
				{
					Fail("template-format", Error.Diagnostic.Message);
				}
				Text = std::move(Result);
			}
			Write(Output, Text);
		}
	}
	void RunFeatures(const FCandidate& C, const std::string& Phase, const J& Inputs, const J& Secrets,
	    const fs::path& Root, const FEnvironmentLayout& L, const I18n::FI18nSnapshot& Display,
	    FWork& W)
	{
		for (const auto& Required : C.Manifest.value("requires", J::object()).value("features", J::array()))
		{
			std::string Name = Required.at("id");
			if (Name == "veroscript.package-manager")
			{
				continue;
			}
			auto Handler = W.Options.Features.find(Name);
			if (Handler == W.Options.Features.end() || !Handler->second ||
			    !Satisfies(Handler->second->GetVersion(), Required.at("version")))
			{
				Fail("installer-feature", "required installer processor is unavailable", Name);
			}
			struct FGate
			{
				std::atomic<bool> Active{true};
				std::thread::id Thread = std::this_thread::get_id();
			};
			auto Gate = std::make_shared<FGate>();
			struct FScope
			{
				~FScope()
				{
					Gate->Active.store(false, std::memory_order_release);
				}

				std::shared_ptr<FGate> Gate;
			} Scope{Gate};
			const auto Check = [Gate, &W]()
			{
				if (!Gate->Active.load(std::memory_order_acquire) ||
				    Gate->Thread != std::this_thread::get_id())
				{
					Fail("processor-lifetime",
					    "installer services are outside their synchronous invocation scope");
				}
				W.Check();
			};
			FInstallProcessingContext Context;
			Context.Phase = Phase;
			Context.ManifestJson = C.Manifest.dump();
			Context.ConfigurationJson = Inputs.dump();
			Context.GeneratedRoot = NativePathText(Root);
			Context.Host = L.Host;
			Context.Target = L.Target;
			Context.CheckCancelled = Check;
			Context.DisplaySnapshot = Display;
			Context.ReadInput = [Check, &Inputs, &Secrets](const std::string& Name, I18n::FI18nArgument& Out,
			                        FDiagnostic& E)
			{
				try
				{
					Check();
					E = {};
					const auto& Values = Secrets.contains(Name) ? Secrets : Inputs;
					if (!Values.contains(Name))
					{
						Fail("input-missing", "installation input is absent", Name);
					}
					Out = Argument(Values.at(Name));
					return true;
				}
				catch (const FFailure& X)
				{
					E = X.Diagnostic;
					return false;
				}
				catch (const std::exception& X)
				{
					E.Code = "input-value";
					E.Message = X.what();
					return false;
				}
			};
			Context.ReadResource = [Check, &C, &W](const std::string& Group, const std::string& Path,
			                           FResourceData& Out, FDiagnostic& E)
			{
				try
				{
					Out = {};
					E = {};
					Check();
					CheckPath(Path);
					auto Groups = C.Manifest.value("resources", J::object());
					if (!Groups.contains(Group))
					{
						Fail("resource-group", "package has no such resource group", Group);
					}
					auto Root = C.Content / U8Path(Groups.at(Group).get<std::string>());
					auto File = Root / U8Path(Path);
					if (!Under(File, Root))
					{
						Fail("resource-path", "resource escaped package group");
					}
					NoLinks(File, true);
					auto Relative = PathUtil::ToUtf8(File.lexically_relative(C.Content));
					auto I = std::find_if(C.Inventory.begin(), C.Inventory.end(),
					    [&](const J& V)
					{
						return V.at("path") == Relative;
					});
					if (!fs::exists(File))
					{
						if (I != C.Inventory.end())
						{
							Fail("resource-integrity",
							    "installation resource is missing from its immutable snapshot");
						}
						return true;
					}
					auto Bytes = Read(File, W.Options.Limits.MaxFileBytes, &W);
					auto Digest = Hash(Bytes);
					if (I == C.Inventory.end() || I->at("directory").get<bool>() || I->at("sha256") != Digest)
					{
						Fail("resource-integrity",
						    "installation resource differs from its immutable snapshot");
					}
					Out.Bytes.assign(Bytes.begin(), Bytes.end());
					Out.ContentIdentity = std::move(Digest);
					Out.bExists = true;
					return true;
				}
				catch (const FFailure& X)
				{
					E = X.Diagnostic;
					Out = {};
					return false;
				}
				catch (const std::exception& X)
				{
					E.Code = "processor-resource";
					E.Message = X.what();
					Out = {};
					return false;
				}
			};
			FDiagnostic Error;
			if (!Handler->second->Process(Context, Error))
			{
				throw FFailure(Error.Code.empty() ? "installer-feature" : Error.Code,
				    Error.Message.empty() ? "installation processor failed" : Error.Message);
			}
			W.Check();
			Inventory(Root, W);
		}
	}
	namespace
	{
		struct FHookState
		{
			bool Active = true;
			const FCandidate* Package = nullptr;
			const FGraph* Graph = nullptr;
			const FEnvironmentLayout* Layout = nullptr;
			FWork* Work = nullptr;
			J Inputs, Secrets;
			fs::path Generated;
			I18n::FI18nSnapshot Display;
			std::set<std::string> Permissions;
		};
		bool HookMember(const FNativeCallContext& C, FValue& Out, std::string& Error)
		{
			try
			{
				auto* S = static_cast<FHookState*>(C.HostThis);
				if (!S && C.ArgCount)
				{
					if (auto H = std::get_if<FHostObjectHandle>(&C.Args[0]);
					    H && *H && (*H)->TypeName == "std.package.install_context")
					{
						S = static_cast<FHookState*>((*H)->Payload);
					}
				}
				if (!S || !S->Active)
				{
					Fail("hook-lifetime", "install_context is outside its invocation scope");
				}
				S->Work->Check();
				std::string Op = static_cast<const char*>(C.UserData);
				auto& Package = *S->Package;
				std::string Name = Package.Manifest.at("name");
				const auto Permission = [&](const char* P)
				{
					if (!S->Permissions.count(P))
					{
						Fail("hook-permission", "hook lacks declared installation permission", P);
					}
				};
				if (Op == "name")
				{
					Out = Name;
				}
				else if (Op == "version")
				{
					Out = Package.Manifest["version"].get<std::string>();
				}
				else if (Op == "text_domain")
				{
					Out = Package.OwnerId;
				}
				else if (Op == "configuration_json")
				{
					Out = S->Inputs.dump();
				}
				else if (Op == "host_json")
				{
					Out = TargetJson(S->Layout->Host).dump();
				}
				else if (Op == "target_json")
				{
					Out = TargetJson(S->Layout->Target).dump();
				}
				else if (Op == "display_snapshot")
				{
					I18n::FI18nError E;
					if (!I18n::ToScriptValue(S->Display, Out, E))
					{
						Fail("hook-display", E.Diagnostic.Message);
					}
				}
				else if (Op == "input")
				{
					auto Key = std::get<std::string>(C.Args[1]);
					auto& Values = S->Secrets.contains(Key) ? S->Secrets : S->Inputs;
					if (!Values.contains(Key))
					{
						Fail("input-missing", "installation input does not exist", Key);
					}
					I18n::FI18nError E;
					if (!I18n::ToScriptValue(Argument(Values[Key]), Out, E))
					{
						Fail("hook-input", E.Diagnostic.Message);
					}
				}
				else if (Op == "dependency_json" || Op == "dependency_text_domain")
				{
					auto N = std::get<std::string>(C.Args[1]);
					if (!S->Graph->Edges.count(Name) || !S->Graph->Edges.at(Name).count(N))
					{
						Fail("hook-dependency", "hook can only inspect direct dependencies", N);
					}
					const auto& D = S->Graph->Nodes.at(N);
					if (Op == "dependency_text_domain")
					{
						Out = D.OwnerId;
					}
					else
					{
						Out = J{{"name", N}, {"version", D.Manifest["version"]}, {"text_domain", D.OwnerId}}
						          .dump();
					}
				}
				else if (Op == "read_text" || Op == "read_dependency_text")
				{
					Permission("resources.read");
					const FCandidate* Source = &Package;
					unsigned Offset = 1;
					if (Op == "read_dependency_text")
					{
						auto N = std::get<std::string>(C.Args[1]);
						if (!S->Graph->Edges.count(Name) || !S->Graph->Edges.at(Name).count(N))
						{
							Fail("hook-dependency", "resource read requires a direct dependency", N);
						}
						Source = &S->Graph->Nodes.at(N);
						Offset = 2;
					}
					auto Group = std::get<std::string>(C.Args[Offset]),
					     Rel = std::get<std::string>(C.Args[Offset + 1]);
					CheckPath(Rel);
					auto Groups = Source->Manifest.value("resources", J::object());
					if (!Groups.contains(Group))
					{
						Fail("resource-group", "package resource group does not exist", Group);
					}
					auto Root = Source->Content / U8Path(Groups[Group].get<std::string>());
					auto File = Root / U8Path(Rel);
					if (!Under(File, Root))
					{
						Fail("resource-path", "hook resource escaped its group");
					}
					auto Budget = S->Work->Options.Limits.HookResourceBytes;
					auto Text = Read(File,
					    Budget ? std::min(Budget, S->Work->Options.Limits.MaxFileBytes)
					           : S->Work->Options.Limits.MaxFileBytes,
					    S->Work);
					auto RelPath = PathUtil::ToUtf8(File.lexically_relative(Source->Content));
					auto I = std::find_if(Source->Inventory.begin(), Source->Inventory.end(),
					    [&](const J& V)
					{
						return V.at("path") == RelPath;
					});
					if (I == Source->Inventory.end() || I->at("sha256") != Hash(Text))
					{
						Fail("resource-integrity", "hook resource changed after snapshot");
					}
					(void)J(Text).dump();
					Out = std::move(Text);
				}
				else if (Op == "write_text")
				{
					Permission("generated.write");
					auto Rel = std::get<std::string>(C.Args[1]);
					CheckPath(Rel);
					if (Rel == "loaders" || Rel.rfind("loaders/", 0) == 0 || Rel.rfind(".vero-", 0) == 0)
					{
						Fail("generated-reserved", "hook output uses installer-owned paths");
					}
					const auto& Text = std::get<std::string>(C.Args[2]);
					if (Text.size() > S->Work->Options.Limits.MaxFileBytes ||
					    (S->Work->Options.Limits.HookResourceBytes &&
					        Text.size() > S->Work->Options.Limits.HookResourceBytes))
					{
						Fail("generated-limit", "generated file exceeds budget");
					}
					auto Path = S->Generated / U8Path(Rel);
					if (!Under(Path, S->Generated))
					{
						Fail("generated-path", "hook output escaped staging root");
					}
					Write(Path, Text);
					Inventory(S->Generated, *S->Work);
				}
				else if (Op == "message" || Op == "message_text")
				{
					Permission("messages");
					std::string Text;
					if (Op == "message")
					{
						Text = std::get<std::string>(C.Args[1]);
					}
					else
					{
						I18n::FI18nText V;
						I18n::FI18nError E;
						I18n::FI18nResolvedText Result;
						if (!I18n::FromScriptValue(C.Args[1], V, E) || !C.I18nServices ||
						    !S->Display.TryResolve(V, Result, E, C.I18nServices->GetI18nWork()))
						{
							if (C.DiagnosticOutput)
							{
								*C.DiagnosticOutput = E.Diagnostic;
							}
							Error = E.Diagnostic.Message;
							return false;
						}
						Text = std::move(Result.Value);
					}
					if (Text.size() > S->Work->Options.Limits.MaxDisplayBytes)
					{
						Fail("hook-output", "hook message exceeds output budget");
					}
					if (S->Work->Options.Message)
					{
						S->Work->Options.Message(Text);
					}
				}
				else
				{
					Fail("hook-operation", "unknown installation context operation");
				}
				return true;
			}
			catch (const FFailure& E)
			{
				Error = E.Diagnostic.Code + ": " + E.Diagnostic.Message;
				return false;
			}
			catch (const std::exception& E)
			{
				Error = E.what();
				return false;
			}
		}
		class FHookCatalog final : public IHostBindingCatalog
		{
		public:
			//~ Begin IHostBindingCatalog Interface
			const std::vector<FHostTypeDesc>& GetTypes() const override
			{
				return Types;
			}

			const std::vector<FNativeFunction>& GetNativeFunctions() const override
			{
				return Natives;
			}

			const std::vector<FHostFunctionDesc>& GetFunctions() const override
			{
				return Functions;
			}

			const FHostFunctionDesc* FindFunction(std::string_view) const override
			{
				return nullptr;
			}

			const FHostTypeDesc* FindType(std::string_view N) const override
			{
				return N == Types[0].Name ? &Types[0] : nullptr;
			}

			const FHostTypeDesc* FindType(FHostTypeId I) const override
			{
				return I == Types[0].Id ? &Types[0] : nullptr;
			}
			//~ End IHostBindingCatalog Interface

			std::vector<FHostTypeDesc> Types;

			std::vector<FNativeFunction> Natives;

			std::vector<FHostFunctionDesc> Functions;
		};
		std::shared_ptr<const IHostBindingCatalog> HookBindings(const std::shared_ptr<FHookState>& State)
		{
			// The public catalog composes directly with Core's immutable i18n catalog;
			// no copied standard descriptors or stub callbacks are registered.
			auto R = std::make_shared<FHookCatalog>();
			FHostTypeDesc Type;
			Type.Id.Value = 1;
			Type.Name = "std.package.install_context";
			Type.Kind = EHostTypeKind::Class;
			Type.ModuleOwner = State;
			R->Types.push_back(std::move(Type));
			const auto Add = [&](const char* Name, ENativeType Result, std::vector<ENativeType> Args = {},
			                     std::string HostResult = {}, std::vector<std::string> Hosts = {})
			{
				FHostMemberDesc M;
				M.Name = Name;
				M.Callback = HookMember;
				M.UserData = const_cast<char*>(Name);
				M.bConstMethod = true;
				M.ReturnType = Result;
				M.ParamTypes = Args;
				M.ReturnHostTypeName = HostResult;
				M.ParamHostTypeNames = Hosts;
				M.NativeName = MakeHostMethodNativeName(R->Types[0].Name, Name);
				FNativeFunction N;
				N.Name = M.NativeName;
				N.HostTypeName = R->Types[0].Name;
				N.Callback = HookMember;
				N.UserData = M.UserData;
				N.ReturnType = Result;
				N.ReturnHostTypeName = HostResult;
				N.CallbackOwner = State;
				N.ParamTypes = {ENativeType::HostObject};
				N.ParamTypes.insert(N.ParamTypes.end(), Args.begin(), Args.end());
				N.ParamHostTypeNames = {R->Types[0].Name};
				Hosts.resize(Args.size());
				N.ParamHostTypeNames.insert(N.ParamHostTypeNames.end(), Hosts.begin(), Hosts.end());
				R->Natives.push_back(std::move(N));
				R->Types[0].Members.push_back(std::move(M));
			};
			for (auto N :
			    {"name", "version", "text_domain", "configuration_json", "host_json", "target_json"})
			{
				Add(N, ENativeType::String);
			}
			Add("input", ENativeType::HostObject, {ENativeType::String}, "std.i18n.argument");
			Add("display_snapshot", ENativeType::HostObject, {}, "std.i18n.snapshot");
			Add("dependency_json", ENativeType::String, {ENativeType::String});
			Add("dependency_text_domain", ENativeType::String, {ENativeType::String});
			Add("read_text", ENativeType::String, {ENativeType::String, ENativeType::String});
			Add("read_dependency_text", ENativeType::String,
			    {ENativeType::String, ENativeType::String, ENativeType::String});
			Add("write_text", ENativeType::Void, {ENativeType::String, ENativeType::String});
			Add("message", ENativeType::Void, {ENativeType::String});
			Add("message_text", ENativeType::Void, {ENativeType::HostObject}, {}, {"std.i18n.text"});
			return R;
		}
		FCompileResult CompileHook(const fs::path& Path, const FCompileOptions& Options, const FWork& Work)
		{
			const auto* Previous = ActiveAnalysisControl;
			std::exception_ptr Cancellation;
			FAnalysisControlScope Control(
			    [&]()
			{
				try
				{
					Work.Check();
					return Previous && Previous->ShouldCancel && Previous->ShouldCancel();
				}
				catch (...)
				{
					// Core propagates analysis cancellation across diagnostic boundaries.
					// Retain even a one-shot caller failure instead of reporting syntax errors.
					Cancellation = std::current_exception();
					return true;
				}
			}, Previous ? Previous->RecordStage : std::function<void(std::string_view, double)>{});
			try
			{
				return VeroScriptCompile::CompileFile(NativePathText(Path), Options);
			}
			catch (const FAnalysisCancelled&)
			{
				if (Cancellation)
				{
					std::rethrow_exception(Cancellation);
				}
				Fail("cancelled", "package hook compilation was cancelled", NativePathText(Path));
			}
		}
	} // namespace
	void RunHooks(const FCandidate& C, const std::string& Phase, const FGraph& G, const J& Inputs,
	    const J& Secrets, const fs::path& Generated, const FEnvironmentLayout& L,
	    const I18n::FI18nSnapshot& Display, FWork& W, bool PreflightOnly)
	{
		FNumericLimits NumericLimits;
		FNumericError NumericError;
		if (!Numerics::ValidateLimits(W.Options.Limits.HookNumericLimits, NumericLimits, NumericError))
		{
			Fail("hook-limits", "invalid numeric hook limits");
		}
		for (const auto& H : C.Manifest.value("install", J::object()).value("hooks", J::array()))
		{
			if (H.at("phase") != Phase)
			{
				continue;
			}
			W.Check();
			if (!W.Options.AllowHooks)
			{
				Fail("hooks-disabled", "package declares hooks; explicit hook permission is required",
				    C.Manifest["name"]);
			}
			const auto& ConstantLimits = W.Options.Limits.HookConstantEvaluationLimits;
			if (!ConstantLimits.MaxWork || !ConstantLimits.MaxCallDepth ||
			    !ConstantLimits.MaxExpressionDepth || !ConstantLimits.MaxMemoryBytes)
			{
				Fail("hook-limits", "constant evaluation hook limits must be positive");
			}
			auto State = std::make_shared<FHookState>();
			State->Package = &C;
			State->Graph = &G;
			State->Layout = &L;
			State->Work = &W;
			State->Generated = Generated;
			State->Inputs = Inputs;
			State->Secrets = Secrets;
			State->Display = Display;
			for (const auto& P : H.value("permissions", std::vector<std::string>{}))
			{
				if (P != "resources.read" && P != "generated.write" && P != "messages")
				{
					Fail("hook-permission", "unsupported hook permission", P);
				}
				State->Permissions.insert(P);
			}
			struct FScope
			{
				~FScope()
				{
					State->Active = false;
					State->Work = nullptr;
					State->Package = nullptr;
					State->Graph = nullptr;
					State->Layout = nullptr;
				}

				std::shared_ptr<FHookState> State;
			} Scope{State};
			auto Bindings = HookBindings(State);
			FModuleEnvironmentDesc D;
			D.EntryOwnerId = C.OwnerId;
			D.Bindings = Bindings;
			D.ScriptCompileExtensions.bAllowSourceImports = false;
			D.CompileProcessing.bCacheable = false;
			// CompileHook binds the operation deadline/callback to this thread's
			// analysis scope, including diagnostics and constexpr evaluation.
			D.CompileProcessing.CheckCancelled = []()
			{
				CheckAnalysisCancellation();
			};
			D.CompileConfiguration = "package-install-hooks:0.1.1";
			for (const auto& N : G.Nodes)
			{
				std::vector<std::string> Dependencies;
				for (const auto& Dep : G.Edges.at(N.first))
				{
					Dependencies.push_back(G.Nodes.at(Dep).OwnerId);
				}
				D.Scopes.push_back({N.second.OwnerId, Dependencies});
				D.SourceRoots.push_back({NativePathText(N.second.Content), N.second.OwnerId});
				auto Modules = N.second.Manifest.value("entries", J::object()).value("modules", J::object());
				for (auto I = Modules.begin(); I != Modules.end(); ++I)
				{
					auto Path = N.second.Content / U8Path(I.value().get<std::string>());
					D.Modules.push_back({I.key(), N.second.OwnerId + ":" + I.key(), NativePathText(Path),
					    N.second.OwnerId, HashFile(Path, W, W.Options.Limits.MaxFileBytes)});
				}
			}
			if (!W.Options.StdlibSearchPaths.empty())
			{
				D.Scopes.push_back({"package.install.stdlib", {}, true});
				for (const auto& Root : W.Options.StdlibSearchPaths)
				{
					auto AbsoluteRoot = NativePathText(Absolute(Root, {}));
					D.SourceRoots.push_back({AbsoluteRoot, "package.install.stdlib"});
					D.StdlibSearchPaths.push_back(AbsoluteRoot);
				}
			}
			std::string Error;
			auto Environment = FModuleEnvironment::Create(std::move(D), Error);
			if (!Environment)
			{
				Fail("hook-environment", Error);
			}
			auto Path = C.Content / U8Path(H.at("path").get<std::string>());
			FCompileOptions O;
			O.ModuleEnvironment = Environment;
			O.ScriptCompileExtensions.bAllowSourceImports = false;
			O.ConstantEvaluationLimits = ConstantLimits;
			auto Compiled = CompileHook(Path, O, W);
			if (!Compiled.bSuccess)
			{
				W.Check();
				Fail("hook-compile", Compiled.Error.Message, NativePathText(Path));
			}
			std::string Entry = H.at("entry");
			std::size_t Matches = 0;
			unsigned Index = 0;
			if (!Compiled.PreparedProgram)
			{
				Fail("hook-entry", "hook source has no declaration snapshot");
			}
			for (const auto& F : Compiled.PreparedProgram->GetDeclarations())
			{
				if (F.Name == Entry && F.ModulePath == Compiled.Module->EntryModulePath &&
				    F.Target == EAnnotationTarget::Function)
				{
					if (!F.bExported || !F.TypeParameters.empty() || F.TypeName != "void" ||
					    F.Parameters.size() != 1 ||
					    F.Parameters[0].TypeName != "std.package.install_context" || F.Parameters[0].bIsRef)
					{
						Fail("hook-entry", "hook entry must be exported void(std.package.install_context)",
						    Entry);
					}
					++Matches;
				}
			}
			if (Matches != 1)
			{
				Fail("hook-entry", "hook entry is missing or ambiguous", Entry);
			}
			Matches = 0;
			for (unsigned I = 0; I < Compiled.Module->Functions.size(); ++I)
			{
				const auto& F = Compiled.Module->Functions[I];
				auto Begin = FindLinkageIdentityStart(F.Name), End = FindFunctionSignatureMarker(F.Name);
				auto Name = std::string_view(F.Name).substr(
				    Begin, (End == std::string::npos ? F.Name.size() : End) - Begin);
				if (F.ModulePath == Compiled.Module->EntryModulePath &&
				    F.ClosureTypeName == "fn(std.package.install_context)->void" && !F.bIsMethod &&
				    !F.bDeleted && !BytecodeFunctionHasRefParams(F) && Name == Entry)
				{
					++Matches;
					Index = I;
				}
			}
			if (Matches != 1)
			{
				Fail("hook-entry", "hook entry must be unique exported void(std.package.install_context)",
				    Entry);
			}
			if (PreflightOnly)
			{
				continue;
			}
			FClientOptions HookPolicy = W.Options;
			HookPolicy.CheckCancelled = [&]()
			{
				W.Check();
			};
			HookPolicy.Limits.TimeoutMilliseconds = W.Options.Limits.HookTimeoutMilliseconds;
			FWork HookWork{HookPolicy};
			State->Work = &HookWork;
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Output, Input, Errors);
			if (W.Options.VmBackendFactory && !W.Options.VmBackendPath.empty())
			{
				Fail("hook-vm", "VM factory and path are exclusive");
			}
			auto Factory = W.Options.VmBackendFactory;
			#if VEROSCRIPT_PACKAGE_MANAGER_DEFAULT_VM
			if (!Factory && W.Options.VmBackendPath.empty())
			{
				static const auto DefaultFactory = BytecodeVm::CreateFactory(GetVmHostServices());
				Factory = DefaultFactory;
			}
			#endif
			if (!Factory && W.Options.VmBackendPath.empty())
			{
				Fail("hook-vm", "install hooks require an explicitly configured VM backend");
			}
			if (!(Factory ? Vm.SetVmBackendFactory(std::move(Factory), Error)
			              : Vm.SetVmBackendPath(W.Options.VmBackendPath, Error)))
			{
				Fail("hook-vm", Error);
			}
			Vm.SetNumericLimits(NumericLimits);
			Vm.SetMemoryContext(std::make_shared<FMemoryContext>(
			    FMemoryLimits{W.Options.Limits.HookBufferBytes, W.Options.Limits.HookBufferBytes}));
			FScriptCapabilities Capabilities;
			Capabilities.bAllowFileRead = Capabilities.bAllowFileWrite = Capabilities.bAllowFileDelete =
			    false;
			Capabilities.bAllowEnvironmentRead = Capabilities.bAllowEnvironmentWrite = false;
			Capabilities.bAllowConsoleInput = Capabilities.bAllowConsoleOutput = false;
			Capabilities.bAllowTaskSpawn = Capabilities.bAllowProcessDirectoryChange =
			    Capabilities.bAllowMemoryAddress = Capabilities.bAllowNondeterminism = false;
			Vm.SetCapabilities(Capabilities);
			Vm.SetExecutionLimits(
			    {W.Options.Limits.HookInstructions, W.Options.Limits.HookTimeoutMilliseconds});
			Vm.SetExecutionCheck(
			    [&]()
			{
				HookWork.Check();
				return true;
			});
			if (!Vm.SetModuleEnvironment(Environment, Error))
			{
				Fail("hook-environment", Error);
			}
			Vm.SetI18nServices({}, {W.Options.Limits.MaxDisplayBytes});
			auto Context = std::make_shared<FHostObject>();
			Context->TypeId = 1;
			Context->TypeName = "std.package.install_context";
			Context->Payload = State.get();
			Context->ModuleOwner = State;
			if (!Vm.ExecuteFunction(*Compiled.Module, static_cast<std::uint16_t>(Index), {Context}))
			{
				W.Check();
				Fail("hook-failed", Vm.GetError().Message, C.Manifest["name"]);
			}
			W.Check();
		}
	}
} // namespace VeroScript::Packages::Detail
