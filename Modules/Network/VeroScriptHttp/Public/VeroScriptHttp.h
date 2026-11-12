// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"
#include "VeroScriptNetworkStream.h"
#include "VeroScriptNetworkTls.h"
#include "VeroScriptNetworkUpgrade.h"
#include "VeroScriptHttpClientOptions.h"
#include <utility>

namespace VeroScript::Http
{
	namespace Backend
	{
		class IClientService;
		class IServerService;
		class IClient;
		class IServer;
		class IExchange;
		struct FAccess;
	}
	struct FCapabilities
	{
		bool bTls = false;
		bool bHttp2 = false;
		std::string Backend;
	};

	struct FServerOptions
	{
		std::shared_ptr<Backend::IServerService> Service;
		std::string BackendId;
		std::uint64_t MaxBodyBytes = 16 * 1024 * 1024;
		std::uint64_t MaxStreamBodyBytes = 64 * 1024 * 1024;
		// Deliver request headers before the body, with a single-consumer bounded body pipe.
		bool bStreamRequests = false;
		std::uint64_t RequestStreamBufferBytes = 64 * 1024;
		std::uint32_t MaxHeaderBytes = 64 * 1024;
		std::uint32_t MaxPendingRequests = 128;
		std::chrono::milliseconds ResponseTimeout{30000};
		std::chrono::milliseconds HandshakeTimeout{10000};
		Network::FTlsOptions Tls;
	};

	struct FBodySource
	{
		std::shared_ptr<Network::FReadableStream> Stream;
		std::optional<std::uint64_t> Length;
	};

	// Request/response records below embed FHeaders and need its complete definition.
	class FHeaders
	{
	public:
		bool TryAdd(std::string Name, std::string Value, Network::FError& Error);
		bool TrySet(std::string Name, std::string Value, Network::FError& Error);
		void Erase(std::string_view Name);
		std::vector<std::string> GetAll(std::string_view Name) const;
		const std::vector<std::pair<std::string, std::string>>& Fields() const noexcept;

	private:
		std::vector<std::pair<std::string, std::string>> Values;
	};
	// Called on the native I/O executor; it must return promptly and must not execute script.
	// Each call must return a fresh source, including rewinds during a redirect.
	using FUploadFactory = std::function<FBodySource(std::uint32_t Attempt, Network::FError& Error)>;

	// Choose buffered Body, a single-use Upload, or a replayable UploadFactory.
	struct FMultipartPart
	{
		std::string Name;
		std::optional<std::string> Filename;
		std::string ContentType;
		Network::FBytes Body;
		// Known length limits the uploaded range; unused source bytes are cancelled at completion.
		std::shared_ptr<Network::FReadableStream> Upload;
		FUploadFactory UploadFactory;
		std::optional<std::uint64_t> UploadLength;
	};

	struct FRequest
	{
		std::string Url;
		std::string Method = "GET";
		FHeaders Headers;
		Network::FBytes Body;
		// A single-use, nonblocking source. Native/script producers supply the paired writer.
		std::shared_ptr<Network::FReadableStream> Upload;
		FUploadFactory UploadFactory;
		std::optional<std::uint64_t> UploadLength;
		std::uint64_t MaxUploadBytes = 64 * 1024 * 1024;
		// Distinct from a raw body, including an explicitly empty multipart body.
		std::optional<std::vector<FMultipartPart>> Multipart;
		Network::FOperationOptions Operation;
		FRetryOptions Retry;
		// Optional diagnostics, including failures/cancellation. Reuse after completion starts a new history.
		std::shared_ptr<FAttemptHistory> History;
		std::chrono::milliseconds ConnectTimeout{10000};
		std::uint64_t MaxHeaderBytes = 64 * 1024;
		std::uint64_t MaxBodyBytes = 16 * 1024 * 1024;
		// Logical totals across all attempts/redirects. Bodies count decoded/source bytes, excluding framing.
		std::uint64_t MaxTotalBodyBytes = 64 * 1024 * 1024;
		std::uint64_t MaxTotalHeaderBytes = 1024 * 1024;
		std::uint64_t MaxTotalUploadBytes = 256 * 1024 * 1024;
		bool bFollowRedirects = false;
		std::uint32_t MaxRedirects = 10;
		bool bRequireHttp2 = false;
		bool bDecodeContent = true;
		// Empty disables environment proxy inheritance.
		std::string Proxy;
		// Empty uses the backend's normal trust store; otherwise a caller-supplied PEM bundle.
		std::string CaFile;
	};

	struct FResponse
	{
		std::uint32_t Status = 0;
		std::uint32_t ProtocolMajor = 0;
		FHeaders Headers;
		FHeaders Trailers;
		Network::FBytes Body;
		std::string EffectiveUrl;
		bool bComplete = false;
		// Accepted body bytes, including streamed bytes absent from Body.
		std::uint64_t BodyBytes = 0;
		std::vector<FAttempt> Attempts;
		// Current attempt details. Early streaming heads contain a non-final snapshot.
		std::shared_ptr<const FAttemptDetails> Details;
	};

