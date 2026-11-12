// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpBackend.h"
#include "BoostNetworkBackend.h"
#include "NetworkNativeStream.h"
#include "NetworkStream.h"
#include "BoostUpgrade.h"
#include "HttpQuotaBody.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <sstream>

namespace VeroScript::Http
{
	namespace N = Network::Detail;
	namespace Beast = boost::beast;
	namespace Wire = Beast::http;

	namespace Detail
	{

	} // namespace Detail

	struct FBoostHttpConnection : std::enable_shared_from_this<FBoostHttpConnection>
	{
		FBoostHttpConnection(std::shared_ptr<N::FContext> C, N::FTcp::socket Socket, FServerOptions O,
		    std::shared_ptr<N::FNativeTlsContext> Tls = {})
		    : Context(std::move(C)), Stream(std::move(Socket), std::move(Tls)), Timer(N::Io(Context)),
		      Options(std::move(O)), Buffer(Options.MaxHeaderBytes + 8192)
		{
		}
		~FBoostHttpConnection()
		{
			N::RemoveResource(Context, ResourceId);
		}
		#include "HttpServerRequestStream.inl"
		#include "HttpServerResponseStream.inl"

		void Close()
		{
			if (bClosed)
			{
				return;
			}
			bClosed = true;
			if (ActiveRequest)
			{
				ActiveRequest->Abort(Network::EError::Closed, "HTTP exchange closed");
			}
			if (ActiveResponse)
			{
				ActiveResponse->Abort(Network::EError::Closed, "HTTP exchange closed");
			}
			N::RemoveResource(Context, ResourceId);
			Timer.cancel();
			Stream.Close();
			if (OnClosed)
			{
				OnClosed();
			}
		}
		void ArmTimeout()
		{
			Timer.expires_after(Options.ResponseTimeout);
			Timer.async_wait(
			    [Weak = weak_from_this()](N::FSystemError Error)
			{
				if (!Error)
				{
					if (auto P = Weak.lock())
					{
						if (P->ActiveRequest)
						{
							N::Fail(P->ActiveRequest->Op, Network::EError::TimedOut, "HTTP request idle timeout");
						}
						if (P->ActiveResponse)
						{
							N::Fail(P->ActiveResponse->Op, Network::EError::TimedOut, "HTTP response idle timeout");
						}
						P->Close();
					}
				}
			});
		}
		template <class Request>
		void ContinueRequest(const Request& Head, bool Done, std::function<void()> Next)
		{
			bool Expected = false, Valid = true;
			if (Head.version() >= 11)
			{
				for (const auto& Field : Head.base())
				{
					if (Field.name() != Wire::field::expect)
					{
						continue;
					}
					Expected = true;
					auto Value = Field.value();
					for (;;)
					{
						const auto Comma = Value.find(',');
						auto Token = Value.substr(0, Comma);
						while (!Token.empty() && (Token.front() == ' ' || Token.front() == '\t'))
						{
							Token.remove_prefix(1);
						}
						while (!Token.empty() && (Token.back() == ' ' || Token.back() == '\t'))
						{
							Token.remove_suffix(1);
						}
						Valid = Valid && Beast::iequals(Token, "100-continue");
						if (Comma == Value.npos)
						{
							break;
						}
						Value.remove_prefix(Comma + 1);
					}
				}
			}
			if (!Valid)
			{
				auto Response = std::make_shared<Wire::response<Wire::empty_body>>(Wire::status::expectation_failed, Head.version());
				Response->content_length(0);
				Response->keep_alive(false);
				Wire::async_write(Stream, *Response,
				    [Self = shared_from_this(), Response](N::FSystemError, std::size_t)
				{
					Self->Close();
				});
				return;
			}
			if (!Expected || Done)
			{
				Next();
				return;
			}
			auto Response = std::make_shared<Wire::response<Wire::empty_body>>(Wire::status::continue_, Head.version());
			Wire::async_write(Stream, *Response,
			    [Self = shared_from_this(), Response, Next = std::move(Next)](N::FSystemError Error, std::size_t)
			{
				if (Error)
				{
					Self->Close();
				}
				else if (!Self->bClosed)
				{
					Next();
				}
			});
		}
		void Read()
		{
			if (bClosed)
			{
				return;
			}
			if (bStopAfterResponse)
			{
				Close();
				return;
			}
			bResponseFinished = false;
			bExchangeDelivered = false;
			if (Options.bStreamRequests)
			{
				ReadStream();
				return;
			}
			Network::FError ReserveError;
			if (!TransportMemory)
			{
				TransportMemory = N::ReserveBytes(Context, Options.MaxHeaderBytes + 8192, ReserveError);
			}
			auto BodyMemory = N::ReserveBytes(Context, 0, ReserveError);
			if (!TransportMemory || !BodyMemory)
			{
				Close();
				return;
			}
			Parser.emplace();
			Parser->get().body().Retention = std::move(BodyMemory);
			Parser->get().body().Limit = Options.MaxBodyBytes;
			Parser->body_limit(Options.MaxBodyBytes);
			Parser->header_limit(Options.MaxHeaderBytes);
			ArmTimeout();
			Wire::async_read_header(Stream, Buffer, *Parser,
			    [Self = shared_from_this()](N::FSystemError Error, std::size_t)
			{
				if (Self->bClosed) return;
				if (Error)
				{
					Self->Close();
					return;
				}
				Self->ContinueRequest(Self->Parser->get(), Self->Parser->is_done(),
				    [Self]
				{
					Self->ReadBufferedBody();
				});
			});
		}
		void ReadBufferedBody()
		{
			Wire::async_read(Stream,
			    Buffer,
			    *Parser,
			    [Self = shared_from_this()](N::FSystemError Error, std::size_t)
			{
				if (Self->bClosed)
				{
					return;
				}
				if (Error)
				{
					Self->Close();
					return;
				}
				auto& Request = Self->Parser->get();
				std::uint64_t HeaderBytes = Request.method_string().size() + Request.target().size();
				for (const auto& Header : Request.base())
				{
					HeaderBytes += Header.name_string().size() + Header.value().size() + 4;
				}
				Network::FError ReserveError;
				auto HeaderMemory = N::ReserveBytes(Self->Context, HeaderBytes, ReserveError);
				if (!HeaderMemory)
				{
					Self->Close();
					return;
				}
				struct FRetention
				{
					std::shared_ptr<const void> Body, Headers;
				};
				Self->IncomingMemory =
				    std::make_shared<FRetention>(FRetention{Request.body().Retention, std::move(HeaderMemory)});
				Self->Incoming = {};
				Self->Incoming.Method = std::string(Request.method_string());
				Self->Incoming.Target = std::string(Request.target());
				Self->Incoming.ProtocolVersion = Request.version();
				Network::FError HeaderError;
				for (const auto& Header : Request.base())
				{
					if (!Self->Incoming.Headers.TryAdd(
					        std::string(Header.name_string()), std::string(Header.value()), HeaderError))
					{
						Self->Close();
						return;
					}
				}
				Self->Incoming.Body = std::move(Request.body().Bytes);
				++Self->Generation;
				Self->bReplied = false;
				Self->ArmTimeout();
				Self->OnRequest(Self);
			});
		}

