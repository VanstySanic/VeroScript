// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptVmBackend.h"
#include "VeroScriptCompile.h"
#include "VeroScriptModuleSupport.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptRun.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPackagePath.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace VeroScript
{
	namespace
	{
		bool MemoryPath(const std::string& S)
		{
			return S.rfind("veroscript-module:/", 0) == 0;
		}
		std::string SourceKey(const std::string& S)
		{
			if (MemoryPath(S) || (!S.empty() && S.front() == '<')) return PathUtil::Normalize(S);
			const auto P = ModuleSupport::Physical(S);
			return P.empty() ? std::string{} : PathUtil::IdentityKey(P);
		}
		void AppendDependency(std::string& S, const FExternalDependency& D)
		{
			for (const auto* V : {&D.Id, &D.Version, &D.Abi, &D.BuildIdentity, &D.ContentIdentity})
			{
				ModuleSupport::Append(S, *V);
			}
		}
	}
	std::shared_ptr<const FModuleEnvironment> FModuleEnvironment::Create(FModuleEnvironmentDesc Descriptor, std::string& Error)
	{
		Error.clear();
		auto Fail = [&](const std::string& E) -> std::shared_ptr<const FModuleEnvironment>
		{
			Error = E;
			return {};
		};
		if (Descriptor.Scopes.size() > 65536 || Descriptor.Modules.size() > 65536 ||
		    Descriptor.SourceRoots.size() > 65536 || Descriptor.Resources.size() > 4096)
		{
			return Fail("module environment exceeds declaration limits");
		}

		if (!ValidateExternalDependencies(Descriptor.AvailableDependencies, Error) ||
		    !CheckExternalDependencies(
		        Descriptor.RequiredDependencies, Descriptor.AvailableDependencies, Error))
		{
			return {};
		}

		auto E = std::shared_ptr<FModuleEnvironment>(new FModuleEnvironment(std::move(Descriptor)));
		auto& C = E->Description;
		if (C.CompileProcessing.ModuleEnvironment)
		{
			return Fail("environment descriptions require unapplied processing options");
		}
		std::sort(C.Scopes.begin(), C.Scopes.end(),
		    [](const auto& A, const auto& B)
		{
			return A.Id < B.Id;
		});
		for (std::size_t I = 0; I < C.Scopes.size(); ++I)
		{
			auto& S = C.Scopes[I];
			if (!ModuleSupport::Text(S.Id) || !E->Scopes.emplace(S.Id, I).second) return Fail("invalid or duplicate module scope");
			std::sort(S.DirectDependencies.begin(), S.DirectDependencies.end());
			if (std::adjacent_find(S.DirectDependencies.begin(), S.DirectDependencies.end()) != S.DirectDependencies.end()) return Fail("duplicate direct module dependency");
		}
		if (!E->Scopes.count(C.EntryOwnerId)) return Fail("entry module scope is not declared");
		for (const auto& S : C.Scopes)
		{
			for (const auto& Id : S.DirectDependencies)
			{
				if (!E->Scopes.count(Id) || Id == S.Id)
				{
					return Fail("invalid direct module dependency: " + Id);
				}
			}
		}

		std::set<std::string> RootKeys;
		for (auto& R : C.SourceRoots)
		{
			R.Path = ModuleSupport::Physical(R.Path);
			if (R.Path.empty() || !E->Scopes.count(R.OwnerId) || !RootKeys.insert(SourceKey(R.Path)).second) return Fail("invalid or duplicate source root");
			E->CanonicalRoots.emplace_back(SourceKey(R.Path), R.OwnerId);
		}
		std::sort(C.SourceRoots.begin(), C.SourceRoots.end(),
		    [](const auto& A, const auto& B)
		{
			return A.Path < B.Path;
		});
		std::sort(C.Modules.begin(), C.Modules.end(),
		    [](const auto& A, const auto& B)
		{
			return A.Name < B.Name;
		});
		std::map<std::string, std::string> Ids;
		std::size_t MemoryBytes = 0;
		std::uint64_t BinaryBytes = 0;
		std::map<const void*, std::shared_ptr<const std::vector<std::uint8_t>>> FrozenBinary;
		for (std::size_t I = 0; I < C.Modules.size(); ++I)
		{
			auto& M = C.Modules[I];
			if (!ModuleSupport::Relative(M.Name) || !ModuleSupport::Text(M.Id) ||
			    !E->Scopes.count(M.OwnerId) || !ModuleSupport::Text(M.ContentIdentity, true) ||
			    !E->Names.emplace(M.Name, I).second)
			{
				return Fail("invalid or duplicate resolved module: " + M.Name);
			}

			if (MemoryPath(M.Path))
			{
				if ((!M.Source && !M.Binary && !C.ArtifactReader) || !ModuleSupport::Relative(M.Path.substr(19))) return Fail("memory module requires a canonical path and content");
			}
			else
			{
				M.Path = ModuleSupport::Physical(M.Path);
			}

			if (M.Path.empty()) return Fail("invalid resolved module source");
			if (M.Source && M.Binary) return Fail("module cannot contain both source and binary content");
			if (M.Binary)
			{
				const auto Existing = FrozenBinary.find(M.Binary.get());
				if (Existing == FrozenBinary.end())
				{
					if (M.Binary->empty() || M.Binary->size() > VEROSCRIPT_PACKAGE_MAX_BYTES - BinaryBytes) return Fail("memory module binary budget exceeded");
					BinaryBytes += M.Binary->size();
					const auto Original = M.Binary.get();
					M.Binary = std::make_shared<const std::vector<std::uint8_t>>(*M.Binary);
					FrozenBinary.emplace(Original, M.Binary);
				}
				else
				{
					M.Binary = Existing->second;
				}
			}
			const auto Key = SourceKey(M.Path);
			const auto Identity = Ids.emplace(M.Id, Key);
			if (!Identity.second && Identity.first->second != Key) return Fail("module identity has conflicting sources");
			const auto Source = E->Sources.emplace(Key, I);
			if (!Source.second)
			{
				const auto& First = C.Modules[Source.first->second];
				if (First.Id != M.Id || First.OwnerId != M.OwnerId ||
				    First.ContentIdentity != M.ContentIdentity || First.Source != M.Source ||
				    static_cast<bool>(First.Binary) != static_cast<bool>(M.Binary) ||
				    (First.Binary && *First.Binary != *M.Binary) ||
				    First.RequiredDependencies != M.RequiredDependencies)
				{
					return Fail("module aliases have conflicting contents or ownership");
				}
			}
			if (M.Source)
			{
				if (M.Source->size() > 64u * 1024u * 1024u - MemoryBytes) return Fail("memory module source budget exceeded");
				MemoryBytes += M.Source->size();
			}
			if (!CheckExternalDependencies(M.RequiredDependencies, C.AvailableDependencies, Error)) return {};
		}
		std::sort(C.Resources.begin(), C.Resources.end(),
		    [](const auto& A, const auto& B)
		{
			return A.Id < B.Id;
		});
		for (std::size_t I = 0; I < C.Resources.size(); ++I)
		{
			const auto& R = C.Resources[I];
			if (!ModuleSupport::Text(R.Id) || !ModuleSupport::Text(R.ContractIdentity) || !R.Provider ||
			    !E->Scopes.count(R.OwnerId) || !E->Providers.emplace(R.Id, I).second)
			{
				return Fail("invalid or duplicate resource provider");
			}

			std::set<std::string> ResourceModules;
			for (const auto& Id : R.ModuleIds)
			{
				if (!ModuleSupport::Text(Id) || !ResourceModules.insert(Id).second)
				{
					return Fail("invalid or duplicate resource module identity");
				}
			}
		}
		for (auto* Paths : {&C.ImportPaths, &C.StdlibSearchPaths})
		{
			for (auto& P : *Paths)
			{
				P = ModuleSupport::Physical(P);
				if (P.empty() || (C.bStrictSources && E->GetSourceOwner(P, true).empty()))
				{
					return Fail("search root is outside the environment");
				}
			}
		}

		if (C.CompileProcessing.Registry)
		{
			auto R = std::make_shared<FCompileProcessorRegistry>();
			for (const auto& P : C.CompileProcessing.Registry->GetProcessors())
			{
				if (!R->Register(P, Error)) return {};
			}

			R->Freeze();
			C.CompileProcessing.Registry = std::move(R);
		}
		std::string F = "veroscript-module-environment:0.1.1";
		auto Add = [&](const std::string& V)
		{
			ModuleSupport::Append(F, V);
		};
		Add(C.EntryOwnerId);
		Add(C.CompileConfiguration);
		Add(C.ArtifactReader ? C.ArtifactReader->GetIdentity() : "file-reader");
		Add(C.ArtifactDecoder ? C.ArtifactDecoder->GetIdentity() : "plain-artifacts");
		Add(C.bStrictSources ? "strict" : "open");
		Add(C.bAllowAmbientSearch ? "ambient" : "explicit");
		Add("scopes");
		Add(std::to_string(C.Scopes.size()));
		for (const auto& S : C.Scopes)
		{
			Add(S.Id);
			Add(S.bHostProvided ? "host" : "dependency");
			Add(std::to_string(S.DirectDependencies.size()));
			for (const auto& V : S.DirectDependencies)
			{
				Add(V);
			}
		}
		Add("roots");
		Add(std::to_string(C.SourceRoots.size()));
		for (const auto& R : C.SourceRoots)
		{
			Add(R.Path);
			Add(R.OwnerId);
		}
		Add("modules");
		Add(std::to_string(C.Modules.size()));
		for (const auto& M : C.Modules)
		{
			Add(M.Name);
			Add(M.Id);
			Add(M.Path);
			Add(M.OwnerId);
			Add(M.ContentIdentity);
			Add(M.Source ? std::to_string(Runtime::Fnv1a64(14695981039346656037ull, *M.Source)) : "file");
			if (M.Binary)
			{
				Add(std::to_string(Runtime::Fnv1a64(14695981039346656037ull,
				    reinterpret_cast<const char*>(M.Binary->data()), M.Binary->size())));
			}
			Add(std::to_string(M.RequiredDependencies.size()));
			for (const auto& R : M.RequiredDependencies)
			{
				AppendDependency(F, R);
			}
		}
		for (const auto* P : {&C.ImportPaths, &C.StdlibSearchPaths, &C.ImplicitImports})
		{
			Add(std::to_string(P->size()));
			for (const auto& V : *P)
			{
				Add(V);
			}
		}
		Add("resources");
		Add(std::to_string(C.Resources.size()));
		for (const auto& R : C.Resources)
		{
			Add(R.Id);
			Add(R.OwnerId);
			Add(R.ContractIdentity);
			Add(std::to_string(R.ModuleIds.size()));
			for (const auto& Id : R.ModuleIds)
			{
				Add(Id);
			}
		}
		Add("processing");
		Add(C.CompileProcessing.ImplementationFingerprint);
		Add(C.CompileProcessing.Registry ? C.CompileProcessing.Registry->GetFingerprint() : "");
		Add(std::to_string(C.MaxResourceBytes));
		const auto& CP = C.CompileProcessing;
		Add(std::to_string(CP.MaxOperations));
		Add(std::to_string(CP.MaxSourceBytes));
		Add(std::to_string(CP.MaxInputBytes));
		Add(std::to_string(CP.MaxEvents));
		Add(std::to_string(CP.MaxDependencies));
		Add(std::to_string(static_cast<unsigned>(CP.CommentRetention)));
		Add(CP.bCacheable ? "cacheable" : "uncached");
		Add(std::to_string(CP.WritableModules.size()));
		for (const auto& M : CP.WritableModules)
		{
			Add(M);
		}

		std::function<void(const std::vector<FAnnotationArgumentDefinition>&)> Arguments = [&](const auto& Ds)
		{
			Add(std::to_string(Ds.size()));
			for (const auto& D : Ds)
			{
				Add(D.Name);
				Add(D.Detail);
				Add(D.Documentation);
				Arguments(D.Children);
			}
		};
		Add(std::to_string(CP.AnnotationDefinitions.size()));
		for (const auto& D : CP.AnnotationDefinitions)
		{
			Add(D.Id);
			Add(D.ProviderId);
			Add(D.Name);
			Add(D.Detail);
			Add(D.Documentation);
			Add(std::to_string(D.Targets));
			Add(D.CaseSensitive ? "1" : "0");
			Arguments(D.Arguments);
		}
		Add(std::to_string(CP.InitialDependencies.size()));
		for (const auto& D : CP.InitialDependencies)
		{
			Add(D.Path);
			Add(D.bExists ? "found" : "missing");
			Add(std::to_string(Runtime::Fnv1a64(14695981039346656037ull, D.Content)));
		}
		const auto& SC = C.ScriptCompileExtensions;
		Add("script-extensions");
		if (SC.VmBackendFactory && !SC.VmBackendPath.empty())
		{
			Error = "compiler VM factory and path are exclusive";
			return {};
		}
		Add(SC.VmBackendPath);
		if (SC.VmBackendFactory)
		{
			const auto& Backend = SC.VmBackendFactory->GetDescription();
			Add(Backend.Id);
			Add(Backend.Version);
			Add(Backend.ImplementationId);
			Add(Backend.Target);
			Add(std::to_string(Backend.Capabilities));
		}
		Add(SC.bAllowSourceImports ? "imports" : "closed");
		Add(SC.BindingConfiguration);
		Add(SC.bBindingsDeterministic ? "deterministic" : "uncached");
		Add(std::to_string(SC.Roots.size()));
		for (const auto& R : SC.Roots)
		{
			Add(R.Path);
			Add(R.RegistrationEntry);
		}
		Add(std::to_string(SC.AllowedRoots.size()));
		for (const auto& P : SC.AllowedRoots)
		{
			Add(P);
		}

		Add(std::to_string(SC.Configuration.size()));
		for (const auto& P : SC.Configuration)
		{
			Add(P.first);
			Add(P.second);
		}
		Add(std::to_string(SC.MaxInstructions));
		Add(std::to_string(SC.TimeoutMilliseconds));
		Add(std::to_string(SC.MaxRoots));
		Add(std::to_string(SC.MaxDependencyDepth));
		Add(std::to_string(SC.MaxRegistrations));
		Add(std::to_string(SC.MaxBridgeBytes));
		Add(std::to_string(SC.MaxBridgeValues));
		Add("bindings");
		if (C.Bindings)
		{
			std::string Catalog;
			if (!ExportBindingCatalogJson(*C.Bindings, Catalog)) return Fail("cannot fingerprint environment binding declarations");
			Add(Catalog);
			std::set<std::string> Providers;
			for (const auto& N : C.Bindings->GetNativeFunctions())
			{
				if (!N.Origin.ProviderId.empty())
				{
					Providers.insert(N.Origin.ProviderId);
				}
			}

			for (const auto& T : C.Bindings->GetTypes())
			{
				if (!T.Origin.ProviderId.empty())
				{
					Providers.insert(T.Origin.ProviderId);
				}
			}

			for (const auto& T : C.Bindings->GetGenericTypeTemplates())
			{
				if (!T.Origin.ProviderId.empty())
				{
					Providers.insert(T.Origin.ProviderId);
				}
			}

			for (const auto& T : C.Bindings->GetTypeAliases())
			{
				if (!T.Origin.ProviderId.empty())
				{
					Providers.insert(T.Origin.ProviderId);
				}
			}

			Add(std::to_string(Providers.size()));
			for (const auto& Id : Providers)
			{
				if (!E->Scopes.count(Id)) return Fail("binding declaration has an undeclared provider scope");
				Add(Id);
				const auto Contract =
				    std::find_if(C.AvailableDependencies.begin(), C.AvailableDependencies.end(),
				        [&](const auto& V)
				{
					return V.Id == Id;
				});
				if (Contract == C.AvailableDependencies.end())
				{
					return Fail("native binding provider requires an available execution contract: " + Id);
				}
				AppendDependency(F, *Contract);
			}
		}
		Add("requirements");
		Add(std::to_string(C.RequiredDependencies.size()));
		for (const auto& R : C.RequiredDependencies)
		{
			AppendDependency(F, R);
		}

		E->CompileFingerprint = std::move(F);
		E->ExecutionFingerprint = C.ExecutionConfiguration;
		if ((C.ExecutionBackends.Jit || C.ExecutionBackends.Vm) && C.ExecutionBackends.Identity.empty())
		{
			return Fail("execution backend resolvers require an immutable identity");
		}
		if (!C.ExecutionBackends.Identity.empty())
		{
			ModuleSupport::Append(E->ExecutionFingerprint, C.ExecutionBackends.Identity);
		}
		for (const auto& Choice : C.SelectedBackends)
		{
			ModuleSupport::Append(E->ExecutionFingerprint, Choice.first);
			ModuleSupport::Append(E->ExecutionFingerprint, Choice.second);
		}
		for (const auto& Provider : C.ServiceProviders)
		{
			ModuleSupport::Append(E->ExecutionFingerprint, Provider.ServiceId);
			ModuleSupport::Append(E->ExecutionFingerprint, Provider.BackendId);
			ModuleSupport::Append(E->ExecutionFingerprint, Provider.ContractId);
			ModuleSupport::Append(E->ExecutionFingerprint, Provider.ContractVersion);
			ModuleSupport::Append(E->ExecutionFingerprint, Provider.ConfigurationKey);
		}
		for (const auto& Service : C.BackendDiagnostics)
		{
			ModuleSupport::Append(E->ExecutionFingerprint, Service.first);
			for (const auto& Attempt : Service.second)
			{
				ModuleSupport::Append(E->ExecutionFingerprint, Attempt.BackendId);
				ModuleSupport::Append(E->ExecutionFingerprint, Attempt.Error);
			}
		}
		auto Available = C.AvailableDependencies;
		std::sort(Available.begin(), Available.end(),
		    [](const auto& A, const auto& B)
		{
			return A.Id < B.Id;
		});
		for (const auto& R : Available)
		{
			AppendDependency(E->ExecutionFingerprint, R);
		}

		return E;
	}
	const FResolvedModule* FModuleEnvironment::FindModule(const std::string& Name) const
	{
		const auto I = Names.find(Name);
		return I == Names.end() ? nullptr : &Description.Modules[I->second];
	}
	const FResolvedModule* FModuleEnvironment::FindSource(const std::string& Path) const
	{
		const auto I = Sources.find(SourceKey(Path));
		return I == Sources.end() ? nullptr : &Description.Modules[I->second];
	}
	std::string FModuleEnvironment::GetSourceOwner(const std::string& Path, bool Directory) const
	{
		if (!Directory)
		{
			if (const auto* M = FindSource(Path)) return M->OwnerId;
		}

		const auto Key = SourceKey(Path);
		std::string Owner;
		std::size_t Best = 0;
		if (!Key.empty())
		{
			for (const auto& R : CanonicalRoots)
			{
				if (ModuleSupport::Within(Key, R.first) && R.first.size() > Best)
				{
					Best = R.first.size();
					Owner = R.second;
				}
			}
		}

		if (Directory && Owner.empty())
		{
			for (const auto& M : Description.Modules)
			{
				if (SourceKey(PathUtil::ToUtf8(PathUtil::FromUtf8(M.Path).parent_path())) == Key)
				{
					if (!Owner.empty() && Owner != M.OwnerId) return {};
					Owner = M.OwnerId;
				}
			}
		}

		if (Owner.empty() && (Path.empty() || Path.front() == '<')) return Description.EntryOwnerId;
		return Owner;
	}
	bool FModuleEnvironment::CanAccessOwner(const std::string& From, const std::string& Target) const
	{
		const auto A = Scopes.find(From), B = Scopes.find(Target);
		if (A == Scopes.end() || B == Scopes.end()) return false;
		if (From == Target || Description.Scopes[B->second].bHostProvided) return true;
		const auto& D = Description.Scopes[A->second].DirectDependencies;
		return std::binary_search(D.begin(), D.end(), Target);
	}
	bool FModuleEnvironment::AuthorizeSource(const std::string& Path, const std::string& Importer, std::string& Error, bool Directory) const
	{
		if (!Description.bStrictSources)
		{
			Error.clear();
			return true;
		}
		const auto Target = GetSourceOwner(Path), From = GetSourceOwner(Importer, Directory);
		if (Target.empty() || From.empty() || !CanAccessOwner(From, Target))
		{
			Error = "module source is outside the declared dependency scope: " + Path + " (from " + Importer +
			    ")";
			return false;
		}
		Error.clear();
		return true;
	}
	bool FModuleEnvironment::ReadResource(const FResourceRequest& R, const std::string& From,
	    const FResourceWork& Work, FResourceData& Out, std::string& Error) const
	{
		Out = {};
		Error.clear();
		if (Work.CheckCancelled)
		{
			Work.CheckCancelled();
		}

		auto EffectiveWork = Work;
		if (Description.MaxResourceBytes &&
		    (!EffectiveWork.MaxBytes || Description.MaxResourceBytes < EffectiveWork.MaxBytes))
		{
			EffectiveWork.MaxBytes = Description.MaxResourceBytes;
		}

		if (!ValidateResourceRequest(R, Error)) return false;
		const auto P = Providers.find(R.ProviderId);
		if (P == Providers.end())
		{
			Error = "resource provider is not available: " + R.ProviderId;
			return false;
		}
		const auto& B = Description.Resources[P->second];
		const auto M = std::find_if(Description.Modules.begin(), Description.Modules.end(),
		    [&](const auto& V)
		{
			return V.Id == R.ModuleId;
		});
		const bool Declared =
		    std::find(B.ModuleIds.begin(), B.ModuleIds.end(), R.ModuleId) != B.ModuleIds.end() ||
		    (M != Description.Modules.end() && M->OwnerId == B.OwnerId);
		const auto FromOwner = Scopes.count(From) ? From : GetSourceOwner(From);
		if (!Declared || (M != Description.Modules.end() && M->OwnerId != B.OwnerId) ||
		    !CanAccessOwner(FromOwner, B.OwnerId))
		{
			Error = "resource is outside the declared dependency scope";
			return false;
		}
		FResourceData Value;
		if (!B.Provider->Read(R, EffectiveWork, Value, Error))
		{
			if (Error.empty())
			{
				Error = "resource provider failed";
			}
			return false;
		}
		if (Work.CheckCancelled)
		{
			Work.CheckCancelled();
		}

		if (!Error.empty() || (EffectiveWork.MaxBytes && Value.Bytes.size() > EffectiveWork.MaxBytes) ||
		    (Value.bExists && !ModuleSupport::Text(Value.ContentIdentity)) ||
		    (!Value.bExists &&
		        (!Value.Bytes.empty() || !Value.ContentIdentity.empty() || !Value.PhysicalPath.empty() || !Value.ArtifactTrustIdentity.empty())))
		{
			Error = "invalid resource provider result or resource byte limit exceeded";
			return false;
		}
		if (!Value.PhysicalPath.empty())
		{
			Value.PhysicalPath = ModuleSupport::Physical(Value.PhysicalPath);
			if (Value.PhysicalPath.empty())
			{
				Error = "resource physical path is not an absolute filesystem path";
				return false;
			}
		}
		if (Value.bExists && Description.ArtifactDecoder)
		{
			FArtifactData Data;
			Data.Owner = Value.Owner;
			Data.Bytes = std::move(Value.Bytes);
			Data.ContentIdentity = Value.ContentIdentity;
			Data.TrustIdentity = Value.ArtifactTrustIdentity;
			const FArtifactWork ArtifactWork{EffectiveWork.MaxBytes ? EffectiveWork.MaxBytes : UINT64_MAX, EffectiveWork.CheckCancelled};
			const FArtifactRequest Request{Value.PhysicalPath.empty() ? MakeResourceDependencyId(R, From) : Value.PhysicalPath, "resource", B.OwnerId};
			if (Data.TrustIdentity.empty() && !Value.PhysicalPath.empty() && Description.ArtifactReader)
			{
				FArtifactData Verified;
				if (!Description.ArtifactReader->Read(Request, ArtifactWork, Verified, Error) || Verified.Bytes != Data.Bytes)
				{
					if (Error.empty())
					{
						Error = "resource provider bytes differ from artifact content";
					}
					return false;
				}
				Data.TrustIdentity = std::move(Verified.TrustIdentity);
				Data.SignatureRecords = std::move(Verified.SignatureRecords);
			}
			if (!DecodeArtifact(Request, ArtifactWork, Data, Error, Description.ArtifactDecoder.get())) return false;
			Value.Bytes = std::move(Data.Bytes);
			Value.Owner = std::move(Data.Owner);
			Value.ContentIdentity = std::move(Data.ContentIdentity);
			Value.ArtifactTrustIdentity = std::move(Data.TrustIdentity);
		}
		struct FOwners
		{
			std::shared_ptr<const FModuleEnvironment> Environment;
			std::shared_ptr<const void> ResourceOwner;
		};
		auto Hold = std::make_shared<FOwners>();
		Hold->Environment = shared_from_this();
		Hold->ResourceOwner = std::move(Value.Owner);
		Out = std::move(Value);
		Out.Owner = std::move(Hold);
		return true;
	}
	bool FModuleEnvironment::ReadResourceDependency(const std::string& Id, const FResourceWork& Work, std::string& Content) const
	{
		FResourceRequest R;
		std::string From, Error;
		FResourceData Data;
		Content.clear();
		if (!ParseResourceDependencyId(Id, R, From) || !ReadResource(R, From, Work, Data, Error))
		{
			throw std::runtime_error(Error.empty() ? "invalid resource dependency identity" : Error);
		}

		Content = ResourceDependencyContent(Data);
		return Data.bExists;
	}
	bool FModuleEnvironment::Apply(FCompileOptions& Options, std::string& Error) const
	{
		Error.clear();
		FCompileOptions Next = Options;
		auto& O = Next;
		if (O.CompileProcessing.ModuleEnvironment.get() == this) return true;
		if (O.CompileProcessing.ModuleEnvironment ||
		    (O.Bindings && Description.Bindings && O.Bindings != Description.Bindings.get()) ||
		    (O.Natives && Description.Bindings && O.Natives != &Description.Bindings->GetNativeFunctions()))
		{
			Error = "conflicting module environment or host binding catalog";
			return false;
		}
		if (Description.Bindings)
		{
			O.Bindings = Description.Bindings.get();
		}

		auto Processing = Description.CompileProcessing;
		if (O.CompileProcessing.Registry && O.CompileProcessing.Registry != Processing.Registry)
		{
			auto R = std::make_shared<FCompileProcessorRegistry>();
			if (Processing.Registry)
			{
				for (const auto& P : Processing.Registry->GetProcessors())
				{
					if (!R->Register(P, Error)) return false;
				}
			}

			for (const auto& P : O.CompileProcessing.Registry->GetProcessors())
			{
				if (!R->Register(P, Error)) return false;
			}

			R->Freeze();
			Processing.Registry = std::move(R);
		}
		const auto EnvironmentCheck = Processing.CheckCancelled, CallerCheck = O.CompileProcessing.CheckCancelled;
		const auto EnvironmentControl = Description.ScriptCompileExtensions.ExecutionControl,
		           CallerControl = O.ScriptCompileExtensions.ExecutionControl;
		Processing.CheckCancelled = [EnvironmentCheck, CallerCheck, EnvironmentControl, CallerControl]
		{
			if (EnvironmentCheck)
			{
				EnvironmentCheck();
			}
			if (CallerCheck)
			{
				CallerCheck();
			}

			if ((EnvironmentControl && EnvironmentControl->IsCancellationRequested()) ||
			    (CallerControl && CallerControl->IsCancellationRequested()))
			{
				throw std::runtime_error("compilation extension cancelled");
			}
		};
		if (O.CompileProcessing.ReadDependency)
		{
			Processing.ReadDependency = O.CompileProcessing.ReadDependency;
		}

		Processing.AnnotationDefinitions.insert(Processing.AnnotationDefinitions.end(),
		    O.CompileProcessing.AnnotationDefinitions.begin(),
		    O.CompileProcessing.AnnotationDefinitions.end());
		Processing.WritableModules.insert(Processing.WritableModules.end(), O.CompileProcessing.WritableModules.begin(), O.CompileProcessing.WritableModules.end());
		Processing.MaxOperations = std::min(Processing.MaxOperations, O.CompileProcessing.MaxOperations);
		Processing.MaxSourceBytes = std::min(Processing.MaxSourceBytes, O.CompileProcessing.MaxSourceBytes);
		Processing.MaxInputBytes = std::min(Processing.MaxInputBytes, O.CompileProcessing.MaxInputBytes);
		Processing.MaxEvents = std::min(Processing.MaxEvents, O.CompileProcessing.MaxEvents);
		Processing.MaxDependencies = std::min(Processing.MaxDependencies, O.CompileProcessing.MaxDependencies);
		Processing.bCacheable = Processing.bCacheable && O.CompileProcessing.bCacheable;
		Processing.CommentRetention = O.CompileProcessing.CommentRetention;
		Processing.InitialDependencies.insert(Processing.InitialDependencies.end(),
		    O.CompileProcessing.InitialDependencies.begin(), O.CompileProcessing.InitialDependencies.end());
		if (O.CompileProcessing.PrepareSource)
		{
			Processing.PrepareSource = O.CompileProcessing.PrepareSource;
		}

		ModuleSupport::Append(
		    Processing.ImplementationFingerprint, O.CompileProcessing.ImplementationFingerprint);
		ModuleSupport::Append(Processing.ImplementationFingerprint, CompileFingerprint);
		Processing.ModuleEnvironment = shared_from_this();
		auto Overlays = O.SourceOverlays;
		O.SourceOverlays.clear();
		for (const auto& M : Description.Modules)
		{
			if (M.Source)
			{
				O.SourceOverlays.push_back({M.Path, *M.Source});
			}
		}

		O.SourceOverlays.insert(O.SourceOverlays.end(), Overlays.begin(), Overlays.end());
		auto Reader = Processing.ReadDependency;
		const FResourceWork Work{Processing.CheckCancelled, Processing.MaxInputBytes};
		Processing.ReadDependency = [E = shared_from_this(), Reader, Overlays = O.SourceOverlays, Work](
		                                std::string_view Path, std::string& Content)
		{
			const std::string Name(Path);
			if (IsResourceDependencyId(Name)) return E->ReadResourceDependency(Name, Work, Content);
			std::string Error;
			if (!E->AuthorizeSource(Name, Name, Error))
			{
				throw std::runtime_error(Error);
			}

			const auto Key = SourceKey(Name);
			for (auto I = Overlays.rbegin(); I != Overlays.rend(); ++I)
			{
				if (SourceKey(I->Path) == Key)
				{
					Content = I->Source;
					return true;
				}
			}

			const auto* M = E->FindSource(Name);
			if ((M && M->Binary) || Detail::IsLibraryPackagePath(Name))
			{
				FArtifactData Data;
				FArtifactWork ArtifactWork{Work.MaxBytes, Work.CheckCancelled};
				const FArtifactRequest Request{Name, "library", M ? M->OwnerId : E->GetSourceOwner(Name)};
				bool Read = false;
				if (M && M->Binary)
				{
					Data.Bytes = *M->Binary;
					Read = DecodeArtifact(Request, ArtifactWork, Data, Error, E->Description.ArtifactDecoder.get());
				}
				else
				{
					Read = ReadArtifact(Request, ArtifactWork, Data, Error, E->Description.ArtifactReader.get(), E->Description.ArtifactDecoder.get());
				}
				if (!Read)
				{
					throw std::runtime_error(Error);
				}
				Content.assign(reinterpret_cast<const char*>(Data.Bytes.data()), Data.Bytes.size());
				return true;
			}
			if (Reader) return Reader(Path, Content);
			std::ifstream Input(PathUtil::FromUtf8(Name), std::ios::binary | std::ios::ate);
			if (!Input)
			{
				Content.clear();
				std::error_code Ec;
				if (!std::filesystem::exists(PathUtil::FromUtf8(Name), Ec) && !Ec) return false;
				throw std::runtime_error("cannot read module dependency");
			}
			const auto Size = Input.tellg();
			if (Size < 0 || static_cast<std::uint64_t>(Size) > Work.MaxBytes)
			{
				throw std::runtime_error("module dependency byte budget exceeded");
			}

			Content.resize(static_cast<std::size_t>(Size));
			Input.seekg(0);
			if (!Input.read(Content.data(), static_cast<std::streamsize>(Content.size())))
			{
				throw std::runtime_error("cannot read module dependency");
			}

			if (Work.CheckCancelled)
			{
				Work.CheckCancelled();
			}
			return true;
		};
		O.CompileProcessing = std::move(Processing);
		for (const auto& P : Description.ImportPaths)
		{
			O.ImportPaths.push_back(P);
		}

		for (const auto& P : Description.StdlibSearchPaths)
		{
			O.StdlibSearchPaths.push_back(P);
		}

		for (const auto& P : Description.ImplicitImports)
		{
			O.ImplicitImportPaths.push_back(P);
		}

		const auto& Policy = Description.ScriptCompileExtensions;
		auto& Extensions = O.ScriptCompileExtensions;
		if (!Extensions.VmBackendFactory && Extensions.VmBackendPath.empty())
		{
			Extensions.VmBackendFactory = Policy.VmBackendFactory;
			Extensions.VmBackendPath = Policy.VmBackendPath;
		}
		if (!Extensions.VmBackendFactory && Extensions.VmBackendPath.empty() && Description.ExecutionBackends.Vm && ResolveBackendSelection("vm", {}, Description.SelectedBackends) != DisabledBackend && !Description.ExecutionBackends.Vm({}, Extensions.VmBackendFactory, Error))
		{
			return false;
		}
		if (Extensions.VmBackendFactory && !Extensions.VmBackendPath.empty())
		{
			Error = "compiler VM factory and path are exclusive";
			return false;
		}
		Extensions.bAllowSourceImports = Extensions.bAllowSourceImports && Policy.bAllowSourceImports;
		Extensions.MaxRoots = std::min(Extensions.MaxRoots, Policy.MaxRoots);
		Extensions.MaxDependencyDepth = std::min(Extensions.MaxDependencyDepth, Policy.MaxDependencyDepth);
		Extensions.MaxRegistrations = std::min(Extensions.MaxRegistrations, Policy.MaxRegistrations);
		Extensions.MaxBridgeBytes = std::min(Extensions.MaxBridgeBytes, Policy.MaxBridgeBytes);
		Extensions.MaxBridgeValues = std::min(Extensions.MaxBridgeValues, Policy.MaxBridgeValues);
		auto Limited = [](std::uint64_t A, std::uint64_t B)
		{
			return !A ? B : !B ? A
			                   : std::min(A, B);
		};
		Extensions.MaxInstructions = Limited(Extensions.MaxInstructions, Policy.MaxInstructions);
		Extensions.TimeoutMilliseconds = Limited(Extensions.TimeoutMilliseconds, Policy.TimeoutMilliseconds);
		if (!Extensions.ExecutionControl)
		{
			Extensions.ExecutionControl = Policy.ExecutionControl;
		}

		if (!Policy.AllowedRoots.empty())
		{
			if (Extensions.AllowedRoots.empty())
			{
				Extensions.AllowedRoots = Policy.AllowedRoots;
			}

			else
			{
				std::set<std::string> Allowed;
				for (const auto& A : Extensions.AllowedRoots)
				{
					for (const auto& B : Policy.AllowedRoots)
					{
						const auto PA = ModuleSupport::Physical(A), PB = ModuleSupport::Physical(B);
						if (PA.empty() || PB.empty())
						{
							Error = "compiler source policy requires absolute roots";
							return false;
						}
						const auto KA = PathUtil::IdentityKey(PA), KB = PathUtil::IdentityKey(PB);
						if (ModuleSupport::Within(KA, KB))
						{
							Allowed.insert(PA);
						}
						else if (ModuleSupport::Within(KB, KA))
						{
							Allowed.insert(PB);
						}
					}
				}

				if (Allowed.empty())
				{
					Error = "compiler source policies have no common root";
					return false;
				}
				Extensions.AllowedRoots.assign(Allowed.begin(), Allowed.end());
			}
		}
		for (const auto& R : Policy.Roots)
		{
			if (std::none_of(Extensions.Roots.begin(), Extensions.Roots.end(),
			        [&](const auto& Existing)
			{
				return PathUtil::Equal(Existing.Path, R.Path) &&
				    Existing.RegistrationEntry == R.RegistrationEntry;
			}))
			{
				Extensions.Roots.push_back(R);
			}
		}

		for (const auto& V : Description.ScriptCompileExtensions.Configuration)
		{
			O.ScriptCompileExtensions.Configuration.emplace(V);
		}

		if (Description.ScriptCompileExtensions.Bindings)
		{
			if (O.ScriptCompileExtensions.Bindings &&
			    O.ScriptCompileExtensions.Bindings != Description.ScriptCompileExtensions.Bindings)
			{
				Error = "conflicting compiler extension binding catalog";
				return false;
			}
			O.ScriptCompileExtensions.Bindings = Description.ScriptCompileExtensions.Bindings;
			O.ScriptCompileExtensions.bBindingsDeterministic = Description.ScriptCompileExtensions.bBindingsDeterministic;
			O.ScriptCompileExtensions.BindingConfiguration = Description.ScriptCompileExtensions.BindingConfiguration;
		}
		O.ModuleEnvironment = shared_from_this();
		Options = std::move(Next);
		return true;
	}
}