	namespace Detail
	{
		struct FHttpStreamState;
	}
	class FClient;
	class FStreamingOperation
	{
		friend class FClient;

	public:
		const Network::FOperation& Operation() const noexcept;
		std::shared_ptr<Network::FReadableStream> Body() const noexcept;
		// False with no error means pending. A published head is an immutable snapshot.
		bool TryHead(FResponse& Out, Network::FError& Error) const;

	private:
		friend struct Backend::FAccess;
		Network::FOperation Op;
		std::shared_ptr<Network::FReadableStream> Reader;
		std::shared_ptr<Detail::FHttpStreamState> State;
	};

	struct FRequestBodyResult
	{
		FHeaders Trailers;
		std::uint64_t BodyBytes = 0;
	};

	struct FIncomingRequest
	{
		std::string Method;
		std::string Target;
		FHeaders Headers;
		Network::FBytes Body;
		// Present only with bStreamRequests. Final trailers/count are in BodyOperation's result.
		std::shared_ptr<Network::FReadableStream> BodyStream;
		Network::FOperation BodyOperation;
		// HTTP/1.x wire version: 10 or 11, independent of the script module version.
		std::uint32_t ProtocolVersion = 11;
	};

	class FClient
	{
	public:
		FClient(Network::FContext& Context, std::shared_ptr<Backend::IClientService> Service, FClientOptions Options = {});
		~FClient();
		FClient(const FClient&) = delete;
		FClient& operator=(const FClient&) = delete;
		Network::FOperation Send(FRequest Request);
		// MaxBodyBytes limits total bytes; BufferBytes limits currently queued bytes.
		// Drain Body while the transfer runs. Waiting without reading may reach its deadline.
		FStreamingOperation SendStream(FRequest Request, std::uint64_t BufferBytes = 256 * 1024);
		FCapabilities Capabilities() const;
		bool BelongsTo(const Network::FContext& Context) const noexcept;
		Network::FOperation Cookies(Network::FOperationOptions Options = {});
		Network::FOperation ClearCookies(Network::FOperationOptions Options = {});
		Network::FOperation ImportCookies(FCookies Cookies, Network::FOperationOptions Options = {});
		void Close();

	private:
		friend struct Backend::FAccess;
		std::shared_ptr<const void> Lifetime;
		std::shared_ptr<Backend::IClient> Impl;
	};

	enum class EResponseState
	{
		Uncommitted,
		HeadersSent,
		BodyStreaming,
		Finished,
		Aborted
	};
	namespace Detail
	{
		struct FServerResponseState;
	}
	class FResponseOperation
	{
		friend class FExchange;

	public:
		const Network::FOperation& Operation() const noexcept;
		EResponseState State() const noexcept;
		std::uint64_t BodyBytes() const noexcept;

	private:
		friend struct Backend::FAccess;
		Network::FOperation Op;
		std::shared_ptr<Detail::FServerResponseState> Progress;
	};

	class FServer;
	class FExchange : public std::enable_shared_from_this<FExchange>
	{
		friend class FServer;

	public:
		~FExchange();
		const FIncomingRequest& Request() const noexcept;
		// Keeps this exchange alive; the transport remains HTTP-owned until a compatible consumer takes it.
		std::shared_ptr<Network::FUpgradeSource> PrepareUpgrade(Network::FError& Error);
		Network::FOperation Reply(FResponse Response, Network::FOperationOptions Options = {});
		// A reader is consumed once. With no reader, Response.Body is the owned source.
		// Headers are snapshotted at submission; trailers are fixed before commitment.
		FResponseOperation ReplyStream(
			FResponse Response, FBodySource Source = {}, Network::FOperationOptions Options = {});

	private:
		friend struct Backend::FAccess;
		explicit FExchange(std::shared_ptr<Backend::IExchange> Value);
		std::shared_ptr<Backend::IExchange> Impl;
	};

	struct FShutdownResult
	{
		bool bForced = false;
		std::uint64_t ClosedConnections = 0;
	};

	class FServer
	{
	public:
		static std::shared_ptr<FServer> Listen(Network::FContext& Context, std::string Address, std::uint16_t Port,
			Network::FError& Error, FServerOptions Options = {});
		~FServer();
		std::uint16_t Port() const noexcept;
		Network::FOperation Accept(Network::FOperationOptions Options = {});
		// Stops admission, cancels queued accepts and grants delivered exchanges a bounded grace period.
		// Success reports logical closure; native completion handlers may still be reclaiming storage.
		Network::FOperation Shutdown(
			std::chrono::milliseconds Grace = std::chrono::seconds(5), Network::FOperationOptions Options = {});
		void Close();

	private:
		friend struct Backend::FAccess;
		explicit FServer(std::shared_ptr<Backend::IServer> Value);
		std::shared_ptr<Backend::IServer> Impl;
	};
} // namespace VeroScript::Http