		void Reply(FResponse Response, const std::shared_ptr<N::FOperation>& Op, std::uint64_t RequestGeneration,
		    std::shared_ptr<N::FByteLease> Retention)
		{
			if (N::Terminal(N::State(Op)))
			{
				return;
			}
			if (bClosed || bReplied || !RequestReady() || Generation != RequestGeneration)
			{
				N::Fail(Op, Network::EError::State, "HTTP exchange closed or already replied");
				return;
			}
			if (Response.Status < 200 || Response.Status > 599 || Response.Body.size() > Options.MaxBodyBytes)
			{
				N::Fail(Op, Network::EError::InvalidArgument, "Invalid HTTP response status or body size");
				return;
			}
			if (!Response.Trailers.Fields().empty())
			{
				StartResponseStream(std::move(Response),
				    {},
				    Op,
				    RequestGeneration,
				    std::move(Retention),
				    std::make_shared<Detail::FServerResponseState>());
				return;
			}
			auto Message = std::make_shared<Wire::response<Wire::vector_body<std::uint8_t>>>(
			    Wire::status(Response.Status), RequestVersion());
			std::uint64_t HeaderBytes = 0;
			for (const auto& Header : Response.Headers.Fields())
			{
				HeaderBytes += Header.first.size() + Header.second.size() + 4;
				if (HeaderBytes > Options.MaxHeaderBytes)
				{
					N::Fail(Op, Network::EError::Limit, "Response header limit exceeded");
					return;
				}
				Message->insert(Header.first, Header.second);
			}
			bool bHead = RequestIsHead();
			bool bBodyAllowed = Response.Status != 204 && Response.Status != 304;
			if (bBodyAllowed && !bHead)
			{
				Message->body() = std::move(Response.Body);
			}
			Message->erase(Wire::field::transfer_encoding);
			Message->content_length(bHead && bBodyAllowed ? Response.Body.size() : Message->body().size());
			if (Response.Status == 204 || Response.Status == 304)
			{
				Message->erase(Wire::field::content_length);
			}
			Message->keep_alive(RequestKeepAlive());
			bReplied = true;
			Timer.cancel();
			N::SetAbort(Op,
			    [Weak = weak_from_this(), RequestGeneration]
			{
				if (auto Self = Weak.lock())
				{
					if (Self->Generation == RequestGeneration)
					{
						Self->Close();
					}
				}
			});
			if (N::Terminal(N::State(Op)))
			{
				return;
			}
			N::Running(Op);
			ArmTimeout();
			Wire::async_write(Stream,
			    *Message,
			    [Self = shared_from_this(), Retention, Message, Op](N::FSystemError Error, std::size_t Count)
			{
				if (Error)
				{
					N::Fail(Op, Network::EError::Write, Error);
					Self->Close();
					return;
				}
				if (!N::Complete(Op, static_cast<std::uint64_t>(Count)))
				{
					Self->Close();
					return;
				}
				Self->ResponseFinished(Message->keep_alive());
			});
		}

