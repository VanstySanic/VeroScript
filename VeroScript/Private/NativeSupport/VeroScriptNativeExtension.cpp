// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNativeExtensionLoader.h"
#include "ModuleSupport/VeroScriptModuleSupport.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>

namespace VeroScript
{
	namespace
	{
		struct FActiveRegistration
		{
			std::size_t Count = 0;
			bool Parallel = false;
		};
		struct FRegistrationBookkeeping
		{
			std::mutex Mutex;
			std::map<std::string, FActiveRegistration> Entries;
		};
		FRegistrationBookkeeping& Registrations()
		{
			// Hosts can register before main and release owners from global
			// destructors. Keep bookkeeping alive for the process lifetime.
			static auto* State = new FRegistrationBookkeeping;
			return *State;
		}
		bool Name(const std::string& S)
		{
			return !S.empty() && S.size() <= 64 && S.front() != '-' && S.back() != '-' &&
				std::all_of(S.begin(), S.end(),
					[](char C)
					{
						return (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '-';
					});
		}
		class FOwnedProcessor final : public ICompileProcessor
		{
		public:
			void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
			{
				Inner->Process(C, O);
			}

			FOwnedProcessor(std::shared_ptr<void> O, std::unique_ptr<ICompileProcessor> P)
				: Owner(std::move(O)), Inner(std::move(P))
			{
			}

		private:
			std::shared_ptr<void> Owner;
			std::unique_ptr<ICompileProcessor> Inner;
		};
		struct FRegistration final : INativeExtensionHost, std::enable_shared_from_this<FRegistration>
		{
			//~ Begin INativeExtensionHost Interface
			~FRegistration() override
			{
				if (Instance)
				{
					try
					{
						Module->GetDescriptor().Destroy(Instance);
					}
					catch (...)
					{
					}
				}
				if (Acquired)
				{
					auto& Active = Registrations().Entries;
					std::lock_guard<std::mutex> Lock(Registrations().Mutex);
					auto I = Active.find(Module->GetDescriptor().Id);
					if (I != Active.end() && --I->second.Count == 0)
					{
						Active.erase(I);
					}
				}
			}

			const std::map<std::string, std::string>& GetConfiguration() const override
			{
				return Options.Configuration;
			}

			std::shared_ptr<const INativeExtensionService> GetService(
				const std::string& Id, const std::string& Version, std::string& Error) const override
			{
				Error.clear();
				for (const auto& S : Options.Services)
				{
					if (S.Id == Id)
					{
						if (S.Version != Version)
						{
							Error = "native host service version mismatch: " + Id;
							return {};
						}
						struct FLease
						{
							std::shared_ptr<const void> Owner;
							std::shared_ptr<const INativeExtensionService> Service;
						};
						auto Lease = std::make_shared<FLease>(FLease{S.Owner, S.Service});
						return std::shared_ptr<const INativeExtensionService>(Lease, S.Service.get());
					}
				}

				Error = "native host service is unavailable: " + Id;
				return {};
			}

			bool RegisterFunction(FHostFunctionDesc D, FNativeCallback C, void* U, std::string& E) override
			{
				return Register(ENativeExtensionRole::Bindings, E,
					[&]
					{
						return Bindings->RegisterNative(std::move(D), C, U, E);
					});
			}

			bool RegisterType(FHostTypeDesc D, FHostTypeId& Id, std::string& E) override
			{
				Id = {};
				return Register(ENativeExtensionRole::Bindings, E,
					[&]
					{
						FBindingOwnerScope Owner(*this, D.ModuleOwner);
						if (!Bindings->RegisterType(D.Name, D.Kind, Id, E, D.Annotations)) return false;
						Types.emplace(Id.Value, D.Name);
						if (!Bindings->SetTypeDocumentation(Id, D.Documentation, E) ||
							!RegisterTypeProtocol(Id, D, E))
						{
							return false;
						}
						for (auto& M : D.Members)
						{
							if (!Bindings->RegisterMember(Id, std::move(M), E)) return false;
						}

						for (const auto& Lifetime : D.CallbackLifetimes)
						{
							Bindings->RetainNativeCallback(Lifetime.first, Lifetime.second);
						}

						return true;
					});
			}

			bool RegisterMember(FHostTypeId Id, FHostMemberDesc M, std::string& E) override
			{
				return Register(ENativeExtensionRole::Bindings, E,
					[&]
					{
						return OwnType(Id, E) && Bindings->RegisterMember(Id, std::move(M), E);
					});
			}

			bool RegisterTypeProtocol(FHostTypeId Id, const FHostTypeDesc& D, std::string& E) override
			{
				return Register(ENativeExtensionRole::Bindings, E,
					[&]
					{
						if (!OwnType(Id, E)) return false;
						if (D.bInheritable && !Bindings->SetTypeInheritable(Id, true, E)) return false;
						if (D.Kind == EHostTypeKind::Enum &&
							(!Bindings->SetEnumUnderlyingType(Id, D.EnumUnderlyingTypeName, E) ||
								!Bindings->SetEnumScoped(Id, D.bScopedEnum, E)))
						{
							return false;
						}
						if (D.CloneCallback &&
							!Bindings->RegisterTypeClone(Id, D.CloneCallback, D.CloneUserData, E))
						{
							return false;
						}
						if (D.EqualsCallback &&
							!Bindings->RegisterTypeEquals(Id, D.EqualsCallback, D.EqualsUserData, E))
						{
							return false;
						}
						if (D.HashCallback &&
							!Bindings->RegisterTypeHash(Id, D.HashCallback, D.HashUserData, E))
						{
							return false;
						}
						if (D.SubclassFactory &&
							!Bindings->RegisterTypeSubclassFactory(
								Id, D.SubclassFactory, D.SubclassFactoryUserData, E))
						{
							return false;
						}
						for (const auto& Lifetime : D.CallbackLifetimes)
						{
							Bindings->RetainNativeCallback(Lifetime.first, Lifetime.second);
						}

						if (D.ModuleOwner)
						{
							if (D.CloneCallback)
							{
								Bindings->RetainNativeCallback(D.CloneUserData, D.ModuleOwner);
							}

							if (D.EqualsCallback)
							{
								Bindings->RetainNativeCallback(D.EqualsUserData, D.ModuleOwner);
							}

							if (D.HashCallback)
							{
								Bindings->RetainNativeCallback(D.HashUserData, D.ModuleOwner);
							}

							if (D.SubclassFactory)
							{
								Bindings->RetainNativeCallback(D.SubclassFactoryUserData, D.ModuleOwner);
							}
						}
						return true;
					});
			}

			bool RegisterGenericType(FGenericHostTypeTemplateDesc D, FGenericHostTypeTemplateId& Id, std::string& E) override
			{
				return Register(ENativeExtensionRole::Bindings, E,
					[&]
					{
						FBindingOwnerScope Owner(*this, D.ModuleOwner);
						if (!Bindings->RegisterGenericHostTypeTemplate(
								D.Name, D.Arity, D.Instantiate, D.UserData, Id, E, D.Documentation))
						{
							return false;
						}
						GenericNames.insert(D.Name);
						return true;
					});
			}

			bool RegisterAlias(FHostTypeAliasDesc D, std::string& E) override
			{
				return Register(ENativeExtensionRole::Bindings, E,
					[&]
					{
						return Bindings->RegisterTypeAlias(
							D.Name, D.TypeParams, D.TargetTypeName, E, D.Documentation);
					});
			}

			bool RegisterAnnotation(FHostAnnotationDesc D, std::string& E) override
			{
				return Register(ENativeExtensionRole::Compiler, E,
					[&]
					{
						return Bindings->RegisterAnnotation(
							D.Name, D.Detail, E, D.Documentation, D.Arguments, D.Targets, D.CaseSensitive);
					});
			}

			bool RegisterProcessor(FCompileProcessorDesc D, std::string& E) override
			{
				return Register(ENativeExtensionRole::Compiler, E,
					[&]
					{
						if (!D.Factory)
						{
							E = "native processor requires a factory";
							return false;
						}
						struct FFactory
						{
							std::shared_ptr<void> Owner;
							decltype(D.Factory) Factory;
						};
						auto Lease = std::make_shared<FFactory>(FFactory{
							JoinCallbackOwners(shared_from_this(), D.ModuleOwner), std::move(D.Factory)});
						D.ModuleOwner = shared_from_this();
						D.Origin = Options.Origin;
						D.Cacheable = D.Cacheable && !Module->GetContentIdentity().empty();
						for (const auto& S : Options.Services)
						{
							D.Cacheable = D.Cacheable && S.bDeterministic && !S.Identity.empty();
							ModuleSupport::Append(D.ConfigurationKey, S.Id);
							ModuleSupport::Append(D.ConfigurationKey, S.Version);
							ModuleSupport::Append(D.ConfigurationKey, S.Identity);
						}
						ModuleSupport::Append(D.ConfigurationKey, Options.ConfigurationKey);
						ModuleSupport::Append(
							D.ConfigurationKey, std::to_string(Options.Configuration.size()));
						for (const auto& C : Options.Configuration)
						{
							ModuleSupport::Append(D.ConfigurationKey, C.first);
							ModuleSupport::Append(D.ConfigurationKey, C.second);
						}
						ModuleSupport::Append(D.ConfigurationKey, Module->GetContentIdentity());
						D.Factory = [Lease]() -> std::unique_ptr<ICompileProcessor>
						{
							auto P = Lease->Factory();
							if (!P) return {};
							return std::make_unique<FOwnedProcessor>(Lease, std::move(P));
						};
						return Processors->Register(std::move(D), E);
					});
			}

			bool RegisterProvider(FNativeServiceProviderDesc D, std::string& E) override
			{
				return Register(ENativeExtensionRole::Provider, E,
					[&]
					{
						if (!Name(D.ServiceId) || !Name(D.BackendId)
							|| D.BackendId == AutomaticBackend || D.BackendId == DisabledBackend
							|| !ModuleSupport::Text(D.ContractId) ||
							!ModuleSupport::Text(D.ContractVersion) || !D.Create || !D.Destroy)
						{
							E = "invalid native service provider";
							return false;
						}
						for (const auto& P : *Providers)
						{
							if (P.ServiceId == D.ServiceId && P.BackendId == D.BackendId)
							{
								E = "duplicate native service/backend identity";
								return false;
							}
						}

						D.Owner = JoinCallbackOwners(shared_from_this(), D.Owner);
						Providers->push_back(std::move(D));
						return true;
					});
			}

			void SubmitCompileOperation(
				FCompileProcessingOutput& Out, FCompileGenerationOperation Operation) const override
			{
				Out.SubmitOperation(std::move(Operation));
			}

			void ReportCompileDiagnostic(FCompileProcessingOutput& Out, FScriptError Error) const override
			{
				Out.ReportDiagnostic(std::move(Error));
			}

			FHostObjectHandle MakeHostObject(
				FHostTypeId Id, void* P, void (*D)(void*), std::string& E) override
			{
				try
				{
					if (!OwnType(Id, E)) return {};
					return Make(Types.at(Id.Value), Id, P, D, E);
				}
				catch (const std::exception& X)
				{
					E = X.what();
					return {};
				}
			}

			FHostObjectHandle MakeHostObjectNamed(const std::string& N, void* P, void (*D)(void*), std::string& E) override
			{
				try
				{
					for (const auto& T : Types)
					{
						if (T.second == N) return Make(N, {T.first}, P, D, E);
					}

					for (const auto& T : GenericNames)
					{
						if (N.rfind(T + '<', 0) == 0 && N.back() == '>' && ModuleSupport::Text(N))
						{
							return Make(N, {}, P, D, E);
						}
					}

					E = "host object type is not owned by this extension";
					return {};
				}
				catch (const std::exception& X)
				{
					E = X.what();
					return {};
				}
			}

			bool ReadResource(const FResourceRequest& R, const FResourceWork& W, FResourceData& D, std::string& E) const override
			{
				if (!Options.ResourceEnvironment)
				{
					E = "native resource access requires an explicit module environment";
					return false;
				}
				return Options.ResourceEnvironment->ReadResource(R, Options.Origin.ProviderId, W, D, E);
			}
			//~ End INativeExtensionHost Interface

			template <class F> bool Register(ENativeExtensionRole Role, std::string& Error, F&& Action)
			{
				Error.clear();
				if (!Open || !(Module->GetDescriptor().Roles & NativeExtensionRole(Role)))
				{
					Error = "native registration is closed or the role is not declared";
					Failed = true;
					if (Failure.empty())
					{
						Failure = Error;
					}
					return false;
				}
				try
				{
					if (Action()) return true;
					if (Error.empty())
					{
						Error = "native registration failed";
					}

				}
				catch (const std::exception& E)
				{
					Error = E.what();
				}
				catch (...)
				{
					Error = "native registration threw an exception";
				}
				Failed = true;
				if (Failure.empty())
				{
					Failure = Error;
				}
				return false;
			}

			struct FBindingOwnerScope
			{
				FBindingOwnerScope(FRegistration& S, std::shared_ptr<void> Owner) : State(S)
				{
					State.Bindings->SetRegistrationOwner(
						JoinCallbackOwners(State.shared_from_this(), std::move(Owner)), State.Options.Origin,
						State.shared_from_this());
				}

				~FBindingOwnerScope()
				{
					State.Bindings->SetRegistrationOwner(State.shared_from_this(), State.Options.Origin);
				}

				FRegistration& State;
			};

			bool Acquire(std::string& Error)
			{
				auto& Active = Registrations().Entries;
				std::lock_guard<std::mutex> Lock(Registrations().Mutex);
				const auto& D = Module->GetDescriptor();
				const bool Parallel = (D.Flags & NativeExtensionParallelRegistrations) != 0;
				auto& A = Active[D.Id];
				if (A.Count && (!A.Parallel || !Parallel))
				{
					Error = "native extension requires process restart before another registration: " +
						std::string(D.Id);
					return false;
				}
				A.Parallel = Parallel;
				++A.Count;
				Acquired = true;
				return true;
			}

			bool OwnType(FHostTypeId Id, std::string& E) const
			{
				if (Types.count(Id.Value)) return true;
				E = "extension cannot mutate a type owned by another registration";
				return false;
			}

			FHostObjectHandle Make(const std::string& Name, FHostTypeId Id, void* Payload, void (*Destroy)(void*), std::string& E)
			{
				E.clear();
				struct FPayload
				{
					~FPayload()
					{
						if (Destroy)
						{
							Destroy(Value);
						}
					}

					std::shared_ptr<void> Owner;

					void* Value = nullptr;

					void (*Destroy)(void*) = nullptr;
				};
				auto Object = std::make_shared<FHostObject>();
				auto Lease = std::make_shared<FPayload>();
				Object->TypeId = Id.Value;
				Object->TypeName = Name;
				Object->ModuleOwner = shared_from_this();
				Object->ProviderIdentity = shared_from_this();
				Lease->Owner = shared_from_this();
				Object->PayloadOwner = Lease;
				Object->Payload = Payload;
				Lease->Value = Payload;
				Lease->Destroy = Destroy;
				return Object;
			}

			std::shared_ptr<const FLoadedNativeExtension> Module;

			FNativeExtensionRegistrationOptions Options;

			void* Instance = nullptr;

			bool Acquired = false, Open = false, Failed = false;

			std::string Failure;

			FHostBindingRegistry* Bindings = nullptr;

			FCompileProcessorRegistry* Processors = nullptr;

			std::vector<FNativeServiceProviderDesc>* Providers = nullptr;

			std::map<std::uint32_t, std::string> Types;

			std::set<std::string> GenericNames;
		};
	}
	struct FNativeExtensionRegistry::FImpl
	{
		std::vector<std::shared_ptr<const void>> Owners;
		std::shared_ptr<const FHostBindingRegistry> Bindings;
		std::shared_ptr<const FCompileProcessorRegistry> Processors;
		std::vector<FNativeServiceProviderDesc> Providers;
		std::set<std::string> Modules;
		std::vector<FExternalDependency> Dependencies;
		std::string Fingerprint;
	};
	FNativeExtensionRegistry::FNativeExtensionRegistry(const FHostBindingRegistry* Base, const FCompileProcessorRegistry* P) : Impl(std::make_unique<FImpl>())
	{
		Impl->Bindings = Base ? std::make_shared<FHostBindingRegistry>(*Base) : std::make_shared<FHostBindingRegistry>();
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		std::string Error;
		if (P)
		{
			for (const auto& D : P->GetProcessors())
			{
				if (!Registry->Register(D, Error))
				{
					throw std::invalid_argument(Error);
				}
			}
		}

		Registry->Freeze();
		Impl->Processors = std::move(Registry);
	}
	FNativeExtensionRegistry::~FNativeExtensionRegistry() = default;
	bool FNativeExtensionRegistry::Add(std::shared_ptr<const FLoadedNativeExtension> Module, FNativeExtensionRegistrationOptions O, std::string& Error)
	try
	{
		Error.clear();
		if (!Module || Impl->Modules.count(Module->GetDescriptor().Id) ||
			!ModuleSupport::Text(O.Origin.ProviderId))
		{
			Error = "missing/duplicate native extension or invalid provider scope";
			return false;
		}
		std::set<std::string> ServiceIds;
		for (const auto& S : O.Services)
		{
			if (!ModuleSupport::Text(S.Id) || !ModuleSupport::Text(S.Version) || !S.Service ||
				!ServiceIds.insert(S.Id).second || (S.bDeterministic && !ModuleSupport::Text(S.Identity)))
			{
				Error = "invalid or duplicate native host service";
				return false;
			}
		}

		auto Bindings = std::make_shared<FHostBindingRegistry>(*Impl->Bindings);
		auto Processors = std::make_shared<FCompileProcessorRegistry>();
		for (const auto& D : Impl->Processors->GetProcessors())
		{
			if (!Processors->Register(D, Error)) return false;
		}

		auto Providers = Impl->Providers;
		// Keep the parameter's code lease through the exception handler as well:
		// what() and the exception destructor may both belong to the module.
		auto State = std::make_shared<FRegistration>();
		State->Module = Module;
		State->Options = std::move(O);
		if (!State->Acquire(Error)) return false;
		State->Bindings = Bindings.get();
		State->Processors = Processors.get();
		State->Providers = &Providers;
		State->Open = true;
		Bindings->SetRegistrationOwner(State, State->Options.Origin);
		struct FRegistrationScope
		{
			void Close()
			{
				if (Closed) return;
				Closed = true;
				State.Open = false;
				State.Bindings = nullptr;
				State.Processors = nullptr;
				State.Providers = nullptr;
				Registry.SetRegistrationOwner({});
			}

			~FRegistrationScope()
			{
				Close();
			}

			FRegistration& State;

			FHostBindingRegistry& Registry;

			bool Closed = false;
		} Scope{*State, *Bindings};
		char Failure[2048]{};
		State->Instance = State->Module->GetDescriptor().Create(
			static_cast<const INativeExtensionHost*>(State.get()), Failure, sizeof(Failure));
		Scope.Close();
		if (!State->Instance || State->Failed || !std::memchr(Failure, 0, sizeof(Failure)) || Failure[0])
		{
			Error = State->Failed ? State->Failure
				: std::memchr(Failure, 0, sizeof(Failure)) && Failure[0]
				? Failure
				: "native extension registration factory failed";
			return false;
		}
		Processors->Freeze();
		auto Next = std::make_unique<FImpl>(*Impl);
		const auto& Descriptor = State->Module->GetDescriptor();
		if (!MergeExternalDependencies(Next->Dependencies, {{State->Options.Origin.ProviderId, Descriptor.Version, Descriptor.Abi, Descriptor.CoreBuild, State->Module->GetContentIdentity()}}, Error)) return false;
		Next->Modules.insert(State->Module->GetDescriptor().Id);
		Next->Owners.push_back(State);
		Next->Bindings = std::move(Bindings);
		Next->Processors = std::move(Processors);
		Next->Providers = std::move(Providers);
		const std::string* Identity[] = {&State->Options.Origin.ProviderId, &State->Options.Origin.ModulePath,
			&State->Options.ConfigurationKey, &State->Module->GetContentIdentity()};
		for (const auto* S : Identity)
		{
			ModuleSupport::Append(Next->Fingerprint, *S);
		}

		ModuleSupport::Append(Next->Fingerprint, State->Module->GetDescriptor().Id);
		ModuleSupport::Append(Next->Fingerprint, State->Module->GetDescriptor().Version);
		ModuleSupport::Append(Next->Fingerprint, State->Options.Origin.bExported ? "export" : "private");
		ModuleSupport::Append(Next->Fingerprint, std::to_string(State->Options.Configuration.size()));
		for (const auto& C : State->Options.Configuration)
		{
			ModuleSupport::Append(Next->Fingerprint, C.first);
			ModuleSupport::Append(Next->Fingerprint, C.second);
		}
		Impl = std::move(Next);
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
	catch (...)
	{
		Error = "native registration factory threw an exception";
		return false;
	}
	std::shared_ptr<const FHostBindingRegistry> FNativeExtensionRegistry::GetBindings() const
	{
		return Impl->Bindings;
	}
	std::shared_ptr<const FCompileProcessorRegistry> FNativeExtensionRegistry::GetProcessors() const
	{
		return Impl->Processors;
	}
	std::vector<FNativeServiceProviderDesc> FNativeExtensionRegistry::GetProviders() const
	{
		return Impl->Providers;
	}
	std::vector<std::shared_ptr<const void>> FNativeExtensionRegistry::GetOwners() const
	{
		return Impl->Owners;
	}
	std::vector<FExternalDependency> FNativeExtensionRegistry::GetDependencies() const
	{
		return Impl->Dependencies;
	}
	std::string FNativeExtensionRegistry::GetFingerprint() const
	{
		return Impl->Fingerprint;
	}
	FNativeServiceInstance::~FNativeServiceInstance()
	{
		if (Handle)
		{
			try
			{
				Destroy(Handle, UserData);
			}
			catch (...)
			{
			}
		}
	}
	const FNativeServiceProviderDesc* FindNativeServiceProvider(
		const std::vector<FNativeServiceProviderDesc>& Providers, const std::string_view Service,
		const std::string_view Backend, const std::string_view Contract, const std::string_view Version, std::string& Error)
	{
		Error.clear();
		const FNativeServiceProviderDesc* Found = nullptr;
		for (const auto& Provider : Providers)
		{
			if (Provider.ServiceId != Service || Provider.BackendId != Backend)
			{
				continue;
			}
			if (Found || Provider.ContractId != Contract || Provider.ContractVersion != Version)
			{
				Error = "native service provider contract mismatch: " + std::string(Service) + '/' + std::string(Backend);
				return nullptr;
			}
			Found = &Provider;
		}
		if (!Found)
		{
			Error = "native service provider is unavailable: " + std::string(Service) + '/' + std::string(Backend);
		}
		return Found;
	}

	std::shared_ptr<FNativeServiceInstance> CreateNativeService(
		const std::vector<FNativeServiceProviderDesc>& Providers, const std::string_view Service,
		const std::string_view Backend, const std::string_view Contract, const std::string_view Version,
		const FNativeServiceCreateContext& Context, std::string& Error, FBackendSelectionResult* Selection)
	{
		const auto Request = ResolveBackendSelection(Service, Backend,
			Context.Environment ? Context.Environment->GetDescription().SelectedBackends
				: std::map<std::string, std::string>{});
		std::vector<std::string> Candidates;
		for (const auto& Provider : Providers)
		{
			if (Provider.ServiceId == Service)
			{
				Candidates.push_back(Provider.BackendId);
			}
		}
		std::shared_ptr<FNativeServiceInstance> Instance;
		auto Result = SelectServiceBackend(Service, Request, std::move(Candidates),
			[&](const std::string_view Id, std::string& Failure)
			{
				const auto* Found = FindNativeServiceProvider(Providers, Service, Id, Contract, Version, Failure);
				if (!Found)
				{
					return false;
				}
				Instance = CreateNativeService(*Found, Context, Failure);
				return Instance != nullptr;
			}, Context.CheckCancelled);
		if (Request == AutomaticBackend && Context.Environment)
		{
			const auto& Diagnostics = Context.Environment->GetDescription().BackendDiagnostics;
			const auto Found = Diagnostics.find(std::string(Service));
			if (Found != Diagnostics.end())
			{
				Result.Rejected.insert(Result.Rejected.end(), Found->second.begin(), Found->second.end());
				if (Result.State == EBackendSelectionState::Unavailable)
				{
					for (const auto& Rejected : Found->second)
					{
						Result.Error += "\n" + Rejected.BackendId + ": " + Rejected.Error;
					}
				}
			}
		}
		Error = Result.Error;
		if (Result.State == EBackendSelectionState::Disabled)
		{
			Error = "service is disabled: " + std::string(Service);
		}
		if (Selection)
		{
			*Selection = std::move(Result);
		}
		return Instance;
	}

	std::shared_ptr<FNativeServiceInstance> CreateNativeService(
		const FNativeServiceProviderDesc& D, const FNativeServiceCreateContext& C, std::string& Error)
	try
	{
		Error.clear();
		if (!D.Create || !D.Destroy || !D.Owner)
		{
			Error = "native service is missing factory, destroy callback or owner";
			return {};
		}
		if (C.CheckCancelled)
		{
			C.CheckCancelled();
		}

		auto P = std::shared_ptr<FNativeServiceInstance>(new FNativeServiceInstance());
		P->Owner = D.Owner;
		P->Environment = C.Environment;
		P->UserData = D.UserData;
		P->Destroy = D.Destroy;
		P->Handle = D.Create(C, D.UserData, Error);
		if (C.CheckCancelled)
		{
			C.CheckCancelled();
		}

		if (!P->Handle || !Error.empty())
		{
			if (Error.empty())
			{
				Error = "native service creation failed";
			}
			return {};
		}
		return P;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return {};
	}
	catch (...)
	{
		Error = "native service factory threw an exception";
		return {};
	}
}
