// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHttp.h"
#include "VeroScriptHttpSse.h"

namespace VeroScript::Http
{
	enum class ESseClientState : std::uint8_t
	{
		Pending,
		EventReady,
		Reconnecting,
		Closed,
		Failed
	};
	struct FSseClientOptions
	{
		FSseClientOptions();
		std::shared_ptr<Backend::IClientService> Service;
		std::string BackendId;
		FRequest Request;
		FSseOptions Parser;
		std::uint64_t BufferBytes = 256 * 1024;
		std::uint64_t MaxPollBytes = 64 * 1024;
		std::optional<std::chrono::milliseconds> IdleTimeout = std::chrono::seconds(60);
		std::chrono::milliseconds ReconnectDelay{3000};
		std::chrono::milliseconds MaxReconnectDelay{60000};
		// Total reconnections over this client's lifetime; zero disables reconnect.
		std::uint32_t MaxReconnects = UINT32_MAX;
	};
	struct FSseWork
	{
		// Borrowed only by Poll; never retained by the client or its I/O provider.
		// Zero units checks cancellation; positive units count consumed input bytes.
		bool (*Check)(void*, std::uint64_t) = nullptr;
		void* UserData = nullptr;
	};
	// One logical owner calls Poll/Close/accessors. It never invokes script on an
	// I/O thread. HostDriven contexts still require the host's normal PollIo calls.
	class FSseClient
	{
	public:
		static std::shared_ptr<FSseClient> Connect(
			Network::FContext& Context, FSseClientOptions Options, Network::FError& Error);
		~FSseClient();
		FSseClient(const FSseClient&) = delete;
		FSseClient& operator=(const FSseClient&) = delete;
		// Out changes only for EventReady. Failure is sticky and available in Error.
		ESseClientState Poll(FSseEvent& Out, FSseWork Work = {});
		void Close() noexcept;
		const Network::FError& Error() const noexcept;
		const std::optional<FResponse>& ResponseHead() const noexcept;
		const std::string& LastEventId() const noexcept;
		std::uint32_t ReconnectCount() const noexcept;
		std::chrono::milliseconds ReconnectRemaining() const noexcept;

	private:
		struct FImpl;
		explicit FSseClient(std::unique_ptr<FImpl> Impl);
		std::unique_ptr<FImpl> Impl;
	};
} // namespace VeroScript::Http