		void Upgrade(const std::shared_ptr<N::FUpgradeSourceState>& Source,
		    std::shared_ptr<N::FUpgradeConsumer> Consumer, const std::shared_ptr<N::FOperation>& Op,
		    std::uint64_t RequestGeneration)
		{
			if (N::Terminal(N::State(Op)))
			{
				return;
			}
			if (bClosed || bReplied || Generation != RequestGeneration || !RequestReady() || RequestVersion() != 11 ||
			    ActiveResponse || ActiveRequest || (Options.bStreamRequests && !StreamParser->is_done()))
			{
				N::Fail(Op, Network::EError::State, "HTTP exchange is not available for protocol upgrade");
				return;
			}
			auto State = std::make_shared<N::FUpgradeLeaseState>();
			State->Context = Context;
			State->Memory = N::ReserveBytes(Context, sizeof(N::FUpgradeLeaseState) + Buffer.size() + 256, Op);
			if (!State->Memory)
			{
				return;
			}
			State->PreRead.resize(Buffer.size());
			N::Asio::buffer_copy(N::Asio::buffer(State->PreRead), Buffer.data());
			auto Lease = std::make_unique<N::FUpgradeLease>(State);
			std::function<void()> Closer = [State]
			{
				State->Close();
			};
			if (N::Terminal(N::State(Op)))
			{
				return;
			}
			// All allocations precede the transport move. The existing resource slot follows the owner.
			Timer.cancel();
			State->Stream.emplace(std::move(Stream));
			bReplied = true;
			bClosed = true;
			State->ResourceId = std::exchange(ResourceId, 0);
			Source->Consumed = true;
			Buffer.consume(Buffer.size());
			if (!N::ReplaceResource(Context, State->ResourceId, std::move(Closer)))
			{
				State->Close();
				N::Fail(Op, Network::EError::Closed, "Context closed during protocol upgrade");
				if (OnClosed)
				{
					OnClosed();
				}
				return;
			}
			if (OnClosed)
			{
				OnClosed();
			}
			if (N::Terminal(N::State(Op)))
			{
				return;
			}
			N::Running(Op);
			Consumer->Take(std::move(Lease), Op);
		}
		std::shared_ptr<N::FContext> Context;
		N::FNativeStream Stream;
		N::Asio::steady_timer Timer;
		FServerOptions Options;
		std::shared_ptr<N::FByteLease> TransportMemory;
		std::shared_ptr<FReplyFlow> ActiveResponse;
		std::shared_ptr<FRequestFlow> ActiveRequest;
		std::optional<Wire::request_parser<Wire::buffer_body>> StreamParser;
		bool bResponseFinished = false, bRequestKeepAlive = false;
		Beast::flat_buffer Buffer;
		std::optional<Wire::request_parser<Detail::FQuotaBody>> Parser;
		std::shared_ptr<const void> IncomingMemory;
		FIncomingRequest Incoming;
		std::function<void(std::shared_ptr<FBoostHttpConnection>)> OnRequest;
		std::function<void()> OnClosed;
		bool bStopAfterResponse = false;
		bool bExchangeDelivered = false;
		std::uint64_t ResourceId = 0;
		std::uint64_t Generation = 0;
		bool bClosed = false, bReplied = false;
	};

