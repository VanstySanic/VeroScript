// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkUpgrade.h"
#include "NetworkNativeStream.h"
#include "BoostNetworkBackend.h"

namespace VeroScript::Network::Detail
{
	inline constexpr std::string_view UpgradeBackend = "veroscript.boost.native-stream.upgrade/1";
	struct FUpgradeLeaseState
	{
		std::shared_ptr<FContext> Context;
		std::optional<FNativeStream> Stream;
		std::shared_ptr<FByteLease> Memory;
		FBytes PreRead;
		std::uint64_t ResourceId = 0;
		void Close() noexcept;
	};
	// Only idle, fully detached transports enter this lease. Taking it is unique and stays on I/O.
	struct FUpgradeLease : IUpgradeLease
	{
		explicit FUpgradeLease(std::shared_ptr<FUpgradeLeaseState> S) : State(std::move(S))
		{
		}
		~FUpgradeLease();
		FUpgradeLease(const FUpgradeLease&) = delete;
		FUpgradeLease& operator=(const FUpgradeLease&) = delete;
		std::shared_ptr<FUpgradeLeaseState> State;
	};
}
