// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetworkUpgrade.h"
#include "NetworkProvider.h"

namespace VeroScript::Network::Detail
{
	struct IUpgradeLease
	{
		virtual ~IUpgradeLease() = default;
	};
	struct FUpgradeConsumer
	{
		virtual ~FUpgradeConsumer() = default;
		virtual std::string_view BackendIdentity() const noexcept = 0;
		virtual void Take(std::unique_ptr<IUpgradeLease>, const std::shared_ptr<FOperation>&) = 0;
		virtual void Cancel() = 0;
	};
	struct FUpgradeSourceState
	{
		using FBegin = std::function<void(const std::shared_ptr<FUpgradeSourceState>&,
			std::shared_ptr<FUpgradeConsumer>, const std::shared_ptr<FOperation>&)>;
		std::shared_ptr<FContext> Context;
		FUpgradeInfo Info;
		std::shared_ptr<FByteLease> Memory;
		FBegin Begin;
		std::atomic<bool> Consumed{false};
	};
	struct FUpgradeAccess
	{
		static std::shared_ptr<FUpgradeSource> Make(
			const std::shared_ptr<FContext>&, FUpgradeInfo, FUpgradeSourceState::FBegin, FError&);
		static void Begin(const std::shared_ptr<FUpgradeSource>&, std::shared_ptr<FUpgradeConsumer>,
			const std::shared_ptr<FOperation>&);
	};
} // namespace VeroScript::Network::Detail
