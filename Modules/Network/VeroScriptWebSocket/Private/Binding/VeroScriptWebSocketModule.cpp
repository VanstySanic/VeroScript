// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptWebSocketModule.h"
#include "VeroScriptWebSocket.h"
#include "WebSocketBinding.h"
#include "NetworkModule.h"
#include "VeroScriptNetworkBuildInfo.h"

namespace VeroScript::WebSocket
{
	namespace
	{
		using namespace NetworkBinding;
		#include "WebSocketRegistration.inl"

		struct FModuleTraits
		{
			static constexpr const char* Id = "websocket";
			static constexpr const char* DiagnosticId = "websocket.binding";
			static constexpr const char* OperationType = "websocket::operation";
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
} // namespace VeroScript::WebSocket
