// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUi.h"
#include "VeroScriptRmlUiModule.h"
#include "VeroScriptNativeExtension.h"
#include <algorithm>
#include <cstring>
#include <exception>

namespace VeroScript::RmlUi
{
	namespace
	{
		class FService final : public Gui::IBackendService
		{
		public:
			std::unique_ptr<Gui::IBackendContext> Create(const Gui::FBackendOptions& Input, std::string& Error) override
			{
				if (!Input.Owner)
				{
					Error = "rmlui.provider_owner_required";
					return {};
				}
				FOptions Options;
				Options.Device = Input.Device;
				Options.Texture = Input.Texture;
				Options.Surface = Input.Surface;
				Options.RenderOptions = Input.RenderOptions;
				Options.MaxCompositionBytes = Input.MaxCompositionBytes;
				Options.Owner = Input.Owner;
				Options.ReadResource = Input.ReadResource;
				Options.DecodeImage = Input.DecodeImage;
				Options.Images = Input.Images;
				Options.Fonts = Input.Fonts;
				Options.TextLayouts = Input.TextLayouts;
				return CreateBackend(Options, Error);
			}
			bool CollectRetired(std::string& Error) override
			{
				return RmlUi::CollectRetired(Error);
			}
		};
		void* CreateService(const FNativeServiceCreateContext& Context, void*, std::string& Error)
		{
			if (Context.CheckCancelled)
			{
				Context.CheckCancelled();
			}
			Error.clear();
			return static_cast<Gui::IBackendService*>(new FService);
		}
		void DestroyService(void* Instance, void*)
		{
			delete static_cast<Gui::IBackendService*>(Instance);
		}
		void CopyError(char* Output, std::uint32_t Capacity, const std::string& Error)
		{
			if (Output && Capacity)
			{
				const auto Size = std::min<std::size_t>(Error.size(), Capacity - 1);
				std::memcpy(Output, Error.data(), Size);
				Output[Size] = 0;
			}
		}
		void* VEROSCRIPT_EXTENSION_CALL Register(const void* Input, char* Output, std::uint32_t Capacity)
		{
			try
			{
				if (!Input)
				{
					return nullptr;
				}
				auto& Host = *const_cast<INativeExtensionHost*>(static_cast<const INativeExtensionHost*>(Input));
				FNativeServiceProviderDesc Provider;
				Provider.ServiceId = Gui::ServiceId;
				Provider.BackendId = "rmlui";
				Provider.ContractId = Gui::ContractId;
				Provider.ContractVersion = Gui::ContractVersion;
				Provider.ConfigurationKey = "rmlui.gui.0.1.1";
				Provider.Create = CreateService;
				Provider.Destroy = DestroyService;
				std::string Error;
				if (!Host.RegisterProvider(std::move(Provider), Error))
				{
					CopyError(Output, Capacity, Error);
					return nullptr;
				}
				return new bool(true);
			}
			catch (const std::exception& Exception)
			{
				CopyError(Output, Capacity, Exception.what());
			}
			catch (...)
			{
				CopyError(Output, Capacity, "rmlui.registration_failed");
			}
			return nullptr;
		}
		void VEROSCRIPT_EXTENSION_CALL Destroy(void* State)
		{
			delete static_cast<bool*>(State);
		}
	} // namespace
} // namespace VeroScript::RmlUi

extern "C" VEROSCRIPT_RMLUI_MODULE_API std::uint32_t VEROSCRIPT_EXTENSION_CALL
veroscript_rmlui_query(std::uint32_t Version, std::uint32_t Size, void* Output)
{
	using namespace VeroScript;
	if (Version != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Output)
	{
		return 0;
	}
	try
	{
		FNativeExtensionDescriptor Descriptor;
		const auto Abi = BuildNativeExtensionAbi();
		const auto* Platform = GetHostPlatformInfo().Platform;
		if (Abi.size() >= sizeof(Descriptor.Abi) || std::strlen(Platform) >= sizeof(Descriptor.Platform))
		{
			return 0;
		}
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		std::memcpy(Descriptor.Platform, Platform, std::strlen(Platform) + 1);
		std::memcpy(Descriptor.Id, "veroscript-rmlui", sizeof("veroscript-rmlui"));
		std::memcpy(Descriptor.Version, "0.1.1", sizeof("0.1.1"));
		Descriptor.Roles = NativeExtensionRole(ENativeExtensionRole::Provider);
		Descriptor.Flags = NativeExtensionParallelRegistrations;
		Descriptor.Create = RmlUi::Register;
		Descriptor.Destroy = RmlUi::Destroy;
		std::memcpy(Output, &Descriptor, sizeof(Descriptor));
		return Version;
	}
	catch (...)
	{
		return 0;
	}
}

#if !defined(VEROSCRIPT_RMLUI_STATIC_PROVIDER)
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t
	VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(std::uint32_t Version, std::uint32_t Size, void* Output)
{
	return veroscript_rmlui_query(Version, Size, Output);
}
#endif
