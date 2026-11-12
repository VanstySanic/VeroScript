// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetworkModule.h"
#include "VeroScriptNetwork.h"
#include "NetworkBinding.h"
#include "NetworkModule.h"
#include "VeroScriptNetworkBuildInfo.h"

namespace VeroScript::Network
{
	namespace
	{
		using namespace NetworkBinding;
		#include "NetworkRegistration.inl"

		struct FModuleTraits
		{
			static constexpr const char* Id = "network";
			static constexpr const char* DiagnosticId = "network.binding";
			static constexpr const char* OperationType = "Network::operation";
			static constexpr auto RegisterTypes = &Register;
		};
	} // namespace

	const char* GetImplementationIdentity() noexcept
	{
		return VEROSCRIPT_NETWORK_IMPLEMENTATION_ID;
	}
	std::uint32_t VEROSCRIPT_EXTENSION_CALL QueryModule(
		std::uint32_t RequestedVersion, std::uint32_t Size, void* Descriptor)
	{
		return TModule<FModuleTraits>::Query(RequestedVersion, Size, Descriptor);
	}
} // namespace VeroScript::Network
