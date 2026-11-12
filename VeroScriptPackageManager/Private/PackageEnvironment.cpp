// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include "VeroScriptCommandLine.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptVmBackendLoader.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptNetworkProviderHost.h"
#include <algorithm>
#include <array>
#include <fstream>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace VeroScript::Packages
{
	using namespace Detail;
	std::string NativeProviderId(const std::string& Package, const std::string& Module,
	    const std::string& Sha)
	{
		if (!Name(Package) || !Name(Module) || !Digest(Sha))
		{
			throw std::invalid_argument("invalid native provider identity");
		}
		return "native." + Id(J{{"package", Package}, {"module", Module}, {"sha256", Sha}});
	}

	namespace
	{
		std::string NativeIdentity(const fs::path& P, const FWork& W)
		{
			NoLinks(P);
			if (fs::file_size(P) > W.Options.Limits.MaxFileBytes)
			{
				Fail("native-limit", "native catalog file exceeds budget");
			}
			std::ifstream In(P, std::ios::binary);
			if (!In)
			{
				Fail("native-read", "cannot read native file");
			}
			std::array<char, 65536> Buffer;
			std::uint64_t Hash = 14695981039346656037ull;
			while (In)
			{
				W.Check();
				In.read(Buffer.data(), Buffer.size());
				Hash = Runtime::Fnv1a64(Hash, Buffer.data(), static_cast<std::size_t>(In.gcount()));
			}
			if (!In.eof())
			{
				Fail("native-read", "failed reading native identity");
			}
			return "binary-fnv64:" + std::to_string(Hash);
		}
		fs::path LoaderPath(const std::string& Root, const std::string& Relative)
		{
			CheckPath(Relative);
			auto P = U8Path(Root) / U8Path(Relative);
			if (!Under(P, U8Path(Root)))
			{
				Fail("loader-path", "loader entry escapes generated root");
			}
			NoLinks(P);
			return P;
		}
	} // namespace
	namespace
	{
		class FPackageResources final : public IResourceProvider
		{
		public:
			bool Read(const FResourceRequest& R, const FResourceWork& Work, FResourceData& Out,
			    std::string& Error) const override
			{
				try
				{
					Out = {};
					Error.clear();
					if (Work.CheckCancelled)
					{
						Work.CheckCancelled();
					}
					CheckPath(R.Path);
					auto IsGenerated = R.Group == "generated";
					if (!IsGenerated && !Groups.contains(R.Group))
					{
						Fail("resource-group", "package has no such resource group");
					}
					auto Base =
					    IsGenerated ? Generated : Root / U8Path(Groups.at(R.Group).get<std::string>());
					auto P = Base / U8Path(R.Path);
					if (!Under(P, Base))
					{
						Fail("resource-path", "resource escaped package group");
					}
					NoLinks(P, true);
					auto Relative = PathUtil::ToUtf8(P.lexically_relative(IsGenerated ? Generated : Root));
					const auto& Inventory = IsGenerated ? GeneratedFiles : Files;
					auto F = std::find_if(Inventory.begin(), Inventory.end(),
					    [&](const J& V)
					{
						return V.at("path") == Relative;
					});
					if (!fs::exists(P))
					{
						if (F != Inventory.end())
						{
							Fail("resource-integrity", "resource is missing from its immutable installation");
						}
						return true;
					}
					if (F == Inventory.end() || F->at("directory").get<bool>())
					{
						Fail("resource-integrity", "resource is absent from its immutable receipt");
					}
					FClientOptions O;
					O.CheckCancelled = Work.CheckCancelled;
					O.Limits.MaxFileBytes = Work.MaxBytes ? Work.MaxBytes : 64ull << 20;
					FWork W{O};
					auto Bytes = Detail::Read(P, O.Limits.MaxFileBytes, &W);
					auto Digest = Hash(Bytes);
					if (F->at("sha256") != Digest)
					{
						Fail("resource-integrity", "resource changed after installation");
					}
					Out.Bytes.assign(Bytes.begin(), Bytes.end());
					Out.ContentIdentity = std::move(Digest);
					Out.PhysicalPath = NativePathText(P);
					Out.Owner = Owner;
					Out.bExists = true;
					return true;
				}
				catch (const FFailure& X)
				{
					Error = X.Diagnostic.Code + ": " + X.Diagnostic.Message;
					Out = {};
					return false;
				}
				catch (const std::exception& X)
				{
					Error = X.what();
					Out = {};
					return false;
				}
			}

			std::shared_ptr<const FInstalledEnvironment::FImpl> Owner;

			fs::path Root, Generated;

			J Groups, Files, GeneratedFiles;
		};
		struct FAuxiliaryLibrary
		{
			std::shared_ptr<const void> Lease;
			#if defined(_WIN32)
			HMODULE Handle = nullptr;
			~FAuxiliaryLibrary()
			{
				if (Handle)
				{
					FreeLibrary(Handle);
				}
			}
			#else
			void* Handle = nullptr;
			~FAuxiliaryLibrary()
			{
				if (Handle)
				{
					dlclose(Handle);
				}
			}
			#endif
		};
		struct FNativeFiles
		{
			std::shared_ptr<const FInstalledEnvironment::FImpl> Snapshot;
			std::vector<std::shared_ptr<FAuxiliaryLibrary>> Libraries;
		};
		std::shared_ptr<FAuxiliaryLibrary> LoadAuxiliary(const fs::path& P, std::shared_ptr<const void> Lease)
		{
			NoLinks(P);
			auto Owner = std::make_shared<FAuxiliaryLibrary>();
			Owner->Lease = std::move(Lease);
			#if defined(_WIN32)
			Owner->Handle = LoadLibraryExW(
			    P.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			#else
			Owner->Handle = dlopen(P.c_str(), RTLD_NOW | RTLD_LOCAL);
			#endif
			if (!Owner->Handle)
			{
				Fail("native-library", "failed loading a declared native dependency",
				    NativePathText(P.filename()));
			}
			return Owner;
		}

		std::map<std::string, std::string> NativeProviders(const FInstalledPackage& P, const J& Files)
		{
			std::map<std::string, std::string> Result;
			auto M = Parse(P.ManifestJson, {});
			for (const auto& N : M.value("entries", J::object()).value("native", J::array()))
			{
				auto I = std::find_if(Files.begin(), Files.end(),
				    [&](const J& F)
				{
					return F.at("path") == N.at("path") && !F.at("directory").get<bool>();
				});
				if (I == Files.end())
				{
					Fail("native-receipt", "native entry is missing from source inventory", P.Name);
				}
				std::string Module = N.at("id");
				Result[Module] = NativeProviderId(P.Name, Module, I->at("sha256"));
			}
			return Result;
		}

	} // namespace
	const FEnvironmentLayout& FInstalledEnvironment::GetLayout() const
	{
		return Impl->Layout;
	}
	const std::vector<FInstalledPackage>& FInstalledEnvironment::GetPackages() const
	{
		return Impl->Packages;
	}
	std::uint64_t FInstalledEnvironment::GetGeneration() const
	{
		return Impl->Generation;
	}
	const std::string& FInstalledEnvironment::GetFingerprint() const
	{
		return Impl->Fingerprint;
	}
	std::string FInstalledEnvironment::ToJson() const
	{
		J Packages = J::array();
		for (const auto& P : Impl->Packages)
		{
			Packages.push_back({{"name", P.Name},
			    {"version", P.Version},
			    {"sourceId", P.SourceId},
			    {"artifactId", P.ArtifactId},
			    {"sha256", P.Sha256},
			    {"path", P.Path},
			    {"enabled", P.Enabled},
			    {"dependencies", P.Dependencies},
			    {"usage", P.Usage},
			    {"textDomain", P.TextDomain}});
		}
		const char* Scope = Impl->Layout.Scope == EEnvironmentScope::Project ? "project"
		    : Impl->Layout.Scope == EEnvironmentScope::Sdk                   ? "sdk"
		                                                                     : "runtime";
		return J{{"format", "veroscript-installed-environment"},
		    {"formatVersion", FormatVersion},
		    {"scope", Scope},
		    {"root", Impl->Layout.Root},
		    {"management", Impl->Layout.Management},
		    {"packagesDirectory", Impl->Layout.Packages},
		    {"target", TargetJson(Impl->Layout.Target)},
		    {"generation", Impl->Generation},
		    {"fingerprint", Impl->Fingerprint},
		    {"composition", Impl->Composition},
		    {"services", Impl->Project.value("services", J::object())},
		    {"packages", Packages}}
		    .dump(2);
	}
	std::string FInstalledEnvironment::GetSelectedBackend(std::string_view Service) const
	{
		const auto Services = Impl->Project.value("services", J::object());
		const auto Found = Services.find(std::string(Service));
		if (Found == Services.end() || !Found->contains("backend"))
		{
			return std::string(AutomaticBackend);
		}
		const auto Id = Found->at("backend").get<std::string>();
		return Id.empty() ? std::string(DisabledBackend) : Id;
	}
	std::vector<std::string> FInstalledEnvironment::GetBackendCandidates(const std::string_view Service) const
	{
		std::vector<std::string> Candidates;
		for (const auto& Package : Impl->Packages)
		{
			if (!Package.Enabled)
			{
				continue;
			}
			const auto Manifest = Parse(Package.ManifestJson, {});
			for (const auto& Backend : Manifest.value("entries", J::object()).value("backends", J::array()))
			{
				if (Backend.at("service").get_ref<const std::string&>() == Service)
				{
					Candidates.push_back(Backend.at("id").get<std::string>());
				}
			}
		}
		std::sort(Candidates.begin(), Candidates.end());
		return Candidates;
	}
	bool FInstalledEnvironment::HasCommandLineModules() const
	{
		for (const auto& P : Impl->Packages)
		{
			if (!P.Enabled)
			{
				continue;
			}

			const auto M = Parse(P.ManifestJson, {});
			for (const auto& N : M.value("entries", J::object()).value("native", J::array()))
			{
				for (const auto& R : N.at("roles"))
				{
					if (R == "cli") return true;
				}
			}
		}
		return false;
	}
	bool FInstalledEnvironment::BuildCoreEnvironment(const FCoreEnvironmentOptions& O,
	    std::shared_ptr<const FModuleEnvironment>& Out,
	    FDiagnostic& E,
	    std::vector<FNativeServiceProviderDesc>* Providers) const
	{
		try
		{
			E = {};
			Out.reset();
			if (Providers)
			{
				Providers->clear();
			}
			FClientOptions Policy;
			Policy.CheckCancelled = O.CheckCancelled;
			FWork W{Policy};
			W.Check();
			FModuleEnvironmentDesc D;
			D.ScriptCompileExtensions = O.ScriptCompileExtensions;
			D.ScriptCompileExtensions.Roots.clear(); // Installed entries define the package roots.
			std::shared_ptr<Network::FBackendCatalog> NetworkBackends;
			D.Owners.push_back(Impl);
			D.EntryOwnerId = O.EntryOwnerId;
			D.bStrictSources = true;
			D.bAllowAmbientSearch = false;
			std::map<std::string, const FInstalledPackage*> Active;
			for (const auto& P : Impl->Packages)
			{
				if (P.Enabled &&
				    (!O.RuntimeOnly || std::find(P.Usage.begin(), P.Usage.end(), "runtime") != P.Usage.end()))
				{
					Active[P.Name] = &P;
				}
			}
			std::map<std::string, std::map<std::string, std::string>> ProviderIds;
			std::map<std::string, std::vector<std::string>> ScopeIds;
			for (const auto& N : Active)
			{
				ProviderIds[N.first] =
				    NativeProviders(*N.second, Impl->Profile.at("packages").at(N.first).at("inventory"));
				ScopeIds[N.first] = {};
				for (const auto& P : ProviderIds[N.first])
				{
					ScopeIds[N.first].push_back(P.second);
				}
			}
			std::map<std::string, std::string> Choices = O.SelectedBackends;
			auto Configured = Impl->Project.value("services", J::object());
			for (auto I = Configured.begin(); I != Configured.end(); ++I)
			{
				if (!Choices.count(I.key()) && I.value().contains("backend"))
				{
					Choices[I.key()] = I.value()["backend"];
				}
			}
			for (const auto& C : Choices)
			{
				if (C.second.empty() || C.second == AutomaticBackend || C.second == DisabledBackend)
				{
					continue;
				}
				std::string File;
				FDiagnostic Error;
				if (!ResolveBackend(C.first, C.second, File, Error) && Error.Code != "backend-static")
				{
					throw FFailure(Error.Code, Error.Message);
				}
			}
			std::vector<std::string> RootDependencies;
			auto Roots = Impl->Project.value("dependencies", J::object());
			for (auto I = Roots.begin(); I != Roots.end(); ++I)
			{
				auto Usage = I.value().value("usage", std::vector<std::string>{"runtime"});
				if (Active.count(I.key()) &&
				    DependencyApplies(I.value(), Impl->Project, I.key(), Impl->Layout.Target,
				        Impl->Lock.value("development", false)) &&
				    (!O.RuntimeOnly || std::find(Usage.begin(), Usage.end(), "runtime") != Usage.end()))
				{
					RootDependencies.push_back(Active.at(I.key())->TextDomain);
					auto Ns = ScopeIds.at(I.key());
					RootDependencies.insert(RootDependencies.end(), Ns.begin(), Ns.end());
				}
			}
			D.Scopes.push_back({O.EntryOwnerId, RootDependencies});
			if (!O.EntryRoot.empty())
			{
				D.SourceRoots.push_back({NativePathText(Absolute(O.EntryRoot, {})), O.EntryOwnerId});
			}
			if (!O.StdlibSearchPaths.empty())
			{
				D.Scopes.push_back({"package.stdlib", {}, true});
				std::set<std::string> StdlibRoots;
				for (const auto& S : O.StdlibSearchPaths)
				{
					auto Path = NativePathText(Absolute(S, {}));
					// Defaults and explicit search paths may name the same directory.
					// Deduplicate this owner only; conflicting source ownership remains invalid.
					if (!StdlibRoots.insert(PathUtil::IdentityKey(Path)).second)
					{
						continue;
					}
					D.SourceRoots.push_back({Path, "package.stdlib"});
					D.StdlibSearchPaths.push_back(Path);
				}
			}
			J CompileIdentity = J::array();
			std::vector<FInstalledPackage> DisplayPackages;
			for (const auto& Pair : Active)
			{
				const auto& P = *Pair.second;
				W.Check();
				const auto& Receipt = Impl->Profile.at("packages").at(P.Name);
				VerifyInventory(U8Path(P.Path), Receipt.at("inventory"), W);
				VerifyInventory(U8Path(P.GeneratedPath), Receipt.at("generatedInventory"), W);
				auto M = Manifest(Parse(P.ManifestJson, {}));
				auto Natives = ScopeIds[P.Name];
				std::vector<std::string> Dependencies = Natives;
				for (const auto& N : O.RuntimeOnly ? P.RuntimeDependencies : P.Dependencies)
				{
					if (!Active.count(N))
					{
						Fail("runtime-domain",
						    "active package execution domain omits its required dependency", N);
					}
					Dependencies.push_back(Active.at(N)->TextDomain);
					auto Ns = ScopeIds.at(N);
					Dependencies.insert(Dependencies.end(), Ns.begin(), Ns.end());
				}
				D.Scopes.push_back({P.TextDomain, Dependencies});
				D.SourceRoots.push_back({P.Path, P.TextDomain});
				D.SourceRoots.push_back({P.GeneratedPath, P.TextDomain});
				for (const auto& N : Natives)
				{
					auto NativeDependencies = Dependencies;
					NativeDependencies.erase(
					    std::remove(NativeDependencies.begin(), NativeDependencies.end(), N),
					    NativeDependencies.end());
					NativeDependencies.push_back(P.TextDomain);
					D.Scopes.push_back({N, NativeDependencies});
				}
				auto Modules = M.value("entries", J::object()).value("modules", J::object());
				for (auto I = Modules.begin(); I != Modules.end(); ++I)
				{
					auto Path = U8Path(P.Path) / U8Path(I.value().get<std::string>());
					D.Modules.push_back({I.key(), P.TextDomain + ":" + I.key(), NativePathText(Path), P.TextDomain,
					    HashFile(Path, W, W.Options.Limits.MaxFileBytes)});
				}
				auto Resources = std::make_shared<FPackageResources>();
				Resources->Owner = Impl;
				Resources->Root = U8Path(P.Path);
				Resources->Generated = U8Path(P.GeneratedPath);
				Resources->Groups = M.value("resources", J::object());
				Resources->Files = Impl->Profile.at("packages").at(P.Name).at("inventory");
				Resources->GeneratedFiles = Impl->Profile.at("packages").at(P.Name).at("generatedInventory");
				FResourceProviderBinding Binding;
				Binding.Owner = Impl;
				Binding.Provider = Resources;
				Binding.Id = P.TextDomain;
				Binding.OwnerId = P.TextDomain;
				Binding.ContractIdentity = "veroscript.package-resources:0.1.1";
				Binding.ModuleIds = {P.TextDomain};
				D.Resources.push_back(std::move(Binding));
				DisplayPackages.push_back(P);
				if (!O.RuntimeOnly)
				{
					for (const auto& C : M.value("entries", J::object()).value("compiler", J::array()))
					{
						D.ScriptCompileExtensions.Roots.push_back(
						    {NativePathText(U8Path(P.Path) / U8Path(C.at("path").get<std::string>())),
						        C.value("registrationEntry", "register_extension")});
					}
				}
				CompileIdentity.push_back({{"owner", P.TextDomain},
				    {"configuration", Parse(P.ConfigurationJson, {})},
				    {"generated", Resources->GeneratedFiles}});
			}
			D.CompileConfiguration = Id(CompileIdentity);
			D.SelectedBackends = Choices;
			D.ExecutionConfiguration =
			    Id(J{{"configuration", CompileIdentity}, {"services", Configured}, {"selected", Choices}});
			if (!O.AnalysisOnly)
			{
				auto Snapshot = Impl;
				if (O.RuntimeOnly)
				{
					Snapshot = std::make_shared<FImpl>(*Impl);
					for (auto& Package : Snapshot->Packages)
					{
						Package.Enabled = Active.count(Package.Name) != 0;
					}
				}
				auto Installed = std::shared_ptr<const FInstalledEnvironment>(new FInstalledEnvironment(std::move(Snapshot)));
				D.ExecutionBackends.Identity = GetFingerprint() + ':' + D.ExecutionConfiguration;
				D.ExecutionBackends.Vm = [Installed, Choices](const std::string_view Request, auto& Factory, std::string& Failure)
				{
					FDiagnostic Diagnostic;
					const bool Success = Installed->LoadVmBackend(ResolveBackendSelection("vm", Request, Choices), Factory, Diagnostic);
					Failure = Diagnostic.Message;
					return Success;
				};
				D.ExecutionBackends.Jit = [Installed, Choices](const std::string_view Request, auto& Factory, std::string& Failure)
				{
					FDiagnostic Diagnostic;
					const bool Success = Installed->LoadJitBackend(ResolveBackendSelection("jit", Request, Choices), Factory, Diagnostic);
					Failure = Diagnostic.Message;
					return Success;
				};
			}
			D.I18nServices = DisplayServices(DisplayPackages, W);
			D.CompileProcessing.CheckCancelled = O.CheckCancelled;
			D.ArtifactDecoder = O.ArtifactDecoder;
			std::string Error;
			auto ResourceEnvironment = FModuleEnvironment::Create(D, Error);
			if (!ResourceEnvironment)
			{
				Fail("module-environment", Error);
			}
			auto Registry = std::make_shared<FNativeExtensionRegistry>(O.BaseBindings, O.BaseProcessors);
			auto AnalysisBindings = std::make_shared<FHostBindingRegistry>(
			    O.BaseBindings ? *O.BaseBindings : FHostBindingRegistry{});
			// Binding signatures may refer to host types supplied by dependencies.
			// Register both native modules and analysis catalogs in graph order.
			std::vector<const FInstalledPackage*> RegistrationOrder;
			std::set<std::string> RegistrationDone, Pending;
			std::function<void(const std::string&)> Visit = [&](const std::string& Name)
			{
				W.Check();
				if (RegistrationDone.count(Name))
				{
					return;
				}
				if (!Pending.insert(Name).second)
				{
					Fail("dependency-cycle", "native registration dependency cycle", Name);
				}
				if (Pending.size() > 256)
				{
					Fail("dependency-depth", "native registration exceeds safe nesting", Name);
				}
				const auto& Package = *Active.at(Name);
				for (const auto& Dependency : O.RuntimeOnly ? Package.RuntimeDependencies : Package.Dependencies)
				{
					Visit(Dependency);
				}
				Pending.erase(Name);
				RegistrationDone.insert(Name);
				RegistrationOrder.push_back(&Package);
			};
			for (const auto& Pair : Active)
			{
				Visit(Pair.first);
			}
			for (const auto* Package : RegistrationOrder)
			{
				const auto& P = *Package;
				auto M = Parse(P.ManifestJson, {});
				auto Entries = M.value("entries", J::object());
				auto Loaders = Parse(P.LoadersJson, {});
				VerifyLoaders(U8Path(P.GeneratedPath), Loaders, W);
				for (const auto& N : Entries.value("native", J::array()))
				{
					W.Check();
					bool Load = false;
					std::uint32_t Roles = 0;
					for (const auto& R : N.at("roles"))
					{
						if (R == "bindings")
						{
							Roles |= 1;
							Load = true;
						}
						if (R == "compiler")
						{
							Roles |= 2;
							Load = Load || !O.RuntimeOnly;
						}
						if (R == "provider")
						{
							Roles |= 4;
						}
						if (R == "cli")
						{
							Roles |= 8;
							Load = Load || (!O.AnalysisOnly && std::any_of(O.Services.begin(), O.Services.end(), [](const auto& S)
							{
								return S.Id == CommandLineServiceId;
							}));
						}
					}
					bool bAutomaticProvider = false, bExplicitProvider = false;
					for (const auto& B : Entries.value("backends", J::array()))
					{
						if (B.contains("module") && B["module"] == N["id"])
						{
							const auto& Service = B.at("service").get_ref<const std::string&>();
							// Registration exposes factories; devices/windows are created only
							// by the consuming service. Automatic selection needs all candidates.
							const auto Choice = ResolveBackendSelection(Service, {}, Choices);
							bAutomaticProvider |= Choice == AutomaticBackend;
							bExplicitProvider |= Choice == B.at("id").get_ref<const std::string&>();
							Load = Load || bAutomaticProvider || bExplicitProvider;
						}
					}
					if (!Load || (O.AnalysisOnly && !(Roles & 3u)))
					{
						continue;
					}
					// Only optional provider registrations may be skipped. Bindings, compiler
					// extensions and explicitly selected providers remain required.
					const bool bOptionalProvider = Roles == NativeExtensionRole(ENativeExtensionRole::Provider) && bAutomaticProvider && !bExplicitProvider;
					const auto RejectProvider = [&](const std::string& Failure)
					{
						for (const auto& Backend : Entries.value("backends", J::array()))
						{
							if (Backend.contains("module") && Backend["module"] == N["id"])
							{
								D.BackendDiagnostics[Backend.at("service").get<std::string>()].push_back(
								    {Backend.at("id").get<std::string>(), Failure});
							}
						}
					};
					auto Provider = ProviderIds.at(P.Name).at(N.at("id").get<std::string>());
					auto Key = "native:" + N.at("id").get<std::string>();
					if (O.AnalysisOnly)
					{
						if (!N.contains("catalog") || !N.contains("componentVersion"))
						{
							Fail("native-catalog",
							    "analysis requires catalog and componentVersion for each native binding "
							    "module",
							    P.Name);
						}
						auto Catalog = Parse(Read(U8Path(P.Path) / U8Path(N["catalog"].get<std::string>()),
						                         W.Options.Limits.MaxManifestBytes, &W),
						    W.Options.Limits);
						for (auto* List : {"functions", "types", "typeAliases", "genericTypeTemplates"})
						{
							if (Catalog.contains(List))
							{
								for (auto& Declaration : Catalog[List])
								{
									Declaration["origin"] = {
									    {"providerId", Provider}, {"modulePath", ""}, {"exported", true}};
								}
							}
						}
						if (!ImportBindingCatalogJson(Catalog.dump(), *AnalysisBindings, Error, false))
						{
							Fail("native-catalog", Error);
						}
						auto Source = U8Path(P.Path) / U8Path(N["path"].get<std::string>());
						auto Static = O.StaticExtensions.find(P.Name + ":" + N.at("id").get<std::string>());
						auto ContentIdentity = N.at("linkage") == "static" &&
						        Static != O.StaticExtensions.end() &&
						        !Static->second.ContentIdentity.empty()
						    ? Static->second.ContentIdentity
						    : NativeIdentity(Source, W);
						D.AvailableDependencies.push_back({Provider, N["componentVersion"],
						    M.at("target").at("abis").at("native-extension"),
						    M.at("target").at("coreBuild"),
						    std::move(ContentIdentity)});
						continue;
					}
					std::shared_ptr<const FLoadedNativeExtension> Module;
					try
					{
						if (TargetJson(Impl->Layout.Target) != TargetJson(Impl->Layout.Host))
						{
							Fail("cross-target-load",
							    "target native modules cannot execute in the package-manager host");
						}
						auto Files = std::make_shared<FNativeFiles>();
						Files->Snapshot = Impl;
						FNativeExtensionLoadOptions Options;
						Options.FileLease = Files;
						Options.ExpectedId = N["id"];
						Options.ExpectedVersion = N.value("componentVersion", "");
						Options.ExpectedRoles = Roles;
						Options.CheckCancelled = O.CheckCancelled;
						if (N.at("linkage") == "static")
						{
							auto I = O.StaticExtensions.find(P.Name + ":" + N.at("id").get<std::string>());
							if (I == O.StaticExtensions.end())
							{
								Fail("static-registration",
								    "static package needs an explicitly linked query registration", P.Name);
							}
							Options.ContentIdentity = I->second.ContentIdentity.empty()
							    ? NativeIdentity(U8Path(P.Path) / U8Path(N.at("path").get<std::string>()), W)
							    : I->second.ContentIdentity;
							Module = QueryNativeExtension(I->second.Query, I->second.Owner, Error, Options);
						}
						else
						{
							if (!Loaders.contains(Key))
							{
								Fail("loader-missing", "native loader view is absent; rerun install");
							}
							for (const auto& L : Loaders[Key].at("libraries"))
							{
								Files->Libraries.push_back(
								    LoadAuxiliary(LoaderPath(P.GeneratedPath, L.get<std::string>()), Impl));
							}
							auto Path = LoaderPath(P.GeneratedPath, Loaders[Key].at("path"));
							Module = LoadNativeExtension(NativePathText(Path), Error, Options);
						}
						W.Check();
						if (!Module)
						{
							Fail("native-load", Error, P.Name);
						}
					}
					catch (const FFailure& Failure)
					{
						W.Check();
						const auto& Code = Failure.Diagnostic.Code;
						if (!bOptionalProvider || (Code != "native-load" && Code != "native-library" && Code != "static-registration" && Code != "cross-target-load"))
						{
							throw;
						}
						RejectProvider(Code + ": " + Failure.Diagnostic.Message);
						continue;
					}
					FNativeExtensionRegistrationOptions Registration;
					Registration.Origin.ProviderId = Provider;
					Registration.ResourceEnvironment = ResourceEnvironment;
					Registration.Services = O.Services;
					const auto NetworkCatalog = std::find_if(Registration.Services.begin(), Registration.Services.end(),
					    [](const auto& Service)
					{
						return Service.Id == Network::BackendCatalogId;
					});
					if (Roles & NativeExtensionRole(ENativeExtensionRole::Provider))
					{
						Network::FBackendCatalog::RemoveFromProviderRegistration(Registration);
					}
					else if (NetworkCatalog == Registration.Services.end() &&
					    (std::string_view(Module->GetDescriptor().Id) == "network" ||
					        std::string_view(Module->GetDescriptor().Id) == "http" ||
					        std::string_view(Module->GetDescriptor().Id) == "websocket"))
					{
						if (!NetworkBackends)
						{
							NetworkBackends = std::make_shared<Network::FBackendCatalog>();
						}
						Registration.Services.push_back(NetworkBackends->Binding());
					}
					if (!(Roles & NativeExtensionRole(ENativeExtensionRole::CommandLine)))
					{
						Registration.Services.erase(std::remove_if(Registration.Services.begin(), Registration.Services.end(),
						                                [](const auto& Service)
						{
							return Service.Id == CommandLineServiceId;
						}),
						    Registration.Services.end());
					}
					Registration.ConfigurationKey = Hash(P.ConfigurationJson);
					auto Before = Registry->GetProviders().size();
					auto Config = Parse(P.ConfigurationJson, {});
					for (auto I = Config.begin(); I != Config.end(); ++I)
					{
						Registration.Configuration[I.key()] =
						    I.value().is_string() ? I.value().get<std::string>() : I.value().dump();
					}
					J PackageContext = {{"name", P.Name},
					    {"version", P.Version},
					    {"resourceProviderId", P.TextDomain},
					    {"moduleId", P.TextDomain},
					    {"textDomain", P.TextDomain},
					    {"dependencies", J::object()}};
					for (const auto& DependencyName : P.Dependencies)
					{
						if (Active.count(DependencyName))
						{
							const auto& Dep = *Active.at(DependencyName);
							PackageContext["dependencies"][DependencyName] = {{"version", Dep.Version},
							    {"resourceProviderId", Dep.TextDomain},
							    {"moduleId", Dep.TextDomain},
							    {"textDomain", Dep.TextDomain}};
						}
					}
					if (Registration.Configuration.count("veroscript.package"))
					{
						Fail("reserved-configuration",
						    "veroscript.package is installer-owned native context");
					}
					Registration.Configuration["veroscript.package"] = PackageContext.dump();
					if (!Registry->Add(std::move(Module), std::move(Registration), Error))
					{
						W.Check();
						if (bOptionalProvider)
						{
							RejectProvider("native-registration: " + Error);
							continue;
						}
						Fail(Error.find("parallel") != std::string::npos ? "restart-required"
						                                                 : "native-registration",
						    Error, P.Name);
					}
					auto Actual = Registry->GetProviders();
					std::set<std::string> Expected, Registered;
					for (const auto& B : Entries.value("backends", J::array()))
					{
						if (B.contains("module") && B["module"] == N["id"])
						{
							Expected.insert(B.at("service").get<std::string>() + ":" +
							    B.at("id").get<std::string>());
						}
					}
					for (std::size_t I = Before; I < Actual.size(); ++I)
					{
						Registered.insert(Actual[I].ServiceId + ":" + Actual[I].BackendId);
					}
					if (Expected != Registered)
					{
						Fail("provider-mismatch",
						    "native provider IDs do not match the declaring module's backend entries",
						    P.Name);
					}
				}
			}
			D.CompileProcessing.Registry = Registry->GetProcessors();
			D.ServiceProviders = Registry->GetProviders();
			if (NetworkBackends)
			{
				std::map<std::string, std::map<std::string, std::string>> NetworkConfiguration;
				for (auto Service = Configured.begin(); Service != Configured.end(); ++Service)
				{
					const auto Configuration = Service.value().value("configuration", J::object());
					for (auto Item = Configuration.begin(); Item != Configuration.end(); ++Item)
					{
						NetworkConfiguration[Service.key()][Item.key()] = Item.value().is_string()
						    ? Item.value().get<std::string>()
						    : Item.value().dump();
					}
				}
				NetworkBackends->Update(Registry->GetProviders(), ResourceEnvironment, Choices, std::move(NetworkConfiguration));
			}
			if (O.AnalysisOnly)
			{
				D.Bindings = AnalysisBindings;
			}
			else
			{
				D.Bindings = Registry->GetBindings();
				D.CompileProcessing.Registry = Registry->GetProcessors();
				auto Contracts = Registry->GetDependencies();
				D.AvailableDependencies.insert(D.AvailableDependencies.end(), Contracts.begin(),
				    Contracts.end());
				auto Owners = Registry->GetOwners();
				D.Owners.insert(D.Owners.end(), Owners.begin(), Owners.end());
			}
			if (O.PrepareBindings)
			{
				auto Prepared = std::make_shared<FHostBindingRegistry>(
				    O.AnalysisOnly ? *AnalysisBindings : *Registry->GetBindings());
				if (!O.PrepareBindings(*Prepared, Error))
				{
					Fail("binding-preparation", Error);
				}
				D.Bindings = std::move(Prepared);
			}
			// Script/resource-only environments have no host catalog. Keeping an
			// artificial empty catalog changes prepared-program host identity and
			// prevents standalone exporters from reusing ordinary script programs.
			if (!O.BaseBindings && !O.PrepareBindings && D.AvailableDependencies.empty() &&
			    Registry->GetDependencies().empty())
			{
				D.Bindings.reset();
			}
			if (Providers && !O.AnalysisOnly)
			{
				*Providers = Registry->GetProviders();
			}
			Out = FModuleEnvironment::Create(std::move(D), Error);
			if (!Out)
			{
				Fail("module-environment", Error);
			}
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			Out.reset();
			if (Providers)
			{
				Providers->clear();
			}
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "environment-build";
			E.Message = X.what();
			Out.reset();
			if (Providers)
			{
				Providers->clear();
			}
			return false;
		}
	}
	bool FInstalledEnvironment::ResolveBackend(const std::string& Service, const std::string& Backend,
	    std::string& Path, FDiagnostic& E) const
	{
		try
		{
			const auto RequestedService = Service;
			const auto RequestedBackend = Backend;
			E = {};
			Path.clear();
			unsigned Count = 0;
			for (const auto& P : Impl->Packages)
			{
				if (P.Enabled)
				{
					auto M = Manifest(Parse(P.ManifestJson, {}));
					for (const auto& B : M.value("entries", J::object()).value("backends", J::array()))
					{
						if (B.at("service") == RequestedService && B.at("id") == RequestedBackend)
						{
							++Count;
							auto Loaders = Parse(P.LoadersJson, {});
							auto Key = B.contains("module") ? "native:" + B["module"].get<std::string>()
							                                : "backends:" + RequestedService + ":" + RequestedBackend;
							if (!Loaders.contains(Key))
							{
								Fail("backend-static", "backend requires explicit static host registration");
							}
							Path = NativePathText(LoaderPath(P.GeneratedPath, Loaders[Key].at("path")));
							NoLinks(U8Path(Path));
						}
					}
				}
			}
			if (!Count)
			{
				Fail("backend-unavailable", "backend ID is not present in the active environment",
				    RequestedService + ":" + RequestedBackend);
			}
			if (Count != 1)
			{
				Fail("backend-conflict", "backend ID has multiple active providers");
			}
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			Path.clear();
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "backend-error";
			E.Message = X.what();
			Path.clear();
			return false;
		}
	}
	bool FInstalledEnvironment::LoadJitBackend(const std::string& Id,
	    std::shared_ptr<const IJitBackendFactory>& Out,
	    FDiagnostic& E, FBackendSelectionResult* Selection) const
	{
		const auto SelectionId = Id;
		if (SelectionId.empty() || SelectionId == AutomaticBackend || SelectionId == DisabledBackend)
		{
			Out.reset();
			E = {};
			const auto Request = SelectionId.empty() ? GetSelectedBackend("jit") : SelectionId;
			auto Result = SelectServiceBackend("jit", Request, GetBackendCandidates("jit"),
			    [&](const std::string_view Candidate, std::string& Error)
			{
				FDiagnostic Failure;
				const bool Loaded = LoadJitBackend(std::string(Candidate), Out, Failure);
				Error = Failure.Code + ": " + Failure.Message;
				return Loaded;
			});
			const bool Success = Result.State != EBackendSelectionState::Failed;
			if (!Success)
			{
				E.Code = "backend-selection";
				E.Message = Result.Error;
			}
			if (Selection)
			{
				*Selection = std::move(Result);
			}
			return Success;
		}
		if (Selection)
		{
			*Selection = {};
			Selection->State = EBackendSelectionState::Failed;
		}
		try
		{
			const auto RequestedId = SelectionId;
			Out.reset();
			E = {};
			std::string Path;
			if (!ResolveBackend("jit", RequestedId, Path, E))
			{
				if (Selection)
				{
					Selection->Error = E.Message;
				}
				return false;
			}
			if (TargetJson(Impl->Layout.Target) != TargetJson(Impl->Layout.Host))
			{
				Fail("cross-target-load", "target JIT libraries cannot execute in the package-manager host");
			}
			FClientOptions Options;
			FWork W{Options};
			auto Files = std::make_shared<FNativeFiles>();
			Files->Snapshot = Impl;
			for (const auto& P : Impl->Packages)
			{
				if (P.Enabled)
				{
					auto Loaders = Parse(P.LoadersJson, {});
					VerifyLoaders(U8Path(P.GeneratedPath), Loaders, W);
					auto Key = "backends:jit:" + RequestedId;
					if (Loaders.contains(Key) &&
					    NativePathText(LoaderPath(P.GeneratedPath, Loaders[Key].at("path"))) == Path)
					{
						VerifyInventory(U8Path(P.GeneratedPath),
						    Impl->Profile.at("packages").at(P.Name).at("generatedInventory"), W);
						for (const auto& L : Loaders[Key].at("libraries"))
						{
							Files->Libraries.push_back(
							    LoadAuxiliary(LoaderPath(P.GeneratedPath, L.get<std::string>()), Impl));
						}
					}
				}
			}
			std::string Error;
			auto Factory = VeroScript::LoadJitBackend(Path, Error);
			if (!Factory)
			{
				Fail("jit-load", Error);
			}
			if (Factory->GetDescription().Id != RequestedId)
			{
				Fail("jit-identity", "loaded JIT backend ID differs from the installed manifest");
			}
			struct FLease
			{
				std::shared_ptr<FNativeFiles> Files;
				std::shared_ptr<const IJitBackendFactory> Factory;
			};
			auto Lease = std::make_shared<FLease>();
			Lease->Files = std::move(Files);
			Lease->Factory = std::move(Factory);
			Out = std::shared_ptr<const IJitBackendFactory>(Lease, Lease->Factory.get());
			if (Selection)
			{
				*Selection = {};
				Selection->State = EBackendSelectionState::Selected;
				Selection->BackendId = RequestedId;
			}
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			Out.reset();
			if (Selection)
			{
				Selection->Error = E.Message;
			}
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "jit-load";
			E.Message = X.what();
			Out.reset();
			if (Selection)
			{
				Selection->Error = E.Message;
			}
			return false;
		}
	}
	bool FInstalledEnvironment::LoadVmBackend(const std::string& Id,
	    std::shared_ptr<const IVmBackendFactory>& Out,
	    FDiagnostic& E, FBackendSelectionResult* Selection) const
	{
		const auto SelectionId = Id;
		if (SelectionId.empty() || SelectionId == AutomaticBackend || SelectionId == DisabledBackend)
		{
			Out.reset();
			E = {};
			const auto Request = SelectionId.empty() ? GetSelectedBackend("vm") : SelectionId;
			auto Result = SelectServiceBackend("vm", Request, GetBackendCandidates("vm"),
			    [&](const std::string_view Candidate, std::string& Error)
			{
				FDiagnostic Failure;
				const bool Loaded = LoadVmBackend(std::string(Candidate), Out, Failure);
				Error = Failure.Code + ": " + Failure.Message;
				return Loaded;
			});
			const bool Success = Result.State != EBackendSelectionState::Failed;
			if (!Success)
			{
				E.Code = "backend-selection";
				E.Message = Result.Error;
			}
			if (Selection)
			{
				*Selection = std::move(Result);
			}
			return Success;
		}
		if (Selection)
		{
			*Selection = {};
			Selection->State = EBackendSelectionState::Failed;
		}
		try
		{
			const auto RequestedId = SelectionId;
			Out.reset();
			E = {};
			std::string Path;
			if (!ResolveBackend("vm", RequestedId, Path, E))
			{
				if (Selection)
				{
					Selection->Error = E.Message;
				}
				return false;
			}
			if (TargetJson(Impl->Layout.Target) != TargetJson(Impl->Layout.Host))
			{
				Fail("cross-target-load", "target VM libraries cannot execute in the package-manager host");
			}
			FClientOptions Options;
			FWork W{Options};
			auto Files = std::make_shared<FNativeFiles>();
			Files->Snapshot = Impl;
			for (const auto& P : Impl->Packages)
			{
				if (P.Enabled)
				{
					auto Loaders = Parse(P.LoadersJson, {});
					VerifyLoaders(U8Path(P.GeneratedPath), Loaders, W);
					auto Key = "backends:vm:" + RequestedId;
					if (Loaders.contains(Key) &&
					    NativePathText(LoaderPath(P.GeneratedPath, Loaders[Key].at("path"))) == Path)
					{
						VerifyInventory(U8Path(P.GeneratedPath),
						    Impl->Profile.at("packages").at(P.Name).at("generatedInventory"), W);
						for (const auto& L : Loaders[Key].at("libraries"))
						{
							Files->Libraries.push_back(
							    LoadAuxiliary(LoaderPath(P.GeneratedPath, L.get<std::string>()), Impl));
						}
					}
				}
			}
			std::string Error;
			auto Factory = VeroScript::LoadVmBackend(Path, Error);
			if (!Factory)
			{
				Fail("vm-load", Error);
			}
			if (Factory->GetDescription().Id != RequestedId)
			{
				Fail("vm-identity", "loaded VM backend ID differs from the installed manifest");
			}
			struct FLease
			{
				std::shared_ptr<FNativeFiles> Files;
				std::shared_ptr<const IVmBackendFactory> Factory;
			};
			auto Lease = std::make_shared<FLease>();
			Lease->Files = std::move(Files);
			Lease->Factory = std::move(Factory);
			Out = std::shared_ptr<const IVmBackendFactory>(Lease, Lease->Factory.get());
			if (Selection)
			{
				*Selection = {};
				Selection->State = EBackendSelectionState::Selected;
				Selection->BackendId = RequestedId;
			}
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			Out.reset();
			if (Selection)
			{
				Selection->Error = E.Message;
			}
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "vm-load";
			E.Message = X.what();
			Out.reset();
			if (Selection)
			{
				Selection->Error = E.Message;
			}
			return false;
		}
	}
	bool FInstalledEnvironment::DisplaySnapshot(const I18n::FI18nConfiguration& In, I18n::FI18nSnapshot& Out,
	    FDiagnostic& E) const
	{
		try
		{
			E = {};
			FClientOptions O;
			O.DisplayConfiguration = In;
			FWork W{O};
			auto Services = DisplayServices(Impl->Packages, W);
			auto Config = In;
			Config.FallbackLanguages.erase(
			    std::remove(Config.FallbackLanguages.begin(), Config.FallbackLanguages.end(), "en-US"),
			    Config.FallbackLanguages.end());
			Config.FallbackLanguages.push_back("en-US");
			std::shared_ptr<I18n::FI18nContext> Context;
			I18n::FI18nError Error;
			if (!I18n::CreateContext(Config, Services, Context, Error))
			{
				Fail("display-context", Error.Diagnostic.Message);
			}
			Out = Context->CaptureSnapshot();
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
			E.Code = "display-error";
			E.Message = X.what();
			Out = {};
			return false;
		}
	}
	FInstalledEnvironment::FInstalledEnvironment(std::shared_ptr<FImpl> P) : Impl(std::move(P))
	{
	}
} // namespace VeroScript::Packages