	struct FBoostHttpExchange : Backend::IExchange, std::enable_shared_from_this<FBoostHttpExchange>
	{
		explicit FBoostHttpExchange(std::shared_ptr<FBoostHttpConnection> Value)
		    : Impl(std::move(Value)), SnapshotRetention(std::move(Impl->IncomingMemory)),
		      Snapshot(std::move(Impl->Incoming)), Generation(Impl->Generation)
		{
		}
		void MarkDelivered() noexcept override
		{
			Impl->bExchangeDelivered = true;
		}
		void Discard() override
		{
			Impl->Close();
		}
		std::shared_ptr<FBoostHttpConnection> Impl;
		std::shared_ptr<const void> SnapshotRetention;
		FIncomingRequest Snapshot;
		std::uint64_t Generation = 0;
		void Dispose() noexcept override;
		const FIncomingRequest& Request() const noexcept override;
		std::shared_ptr<Network::FUpgradeSource> PrepareUpgrade(std::shared_ptr<FExchange> Owner, Network::FError& Error) override;
		Network::FOperation Reply(FResponse Response, Network::FOperationOptions Options) override;
		FResponseOperation ReplyStream(FResponse Response, FBodySource Source, Network::FOperationOptions Options) override;
	};

	void FBoostHttpExchange::Dispose() noexcept
	{
		N::OnDestroy(Impl->Context,
		    [this]
		{
			auto P = Impl;
			N::Post(P->Context,
			    [P, G = Generation]
			{
				if (!P->bReplied && G == P->Generation)
				{
					P->Close();
				}
			});
		});
	}

	const FIncomingRequest& FBoostHttpExchange::Request() const noexcept
	{
		return Snapshot;
	}
	std::shared_ptr<Network::FUpgradeSource> FBoostHttpExchange::PrepareUpgrade(std::shared_ptr<FExchange> Owner, Network::FError& Error)
	{
		Error = {};
		try
		{
			auto Keep = weak_from_this().lock();
			if (!Keep || !Owner)
			{
				Error = {Network::EError::State, "Upgrade requires a shared exchange handle", 0};
				return {};
			}
			const auto Lengths = Snapshot.Headers.GetAll("Content-Length");
			const bool EmptyLength = Lengths.empty() || (Lengths.size() == 1 && !Lengths[0].empty() && std::all_of(Lengths[0].begin(), Lengths[0].end(), [](char C)
			{
				return C == '0';
			}));
			if (Snapshot.ProtocolVersion != 11 || !Snapshot.Body.empty() ||
			    !Snapshot.Headers.GetAll("Transfer-Encoding").empty() || !EmptyLength)
			{
				Error = {Network::EError::InvalidArgument, "Upgrade requires HTTP/1.1 without a request body", 0};
				return {};
			}
			Wire::request<Wire::empty_body> Request;
			Request.method_string(Snapshot.Method);
			Request.target(Snapshot.Target);
			Request.version(Snapshot.ProtocolVersion);
			for (const auto& H : Snapshot.Headers.Fields())
			{
				Request.insert(H.first, H.second);
			}
			std::ostringstream Text;
			Text << Request;
			Network::FUpgradeInfo Info;
			Info.Protocol = "websocket";
			Info.BackendIdentity = std::string(N::UpgradeBackend);
			Info.RequestHead = Text.str();
			Info.RequestGeneration = Generation;
			if (Info.RequestHead.size() > N::Options(Impl->Context).MaxBufferBytes)
			{
				Error = {Network::EError::Limit, "Upgrade request headers exceed the buffer limit", 0};
				return {};
			}
			return N::FUpgradeAccess::Make(
			    Impl->Context,
			    std::move(Info),
			    [Keep = std::move(Keep), Owner = std::move(Owner)](const auto& S, auto Consumer, const auto& Op)
			{
				(void)Owner; // Retain the facade until transfer completes or the source is released.
				Keep->Impl->Upgrade(S, std::move(Consumer), Op, Keep->Generation);
			},
			    Error);
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = Network::EError::Allocation;
			Error.Message.clear();
		}
		catch (...)
		{
			Error.Code = Network::EError::Internal;
			Error.Message.clear();
		}
		return {};
	}

