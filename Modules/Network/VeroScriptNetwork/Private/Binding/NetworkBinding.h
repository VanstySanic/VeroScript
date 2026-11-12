// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkBindingSupport.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterNetworkUrl(FState& S, FHostTypeId Api, std::string& Error);
	bool RegisterNetworkStreams(FState& S, FHostTypeId Api, std::string& Error);
	bool RegisterNetworkFiles(FState&, FHostTypeId, std::string&);
	bool RegisterNetworkUpgrade(FState&, std::string&);
	bool RegisterNetworkTls(FState& S, FHostTypeId Api, std::string& Error);
	std::shared_ptr<const INetworkBindingService> MakeNetworkBindingService(
		FState&, const std::shared_ptr<Network::FContext>&, IScriptCallInvoker&, std::string&);
} // namespace VeroScript::NetworkBinding
