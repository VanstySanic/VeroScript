// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiServices.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptResources.h"
#include <algorithm>
#include <exception>
#include <map>
#include <mutex>

namespace VeroScript::Gui
{
	namespace
	{
		class FPinnedBackend final : public IBackendContext
		{
		public:
			//~ Begin IBackendContext Interface
			FCapabilities GetCapabilities() const noexcept override
			{
				return Backend->GetCapabilities();
			}
			bool Synchronize(const FViewport &Viewport, const std::vector<FBackendNode> &Nodes,
			    std::string &Error) override
			{
				return Backend->Synchronize(Viewport, Nodes, Error);
			}
			bool Update(double Time, std::vector<FLayoutBox> &Layout, std::string &Error) override
			{
				return Backend->Update(Time, Layout, Error);
			}
			bool Render(bool &Presented, std::string &Error) override
			{
				return Backend->Render(Presented, Error);
			}
			bool Close(std::string &Error) override
			{
				return Backend->Close(Error);
			}
			bool SetResourceReader(FResourceReader Reader, std::string &Error) override
			{
				return Backend->SetResourceReader(std::move(Reader), Error);
			}

			std::optional<bool> Contains(std::uint64_t Id, double X, double Y) const override
			{
				return Backend->Contains(Id, X, Y);
			}

			bool LoadFont(const FFontFace &Font, std::string &Error) override
			{
				return Backend->LoadFont(Font, Error);
			}
			bool FocusTextControl(std::uint64_t Id, const FTextInputServices &Services, std::string &Error) override
			{
				return Backend->FocusTextControl(Id, Services, Error);
			}
			bool EditText(std::uint64_t Id, const Platform::FEvent &Input, FTextEditResult &Result,
			    std::string &Error) override
			{
				return Backend->EditText(Id, Input, Result, Error);
			}
			bool IsTextComposing(std::uint64_t Id) const override
			{
				return Backend->IsTextComposing(Id);
			}
			bool SetTextSelection(std::uint64_t Id, std::int64_t Start, std::int64_t End, std::string &Error) override
			{
				return Backend->SetTextSelection(Id, Start, End, Error);
			}
			//~ End IBackendContext Interface

			FPinnedBackend(std::shared_ptr<FNativeServiceInstance> InOwner, std::unique_ptr<IBackendContext> InBackend)
			    : Owner(std::move(InOwner)), Backend(std::move(InBackend))
			{
			}

		private:
			// The foreign backend and its deleter finish before the Core-owned lease is released.
			std::shared_ptr<FNativeServiceInstance> Owner;
			std::unique_ptr<IBackendContext> Backend;
		};

		const FNativeServiceProviderDesc *Find(const std::vector<FNativeServiceProviderDesc> &Providers,
		    const std::string &Service, const std::string &Backend,
		    const std::string &Contract, const std::string &Version,
		    std::string &Error)
		{
			const auto *Found = FindNativeServiceProvider(Providers, Service, Backend, Contract, Version, Error);
			if (!Found)
			{
				Error = std::string(Error.find("contract mismatch") != std::string::npos
				                ? "gui.provider_contract_mismatch: "
				                : "gui.provider_unavailable: ") +
				    Service + '/' + Backend;
			}
			return Found;
		}

		std::shared_ptr<FNativeServiceInstance> CreateService(const std::vector<FNativeServiceProviderDesc> &Providers,
		    const char *Service, const std::string &Backend, const char *Contract, const char *Version,
		    const FNativeServiceCreateContext &Context, std::string &Error)
		{
			auto Instance = CreateNativeService(Providers, Service, Backend, Contract, Version, Context, Error);
			if (!Instance)
			{
				Error = std::string(Error.find("contract mismatch") != std::string::npos
				                ? "gui.provider_contract_mismatch: "
				                : "gui.provider_unavailable: ") +
				    Error;
			}
			return Instance;
		}

