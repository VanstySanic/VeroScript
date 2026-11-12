// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"

namespace VeroScript::Network
{
	struct FUpgradeInfo
	{
		std::uint32_t ContractVersion = 1;
		std::string Protocol, BackendIdentity;
		// Owned HTTP request headers, without a body. This is metadata, never first-frame data.
		std::string RequestHead;
		std::uint64_t RequestGeneration = 0;
	};
	namespace Detail
	{
		struct FUpgradeSourceState;
		struct FUpgradeAccess;
	} // namespace Detail
	// Preparing a source does not detach its transport or send a protocol response.
	class FUpgradeSource
	{
		friend struct Detail::FUpgradeAccess;

	public:
		const FUpgradeInfo& Info() const noexcept;
		bool BelongsTo(const FContext& Context) const noexcept;
		bool IsConsumed() const noexcept;

	private:
		explicit FUpgradeSource(std::shared_ptr<Detail::FUpgradeSourceState>);
		std::shared_ptr<Detail::FUpgradeSourceState> Impl;
	};
} // namespace VeroScript::Network
