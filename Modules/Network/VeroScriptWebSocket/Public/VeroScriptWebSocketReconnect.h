// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptWebSocket.h"

namespace VeroScript::WebSocket
{
	struct FReconnectPolicy
	{
		// One attempt disables retries. Invoke explicitly after an old connection
		// closes to reconnect; the helper never replaces a caller's live handle.
		std::uint32_t MaxAttempts = 1;
		std::chrono::milliseconds Delay{100};
		std::chrono::milliseconds MaxDelay{5000};
		std::chrono::milliseconds MaxElapsed{30000};
		bool bExponentialBackoff = true;
		bool bRetryProtocolErrors = false;
	};

	struct FReconnectAttempt
	{
		std::uint32_t Number = 0;
		bool bComplete = false;
		bool bConnected = false;
		Network::FError Error;
		// Only diagnostic text can be truncated, never attempt numbering/status.
		bool bMessageTruncated = false;
	};

	struct FReconnectSnapshot
	{
		std::uint64_t Generation = 0;
		std::vector<FReconnectAttempt> Attempts;
		bool bFinal = false;
		Network::FError Error;
		bool bMessageTruncated = false;
	};

	namespace Detail
	{
		struct FReconnectState;
	}

	class FReconnectTask
	{
	public:
		const Network::FOperation& Operation() const noexcept;
		// An owned point-in-time copy; subsequent attempts cannot change it.
		// Caller-owned copies are outside the context's retained storage.
		bool TrySnapshot(FReconnectSnapshot& Out, Network::FError& Error) const;
		// Module-owned copy charged to the context for its entire shared lifetime.
		std::shared_ptr<const FReconnectSnapshot> Snapshot(Network::FError& Error) const;

	private:
		friend FReconnectTask ConnectWithRetry(Network::FContext&, std::string, FReconnectPolicy, FOptions);
		Network::FOperation Op;
		std::shared_ptr<Detail::FReconnectState> State;
	};

	// Result is shared_ptr<FConnection>, identical to Connect. Each invocation
	// owns an independent generation. No application message is sent or replayed.
	FReconnectTask ConnectWithRetry(
		Network::FContext& Context, std::string Url, FReconnectPolicy Policy = {}, FOptions Options = {});
} // namespace VeroScript::WebSocket
