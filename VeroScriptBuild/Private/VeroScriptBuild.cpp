// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "BuildInternal.h"
#include "VeroScriptJitBuild.h"
#include "VeroScriptVersion.h"
#include "VeroScriptPath.h"
#include <algorithm>
#include <functional>

namespace VeroScript::Build
{
	using namespace Detail;
	namespace
	{
		J Merge(J Base, const J& Override)
		{
			for (auto I = Override.begin(); I != Override.end(); ++I)
			{
				if (I.value().is_object() && Base.contains(I.key()) && Base.at(I.key()).is_object())
				{
					Base[I.key()] = Merge(Base[I.key()], I.value());
				}
				else
				{
					Base[I.key()] = I.value();
				}
			}
			return Base;
		}
		void CheckReferences(const J& V, const std::map<std::string, std::map<std::string, FArtifact>>& Outputs)
		{
			for (const auto& Input : ArtifactReferences(V))
			{
				const auto T = Input.at("target").get<std::string>(), O = Input.at("output").get<std::string>();
				if (!Outputs.count(T) || !Outputs.at(T).count(O))
				{
					Fail("artifact-reference", "unknown target or named artifact", T + ":" + O);
				}
				if (Input.contains("path") && Outputs.at(T).at(O).Type != "directory")
				{
					Fail("artifact-member", "only directory outputs can have a member path", T + ":" + O);
				}
			}
		}
		void Type(const J& V, const std::set<std::string>& Kinds,
		    const std::map<std::string, std::map<std::string, FArtifact>>& Outputs)
		{
			if (!V.is_object()) return;
			const auto& A = Outputs.at(V.at("target")).at(V.at("output"));
			if (!Kinds.count(A.Kind) && !(V.contains("path") && Kinds.count("source") && A.Kind == "source-tree"))
			{
				Fail("artifact-kind", "artifact kind is invalid at this input", A.Target + ":" + A.Output);
			}
		}
		void ExistingInput(const J& Input, FBuildPlan::FImpl& P, bool DirectoryCopy)
		{
			if (!Input.is_string()) return;
			const auto Path = Absolute(Input.get<std::string>(), PathUtil::FromUtf8(P.Request.ProjectRoot));
			if (DirectoryCopy && (Under(PathUtil::FromUtf8(P.OutputRoot), Path) || Under(PathUtil::FromUtf8(P.Request.ProjectRoot) / ".veroscript/build", Path)))
			{
				Fail("recursive-input", "directory copy input contains the output or build cache", NativePathText(Path));
			}
			Snapshot(Path, P.Inputs, P.Request.Limits);
		}
	}
	std::string FBuildPlan::ToJson() const
	{
		J Targets = J::array();
		for (const auto& N : Impl->Order)
		{
			J Outputs = J::object();
			for (const auto& O : Impl->Outputs.at(N))
			{
				Outputs[O.first] = {{"path", O.second.Path}, {"kind", O.second.Kind}, {"type", O.second.Type}};
			}
			Targets.push_back({{"id", N}, {"kind", Impl->Targets.at(N).at("kind")}, {"outputs", Outputs}, {"generatedInputsValidatedAtExecution", !References(Impl->Targets.at(N)).empty()}});
		}
		return J{{"format", "veroscript-build-plan"}, {"formatVersion", "0.1.1"}, {"identity", Impl->Identity},
		    {"projectRoot", Impl->Request.ProjectRoot}, {"profile", Impl->Request.Profile}, {"platform", Impl->Platform},
		    {"targets", Targets}, {"inputFiles", Impl->Inputs.size()}, {"derivedProtectionRequiresExplicitPolicy", true}}
		    .dump(2);
	}

	const std::vector<std::string>& FBuildPlan::GetOrder() const
	{
		return Impl->Order;
	}

	FBuildPlan::FBuildPlan(std::shared_ptr<FImpl> P) : Impl(std::move(P))
	{
	}

