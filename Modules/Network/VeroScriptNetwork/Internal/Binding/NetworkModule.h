// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkBindingSupport.h"
#include <cstring>

namespace VeroScript::NetworkBinding
{
	template <class T>
	class TModule
	{
	public:
		static std::uint32_t Query(std::uint32_t RequestedVersion, std::uint32_t Size, void* Descriptor)
		{
			if (RequestedVersion != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) ||
				!Descriptor)
			{
				return 0;
			}
			FNativeExtensionDescriptor D;
			const auto Abi = BuildNativeExtensionAbi();
			if (Abi.size() >= sizeof(D.Abi))
			{
				return 0;
			}
			std::memcpy(D.Abi, Abi.c_str(), Abi.size() + 1);
			std::memcpy(D.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
			std::memcpy(D.Platform, GetHostPlatformInfo().Platform, std::strlen(GetHostPlatformInfo().Platform) + 1);
			std::memcpy(D.Id, T::Id, std::strlen(T::Id) + 1);
			std::memcpy(D.Version, "0.1.1", 6);
			D.Roles = NativeExtensionRole(ENativeExtensionRole::Bindings);
			D.Flags = NativeExtensionParallelRegistrations;
			D.Create = Create;
			D.Destroy = Destroy;
			std::memcpy(Descriptor, &D, sizeof(D));
			return NativeExtensionApiVersion;
		}

	private:
		static void* VEROSCRIPT_EXTENSION_CALL Create(const void* Host, char* Buffer, std::uint32_t Capacity)
		{
			auto S = std::make_unique<FState>();
			S->DiagnosticId = T::DiagnosticId;
			S->Host = const_cast<INativeExtensionHost*>(static_cast<const INativeExtensionHost*>(Host));
			std::string Error;
			std::string OptionalError;
			S->Backends = std::static_pointer_cast<const Network::IBackendCatalog>(
				S->Host->GetService(Network::BackendCatalogId, Network::BackendCatalogVersion, OptionalError));
			if (T::RegisterTypes(*S, Error))
			{
				FValue Lease;
				if (Make(*S, T::OperationType, FOperationValue{}, Lease, Error))
				{
					S->CodeOwner = std::get<FHostObjectHandle>(Lease)->ModuleOwner;
					return S.release();
				}
			}
			if (Capacity)
			{
				const auto Count = std::min<std::size_t>(Error.size(), Capacity - 1);
				std::memcpy(Buffer, Error.data(), Count);
				Buffer[Count] = 0;
			}
			return nullptr;
		}
		static void VEROSCRIPT_EXTENSION_CALL Destroy(void* State)
		{
			delete static_cast<FState*>(State);
		}
	};
} // namespace VeroScript::NetworkBinding
