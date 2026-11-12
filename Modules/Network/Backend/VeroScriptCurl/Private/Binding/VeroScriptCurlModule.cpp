// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCurlModule.h"
#include "VeroScriptCurl.h"
#include "VeroScriptBackendBuildInfo.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <string_view>

namespace VeroScript::Curl
{
	std::unique_ptr<Http::Backend::IClientService> NewHttpClientService();
	namespace
	{
		void WriteError(std::string_view Error, char* Output, std::uint32_t Capacity) noexcept
		{
			if (!Output || !Capacity) return;
			const auto Count = std::min<std::size_t>(Error.size(), Capacity - 1);
			if (Count)
			{
				std::memcpy(Output, Error.data(), Count);
			}
			Output[Count] = 0;
		}
		template <class T, auto Factory>
		void* Create(const FNativeServiceCreateContext& Context, void*, std::string& Error)
		{
			Error.clear();
			if (Context.CheckCancelled)
			{
				Context.CheckCancelled();
			}
			if (!Context.Configuration.empty())
			{
				Error = "Unsupported backend service configuration";
				return nullptr;
			}
			return Factory().release();
		}
		template <class T>
		void DestroyService(void* Value, void*)
		{
			delete static_cast<T*>(Value);
		}
		template <class T, auto Factory>
		bool Add(INativeExtensionHost& Host, const char* Service, const char* Contract, std::string& Error)
		{
			FNativeServiceProviderDesc Provider;
			Provider.ServiceId = Service;
			Provider.BackendId = "curl";
			Provider.ContractId = Contract;
			Provider.ContractVersion = "0.1.1";
			Provider.ConfigurationKey = Provider.BackendId + '.' + Service + ".0.1.1";
			Provider.Create = Create<T, Factory>;
			Provider.Destroy = DestroyService<T>;
			return Host.RegisterProvider(std::move(Provider), Error);
		}
		void* VEROSCRIPT_EXTENSION_CALL Register(const void* Input, char* Output, std::uint32_t Capacity)
		try
		{
			if (!Input)
			{
				WriteError("Missing extension host", Output, Capacity);
				return nullptr;
			}
			auto Lease = std::make_unique<int>(0);
			auto& Host = *const_cast<INativeExtensionHost*>(static_cast<const INativeExtensionHost*>(Input));
			std::string Error;
			if (!Add<Http::Backend::IClientService, NewHttpClientService>(Host, "http-client", Http::Backend::ClientContractId, Error))
			{
				WriteError(Error, Output, Capacity);
				return nullptr;
			}
			return Lease.release();
		}
		catch (const std::exception& Error)
		{
			WriteError(Error.what(), Output, Capacity);
			return nullptr;
		}
		catch (...)
		{
			WriteError("Backend provider registration failed", Output, Capacity);
			return nullptr;
		}
		void VEROSCRIPT_EXTENSION_CALL Destroy(void* Value)
		{
			delete static_cast<int*>(Value);
		}
	}
	const char* GetImplementationIdentity() noexcept
	{
		return VEROSCRIPT_BACKEND_IMPLEMENTATION_ID;
	}
	std::uint32_t VEROSCRIPT_EXTENSION_CALL QueryModule(std::uint32_t Version, std::uint32_t Size, void* Output)
	try
	{
		if (Version != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Output) return 0;
		const auto Abi = BuildNativeExtensionAbi();
		FNativeExtensionDescriptor Descriptor;
		const auto* Platform = GetHostPlatformInfo().Platform;
		if (Abi.size() >= sizeof(Descriptor.Abi) || std::strlen(Platform) >= sizeof(Descriptor.Platform)) return 0;
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		std::memcpy(Descriptor.Platform, Platform, std::strlen(Platform) + 1);
		std::memcpy(Descriptor.Id, "curl", sizeof("curl"));
		std::memcpy(Descriptor.Version, "0.1.1", sizeof("0.1.1"));
		Descriptor.Roles = NativeExtensionRole(ENativeExtensionRole::Provider);
		Descriptor.Flags = NativeExtensionParallelRegistrations;
		Descriptor.Create = Register;
		Descriptor.Destroy = Destroy;
		std::memcpy(Output, &Descriptor, sizeof(Descriptor));
		return Version;
	}
	catch (...)
	{
		return 0;
	}
} // namespace VeroScript::Curl

extern "C" std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_curl_query(
    std::uint32_t Version, std::uint32_t Size, void* Output)
{
	return VeroScript::Curl::QueryModule(Version, Size, Output);
}
