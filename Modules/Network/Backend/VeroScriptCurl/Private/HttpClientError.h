// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHttpBackend.h"
#include <curl/curl.h>
#include <stdexcept>

namespace VeroScript::Http::Detail
{
	inline Network::EError TransportError(CURLcode Code) noexcept
	{
		switch (Code)
		{
		case CURLE_OK:
			return Network::EError::None;
		case CURLE_COULDNT_RESOLVE_HOST:
		case CURLE_COULDNT_RESOLVE_PROXY:
			return Network::EError::Resolve;
		case CURLE_COULDNT_CONNECT:
			return Network::EError::Connect;
		case CURLE_OPERATION_TIMEDOUT:
			return Network::EError::TimedOut;
		case CURLE_SEND_ERROR:
			return Network::EError::Write;
		case CURLE_RECV_ERROR:
		case CURLE_GOT_NOTHING:
		case CURLE_PARTIAL_FILE:
		case CURLE_HTTP2_STREAM:
			return Network::EError::Read;
		case CURLE_OUT_OF_MEMORY:
			return Network::EError::Allocation;
		default:
			return Network::EError::Protocol;
		}
	}

} // namespace VeroScript::Http::Detail
