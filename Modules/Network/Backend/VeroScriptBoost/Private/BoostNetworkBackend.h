// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkProvider.h"
#include "NetworkAsio.h"
#include "VeroScriptNetworkBackend.h"
#include <stdexcept>

namespace VeroScript::Network::Detail
{
	inline Asio::io_context& Io(const std::shared_ptr<FContext>& Context)
	{
		auto* Native = Runtime(Context)->QueryInterface("veroscript.boost.asio-context/1");
		if (!Native)
		{
			throw std::invalid_argument("This Boost service requires a compatible Boost network context");
		}
		return *static_cast<Asio::io_context*>(Native);
	}
	inline void Fail(const std::shared_ptr<FOperation>& Operation, EError Code, const FSystemError& Error)
	{
		if (!Terminal(State(Operation)))
		{
			Fail(Operation, Code, Error.message(), Error.value());
		}
	}
}
namespace VeroScript::Network
{
	FOperation BoostResolve(FContext&, std::string, std::uint16_t, FOperationOptions);
	FOperation BoostConnectTcp(FContext&, std::string, std::uint16_t, FOperationOptions);
	std::shared_ptr<FTcpListener> BoostListenTcp(FContext&, std::string, std::uint16_t, FError&, int);
	std::shared_ptr<FUdpSocket> BoostBindUdp(FContext&, std::string, std::uint16_t, FError&);
}
