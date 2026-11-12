// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"
#include <boost/asio.hpp>

namespace VeroScript::Network::Detail
{
	namespace Asio = boost::asio;
	using FTcp = Asio::ip::tcp;
	using FSystemError = boost::system::error_code;
} // namespace VeroScript::Network::Detail