	std::string FBuildResult::ToJson() const
	{
		J T = J::array(), D = J::array();
		for (const auto& R : Targets)
		{
			J A = J::array();
			for (const auto& O : R.Artifacts)
			{
				A.push_back({{"output", O.Output}, {"path", O.Path}, {"kind", O.Kind}, {"type", O.Type}, {"sha256", O.Identity}, {"signature", O.SignaturePath}});
			}
			T.push_back({{"id", R.Id}, {"cached", R.Cached}, {"artifacts", A}});
		}
		for (const auto& E : Diagnostics)
		{
			D.push_back({{"code", E.Code}, {"message", E.Message}, {"target", E.Target}, {"path", E.Path}, {"stage", E.Stage}});
		}
		return J{{"format", "veroscript-build-result"}, {"formatVersion", "0.1.1"}, {"targets", T}, {"diagnostics", D}}.dump(2);
	}

	bool FBuilder::Plan(const FBuildRequest& Request, const FServices& Services,
	    std::shared_ptr<const FBuildPlan>& Out, FDiagnostic& Error)
	{
		Out.reset();
		std::string Current;
		try
		{
			auto P = std::make_shared<FBuildPlan::FImpl>();
			P->Request = Request;
			P->Services = Services;
			const auto FreezePaths = [](const auto& Source, auto& Storage, auto& Views)
			{
				for (const auto V : Source)
				{
					Storage.emplace_back(V);
				}
				Views.clear();
				for (const auto& V : Storage)
				{
					Views.emplace_back(V);
				}
			};
			FreezePaths(Services.Compile.ImportPaths, P->ImportPaths, P->Services.Compile.ImportPaths);
			FreezePaths(Services.Compile.StdlibSearchPaths, P->StdlibPaths, P->Services.Compile.StdlibSearchPaths);
			FreezePaths(Services.Compile.Defines, P->Defines, P->Services.Compile.Defines);
			FreezePaths(Services.Compile.ImplicitImportPaths, P->ImplicitImports, P->Services.Compile.ImplicitImportPaths);
			P->Request.Limits.Content.CheckCancelled = Request.CheckCancelled;
			Id(Request.Profile);
			const auto Root = PathUtil::FromUtf8(Request.ProjectRoot);
			Physical(Root);
			if (!fs::is_directory(Root))
			{
				Fail("project-root", "project root is not a directory");
			}
			P->Request.ProjectRoot = NativePathText(Root.lexically_normal());
			const auto ConfigPath = Request.BuildFile.empty() ? Root / "veroscript-build.json" : PathUtil::FromUtf8(Request.BuildFile);
			Physical(ConfigPath);
			P->Request.BuildFile = NativePathText(ConfigPath.lexically_normal());
			P->Configuration = ValidateConfiguration(Read(ConfigPath, P->Request.Limits), P->Request);
			if (!P->Configuration.at("profiles").contains(Request.Profile))
			{
				Fail("profile", "unknown build profile", Request.Profile);
			}
			const auto& Profile = P->Configuration.at("profiles").at(Request.Profile);
			P->Targets = P->Configuration.at("targets");
			for (auto T = P->Targets.begin(); T != P->Targets.end(); ++T)
			{
				if (T.value().at("kind") == "library" || T.value().at("kind") == "application")
				{
					T.value()["compile"] = Merge(Profile.value("compile", J::object()), T.value().value("compile", J::object()));
				}
				const auto O = Profile.value("targetOverrides", J::object());
				if (O.contains(T.key()))
				{
					T.value() = Merge(T.value(), O.at(T.key()));
				}
			}
			// Validate merged policies with the exact same strict parser.
			auto Effective = P->Configuration;
			Effective["targets"] = P->Targets;
			Effective["profiles"] = {{"release", J::object()}};
			(void)ValidateConfiguration(Effective.dump(), P->Request);
			P->Platform = P->Configuration.value("platform", J("host"));
			if (P->Platform == "host")
			{
				P->Platform = Parse(Services.HostPlatformJson.empty() ? Application::HostApplicationTargetJson() : Services.HostPlatformJson, P->Request.Limits);
			}
			if (P->Platform != "portable" && !P->Platform.is_object())
			{
				Fail("platform", "platform must be host, portable or an explicit target");
			}
			if (P->Platform.is_object())
			{
				Application::FDiagnostic TargetError;
				Require(Application::ValidateApplicationTarget(P->Platform.dump(), TargetError, P->Request.Limits.Content), TargetError);
			}
			P->PlatformId = P->Platform == "portable" ? "portable" : std::string(GetHostPlatformString()) + "-" + Application::ContentSha256(P->Platform.dump()).substr(0, 12);
			const auto Output = Request.OutputDirectory.empty() ? Absolute(P->Configuration.value("outputDirectory", "dist"), Root) : PathUtil::FromUtf8(Request.OutputDirectory);
			Physical(Output, true);
			if (Under(Root, Output))
			{
				Fail("output-root", "output root cannot contain the project source root");
			}
			P->OutputRoot = NativePathText(Output);
			std::set<std::string> Paths;
			for (auto T = P->Targets.begin(); T != P->Targets.end(); ++T)
			{
				auto& Outputs = P->Outputs[T.key()];
				const auto K = T.value().at("kind").get<std::string>();
				if (K == "group")
				{
					continue;
				}
				const auto TargetRoot = Output / Request.Profile / P->PlatformId / T.key();
				const auto Add = [&](const std::string& Name, const std::string& Rel, const std::string& Type, const std::string& Kind)
				{
					Safe(Rel);
					FArtifact A{T.key(), Name, NativePathText(TargetRoot / PathUtil::FromUtf8(Rel)), Type, Kind, {}, P->Platform.dump(), {}, {}};
					const auto Key = PathUtil::IdentityKey(A.Path);
					for (const auto& Pth : Paths)
					{
						if (Under(PathUtil::FromUtf8(Key), PathUtil::FromUtf8(Pth)) || Under(PathUtil::FromUtf8(Pth), PathUtil::FromUtf8(Key)))
						{
							Fail("output-overlap", "build outputs overlap", A.Path);
						}
					}
					Paths.insert(Key);
					Outputs.emplace(Name, std::move(A));
				};
				if (K == "custom")
				{
					for (auto O = T.value()["outputs"].begin(); O != T.value()["outputs"].end(); ++O)
					{
						Add(O.key(), O.value().at("path"), O.value().at("type"), O.value().at("kind"));
					}
				}
				else
				{
					const auto Kind = K == "bundle" ? (T.value().value("layout", "vsapp") == "directory" ? "application-directory" : T.value().value("layout", "vsapp") == "vsapp" ? "application"
					                                                                                                                                                               : "generic")
					    : K == "deployment"         ? "environment"
					                                : K;
					Add("main", T.value().at("output"), K == "deployment" || (K == "bundle" && T.value().value("layout", "vsapp") == "directory") ? "directory" : "file", Kind);
					const auto Protection = Detail::Protection(T.value().value("protection", J::object()), K == "application" || K == "bundle");
					if (!Protection.SigningKey.empty() && Protection.SignatureScope == "artifact")
					{
						Add("signature", T.value().at("output").get<std::string>() + ".sig.json", "file", "signature");
					}
				}
			}
			std::map<std::string, int> State;
			std::function<void(const std::string&, std::size_t)> Visit = [&](const std::string& N, std::size_t Depth)
			{
				if (Request.CheckCancelled)
				{
					Request.CheckCancelled();
				}
				if (Depth > Request.Limits.MaxDepth)
				{
					Fail("graph-depth", "build graph exceeds nesting budget", N);
				}
				if (!P->Targets.contains(N))
				{
					Fail("unknown-target", "unknown build target", N);
				}
				if (State[N] == 1)
				{
					Fail("build-cycle", "build graph contains a cycle", N);
				}
				if (State[N] == 2) return;
				State[N] = 1;
				const auto& T = P->Targets.at(N);
				CheckReferences(T, P->Outputs);
				auto Deps = References(T);
				for (const auto& D : T.value("dependsOn", std::vector<std::string>{}))
				{
					Deps.insert(D);
				}
				for (const auto& D : Deps)
				{
					Visit(D, Depth + 1);
				}
				State[N] = 2;
				P->Order.push_back(N);
			};
			// Validate every graph, even if capabilities for it are not requested.
			for (auto T = P->Targets.begin(); T != P->Targets.end(); ++T)
			{
				Visit(T.key(), 0);
			}
			P->Order.clear();
			State.clear();
			auto Targets = Request.Targets.empty() ? P->Configuration.value("defaultTargets", std::vector<std::string>{}) : Request.Targets;
			if (Targets.empty())
			{
				Fail("default-targets", "no target selected and no defaultTargets declared");
			}
			std::sort(Targets.begin(), Targets.end());
			for (const auto& T : Targets)
			{
				Visit(T, 0);
			}
			Snapshot(ConfigPath, P->Inputs, P->Request.Limits);
			for (const auto& N : P->Order)
			{
				Current = N;
				const auto& T = P->Targets.at(N);
				const auto K = T.at("kind").get<std::string>();
				for (const auto* F : {"entry", "source", "application"})
				{
					if (T.contains(F))
					{
						ExistingInput(T.at(F), *P, F == std::string("source"));
						if (F == std::string("entry"))
						{
							Type(T.at(F), {"source"}, P->Outputs);
						}
						if (F == std::string("application"))
						{
							Type(T.at(F), {"application"}, P->Outputs);
						}
					}
				}
				for (const auto& F : T.value("files", J::array()))
				{
					ExistingInput(F.at("from"), *P, F.at("from").is_string() && fs::is_directory(Absolute(F.at("from"), Root)));
					if (F.contains("signature"))
					{
						ExistingInput(F.at("signature"), *P, false);
					}
				}
				const auto Modules = T.value("modules", J::object());
				for (const auto& M : Modules)
				{
					ExistingInput(M, *P, false);
					Type(M, {"library", "source"}, P->Outputs);
				}
				for (const auto* C : {"runtime", "environment", "launcher"})
				{
					if (T.contains(C))
					{
						const auto& Component = T.at(C);
						for (const auto* F : {"component", "artifact", "path"})
						{
							if (Component.contains(F))
							{
								ExistingInput(Component.at(F), *P, Component.at(F).is_string() && fs::is_directory(Absolute(Component.at(F), Root)));
								Type(Component.at(F), {C == std::string("runtime") ? "runtime-component" : C == std::string("environment") ? "environment"
								                                                                                                           : "launcher"},
								    P->Outputs);
							}
						}
					}
				}
				if (K == "bundle" && T.value("launcher", J::object()).value("mode", "none") == "generated")
				{
					if (Services.LauncherTemplate.empty())
					{
						Fail("launcher-provider", "no compatible launcher template was supplied");
					}
					const auto Launcher = PathUtil::FromUtf8(Services.LauncherTemplate);
					Physical(Launcher);
					if (!fs::is_regular_file(Launcher))
					{
						Fail("launcher-provider", "launcher template must be a regular file", Services.LauncherTemplate);
					}
					Snapshot(Launcher, P->Inputs, P->Request.Limits);
				}
				if (K == "distribution" && !Services.Pack)
				{
					Fail("pack-service", "distribution packaging service is unavailable");
				}
				if (K == "deployment" && (!Services.Deploy || !Services.LeaseDeployment))
				{
					Fail("deploy-service", "dependency deployment and publisher lease services are required");
				}
				if (T.contains("protection") && !T.at("protection").empty() && !Services.Protection)
				{
					Fail("protection-service", "target requires a protection service");
				}
				if (K == "custom")
				{
					const auto Provider = T.at("provider").get<std::string>();
					for (const auto& I : T.value("inputs", J::object()))
					{
						ExistingInput(I, *P, I.is_string() && fs::is_directory(Absolute(I, Root)));
					}
					if (Provider == "process" || Provider == "script")
					{
						if ((Provider == "process" && !Services.AllowProcess) || (Provider == "script" && !Services.AllowScript))
						{
							Fail("step-permission", "custom step requires explicit host permission", Provider);
						}
						const auto& O = T.at("options");
						Keys(O, Provider == "process" ? std::initializer_list<const char*>{"program", "arguments", "cwd", "environment", "toolchainIdentity"} : std::initializer_list<const char*>{"entry", "toolchainIdentity"}, "custom.options");
						if (!O.at(Provider == "process" ? "program" : "entry").is_string() ||
						    (O.contains("toolchainIdentity") && !O.at("toolchainIdentity").is_string()))
						{
							Fail("step-options", "step paths and toolchain identity must be strings");
						}
						if (Provider == "process")
						{
							const auto Arguments = O.value("arguments", J::array()), Variables = O.value("environment", J::object());
							if (!Arguments.is_array() || !Variables.is_object())
							{
								Fail("step-options", "process arguments must be an array and environment must be an object");
							}
							for (const auto& A : Arguments)
							{
								if (A.is_string())
								{
									continue;
								}
								Keys(A, {"input", "output"}, "process.argument");
								if (A.size() != 1 || !A.begin().value().is_string())
								{
									Fail("step-options", "invalid process argument reference");
								}
								const auto Name = A.begin().value().get<std::string>();
								if (!(A.contains("input") ? T.value("inputs", J::object()).contains(Name) : T.at("outputs").contains(Name)))
								{
									Fail("step-options", "unknown named process input or output", Name);
								}
							}
							for (auto V = Variables.begin(); V != Variables.end(); ++V)
							{
								if (!V.value().is_string())
								{
									Fail("step-options", "process environment values must be strings", V.key());
								}
							}
							if (O.contains("cwd"))
							{
								if (!O.at("cwd").is_string() || !fs::is_directory(Absolute(O.at("cwd"), Root)))
								{
									Fail("step-options", "process working directory must exist");
								}
								Physical(Absolute(O.at("cwd"), Root));
							}
						}
						ExistingInput(O.at(Provider == "process" ? "program" : "entry"), *P, false);
						P->ProviderIdentities[Provider] = "veroscript." + Provider;
						P->ProviderVersions[Provider] = "0.1.1";
						if (T.value("cache", false) && O.value("toolchainIdentity", "").empty())
						{
							Fail("step-cache", "cached custom steps require an explicit toolchain identity");
						}
					}
					else
					{
						const auto I = Services.Providers.find(Provider);
						if (I == Services.Providers.end() || !I->second || I->second->GetIdentity().empty() || I->second->GetVersion().empty())
						{
							Fail("provider", "custom build provider is unavailable", Provider);
						}
						if (!I->second->Plan(T.value("options", J::object()).dump(), P->Platform.dump(), Error))
						{
							throw FFailure(Error.Code, Error.Message, Error.Path);
						}
						if (T.value("cache", false) && !I->second->IsDeterministic())
						{
							Fail("provider-cache", "provider has not declared deterministic execution", Provider);
						}
						P->ProviderIdentities[Provider] = I->second->GetIdentity();
						P->ProviderVersions[Provider] = I->second->GetVersion();
					}
				}
			}
			// Conservative source graph snapshot; skip builder-owned and VCS directories.
			for (auto I = fs::recursive_directory_iterator(Root); I != fs::recursive_directory_iterator(); ++I)
			{
				if (Request.CheckCancelled)
				{
					Request.CheckCancelled();
				}
				if (I->is_directory() && (I->path().filename() == ".veroscript" || I->path().filename() == ".git" || Under(I->path(), Output)))
				{
					I.disable_recursion_pending();
					continue;
				}
				if (I->is_symlink())
				{
					I.disable_recursion_pending();
					continue;
				}
				if (I->is_regular_file() && (I->path().extension() == ".vs" || I->path().extension() == ".vslib"))
				{
					Snapshot(I->path(), P->Inputs, P->Request.Limits);
				}
			}
			for (const auto* Name : {"veroscript-project.json", "veroscript.lock"})
			{
				if (fs::exists(Root / Name))
				{
					Snapshot(Root / Name, P->Inputs, P->Request.Limits);
				}
			}
			std::set<fs::path> SearchRoots;
			for (const auto& V : P->ImportPaths)
			{
				SearchRoots.insert(Absolute(V, Root));
			}
			for (const auto& V : P->StdlibPaths)
			{
				SearchRoots.insert(Absolute(V, Root));
			}
			for (const auto& N : P->Order)
			{
				if (P->Targets.at(N).at("kind") == "custom" && P->Targets.at(N).at("provider") == "script")
				{
					SearchRoots.insert(Absolute(P->Targets.at(N).at("options").at("entry"), Root).parent_path());
				}
			}
			for (const auto& N : P->Order)
			{
				for (const auto* Field : {"importPaths", "stdlibSearchPaths"})
				{
					for (const auto& V : P->Targets.at(N).value("compile", J::object()).value(Field, std::vector<std::string>{}))
					{
						SearchRoots.insert(Absolute(V, Root));
					}
				}
			}
			for (const auto& SearchRoot : SearchRoots)
			{
				Physical(SearchRoot);
				for (auto I = fs::recursive_directory_iterator(SearchRoot); I != fs::recursive_directory_iterator(); ++I)
				{
					if (Request.CheckCancelled)
					{
						Request.CheckCancelled();
					}
					if (I->is_directory() && (I->path().filename() == ".veroscript" || I->path().filename() == ".git" || Under(I->path(), Output)))
					{
						I.disable_recursion_pending();
						continue;
					}
					if (I->is_symlink())
					{
						I.disable_recursion_pending();
						continue;
					}
					if (I->is_regular_file() && (I->path().extension() == ".vs" || I->path().extension() == ".vslib"))
					{
						Snapshot(I->path(), P->Inputs, P->Request.Limits);
					}
				}
			}
			J HostCompile = J::object();
			HostCompile["macros"] = J::array();
			for (const auto& M : Services.Compile.PredefinedMacros)
			{
				HostCompile["macros"].push_back({M.Name, M.Parameters, M.Body, M.bVariadic, M.SourceName});
			}
			HostCompile["overlays"] = J::array();
			for (const auto& O : Services.Compile.SourceOverlays)
			{
				HostCompile["overlays"].push_back({O.Path, Application::ContentSha256(O.Source)});
			}
			HostCompile["processors"] = Services.Compile.CompileProcessing.Registry ? Services.Compile.CompileProcessing.Registry->GetFingerprint() : "";
			HostCompile["processorConfiguration"] = Services.Compile.CompileProcessing.ImplementationFingerprint;
			HostCompile["annotations"] = J::array();
			if (Services.Compile.AnnotationProcessing.Registry)
			{
				for (const auto& D : Services.Compile.AnnotationProcessing.Registry->GetProcessors())
				{
					HostCompile["annotations"].push_back({D.Name, D.ProcessorId, D.Version, D.ConfigurationKey, D.Order, D.Targets, D.Operations, D.Cacheable});
				}
			}
			HostCompile["scriptExtensions"] = Services.Compile.ScriptCompileExtensions.Configuration;
			HostCompile["scriptBindings"] = Services.Compile.ScriptCompileExtensions.BindingConfiguration;
			HostCompile["vmBackendPath"] = Services.Compile.ScriptCompileExtensions.VmBackendPath;
			HostCompile["scriptRoots"] = J::array();
			for (const auto& R : Services.Compile.ScriptCompileExtensions.Roots)
			{
				HostCompile["scriptRoots"].push_back({R.Path, R.RegistrationEntry});
			}
			// Assembly rule changes must not reuse receipts from the previous pipeline.
			P->Identity = Application::ContentSha256(J{{"cacheRevision", 3}, {"config", P->Configuration}, {"profile", Request.Profile}, {"platform", P->Platform},
			    {"environment", Services.Identity}, {"core", VEROSCRIPT_CORE_IMPLEMENTATION_ID}, {"inputs", P->Inputs},
			    {"compilePaths", {P->ImportPaths, P->StdlibPaths, P->Defines, P->ImplicitImports}},
			    {"moduleEnvironment", Services.Compile.ModuleEnvironment ? Services.Compile.ModuleEnvironment->GetCompileFingerprint() : ""},
			    {"circular", Services.Compile.bAllowCircularImports}, {"projectAccess", Services.Compile.bAllowProjectWideAccess},
			    {"hostCompile", HostCompile},
			    {"providers", P->ProviderIdentities}, {"versions", P->ProviderVersions}, {"protection", Services.Protection ? Services.Protection->GetIdentity() : ""}}
			        .dump());
			Out = std::shared_ptr<const FBuildPlan>(new FBuildPlan(std::move(P)));
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			Error.Target = Current;
			Error.Stage = "plan";
			return false;
		}
		catch (const std::exception& E)
		{
			Error = {"build-plan", E.what(), Current, {}, "plan"};
			return false;
		}
	}
}
