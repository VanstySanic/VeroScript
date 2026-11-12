// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"
#include "VeroScriptNetworkTls.h"
#include "VeroScriptNetworkUpgrade.h"

namespace VeroScript::WebSocket
{
	namespace Backend
	{
		class IService;
		class IConnection;
		class IServer;
		struct FAccess;
	}
	namespace Detail
	{
		struct FConnectionAccess;
	}
	enum class EControlType : std::uint8_t
	{
		Ping,
		Pong
	};

	struct FMessage
	{
		Network::FBytes Bytes;
		bool bText = false;
		bool bFinal = true;
		// Receive chunks mark both ends; bFinal alone need not be a whole message.
		bool bFirst = true;
	};

	struct FControlEvent
	{
		EControlType Type = EControlType::Ping;
		std::string Payload;
	};

	struct FCompressionOptions
	{
		// The native compressor supports 9..15. Values describe client/server directions, not local/remote.
		std::int32_t ClientMaxWindowBits = 15, ServerMaxWindowBits = 15;
		bool bClientNoContextTakeover = false, bServerNoContextTakeover = false;
		std::int32_t Level = 8, MemoryLevel = 4;
	};

	struct FCompressionInfo
	{
		bool bEnabled = false;
		std::uint32_t ClientWindowBits = 0, ServerWindowBits = 0;
		bool bClientNoContextTakeover = false, bServerNoContextTakeover = false;
	};
	bool CompressionSupported(const Backend::IService& Service) noexcept;

	struct FOptions
	{
		std::shared_ptr<Backend::IService> Service;
		std::string BackendId;
		std::uint64_t MaxMessageBytes = 16 * 1024 * 1024;
		std::uint64_t MaxQueuedBytes = 32 * 1024 * 1024;
		std::uint32_t MaxQueuedMessages = 128;
		bool bCompression = false;
		FCompressionOptions Compression;
		Network::FOperationOptions Operation;
		Network::FTlsOptions Tls;
		// Fragment mode returns bounded byte chunks with bFinal marking message completion.
		bool bReceiveFragments = false;
		std::optional<std::chrono::milliseconds> IdleTimeout = std::chrono::seconds(60);
		bool bKeepAlivePings = true;
		// Client offer order; server preference order. Tokens are case sensitive.
		std::vector<std::string> Subprotocols;
		bool bRequireSubprotocol = false;
		// Explicit HTTP CONNECT proxy. Empty disables proxying; process environment is never read.
		std::string ProxyUrl;
		std::string ProxyAuthorization;
	};

	struct FCloseInfo
	{
		std::uint16_t Code = 1006;
		std::string Reason;
		bool bClean = false;
		bool bClosed = false;
	};

	class FServer;
	class FConnection
	{
		friend class FServer;
		friend struct Detail::FConnectionAccess;

	public:
		static Network::FOperation Connect(Network::FContext& Context, std::string Url, FOptions Options = {});
		static Network::FOperation AcceptUpgrade(
			Network::FContext& Context, std::shared_ptr<Network::FUpgradeSource> Source, FOptions Options = {});
		~FConnection();
		Network::FOperation Send(FMessage Message, Network::FOperationOptions Options = {});
		Network::FOperation SendFragment(FMessage Fragment, Network::FOperationOptions Options = {});
		Network::FOperation Ping(std::string Payload = {}, Network::FOperationOptions Options = {});
		// Nonblocking owner-independent snapshot; native I/O keeps processing control frames.
		bool TryTakeControl(FControlEvent& Out);
		Network::FOperation Receive(Network::FOperationOptions Options = {});
		Network::FOperation Close(std::uint16_t Code = 1000, std::string Reason = {}, Network::FOperationOptions Options = {});
		FCloseInfo CloseInfo() const;
		std::string Subprotocol() const;
		FCompressionInfo CompressionInfo() const;

	private:
		friend struct Backend::FAccess;
		Network::FOperation Submit(FMessage Message, Network::FOperationOptions Options, bool Fragment, bool Ping);
		explicit FConnection(std::shared_ptr<Backend::IConnection> Value);
		std::shared_ptr<Backend::IConnection> Impl;
	};

	class FServer
	{
	public:
		static std::shared_ptr<FServer> Listen(
			Network::FContext& Context, std::string Address, std::uint16_t Port, Network::FError& Error, FOptions Options = {});
		~FServer();
		std::uint16_t Port() const noexcept;
		Network::FOperation Accept(Network::FOperationOptions Options = {});
		void Close();

	private:
		friend struct Backend::FAccess;
		explicit FServer(std::shared_ptr<Backend::IServer> Value);
		std::shared_ptr<Backend::IServer> Impl;
	};
} // namespace VeroScript::WebSocket
