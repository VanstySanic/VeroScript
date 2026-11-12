// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHttpClientOptions.h"
#include "NetworkProvider.h"

namespace VeroScript::Http::Detail
{
	struct FAttemptHistoryAccess
	{
		static bool Bind(const std::shared_ptr<FAttemptHistory>&, const std::shared_ptr<Network::Detail::FContext>&,
			const std::shared_ptr<Network::Detail::FOperation>&);
		static bool Begin(const std::shared_ptr<FAttemptHistory>&, const std::shared_ptr<Network::Detail::FOperation>&,
			std::uint32_t) noexcept;
		static void Append(const std::shared_ptr<FAttemptHistory>&, const FAttempt&) noexcept;
		static void Lost(const std::shared_ptr<FAttemptHistory>&) noexcept;
		static void End(const std::shared_ptr<FAttemptHistory>&, const std::shared_ptr<Network::Detail::FOperation>&,
			std::uint32_t) noexcept;
	};
} // namespace VeroScript::Http::Detail
