// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNativeExtension.h"

namespace VeroScript::Network
{
	inline constexpr const char* BackendCatalogId = "veroscript.network.backends";
	inline constexpr const char* BackendCatalogVersion = "0.1.1";
	inline bool IsBackendService(std::string_view Service) noexcept
	{
		return Service == "network" || Service == "http-client" || Service == "http-server" ||
			Service == "websocket-client" || Service == "websocket-server";
	}
	// Supplied by the embedding host; resolution uses Core's registered provider contracts.
	class IBackendCatalog : public INativeExtensionService
	{
	public:
		virtual std::shared_ptr<void> Resolve(std::string_view Service, std::string_view Backend,
			std::string_view Contract, std::string_view Version, std::string& Error) const = 0;
	};
} // namespace VeroScript::Network