	Network::FOperation FBoostHttpExchange::Reply(FResponse Response, Network::FOperationOptions Options)
	{
		auto P = Impl;
		auto Op = N::Begin(P->Context, Options);
		if (!N::Terminal(N::State(Op)))
		{
			auto Retention = N::ReserveBytes(P->Context, Response.Body.capacity(), Op);
			if (!Retention)
			{
				return N::FAccess::Wrap(Op);
			}
			for (const auto* Fields : {&Response.Headers, &Response.Trailers})
			{
				for (const auto& Header : Fields->Fields())
				{
					for (const auto Bytes :
					    {Header.first.capacity(), Header.second.capacity(), Header.first.size(), Header.second.size()})
					{
						if (Bytes > N::Options(P->Context).MaxRetainedBytes -
						            std::min(Retention->Size(), N::Options(P->Context).MaxRetainedBytes) ||
						    !Retention->TryResize(Retention->Size() + Bytes))
						{
							N::Fail(Op, Network::EError::Limit, "Context payload byte limit exceeded");
							return N::FAccess::Wrap(Op);
						}
					}
				}
			}
			N::Post(P->Context,
			    [P, Op, G = Generation, Retention, Response = std::move(Response)]() mutable
			{
				P->Reply(std::move(Response), Op, G, std::move(Retention));
			});
		}
		return N::FAccess::Wrap(Op);
	}

	FResponseOperation FBoostHttpExchange::ReplyStream(FResponse Response, FBodySource Source, Network::FOperationOptions Options)
	{
		auto P = Impl;
		FResponseOperation Result;
		auto Op = N::Begin(P->Context, Options);
		const auto Progress = std::make_shared<Detail::FServerResponseState>();
		Result = Backend::FAccess::Response(N::FAccess::Wrap(Op), Progress);
		N::ObserveTerminal(Op,
		    [Progress](auto Status, const auto&)
		{
			if (Status != Network::EOperationState::Succeeded)
			{
				Progress->State = EResponseState::Aborted;
			}
		});
		if (N::Terminal(N::State(Op)))
		{
			return Result;
		}
		auto Retention = N::ReserveBytes(P->Context, Response.Body.capacity(), Op);
		if (!Retention)
		{
			return Result;
		}
		for (const auto* Fields : {&Response.Headers, &Response.Trailers})
		{
			for (const auto& H : Fields->Fields())
			{
				for (const auto Bytes : {H.first.capacity(), H.second.capacity(), H.first.size(), H.second.size()})
				{
					if (Bytes > N::Options(P->Context).MaxRetainedBytes -
					            std::min(Retention->Size(), N::Options(P->Context).MaxRetainedBytes) ||
					    !Retention->TryResize(Retention->Size() + Bytes))
					{
						N::Fail(Op, Network::EError::Limit, "HTTP response snapshot byte limit exceeded");
						return Result;
					}
				}
			}
		}
		N::Post(P->Context,
		    [P,
		        Op,
		        Progress,
		        Retention,
		        G = Generation,
		        Response = std::move(Response),
		        Source = std::move(Source)]() mutable
		{
			P->StartResponseStream(std::move(Response), std::move(Source), Op, G, std::move(Retention), Progress);
		});
		return Result;
	}

	struct FBoostHttpServer : Backend::IServer, std::enable_shared_from_this<FBoostHttpServer>
	{
		FBoostHttpServer(std::shared_ptr<N::FContext> C, FServerOptions O, Backend::FExchangeFactory Factory)
		    : Context(std::move(C)), Acceptor(N::Io(Context)), ShutdownTimer(N::Io(Context)), Options(O), ExchangeFactory(Factory)
		{
		}
		#include "HttpServerShutdown.inl"

