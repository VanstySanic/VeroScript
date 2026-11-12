// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetworkBackend.h"
#include "VeroScriptHttp.h"
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace VeroScript::Http::Detail
{
	class FClientCreationError final : public std::runtime_error
	{
	public:
		explicit FClientCreationError(Network::EError Value)
			: std::runtime_error(Value == Network::EError::Closed ? "Context closed" :
				Value == Network::EError::InvalidArgument ? "Invalid HTTP client options" :
				Value == Network::EError::Unsupported ? "HTTP client backend is unavailable" :
				"Context connection limit exceeded"), Code(Value)
		{
		}
		Network::EError Code;
	};
	struct FHttpStreamState
	{
		std::shared_ptr<const void> Retention;
		std::mutex Mutex;
		std::optional<FResponse> Head;
	};
	struct FServerResponseState
	{
		std::atomic<EResponseState> State{EResponseState::Uncommitted};
		std::atomic<std::uint64_t> Bytes{0};
	};
}

namespace VeroScript::Http::Backend
{
	inline constexpr const char* ClientServiceId = "http-client";
	inline constexpr const char* ServerServiceId = "http-server";
	inline constexpr const char* ClientContractId = "veroscript.http.client";
	inline constexpr const char* ServerContractId = "veroscript.http.server";
	inline constexpr const char* ContractVersion = "0.1.1";
	class IClient
	{
	public:
		virtual ~IClient() = default;
		virtual Network::FOperation Send(FRequest) = 0;
		virtual FStreamingOperation SendStream(FRequest, std::uint64_t BufferBytes) = 0;
		virtual FCapabilities Capabilities() const = 0;
		virtual bool BelongsTo(const Network::FContext&) const noexcept = 0;
		virtual Network::FOperation Cookies(Network::FOperationOptions) = 0;
		virtual Network::FOperation ClearCookies(Network::FOperationOptions) = 0;
		virtual Network::FOperation ImportCookies(FCookies, Network::FOperationOptions) = 0;
		virtual void RequestClose() = 0;
		virtual void Dispose() noexcept = 0;
	};
	class IExchange
	{
	public:
		virtual ~IExchange() = default;
		virtual const FIncomingRequest& Request() const noexcept = 0;
		virtual std::shared_ptr<Network::FUpgradeSource> PrepareUpgrade(std::shared_ptr<FExchange> Owner, Network::FError&) = 0;
		virtual Network::FOperation Reply(FResponse, Network::FOperationOptions) = 0;
		virtual FResponseOperation ReplyStream(FResponse, FBodySource, Network::FOperationOptions) = 0;
		// Server queue hooks run on the owning I/O executor.
		virtual void MarkDelivered() noexcept = 0;
		virtual void Discard() = 0;
		virtual void Dispose() noexcept = 0;
	};
	class IServer
	{
	public:
		virtual ~IServer() = default;
		virtual std::uint16_t Port() const noexcept = 0;
		virtual Network::FOperation Accept(Network::FOperationOptions) = 0;
		virtual Network::FOperation Shutdown(std::chrono::milliseconds Grace, Network::FOperationOptions) = 0;
		virtual void RequestClose() = 0;
		virtual void Dispose() noexcept = 0;
	};
	class IClientService
	{
	public:
		virtual ~IClientService() = default;
		virtual const char* GetBackendId() const noexcept = 0;
		virtual const char* GetFoundationIdentity() const noexcept = 0;
		virtual std::shared_ptr<IClient> Create(Network::FContext&, FClientOptions) = 0;
	};
	using FExchangeFactory = std::shared_ptr<FExchange> (*)(
		const std::shared_ptr<Network::Detail::FContext>&, std::shared_ptr<IExchange>);
	struct FAccess
	{
		static std::shared_ptr<FServer> Wrap(
			const std::shared_ptr<Network::Detail::FContext>&, std::shared_ptr<IServer>);
		// Captured in the HTTP frontend to preserve enable_shared_from_this.
		static std::shared_ptr<FExchange> Wrap(
			const std::shared_ptr<Network::Detail::FContext>&, std::shared_ptr<IExchange>);
		static IExchange& Implementation(FExchange&);
		static FStreamingOperation Stream(Network::FOperation, std::shared_ptr<Network::FReadableStream>,
			std::shared_ptr<Detail::FHttpStreamState>);
		static FResponseOperation Response(Network::FOperation, std::shared_ptr<Detail::FServerResponseState>);
	};

	class IServerService
	{
	public:
		virtual ~IServerService() = default;
		virtual const char* GetBackendId() const noexcept = 0;
		virtual const char* GetFoundationIdentity() const noexcept = 0;
		virtual std::shared_ptr<FServer> Listen(Network::FContext&, std::string Address, std::uint16_t Port,
			Network::FError&, FServerOptions, FExchangeFactory) = 0;
	};
} // namespace VeroScript::Http::Backend