		// Applications made from the same factory set share immutable resource configurations.
		// Weak entries keep live RmlUi contexts compatible without extending service/module lifetimes.
		template <class T>
		auto ResourceFactory(std::shared_ptr<const std::vector<FNativeServiceProviderDesc>> Providers,
		    FNativeServiceCreateContext Context, const char *ServiceId, const char *ContractId, const char *Version)
		{
			struct FCache
			{
				std::mutex Mutex;
				std::map<std::string, std::weak_ptr<T>> Instances;
			};
			auto Cache = std::make_shared<FCache>();
			return [Providers = std::move(Providers), Context = std::move(Context), Cache, ServiceId, ContractId, Version](const std::string &Backend, std::string &Error) -> std::shared_ptr<T>
			{
				Error.clear();
				const auto Request = ResolveBackendSelection(ServiceId, Backend, Context.Environment ? Context.Environment->GetDescription().SelectedBackends : std::map<std::string, std::string>{});
				{
					std::lock_guard<std::mutex> Guard(Cache->Mutex);
					const auto Cached = Cache->Instances.find(Request);
					if (Cached != Cache->Instances.end())
					{
						if (auto Existing = Cached->second.lock()) return Existing;
					}
				}
				std::vector<std::string> Candidates;
				for (const auto &Provider : *Providers)
				{
					if (Provider.ServiceId == ServiceId)
					{
						Candidates.push_back(Provider.BackendId);
					}
				}
				std::shared_ptr<T> Service;
				const auto Selection = SelectServiceBackend(ServiceId, Request, std::move(Candidates),
				    [&](const std::string_view Id, std::string &Failure)
				{
					const auto *Provider = Find(*Providers, ServiceId, std::string(Id), ContractId, Version, Failure);
					if (!Provider) return false;
					{
						std::lock_guard<std::mutex> Guard(Cache->Mutex);
						const auto Cached = Cache->Instances.find(std::string(Id));
						if (Cached != Cache->Instances.end())
						{
							Service = Cached->second.lock();
							if (Service) return true;
						}
					}
					auto Instance = CreateNativeService(*Provider, Context, Failure);
					if (!Instance) return false;
					auto *Pointer = static_cast<T *>(Instance->Get());
					Service = std::shared_ptr<T>(std::move(Instance), Pointer);
					{
						std::lock_guard<std::mutex> Guard(Cache->Mutex);
						auto &Entry = Cache->Instances[std::string(Id)];
						if (auto Existing = Entry.lock())
						{
							Service = std::move(Existing);
						}
						else
						{
							Entry = Service;
						}
					}
					return true;
				}, Context.CheckCancelled);
				if (!Service)
				{
					Error = Selection.State == EBackendSelectionState::Disabled
					    ? "gui.provider_unavailable: " + std::string(ServiceId) + "/none"
					    : "gui.provider_unavailable: " + Selection.Error;
					return {};
				}
				{
					std::lock_guard<std::mutex> Guard(Cache->Mutex);
					auto &Entry = Cache->Instances[Request];
					if (auto Existing = Entry.lock()) return Existing;
					Entry = Service;
				}
				return Service;
			};
		}
	} // namespace

	FResourceReader MakeGuiResourceReader(std::shared_ptr<const FModuleEnvironment> Environment)
	{
		return [Environment = std::move(Environment)](const std::string &Identity, std::vector<std::uint8_t> &Bytes,
		           std::string &Error)
		{
			if (!IsResourceDependencyId(Identity))
			{
				return ReadGuiResourceFile(Identity, Bytes, Error);
			}
			FResourceRequest Request;
			std::string Requester;
			FResourceData Resource;
			if (!Environment || !ParseResourceDependencyId(Identity, Request, Requester))
			{
				Error = "gui.resource_provider_unavailable";
				return false;
			}
			if (!Environment->ReadResource(Request, Requester, {}, Resource, Error) || !Resource.bExists)
			{
				if (Error.empty())
				{
					Error = "gui.resource_not_found: " + Identity;
				}
				return false;
			}
			Bytes = std::move(Resource.Bytes);
			return true;
		};
	}

	std::unique_ptr<IBackendContext> CreateRegisteredGuiBackend(const FNativeServiceProviderDesc &Provider,
	    FBackendOptions Options,
	    const FNativeServiceCreateContext &Context,
	    std::string &Error)
	{
		if (Provider.ServiceId != ServiceId || Provider.ContractId != ContractId ||
		    Provider.ContractVersion != ContractVersion)
		{
			Error = "gui.provider_contract_mismatch";
			return {};
		}
		auto Instance = CreateNativeService(Provider, Context, Error);
		if (!Instance)
		{
			return {};
		}
		Options.Owner = Instance;
		std::unique_ptr<IBackendContext> Backend;
		try
		{
			Backend = static_cast<IBackendService *>(Instance->Get())->Create(Options, Error);
		}
		catch (const std::exception &Failure)
		{
			Error = "gui.provider_unavailable: " + std::string(Failure.what());
			return {};
		}
		catch (...)
		{
			Error = "gui.provider_unavailable: backend factory exception";
			return {};
		}
		if (!Backend || !Error.empty())
		{
			return {};
		}
		return std::make_unique<FPinnedBackend>(std::move(Instance), std::move(Backend));
	}