		void Deliver()
		{
			while (!Ready.empty() && !Waiters.empty())
			{
				auto Op = Waiters.front();
				Waiters.pop_front();
				if (!N::Terminal(N::State(Op)) && N::Complete(Op, Ready.front()))
				{
					Backend::FAccess::Implementation(*Ready.front()).MarkDelivered();
					Ready.pop_front();
				}
			}
		}
		void AcceptSocket()
		{
			if (bClosed || bStopping)
			{
				return;
			}
			Acceptor.async_accept(
			    [Self = shared_from_this()](N::FSystemError Error, N::FTcp::socket Socket)
			{
				if (Self->bClosed || Self->bStopping)
				{
					return;
				}
				if (Error)
				{
					Self->Close();
					return;
				}
				auto P =
				    std::make_shared<FBoostHttpConnection>(Self->Context, std::move(Socket), Self->Options, Self->Tls);
				P->ResourceId = N::AddResource(Self->Context,
				    [Weak = std::weak_ptr<FBoostHttpConnection>(P)]
				{
					if (auto Connection = Weak.lock())
					{
						Connection->Close();
					}
				});
				if (P->ResourceId)
				{
					Self->Connections.push_back(P);
					Self->Connections.erase(std::remove_if(Self->Connections.begin(),
					                            Self->Connections.end(),
					                            [](const auto& W)
					{
						return W.expired();
					}),
					    Self->Connections.end());
					P->OnClosed = [Weak = Self->weak_from_this()]
					{
						if (auto Server = Weak.lock())
						{
							Server->CheckShutdown();
						}
					};
					P->OnRequest = [Weak = Self->weak_from_this()](std::shared_ptr<FBoostHttpConnection> Connection)
					{
						auto Server = Weak.lock();
						if (!Server || Server->bClosed || Server->bStopping ||
						    Server->Ready.size() >= Server->Options.MaxPendingRequests)
						{
							Connection->Close();
							return;
						}
						Server->Ready.push_back(Server->ExchangeFactory(Server->Context,
						    std::make_shared<FBoostHttpExchange>(std::move(Connection))));
						Server->Deliver();
					};
					P->Stream.Handshake(
					    true,
					    "",
					    [P](N::FSystemError Error)
					{
						if (Error)
						{
							P->Close();
						}
						else if (!P->bClosed)
						{
							P->Read();
						}
					},
					    P->Options.HandshakeTimeout);
				}
				Self->AcceptSocket();
			});
		}
		void Close()
		{
			if (bClosed)
			{
				return;
			}
			bClosed = true;
			bStopping = true;
			ShutdownTimer.cancel();
			if (auto Pending = std::move(ShutdownOperation))
			{
				N::Fail(Pending, Network::EError::Closed, "HTTP server closed during shutdown");
			}
			N::RemoveResource(Context, ResourceId);
			N::FSystemError Ignore;
			Acceptor.close(Ignore);
			for (const auto& Weak : Connections)
			{
				if (auto P = Weak.lock())
				{
					P->Close();
				}
			}
			for (const auto& Op : Waiters)
			{
				N::Fail(Op, Network::EError::Closed, "HTTP server closed");
			}
			Waiters.clear();
			Ready.clear();
		}
		std::shared_ptr<N::FContext> Context;
		N::FTcp::acceptor Acceptor;
		N::Asio::steady_timer ShutdownTimer;
		std::shared_ptr<N::FOperation> ShutdownOperation;
		std::uint64_t ShutdownConnections = 0;
		bool bStopping = false, bClosingConnections = false;
		FServerOptions Options;
		std::deque<std::shared_ptr<FExchange>> Ready;
		Backend::FExchangeFactory ExchangeFactory;
		std::shared_ptr<N::FNativeTlsContext> Tls;
		std::deque<std::shared_ptr<N::FOperation>> Waiters;
		std::vector<std::weak_ptr<FBoostHttpConnection>> Connections;
		std::uint16_t ListenPort = 0;
		std::uint64_t ResourceId = 0;
		bool bClosed = false;

		void Dispose() noexcept override;
		std::uint16_t Port() const noexcept override;
		Network::FOperation Accept(Network::FOperationOptions Options) override;
		Network::FOperation Shutdown(std::chrono::milliseconds Grace, Network::FOperationOptions Options) override;
		void RequestClose() override;
	};

