// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetworkBackend.h"
#include "VeroScriptWebSocket.h"

namespace VeroScript::WebSocket::Backend
{
	inline constexpr const char* ClientServiceId = "websocket-client";
	inline constexpr const char* ServerServiceId = "websocket-server";
	inline constexpr const char* ContractId = "veroscript.websocket";
	inline constexpr const char* ContractVersion = "0.1.1";
	class IConnection
	{
	public:
		virtual ~IConnection() = default;
		virtual Network::FOperation Submit(FMessage, Network::FOperationOptions, bool Fragment, bool Ping) = 0;
		virtual Network::FOperation Ping(std::string, Network::FOperationOptions) = 0;
		virtual bool TryTakeControl(FControlEvent&) = 0;
		virtual Network::FOperation Receive(Network::FOperationOptions) = 0;
		virtual Network::FOperation Close(std::uint16_t, std::string, Network::FOperationOptions) = 0;
		virtual FCloseInfo CloseInfo() const = 0;
		virtual std::string Subprotocol() const = 0;
		virtual FCompressionInfo CompressionInfo() const = 0;
		virtual void Dispose() noexcept = 0;
	};

	class IServer
	{
	public:
		virtual ~IServer() = default;
		virtual std::uint16_t Port() const noexcept = 0;
		virtual Network::FOperation Accept(Network::FOperationOptions) = 0;
		virtual void RequestClose() = 0;
		virtual void Dispose() noexcept = 0;
	};

	struct FAccess
	{
		static std::shared_ptr<FConnection> Wrap(
			const std::shared_ptr<Network::Detail::FContext>&, std::shared_ptr<IConnection>);
		static std::shared_ptr<FServer> Wrap(
			const std::shared_ptr<Network::Detail::FContext>&, std::shared_ptr<IServer>);
	};

	class IService
	{
	public:
		virtual ~IService() = default;
		virtual const char* GetBackendId() const noexcept = 0;
		virtual const char* GetFoundationIdentity() const noexcept = 0;
		virtual bool CompressionSupported() const noexcept = 0;
		virtual Network::FOperation Connect(const std::shared_ptr<Network::Detail::FContext>&, std::string Url, FOptions) = 0;
		virtual Network::FOperation AcceptUpgrade(Network::FContext&, std::shared_ptr<Network::FUpgradeSource>, FOptions) = 0;
		virtual std::shared_ptr<FServer> Listen(Network::FContext&, std::string Address, std::uint16_t Port,
			Network::FError&, FOptions) = 0;
	};
} // namespace VeroScript::WebSocket::Backend