	FApplicationFactories MakeRegisteredServiceFactories(std::vector<FNativeServiceProviderDesc> Providers,
	    std::shared_ptr<const FModuleEnvironment> Environment)
	{
		auto Shared = std::make_shared<const std::vector<FNativeServiceProviderDesc>>(std::move(Providers));
		FNativeServiceCreateContext Context;
		Context.Environment = std::move(Environment);
		FApplicationFactories Result;
		Result.DefaultBackend = [Context](const std::string_view Service)
		{
			return ResolveBackendSelection(Service, {}, Context.Environment ? Context.Environment->GetDescription().SelectedBackends : std::map<std::string, std::string>{});
		};
		Result.Platform = [Shared, Context](const auto &Backend, auto &Error) -> std::shared_ptr<Platform::IService>
		{
			auto Instance = CreateService(*Shared, Platform::ServiceId, Backend,
			    Platform::ContractId, Platform::ContractVersion, Context, Error);
			if (!Instance)
			{
				return {};
			}
			auto *Service = static_cast<Platform::IService *>(Instance->Get());
			return std::shared_ptr<Platform::IService>(std::move(Instance), Service);
		};
		Result.Graphics = [Shared, Context](const auto &Backend, auto &Error) -> std::shared_ptr<Graphics::IService>
		{
			auto Instance = CreateService(*Shared, Graphics::ServiceId, Backend,
			    Graphics::ContractId, Graphics::ContractVersion, Context, Error);
			if (!Instance)
			{
				return {};
			}
			auto *Service = static_cast<Graphics::IService *>(Instance->Get());
			return std::shared_ptr<Graphics::IService>(std::move(Instance), Service);
		};
		if (std::any_of(Shared->begin(), Shared->end(),
		        [](const auto &Item)
		{
			return Item.ServiceId == Image::ServiceId;
		}))
		{
			Result.Image = [Shared, Context](const auto &Backend, auto &Error) -> std::shared_ptr<Image::IService>
			{
				auto Instance = CreateService(*Shared, Image::ServiceId, Backend,
				    Image::ContractId, Image::ContractVersion, Context, Error);
				if (!Instance)
				{
					return {};
				}
				auto *Service = static_cast<Image::IService *>(Instance->Get());
				return std::shared_ptr<Image::IService>(std::move(Instance), Service);
			};
		}
		if (std::any_of(Shared->begin(), Shared->end(),
		        [](const auto &Item)
		{
			return Item.ServiceId == Font::ServiceId;
		}))
		{
			Result.Font = ResourceFactory<Font::IService>(Shared, Context, Font::ServiceId,
			    Font::ContractId, Font::ContractVersion);
		}
		if (std::any_of(Shared->begin(), Shared->end(),
		        [](const auto &Item)
		{
			return Item.ServiceId == Font::TextLayoutServiceId;
		}))
		{
			Result.TextLayout = ResourceFactory<Font::ITextLayoutService>(Shared, Context, Font::TextLayoutServiceId,
			    Font::TextLayoutContractId, Font::TextLayoutContractVersion);
		}
		Result.Gui = [Shared, Context](const auto &Backend, const FBackendOptions &Input,
		                 auto &Error) -> std::unique_ptr<IBackendContext>
		{
			std::vector<std::string> Candidates;
			for (const auto &Provider : *Shared)
			{
				if (Provider.ServiceId == ServiceId)
				{
					Candidates.push_back(Provider.BackendId);
				}
			}
			std::unique_ptr<IBackendContext> Created;
			const auto Request = ResolveBackendSelection(ServiceId, Backend, Context.Environment ? Context.Environment->GetDescription().SelectedBackends : std::map<std::string, std::string>{});
			const auto Selection = SelectServiceBackend(ServiceId, Request, std::move(Candidates),
			    [&](const std::string_view Id, std::string &Failure)
			{
				const auto *Provider = Find(*Shared, ServiceId, std::string(Id), ContractId, ContractVersion, Failure);
				if (!Provider)
				{
					return false;
				}
				auto Options = Input;
				Options.ReadResource = MakeGuiResourceReader(Context.Environment);
				Created = CreateRegisteredGuiBackend(*Provider, std::move(Options), Context, Failure);
				return Created != nullptr;
			}, Context.CheckCancelled);
			Error = Selection.State == EBackendSelectionState::Disabled ? "gui.provider_unavailable: gui/none"
			    : Created                                               ? std::string{}
			                                                            : "gui.provider_unavailable: " + Selection.Error;
			return Created;
		};
		return Result;
	}
} // namespace VeroScript::Gui