	std::shared_ptr<FServer> BoostListenHttp(
	    Network::FContext& Context, std::string Address, std::uint16_t Port, Network::FError& Error,
	    FServerOptions Options, Backend::FExchangeFactory Factory)
	{
		Error = {};
		auto C = N::FAccess::Context(Context);
		if (!Factory || Address.empty() || Address.find('\0') != std::string::npos ||
		    Options.MaxBodyBytes > N::Options(C).MaxBufferBytes || Options.MaxBodyBytes > Network::FBytes{}.max_size() ||
		    Options.MaxHeaderBytes > N::Options(C).MaxBufferBytes || Options.MaxHeaderBytes > UINT32_MAX - 8192 ||
		    !Options.MaxPendingRequests ||
		    (Options.bStreamRequests &&
		        (!Options.RequestStreamBufferBytes || Options.RequestStreamBufferBytes > N::Options(C).MaxBufferBytes ||
		            Options.RequestStreamBufferBytes > static_cast<std::uint64_t>(INT64_MAX) ||
		            Options.RequestStreamBufferBytes > Network::FBytes{}.max_size())) ||
		    Options.ResponseTimeout.count() <= 0 || Options.ResponseTimeout > std::chrono::hours(24 * 365) ||
		    Options.HandshakeTimeout.count() < 0 || Options.HandshakeTimeout > std::chrono::hours(24 * 365))
		{
			Error = {Network::EError::InvalidArgument, "Invalid HTTP server limits", 0};
			return {};
		}
		std::shared_ptr<N::FNativeTlsContext> Tls;
		if (Options.Tls.bEnabled)
		{
			Tls = N::FNativeTlsContext::Create(Options.Tls, true, Error);
			if (!Tls)
			{
				return {};
			}
		}
		Options.Tls = {};
		auto P = std::make_shared<FBoostHttpServer>(C, std::move(Options), Factory);
		P->Tls = std::move(Tls);
		N::FSystemError Ec;
		auto Ip = N::Asio::ip::make_address(Address, Ec);
		if (!Ec)
		{
			N::FTcp::endpoint Ep(Ip, Port);
			P->Acceptor.open(Ep.protocol(), Ec);
			if (!Ec)
			{
				P->Acceptor.bind(Ep, Ec);
			}
			if (!Ec)
			{
				P->Acceptor.listen(N::Asio::socket_base::max_listen_connections, Ec);
			}
		}
		if (Ec)
		{
			Error = {Network::EError::Connect, Ec.message(), Ec.value()};
			return {};
		}
		P->ListenPort = P->Acceptor.local_endpoint().port();
		P->ResourceId = N::AddResource(C,
		    [Weak = std::weak_ptr<FBoostHttpServer>(P)]
		{
			if (auto Server = Weak.lock())
			{
				Server->Close();
			}
		});
		if (!P->ResourceId)
		{
			Error = {Network::EError::Limit, "Context closed or connection limit exceeded", 0};
			return {};
		}
		N::Post(C,
		    [P]
		{
			P->AcceptSocket();
		});
		return Backend::FAccess::Wrap(C, std::move(P));
	}

	void FBoostHttpServer::Dispose() noexcept
	{
		N::OnDestroy(Context,
		    [this]
		{
			RequestClose();
		});
	}

	std::uint16_t FBoostHttpServer::Port() const noexcept
	{
		return ListenPort;
	}

	Network::FOperation FBoostHttpServer::Accept(Network::FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (!N::Terminal(N::State(Op)))
		{
			N::Post(P->Context,
			    [P, Op]
			{
				if (N::Terminal(N::State(Op)))
				{
					return;
				}
				if (P->bClosed || P->bStopping)
				{
					N::Fail(Op, Network::EError::Closed, "HTTP server closed");
					return;
				}
				N::SetAbort(Op,
				    [Weak = std::weak_ptr<FBoostHttpServer>(P), Pending = std::weak_ptr<N::FOperation>(Op)]
				{
					if (auto Server = Weak.lock())
					{
						auto Op = Pending.lock();
						Server->Waiters.erase(std::remove(Server->Waiters.begin(), Server->Waiters.end(), Op),
						    Server->Waiters.end());
					}
				});
				P->Waiters.push_back(Op);
				N::Running(Op);
				P->Deliver();
			});
		}
		return N::FAccess::Wrap(Op);
	}

	Network::FOperation FBoostHttpServer::Shutdown(std::chrono::milliseconds Grace, Network::FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (!N::Terminal(N::State(Op)))
		{
			N::Post(P->Context,
			    [P, Op, Grace]
			{
				P->Shutdown(Op, Grace);
			});
		}
		return N::FAccess::Wrap(Op);
	}

	void FBoostHttpServer::RequestClose()
	{
		auto P = shared_from_this();
		N::Post(P->Context,
		    [P]
		{
			P->Close();
		});
	}

} // namespace VeroScript::Http
