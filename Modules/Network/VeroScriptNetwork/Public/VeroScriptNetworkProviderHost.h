// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetworkProviders.h"
#include <algorithm>
#include <mutex>
#include <tuple>

namespace VeroScript::Network
{
	// Host-side adapter. Backend providers themselves must not retain this catalog
	// in their registration options: the catalog owns their registration/code leases.
	class FBackendCatalog final : public IBackendCatalog, public std::enable_shared_from_this<FBackendCatalog>
	{
		using FInstanceKey = std::tuple<std::string, std::string, std::string, std::string>;

	public:
		void Update(std::vector<FNativeServiceProviderDesc> Providers,
		    std::shared_ptr<const FModuleEnvironment> Environment = {},
		    std::map<std::string, std::string> Selected = {},
		    std::map<std::string, std::map<std::string, std::string>> Configuration = {})
		{
			std::vector<FNativeServiceProviderDesc> NetworkProviders;
			for (auto& Provider : Providers)
			{
				if (IsBackendService(Provider.ServiceId))
				{
					NetworkProviders.push_back(std::move(Provider));
				}
			}
			std::lock_guard<std::mutex> Lock(Mutex);
			// Native owners are released after the lock, including reentrant finalizers.
			Entries.swap(NetworkProviders);
			Resources = std::move(Environment);
			Defaults = std::move(Selected);
			Configurations = std::move(Configuration);
			Instances.clear();
			++Generation;
		}
		std::shared_ptr<void> Resolve(std::string_view Service, std::string_view Backend,
		    std::string_view Contract, std::string_view Version, std::string& Error) const override
		{
			Error.clear();
			std::vector<FNativeServiceProviderDesc> Providers;
			FNativeServiceCreateContext Context;
			FInstanceKey Key;
			std::string Choice;
			std::uint64_t Snapshot;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				Choice = ResolveBackendSelection(Service, Backend, Defaults);
				Key = {std::string(Service), Choice, std::string(Contract), std::string(Version)};
				const auto Cached = Instances.find(Key);
				if (Cached != Instances.end())
				{
					if (auto Instance = Cached->second.lock()) return Instance;
				}
				Providers = Entries;
				Snapshot = Generation;
				Context.Environment = Resources.lock();
				const auto Options = Configurations.find(std::string(Service));
				if (Options != Configurations.end())
				{
					Context.Configuration = Options->second;
				}
			}
			std::vector<std::string> Candidates;
			for (const auto& Provider : Providers)
			{
				if (Provider.ServiceId == Service)
				{
					Candidates.push_back(Provider.BackendId);
				}
			}
			std::shared_ptr<void> Result;
			const auto Selection = SelectServiceBackend(Service, Choice, std::move(Candidates),
			    [&](const std::string_view Id, std::string& Failure)
			{
				const auto* Provider = FindNativeServiceProvider(Providers, Service, Id, Contract, Version, Failure);
				if (!Provider) return false;
				const FInstanceKey Identity{std::string(Service), std::string(Id), std::string(Contract), std::string(Version)};
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					const auto Cached = Instances.find(Identity);
					if (Generation == Snapshot && Cached != Instances.end())
					{
						if ((Result = Cached->second.lock())) return true;
					}
				}
				auto Instance = CreateNativeService(*Provider, Context, Failure);
				if (!Instance) return false;
				void* Handle = Instance->Get();
				Result = std::shared_ptr<void>(std::move(Instance), Handle);
				// Releasing an unselected instance may reenter the catalog.
				std::shared_ptr<void> Discarded;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					if (Generation == Snapshot)
					{
						auto& Slot = Instances[Identity];
						if (auto Existing = Slot.lock())
						{
							Discarded = std::move(Result);
							Result = std::move(Existing);
						}
						else
						{
							Slot = Result;
						}
					}
				}
				return true;
			});
			if (!Result)
			{
				const bool ContractMismatch = Choice != AutomaticBackend &&
				    std::any_of(Selection.Rejected.begin(), Selection.Rejected.end(), [](const auto& Rejected)
				{
					return Rejected.Error.find("contract mismatch") != std::string::npos;
				});
				Error = std::string(ContractMismatch ? "network.backend_contract_mismatch: " : "network.backend_unavailable: ") + std::string(Service) + '/' + Choice + ": " + (Selection.State == EBackendSelectionState::Disabled ? "service is disabled" : Selection.Error);
				return {};
			}
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (Generation == Snapshot)
				{
					auto& Slot = Instances[Key];
					if (auto Existing = Slot.lock()) return Existing;
					Slot = Result;
				}
			}
			return Result;
		}
		FNativeExtensionServiceBinding Binding()
		{
			FNativeExtensionServiceBinding Result;
			Result.Service = shared_from_this();
			Result.Id = BackendCatalogId;
			Result.Version = BackendCatalogVersion;
			return Result;
		}
		static void RemoveFromProviderRegistration(FNativeExtensionRegistrationOptions& Options)
		{
			auto& Services = Options.Services;
			Services.erase(std::remove_if(Services.begin(), Services.end(), [](const auto& Service)
			{
				return Service.Id == BackendCatalogId;
			}),
			    Services.end());
		}

	private:
		mutable std::mutex Mutex;
		// Weak entries reuse stateless factories while contexts own them, without retaining an environment.
		mutable std::map<FInstanceKey, std::weak_ptr<void>> Instances;
		std::uint64_t Generation = 0;
		std::vector<FNativeServiceProviderDesc> Entries;
		std::weak_ptr<const FModuleEnvironment> Resources;
		std::map<std::string, std::string> Defaults;
		std::map<std::string, std::map<std::string, std::string>> Configurations;
	};
} // namespace VeroScript::Network
