// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStbModule.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptStb.h"
#include <algorithm>
#include <cstring>
#include <exception>

namespace VeroScript::Stb
{
	namespace
	{
		struct FRegistration
		{
			INativeExtensionHost *Host = nullptr;
		};

		class FProvider final : public Image::IService
		{
		public:
			FProvider() : Service(CreateImageService())
			{
			}
			const char *GetBackendId() const noexcept override
			{
				return "stb";
			}
			bool Decode(const std::vector<std::uint8_t> &Bytes, Image::FDecodedImage &Result, std::string &Error,
						const Image::FDecodeOptions &Options) const override
			{
				return Service->Decode(Bytes, Result, Error, Options);
			}

		private:
			std::shared_ptr<Image::IService> Service;
		};
		void *Create(const FNativeServiceCreateContext &Context, void *, std::string &Error)
		{
			if (Context.CheckCancelled)
			{
				Context.CheckCancelled();
			}
			Error.clear();
			return static_cast<Image::IService *>(new FProvider());
		}
		void DestroyService(void *Value, void *)
		{
			delete static_cast<Image::IService *>(Value);
		}
		void *VEROSCRIPT_EXTENSION_CALL Register(const void *Input, char *Output, std::uint32_t Capacity)
		{
			std::string Error;
			try
			{
				if (!Input)
				{
					return nullptr;
				}
				auto State = std::make_unique<FRegistration>();
				State->Host = const_cast<INativeExtensionHost *>(static_cast<const INativeExtensionHost *>(Input));

				FNativeServiceProviderDesc Provider;
				Provider.ServiceId = Image::ServiceId;
				Provider.BackendId = "stb";
				Provider.ContractId = Image::ContractId;
				Provider.ContractVersion = Image::ContractVersion;
				Provider.ConfigurationKey = "stb.Image.0.1.1";
				Provider.UserData = State.get();
				Provider.Create = Create;
				Provider.Destroy = DestroyService;
				if (State->Host->RegisterProvider(std::move(Provider), Error))
				{
					return State.release();
				}
			}
			catch (const std::exception &Exception)
			{
				Error = Exception.what();
			}
			catch (...)
			{
				Error = "stb.registration_failed";
			}
			if (Output && Capacity)
			{
				auto Count = std::min<std::size_t>(Error.size(), Capacity - 1);
				std::memcpy(Output, Error.data(), Count);
				Output[Count] = 0;
			}
			return nullptr;
		}
		void VEROSCRIPT_EXTENSION_CALL Destroy(void *State)
		{
			delete static_cast<FRegistration *>(State);
		}
	} // namespace
} // namespace VeroScript::Stb
extern "C" VEROSCRIPT_STB_MODULE_API std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_stb_query(std::uint32_t Version,
																								  std::uint32_t Size,
																								  void *Output)
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
		const auto *Platform = GetHostPlatformInfo().Platform;
		if (Abi.size() >= sizeof(Descriptor.Abi) || std::strlen(Platform) >= sizeof(Descriptor.Platform))
		{
			return 0;
		}
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		std::memcpy(Descriptor.Platform, Platform, std::strlen(Platform) + 1);
		std::memcpy(Descriptor.Id, "veroscript-stb", sizeof("veroscript-stb"));
		std::memcpy(Descriptor.Version, "0.1.1", sizeof("0.1.1"));
		Descriptor.Roles = NativeExtensionRole(ENativeExtensionRole::Provider);
		Descriptor.Flags = NativeExtensionParallelRegistrations;
		Descriptor.Create = Stb::Register;
		Descriptor.Destroy = Stb::Destroy;
		std::memcpy(Output, &Descriptor, sizeof(Descriptor));
		return Version;
	}
	catch (...)
	{
		return 0;
	}
}
#if !defined(VEROSCRIPT_STB_STATIC_PROVIDER)
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t
	VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(std::uint32_t V, std::uint32_t S, void *O)
{
	return veroscript_stb_query(V, S, O);
}
#endif
