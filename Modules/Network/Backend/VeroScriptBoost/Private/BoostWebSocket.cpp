// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptWebSocketBackend.h"
#include "BoostNetworkBackend.h"
#include "NetworkNativeStream.h"
#include "BoostUpgrade.h"
#include "WebSocketProtocols.h"
#include "WebSocketCompressionPolicy.h"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/read_until.hpp>

namespace VeroScript::WebSocket
{
	namespace N = Network::Detail;
	namespace Beast = boost::beast;
	namespace Ws = Beast::websocket;
	static bool BoostCompressionSupported() noexcept
	{
		return VEROSCRIPT_WEBSOCKET_COMPRESSION != 0;
	}

	static bool ValidClose(std::uint16_t Code, const std::string& Reason)
	{
		if (Reason.size() > 123 || !Ws::detail::is_valid_close_code(Code))
		{
			return false;
		}
		Ws::detail::utf8_checker Utf8;
		return Utf8.write(reinterpret_cast<const std::uint8_t*>(Reason.data()), Reason.size()) && Utf8.finish();
	}

	struct FBoostWebSocketConnection : Backend::IConnection, std::enable_shared_from_this<FBoostWebSocketConnection>
	{
		struct FSend
		{
			std::shared_ptr<N::FByteLease> Retention;
			std::shared_ptr<N::FOperation> Operation;
			FMessage Message;
			bool bClose = false;
			bool bFragment = false, bPing = false;
			Ws::close_reason Close;
		};

		FBoostWebSocketConnection(std::shared_ptr<N::FContext> C, FOptions O, std::shared_ptr<N::FNativeTlsContext> Tls = {})
		    : Context(std::move(C)), Stream(N::Io(Context), std::move(Tls)), Resolver(std::in_place, N::Io(Context)),
		      Options(std::move(O)), ReadBuffer(Options.MaxMessageBytes)
		{
		}
		FBoostWebSocketConnection(std::shared_ptr<N::FContext> C, N::FTcp::socket Socket, FOptions O,
		    std::shared_ptr<N::FNativeTlsContext> Tls = {})
		    : Context(std::move(C)), Stream(std::move(Socket), std::move(Tls)), Resolver(std::in_place, N::Io(Context)),
		      Options(std::move(O)), ReadBuffer(Options.MaxMessageBytes)
		{
		}
		~FBoostWebSocketConnection()
		{
			N::RemoveResource(Context, ResourceId);
		}

		bool Prepare()
		{
			if (!Detail::ValidProtocols(Options.Subprotocols, Options.bRequireSubprotocol) ||
			    !Detail::ValidCompressionOptions(Options) || (Options.bCompression && !BoostCompressionSupported()) ||
			    !Options.MaxMessageBytes || Options.MaxMessageBytes > N::Options(Context).MaxBufferBytes ||
			    Options.MaxQueuedBytes < Options.MaxMessageBytes || Options.MaxQueuedBytes > SIZE_MAX ||
			    !Options.MaxQueuedMessages ||
			    (Options.IdleTimeout &&
			        (Options.IdleTimeout->count() <= 0 || *Options.IdleTimeout > std::chrono::hours(24 * 365))))
			{
				return false;
			}
			ReadMemory = N::ReserveBytes(Context, std::min<std::uint64_t>(16384, Options.MaxMessageBytes), SetupError);
			if (!ReadMemory)
			{
				return false;
			}
			MessageMemory = N::ReserveBytes(Context, 0, SetupError);
			if (!MessageMemory)
			{
				return false;
			}
			ControlMemory = N::ReserveBytes(Context, std::uint64_t(Options.MaxQueuedMessages) * 125, SetupError);
			if (!ControlMemory)
			{
				return false;
			}
			if (Options.bCompression)
			{
				// Conservative bound for both pinned Beast codecs (all supported window/memory levels).
				CompressionMemory = N::ReserveBytes(Context, 1024 * 1024, SetupError);
				if (!CompressionMemory)
				{
					return false;
				}
			}
			ReadBuffer.reserve(static_cast<std::size_t>(ReadMemory->Size()));
			return true;
		}
		bool Register()
		{
			if (!Prepare())
			{
				return false;
			}
			ResourceId = N::AddResource(Context,
			    [Weak = weak_from_this()]
			{
				if (auto Self = Weak.lock())
				{
					Self->Abort("Context closed");
				}
			});
			return ResourceId != 0;
		}

		#include "WebSocketCompression.inl"

		void Configure(Beast::role_type Role)
		{
			auto Timeout = Ws::stream_base::timeout::suggested(Role);
			Timeout.idle_timeout = Options.IdleTimeout ? *Options.IdleTimeout : Ws::stream_base::none();
			Timeout.keep_alive_pings = Options.bKeepAlivePings;
			Stream.set_option(Timeout);
			Stream.control_callback(
			    [Weak = weak_from_this()](Ws::frame_type Type, Beast::string_view Data)
			{
				if (auto Self = Weak.lock())
				{
					if (Type == Ws::frame_type::close)
					{
						return;
					}
					std::lock_guard<std::mutex> Lock(Self->InfoMutex);
					if (Self->Controls.size() < Self->Options.MaxQueuedMessages)
					{
						Self->Controls.push_back(
						    {Type == Ws::frame_type::ping ? EControlType::Ping : EControlType::Pong,
						        std::string(Data)});
					}
					else
					{
						Self->bControlOverflow = true;
						Self->Stream.next_layer().Close();
					}
				}
			});
			Stream.read_message_max(Options.MaxMessageBytes);
			Stream.set_option(CompressionParameters(Options.bCompression));
			if (Role == Beast::role_type::server)
			{
				ServerDecorator({});
			}
			Stream.auto_fragment(true);
			if (Role == Beast::role_type::client && !Options.Subprotocols.empty())
			{
				std::string Offer;
				for (const auto& Protocol : Options.Subprotocols)
				{
					if (!Offer.empty())
					{
						Offer += ", ";
					}
					Offer += Protocol;
				}
				Stream.set_option(Ws::stream_base::decorator(
				    [Offer = std::move(Offer)](Ws::request_type& Request)
				{
					Request.set(Beast::http::field::sec_websocket_protocol, Offer);
				}));
			}
		}

		bool AcceptSelectedProtocol()
		{
			const auto Field = Beast::http::field::sec_websocket_protocol;
			const auto Value = HandshakeResponse[Field];
			const std::string Selected(Value.data(), Value.size());
			if (HandshakeResponse.count(Field) > 1 || (Selected.empty() && Options.bRequireSubprotocol) ||
			    (!Selected.empty() &&
			        (!Detail::ProtocolToken(Selected) ||
			            std::find(Options.Subprotocols.begin(), Options.Subprotocols.end(), Selected) ==
			                Options.Subprotocols.end())))
			{
				return false;
			}
			{
				std::lock_guard<std::mutex> Lock(InfoMutex);
				SelectedProtocol = Selected;
			}
			CaptureCompression(HandshakeResponse);
			HandshakeResponse = {};
			return true;
		}

		void AcceptWebSocket(std::function<void(N::FSystemError)> Done)
		{
			if (Options.Subprotocols.empty() && !Options.bCompression)
			{
				Stream.async_accept(std::move(Done));
				return;
			}
			Network::FError Error;
			HandshakeMemory = N::ReserveBytes(Context, 65536, Error);
			if (!HandshakeMemory)
			{
				Done(N::Asio::error::no_buffer_space);
				return;
			}
			HandshakeInput.reserve(65536);
			N::Asio::async_read_until(Stream.next_layer(),
			    N::Asio::dynamic_buffer(HandshakeInput, 65536),
			    "\r\n\r\n",
			    [Self = shared_from_this(), Done = std::move(Done)](N::FSystemError Ec, std::size_t Size) mutable
			{
				if (Ec)
				{
					Done(Ec);
					return;
				}
				Beast::http::request_parser<Beast::http::empty_body> Parser;
				Parser.header_limit(65536);
				Parser.put(N::Asio::buffer(Self->HandshakeInput.data(), Size), Ec);
				std::vector<std::string> Offered;
				if (!Ec && Parser.is_header_done())
				{
					for (const auto& Field : Parser.get())
					{
						if (Field.name() == Beast::http::field::sec_websocket_protocol &&
						    !Detail::ParseProtocols(
						        std::string_view(Field.value().data(), Field.value().size()), Offered))
						{
							Ec = N::Asio::error::invalid_argument;
							break;
						}
					}
				}
				if (Ec || !Parser.is_header_done())
				{
					Done(Ec ? Ec : N::Asio::error::invalid_argument);
					return;
				}
				std::string Selected;
				for (const auto& Protocol : Self->Options.Subprotocols)
				{
					if (std::find(Offered.begin(), Offered.end(), Protocol) != Offered.end())
					{
						Selected = Protocol;
						break;
					}
				}
				if (Selected.empty() && Self->Options.bRequireSubprotocol)
				{
					Done(N::Asio::error::invalid_argument);
					return;
				}
				Self->SelectServerCompression(Parser.get());
				Self->ServerDecorator(Selected);
				{
					std::lock_guard<std::mutex> Lock(Self->InfoMutex);
					Self->SelectedProtocol = Selected;
				}
				// Pass the original bytes, including any first frame read ahead,
				// into Beast's upgrade parser; no input is dropped or reserialized.
				Self->Stream.async_accept(N::Asio::buffer(Self->HandshakeInput),
				    [Self, Done = std::move(Done)](N::FSystemError Error) mutable
				{
					std::string().swap(Self->HandshakeInput);
					Self->HandshakeMemory.reset();
					Done(Error);
				});
			});
		}

		std::shared_ptr<FConnection> Wrap()
		{
			return Backend::FAccess::Wrap(Context, shared_from_this());
		}

		void Abort(
		    std::string_view Message, bool bClean = false, Network::EError Code = Network::EError::Closed, int SystemCode = 0)
		{
			if (bClosed)
			{
				return;
			}
			if (bControlOverflow)
			{
				// Read, write or close callbacks may observe the forced close first.
				// Preserve its cause before publishing any pending terminal states.
				Message = "WebSocket control queue limit exceeded";
				bClean = false;
				Code = Network::EError::Limit;
				SystemCode = 0;
			}
			bClosed = true;
			FailureCode = bClean ? Network::EError::Closed : Code;
			FailureSystemCode = SystemCode;
			if (HandshakeTimer)
			{
				HandshakeTimer->cancel();
			}
			N::RemoveResource(Context, ResourceId);
			Stream.next_layer().Close();
			// A retained stream may have no pending read after a rejected handshake.
			// Closing its socket alone leaves Beast's idle timer on the executor.
			Ws::stream_base::timeout Disabled;
			Disabled.handshake_timeout = Ws::stream_base::none();
			Disabled.idle_timeout = Ws::stream_base::none();
			Disabled.keep_alive_pings = false;
			Stream.set_option(Disabled);
			// cancel() creates a new token for reuse; destruction invalidates the old token without allocation.
			Resolver.reset();
			{
				std::lock_guard<std::mutex> Lock(InfoMutex);
				Info.bClean = bClean;
				Info.bClosed = true;
				if (bClean)
				{
					auto Reason = Stream.reason();
					Info.Code = Reason.code ? static_cast<std::uint16_t>(Reason.code) : 1005;
					Info.Reason.assign(Reason.reason.data(), Reason.reason.size());
				}
				else
				{
					try
					{
						Info.Reason = Message;
					}
					catch (const std::bad_alloc&)
					{
						Info.Reason.clear();
					}
				}
			}
			for (auto& Waiter : Receivers)
			{
				if (!N::Terminal(N::State(Waiter)))
				{
					N::Fail(Waiter, FailureCode, std::string(Message), FailureSystemCode);
				}
			}
			Receivers.clear();
			for (auto& Pending : Sends)
			{
				if (Pending->bClose && bClean)
				{
					FCloseInfo Close;
					{
						std::lock_guard<std::mutex> Lock(InfoMutex);
						Close = Info;
					}
					N::Complete(Pending->Operation, std::move(Close));
				}
				else
				{
					if (!N::Terminal(N::State(Pending->Operation)))
					{
						N::Fail(Pending->Operation, FailureCode, std::string(Message), FailureSystemCode);
					}
				}
			}
			Sends.clear();
			SendBytes = 0;
		}

		void Deliver()
		{
			while (!Messages.empty() && !Receivers.empty())
			{
				auto Op = std::move(Receivers.front());
				Receivers.pop_front();
				if (N::Terminal(N::State(Op)))
				{
					continue;
				}
				if (N::Finish(Context, Op, Network::EOperationState::Succeeded, {}, Messages.front().Value))
				{
					ReceiveBytes -= Messages.front().Bytes;
					Messages.pop_front();
				}
			}
		}
		void ArmHandshakeDeadline()
		{
			if (!Options.Operation.Timeout)
			{
				return;
			}
			HandshakeTimer = std::make_shared<N::Asio::steady_timer>(N::Io(Context));
			HandshakeTimer->expires_after(*Options.Operation.Timeout);
			HandshakeTimer->async_wait(
			    [Weak = weak_from_this()](N::FSystemError Error)
			{
				if (!Error)
				{
					if (auto P = Weak.lock())
					{
						P->Abort("WebSocket handshake deadline exceeded");
					}
				}
			});
		}

		void Read()
		{
			if (bClosed)
			{
				return;
			}
			Stream.async_read_some(ReadBuffer,
			    static_cast<std::size_t>(ReadMemory->Size()),
			    [Self = shared_from_this()](N::FSystemError Error, std::size_t)
			{
				if (Self->bClosed)
				{
					return;
				}
				if (Self->bControlOverflow)
				{
					// The control callback closes the stream to stop this read.
					// Preserve its cause instead of reporting the resulting I/O error.
					Self->Abort("WebSocket control queue limit exceeded", false, Network::EError::Limit);
					return;
				}
				if (Error)
				{
					const auto Code = Error == Ws::error::closed ? Network::EError::Closed
					    : Error == Ws::error::message_too_big    ? Network::EError::Limit
					    : Error == Beast::error::timeout || Error == N::Asio::error::timed_out
					    ? Network::EError::TimedOut
					    : Error == Ws::condition::protocol_violation ||
					        Error.category() ==
					            make_error_code(Beast::zlib::error::invalid_block_type).category()
					    ? Network::EError::Protocol
					    : Network::EError::Read;
					Self->Abort(Error.message(), Error == Ws::error::closed, Code, Error.value());
					return;
				}
				const auto Size = Self->ReadBuffer.size();
				const auto Needed = Self->IncomingMessage.size() + Size;
				if (Needed > Self->Options.MaxMessageBytes)
				{
					Self->Abort("WebSocket message limit exceeded", false, Network::EError::Limit);
					return;
				}
				if (Needed > Self->IncomingMessage.capacity())
				{
					const auto Current = Self->IncomingMessage.capacity();
					const auto Limit = static_cast<std::size_t>(Self->Options.MaxMessageBytes);
					const auto Growth = Current > Limit / 2 ? Limit : std::max<std::size_t>(16384, Current * 2);
					const auto Capacity = std::min(Limit, std::max(Needed, Growth));
					if (!Self->MessageMemory->TryResize(Capacity))
					{
						Self->Abort("Context payload byte limit exceeded", false, Network::EError::Limit);
						return;
					}
					Self->IncomingMessage.reserve(Capacity);
				}
				const auto Previous = Self->IncomingMessage.size();
				Self->IncomingMessage.resize(Needed);
				if (Size)
				{
					N::Asio::buffer_copy(
					    N::Asio::buffer(Self->IncomingMessage.data() + Previous, Size), Self->ReadBuffer.data());
				}
				Self->ReadBuffer.consume(Size);
				if (!Self->Options.bReceiveFragments && !Self->Stream.is_message_done())
				{
					Self->Read();
					return;
				}
				FMessage Message;
				Message.bText = Self->Stream.got_text();
				Message.bFinal = Self->Stream.is_message_done();
				Message.bFirst = !Self->bReceiveMessageOpen;
				Self->bReceiveMessageOpen = !Message.bFinal;
				Message.Bytes = std::move(Self->IncomingMessage);
				if (Self->Messages.size() >= Self->Options.MaxQueuedMessages ||
				    Message.Bytes.size() >
				        Self->Options.MaxQueuedBytes - std::min(Self->ReceiveBytes, Self->Options.MaxQueuedBytes))
				{
					Self->Abort("WebSocket receive queue limit exceeded", false, Network::EError::Limit);
					return;
				}
				Self->ReceiveBytes += Message.Bytes.size();
				const auto Bytes = Message.Bytes.size();
				auto Value = N::OwnResult(Self->Context, std::move(Message), Self->MessageMemory);
				Self->Messages.push_back({std::move(Value), Bytes});
				Network::FError ReserveError;
				Self->MessageMemory = N::ReserveBytes(Self->Context, 0, ReserveError);
				if (!Self->MessageMemory)
				{
					Self->Abort("Unable to reserve WebSocket message storage");
					return;
				}
				Self->Deliver();
				Self->Read();
			});
		}

		void Write()
		{
			if (bClosed || bWriting || Sends.empty())
			{
				return;
			}
			while (!Sends.empty() && N::Terminal(N::State(Sends.front()->Operation)))
			{
				if (Sends.front()->bClose)
				{
					Abort("WebSocket close cancelled");
					return;
				}
				SendBytes -= Sends.front()->Message.Bytes.size();
				Sends.pop_front();
			}
			if (Sends.empty())
			{
				return;
			}
			auto Item = Sends.front();
			bWriting = true;
			N::Running(Item->Operation);
			if (Item->bClose)
			{
				Stream.async_close(Item->Close,
				    [Self = shared_from_this(), Item](N::FSystemError Error)
				{
					Self->bWriting = false;
					Self->Abort(Error ? Error.message() : "WebSocket closed", !Error || Error == Ws::error::closed);
				});
				return;
			}
			auto Complete = [Self = shared_from_this(), Item](N::FSystemError Error, std::size_t Bytes)
			{
				Self->bWriting = false;
				if (Self->bClosed)
				{
					return;
				}
				if (Error)
				{
					Self->Abort(Error.message(), false, Network::EError::Write, Error.value());
					return;
				}
				Self->Sends.pop_front();
				Self->SendBytes -= Item->Message.Bytes.size();
				if (!N::Complete(Item->Operation, static_cast<std::uint64_t>(Bytes)))
				{
					Self->Abort("Active WebSocket send cancelled");
					return;
				}
				Self->Write();
			};
			if (Item->bPing)
			{
				Ws::ping_data Data;
				if (!Item->Message.Bytes.empty())
				{
					Data.assign(reinterpret_cast<const char*>(Item->Message.Bytes.data()), Item->Message.Bytes.size());
				}
				Stream.async_ping(Data,
				    [Complete, Item](N::FSystemError Error) mutable
				{
					Complete(Error, Item->Message.Bytes.size());
				});
				return;
			}
			const auto Reject = [&](Network::EError Code, const char* Message)
			{
				bWriting = false;
				Sends.pop_front();
				SendBytes -= Item->Message.Bytes.size();
				N::Fail(Item->Operation, Code, Message);
				N::Asio::post(N::Io(Context),
				    [Self = shared_from_this()]
				{
					Self->Write();
				});
			};
			if ((!Item->bFragment && bFragmentOpen) ||
			    (Item->bFragment && bFragmentOpen && Item->Message.bText != bFragmentText))
			{
				Reject(Network::EError::State, "A fragmented message owns the WebSocket writer");
				return;
			}
			if (!bFragmentOpen)
			{
				Stream.text(Item->Message.bText);
			}
			if (Item->bFragment)
			{
				const auto PriorBytes = bFragmentOpen ? FragmentBytes : 0;
				if (Item->Message.Bytes.size() > Options.MaxMessageBytes - PriorBytes)
				{
					Reject(Network::EError::Limit, "Fragmented WebSocket message limit exceeded");
					return;
				}
				auto Utf8 = bFragmentOpen ? FragmentUtf8 : Ws::detail::utf8_checker{};
				if (Item->Message.bText && ((!Item->Message.Bytes.empty() && !Utf8.write(Item->Message.Bytes.data(), Item->Message.Bytes.size())) || (Item->Message.bFinal && !Utf8.finish())))
				{
					Reject(Network::EError::InvalidArgument, "Invalid UTF-8 fragmented message");
					return;
				}
				FragmentUtf8 = Utf8;
				FragmentBytes = PriorBytes + Item->Message.Bytes.size();
				bFragmentText = Item->Message.bText;
				bFragmentOpen = !Item->Message.bFinal;
				Stream.async_write_some(
				    Item->Message.bFinal, N::Asio::buffer(Item->Message.Bytes), std::move(Complete));
			}
			else
			{
				Stream.async_write(N::Asio::buffer(Item->Message.Bytes), std::move(Complete));
			}
		}

		void Queue(const std::shared_ptr<FSend>& Item)
		{
			if (N::Terminal(N::State(Item->Operation)))
			{
				return;
			}
			if (bClosed || bClosing)
			{
				N::Fail(Item->Operation, Network::EError::Closed, "WebSocket closing or closed");
				return;
			}
			if (!Item->bClose &&
			    (Item->Message.Bytes.size() > Options.MaxMessageBytes || Sends.size() >= Options.MaxQueuedMessages ||
			        Item->Message.Bytes.size() > Options.MaxQueuedBytes - std::min(SendBytes, Options.MaxQueuedBytes)))
			{
				N::Fail(Item->Operation, Network::EError::Limit, "WebSocket send queue limit exceeded");
				return;
			}
			if (Item->Message.bText && !Item->bFragment && !Item->bPing)
			{
				Ws::detail::utf8_checker Utf8;
				if ((!Item->Message.Bytes.empty() &&
				        !Utf8.write(Item->Message.Bytes.data(), Item->Message.Bytes.size())) ||
				    !Utf8.finish())
				{
					N::Fail(Item->Operation, Network::EError::InvalidArgument, "Invalid UTF-8 text message");
					return;
				}
			}
			N::SetAbort(Item->Operation,
			    [Weak = weak_from_this(), WeakItem = std::weak_ptr<FSend>(Item)]
			{
				auto Self = Weak.lock();
				auto Pending = WeakItem.lock();
				if (!Self || !Pending || Self->bClosed)
				{
					return;
				}
				auto It = std::find(Self->Sends.begin(), Self->Sends.end(), Pending);
				if (It == Self->Sends.end())
				{
					return;
				}
				if (Pending->bClose || (It == Self->Sends.begin() && Self->bWriting))
				{
					Self->Abort("Active WebSocket send cancelled");
				}
				else
				{
					Self->SendBytes -= Pending->Message.Bytes.size();
					Self->Sends.erase(It);
				}
			});
			if (N::Terminal(N::State(Item->Operation)))
			{
				return;
			}
			bClosing = Item->bClose;
			SendBytes += Item->Message.Bytes.size();
			Sends.push_back(Item);
			Write();
		}

		std::shared_ptr<N::FContext> Context;
		Ws::stream<N::FNativeStream, (VEROSCRIPT_WEBSOCKET_COMPRESSION != 0)> Stream;
		std::optional<N::FTcp::resolver> Resolver;
		std::shared_ptr<N::Asio::steady_timer> HandshakeTimer;
		FOptions Options;
		std::shared_ptr<N::FByteLease> HandshakeMemory;
		std::string HandshakeInput, SelectedProtocol;
		std::string ServerCompressionHeader;
		Ws::response_type HandshakeResponse;
		std::shared_ptr<N::FByteLease> ReadMemory, MessageMemory, ControlMemory;
		std::shared_ptr<N::FByteLease> CompressionMemory;
		Network::FError SetupError;
		std::shared_ptr<N::FByteLease> UpgradeMemory, ProxyMemory;
		Beast::flat_buffer ReadBuffer;
		Network::FBytes IncomingMessage;
		struct FQueuedMessage
		{
			std::shared_ptr<const Network::Detail::FOwnedResult> Value;
			std::size_t Bytes;
		};
		std::deque<FQueuedMessage> Messages;
		std::deque<std::shared_ptr<N::FOperation>> Receivers;
		std::deque<std::shared_ptr<FSend>> Sends;
		mutable std::mutex InfoMutex;
		FCloseInfo Info;
		FCompressionInfo SelectedCompression;
		Network::EError FailureCode = Network::EError::Closed;
		int FailureSystemCode = 0;
		std::deque<FControlEvent> Controls;
		Ws::detail::utf8_checker FragmentUtf8;
		std::uint64_t FragmentBytes = 0;
		std::uint64_t ReceiveBytes = 0, SendBytes = 0, ResourceId = 0;
		bool bClosed = false, bClosing = false, bWriting = false;
		bool bControlOverflow = false, bFragmentOpen = false, bFragmentText = false;
		bool bReceiveMessageOpen = false;

		void Dispose() noexcept override;
		Network::FOperation Ping(std::string Payload, Network::FOperationOptions Options) override;
		bool TryTakeControl(FControlEvent& Out) override;
		Network::FOperation Submit(FMessage Message, Network::FOperationOptions Options, bool Fragment, bool Ping) override;
		Network::FOperation Receive(Network::FOperationOptions Options) override;
		Network::FOperation Close(std::uint16_t Code, std::string Reason, Network::FOperationOptions Options) override;
		FCloseInfo CloseInfo() const override;
		std::string Subprotocol() const override;
		FCompressionInfo CompressionInfo() const override;
	};

	Network::FOperation BoostAcceptWebSocketUpgrade(
	    Network::FContext& Context, std::shared_ptr<Network::FUpgradeSource> Source, FOptions Options)
	{
		auto C = N::FAccess::Context(Context);
		auto Op = N::Begin(C, Options.Operation);
		if (N::Terminal(N::State(Op)))
		{
			return N::FAccess::Wrap(Op);
		}
		if (!Options.ProxyUrl.empty() || !Options.ProxyAuthorization.empty())
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Proxy options apply only to WebSocket clients");
			return N::FAccess::Wrap(Op);
		}
		if (!Detail::ValidCompressionOptions(Options) || (Options.bCompression && !BoostCompressionSupported()))
		{
			N::Fail(Op,
			    Detail::ValidCompressionOptions(Options) ? Network::EError::Unsupported : Network::EError::InvalidArgument,
			    "Unsupported or invalid WebSocket compression options");
			return N::FAccess::Wrap(Op);
		}

		try
		{
			if (!Source || !Source->BelongsTo(Context) || Source->Info().ContractVersion != 1 ||
			    Source->Info().BackendIdentity != N::UpgradeBackend || Source->Info().Protocol != "websocket")
			{
				N::Fail(Op, Network::EError::InvalidArgument, "WebSocket upgrade source is incompatible");
				return N::FAccess::Wrap(Op);
			}
			const auto& Text = Source->Info().RequestHead;
			if (Text.empty() || Text.size() > 65536)
			{
				N::Fail(Op, Network::EError::Limit, "WebSocket upgrade headers exceed their limit");
				return N::FAccess::Wrap(Op);
			}
			using FRequest = Beast::http::request<Beast::http::empty_body>;
			Beast::http::request_parser<Beast::http::empty_body> Parser;
			Parser.header_limit(65536);
			Parser.body_limit(0);
			N::FSystemError Error;
			const auto Used = Parser.put(N::Asio::buffer(Text), Error);
			if (Error || Used != Text.size() || !Parser.is_done())
			{
				N::Fail(Op, Network::EError::Protocol, "Invalid WebSocket upgrade headers");
				return N::FAccess::Wrap(Op);
			}
			auto Request = std::make_shared<FRequest>(Parser.release());
			using Field = Beast::http::field;
			const auto Key = (*Request)[Field::sec_websocket_key];
			auto Base64 = [](unsigned char Ch) -> int
			{
				if (Ch >= 'A' && Ch <= 'Z')
				{
					return Ch - 'A';
				}
				if (Ch >= 'a' && Ch <= 'z')
				{
					return Ch - 'a' + 26;
				}
				if (Ch >= '0' && Ch <= '9')
				{
					return Ch - '0' + 52;
				}
				return Ch == '+' ? 62 : Ch == '/' ? 63
				                                  : -1;
			};
			bool ValidKey = Key.size() == 24 && Key.substr(22) == "==";
			if (ValidKey)
			{
				for (std::size_t I = 0; I < 22; ++I)
				{
					ValidKey = ValidKey && Base64(Key[I]) >= 0;
				}
				ValidKey = ValidKey && (Base64(Key[21]) & 15) == 0;
			}
			if (!Ws::is_upgrade(*Request) || Request->version() != 11 || Request->method() != Beast::http::verb::get ||
			    Request->count(Field::host) != 1 || (*Request)[Field::host].empty() ||
			    Request->count(Field::sec_websocket_key) != 1 || !ValidKey ||
			    Request->count(Field::sec_websocket_version) != 1 || (*Request)[Field::sec_websocket_version] != "13" ||
			    Request->count(Field::transfer_encoding))
			{
				N::Fail(Op, Network::EError::Protocol, "Invalid WebSocket handshake request");
				return N::FAccess::Wrap(Op);
			}
			auto P = std::make_shared<FBoostWebSocketConnection>(C, std::move(Options));
			if (!P->Prepare())
			{
				N::Fail(Op, P->SetupError ? P->SetupError.Code : Network::EError::Limit, "WebSocket setup failed");
				return N::FAccess::Wrap(Op);
			}
			std::vector<std::string> Offered;
			for (const auto& H : Request->base())
			{
				if (H.name() == Field::sec_websocket_protocol &&
				    !Detail::ParseProtocols(std::string_view(H.value().data(), H.value().size()), Offered))
				{
					N::Fail(Op, Network::EError::Protocol, "Invalid WebSocket subprotocol offer");
					return N::FAccess::Wrap(Op);
				}
			}
			std::string Selected;
			for (const auto& Protocol : P->Options.Subprotocols)
			{
				if (std::find(Offered.begin(), Offered.end(), Protocol) != Offered.end())
				{
					Selected = Protocol;
					break;
				}
			}
			if (Selected.empty() && P->Options.bRequireSubprotocol)
			{
				N::Fail(Op, Network::EError::Protocol, "No matching WebSocket subprotocol");
				return N::FAccess::Wrap(Op);
			}
			P->Configure(Beast::role_type::server);
			P->SelectServerCompression(*Request);
			P->ServerDecorator(Selected);
			P->SelectedProtocol = Selected;
			struct FConsumer final : N::FUpgradeConsumer, std::enable_shared_from_this<FConsumer>
			{
				std::string_view BackendIdentity() const noexcept override
				{
					return N::UpgradeBackend;
				}
				std::shared_ptr<FBoostWebSocketConnection> Connection;
				std::shared_ptr<FRequest> Request;
				std::shared_ptr<const Network::Detail::FOwnedResult> Success;
				std::shared_ptr<N::FByteLease> Memory;
				std::function<void()> Closer;
				void Cancel() override
				{
					Connection->Abort("WebSocket upgrade cancelled");
				}
				void Take(std::unique_ptr<N::IUpgradeLease> IncomingLease, const std::shared_ptr<N::FOperation>& Op) override
				{
					auto Lease = std::unique_ptr<N::FUpgradeLease>(static_cast<N::FUpgradeLease*>(IncomingLease.release()));
					auto P = Connection;
					auto& State = *Lease->State;
					if (N::Terminal(N::State(Op)))
					{
						return;
					}
					if (!State.Stream || State.Context != P->Context || !State.ResourceId ||
					    !N::ReplaceResource(P->Context, State.ResourceId, std::move(Closer)))
					{
						N::Fail(Op, Network::EError::Closed, "WebSocket upgrade transport is unavailable");
						return;
					}
					P->ResourceId = std::exchange(State.ResourceId, 0);
					P->Stream.next_layer() = std::move(*State.Stream);
					State.Stream.reset();
					P->UpgradeMemory = std::move(State.Memory);
					P->Stream.next_layer().Prepend(std::move(State.PreRead));
					P->Stream.async_accept(*Request,
					    [Self = shared_from_this(), Op](N::FSystemError E)
					{
						auto P = Self->Connection;
						if (E)
						{
							N::Fail(Op, Network::EError::Protocol, E);
							P->Abort("WebSocket upgrade handshake failed");
							return;
						}
						if (N::Finish(P->Context, Op, Network::EOperationState::Succeeded, {}, Self->Success))
						{
							P->Read();
						}
						else
						{
							P->Abort("WebSocket upgrade ended before delivery");
						}
					});
				}
			};
			auto Consumer = std::make_shared<FConsumer>();
			Consumer->Connection = P;
			Consumer->Request = std::move(Request);
			Consumer->Memory = N::ReserveBytes(C, sizeof(FConsumer) + sizeof(FRequest) + Text.size() * 3 + 512, Op);
			if (!Consumer->Memory)
			{
				return N::FAccess::Wrap(Op);
			}
			Consumer->Success = N::OwnResult(C, P->Wrap());
			Consumer->Closer = [Weak = std::weak_ptr<FBoostWebSocketConnection>(P)]
			{
				if (auto Connection = Weak.lock())
				{
					Connection->Abort("Context closed");
				}
			};
			N::FUpgradeAccess::Begin(Source, std::move(Consumer), Op);
		}
		catch (const std::bad_alloc&)
		{
			N::Fail(Op, Network::EError::Allocation, "WebSocket upgrade allocation failed");
		}
		catch (...)
		{
			N::Fail(Op, Network::EError::Internal, "WebSocket upgrade setup failed");
		}
		return N::FAccess::Wrap(Op);
	}

	Network::FOperation BoostConnectWebSocket(
	    const std::shared_ptr<N::FContext>& C, std::string UrlText, FOptions Options)
	{
		auto Op = N::Begin(C, Options.Operation);
		if (N::Terminal(N::State(Op)))
		{
			return N::FAccess::Wrap(Op);
		}
		Network::FUrl Url;
		Network::FError Error;
		if (!Detail::ValidProtocols(Options.Subprotocols, Options.bRequireSubprotocol))
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Invalid WebSocket subprotocols");
			return N::FAccess::Wrap(Op);
		}
		if (!Detail::ValidCompressionOptions(Options) || (Options.bCompression && !BoostCompressionSupported()))
		{
			N::Fail(Op,
			    Detail::ValidCompressionOptions(Options) ? Network::EError::Unsupported : Network::EError::InvalidArgument,
			    "Unsupported or invalid WebSocket compression options");
			return N::FAccess::Wrap(Op);
		}
		if (!Network::FUrl::TryParse(UrlText, Url, Error) || (Url.Scheme != "ws" && Url.Scheme != "wss"))
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Expected a ws or wss URL");
			return N::FAccess::Wrap(Op);
		}
		Network::FUrl Proxy;
		if ((!Options.ProxyAuthorization.empty() && Options.ProxyUrl.empty()) ||
		    Options.ProxyAuthorization.size() > 4096 || Options.ProxyUrl.size() > 4096 ||
		    std::any_of(Options.ProxyAuthorization.begin(),
		        Options.ProxyAuthorization.end(),
		        [](unsigned char Ch)
		{
			return Ch < 0x20 || Ch >= 0x7f;
		}) ||
		    (!Options.ProxyUrl.empty() &&
		        (!Network::FUrl::TryParse(Options.ProxyUrl, Proxy, Error) || Proxy.Scheme != "http" || Proxy.Path != "/" ||
		            Proxy.bHasQuery || Proxy.bHasFragment)))
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Invalid WebSocket HTTP CONNECT proxy options");
			return N::FAccess::Wrap(Op);
		}

		std::shared_ptr<N::FNativeTlsContext> Tls;
		if (Url.Scheme == "wss")
		{
			Tls = N::FNativeTlsContext::Create(Options.Tls, false, Error);
			if (!Tls)
			{
				N::Fail(Op, Error.Code, Error.Message);
				return N::FAccess::Wrap(Op);
			}
		}
		else if (Options.Tls.bEnabled)
		{
			N::Fail(Op, Network::EError::InvalidArgument, "TLS options require a wss URL");
			return N::FAccess::Wrap(Op);
		}
		Options.Tls = {};
		auto P = std::make_shared<FBoostWebSocketConnection>(C, std::move(Options), std::move(Tls));
		if (!P->Register())
		{
			N::Fail(Op, P->SetupError ? P->SetupError.Code : Network::EError::Limit, "WebSocket setup failed");
			return N::FAccess::Wrap(Op);
		}
		if (!P->Options.ProxyUrl.empty())
		{
			P->ProxyMemory = N::ReserveBytes(C, 320 * 1024, Error);
			if (!P->ProxyMemory)
			{
				N::Fail(Op, Error.Code, "WebSocket proxy buffers exceed the context limit");
				return N::FAccess::Wrap(Op);
			}
		}

		N::SetAbort(Op,
		    [Weak = std::weak_ptr<FBoostWebSocketConnection>(P)]
		{
			if (auto Self = Weak.lock())
			{
				Self->Abort("WebSocket connect cancelled");
			}
		});
		N::Post(C,
		    [P, Op, Url = std::move(Url), Proxy = std::move(Proxy)]
		{
			if (N::Terminal(N::State(Op)))
			{
				return;
			}
			N::Running(Op);
			P->Configure(Beast::role_type::client);
			P->Resolver->async_resolve(Proxy.Host.empty() ? Url.Host : Proxy.Host,
			    std::to_string(Proxy.Host.empty() ? Url.Port : Proxy.Port),
			    [P, Op, Url](N::FSystemError Error, auto Addresses)
			{
				if (P->bClosed)
				{
					return;
				}
				if (Error)
				{
					N::Fail(Op, Network::EError::Resolve, Error);
					P->Abort(Error.message());
					return;
				}
				P->Stream.next_layer().Connect(std::move(Addresses),
				    [P, Op, Url](N::FSystemError Ec)
				{
					if (P->bClosed)
					{
						return;
					}
					if (Ec)
					{
						N::Fail(Op, Network::EError::Connect, Ec);
						P->Abort(Ec.message());
						return;
					}
					auto Handshake = [P, Op, Url](N::FSystemError TunnelError)
					{
						if (P->bClosed)
						{
							return;
						}
						if (TunnelError)
						{
							N::Fail(Op, Network::EError::Protocol, TunnelError);
							P->Abort("HTTP CONNECT proxy negotiation failed");
							return;
						}

						P->Stream.next_layer().Handshake(
						    false,
						    Url.Host,
						    [P, Op, Url](N::FSystemError TlsError)
						{
							if (P->bClosed)
							{
								return;
							}
							if (TlsError)
							{
								N::Fail(Op, Network::EError::Connect, TlsError);
								P->Abort(TlsError.message());
								return;
							}
							P->Stream.async_handshake(P->HandshakeResponse,
							    Url.Authority(),
							    Url.Target(),
							    [P, Op](N::FSystemError HandshakeError)
							{
								if (P->bClosed)
								{
									return;
								}
								if (HandshakeError)
								{
									N::Fail(Op, Network::EError::Protocol, HandshakeError);
									P->Abort(HandshakeError.message());
									return;
								}
								if (!P->AcceptSelectedProtocol())
								{
									N::Fail(Op,
									    Network::EError::Protocol,
									    "Invalid WebSocket subprotocol selection");
									P->Abort("Invalid WebSocket subprotocol selection");
									return;
								}
								N::Complete(Op, P->Wrap());
								P->Read();
							});
						},
						    P->Options.Operation.Timeout);
					};
					if (P->Options.ProxyUrl.empty())
					{
						Handshake({});
					}
					else
					{
						std::string Authority = Url.Host;
						if (Url.bIpv6)
						{
							Authority.insert(0, "[");
							Authority.push_back(']');
						}
						Authority.push_back(':');
						Authority.append(std::to_string(Url.Port));
						P->Stream.next_layer().Tunnel(
						    Authority, P->Options.ProxyAuthorization, std::move(Handshake));
					}
				});
			});
		});
		return N::FAccess::Wrap(Op);
	}

	void FBoostWebSocketConnection::Dispose() noexcept
	{
		N::OnDestroy(Context,
		    [this]
		{
			auto P = shared_from_this();
			N::Post(P->Context,
			    [P]
			{
				P->Abort("WebSocket handle released");
			});
		});
	}

	Network::FOperation FBoostWebSocketConnection::Ping(std::string Payload, Network::FOperationOptions Options)
	{
		if (Payload.size() > 125)
		{
			auto Op = N::Begin(Context, Options);
			if (!N::Terminal(N::State(Op)))
			{
				N::Fail(Op, Network::EError::InvalidArgument, "WebSocket Ping payload exceeds 125 bytes");
			}
			return N::FAccess::Wrap(Op);
		}
		FMessage Message;
		Message.Bytes.assign(Payload.begin(), Payload.end());
		return Submit(std::move(Message), Options, false, true);
	}
	bool FBoostWebSocketConnection::TryTakeControl(FControlEvent& Out)
	{
		std::lock_guard<std::mutex> Lock(InfoMutex);
		if (Controls.empty())
		{
			return false;
		}
		Out = std::move(Controls.front());
		Controls.pop_front();
		return true;
	}
	Network::FOperation FBoostWebSocketConnection::Submit(FMessage Message, Network::FOperationOptions Options, bool Fragment, bool Ping)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (!N::Terminal(N::State(Op)))
		{
			if ((Ping && Message.Bytes.size() > 125) || (!Fragment && !Message.bFinal))
			{
				N::Fail(Op, Network::EError::InvalidArgument, "Invalid WebSocket control or complete message");
				return N::FAccess::Wrap(Op);
			}
			auto Retention = N::ReserveBytes(P->Context, Message.Bytes.capacity(), Op);
			if (!Retention)
			{
				return N::FAccess::Wrap(Op);
			}
			auto Item = std::make_shared<FBoostWebSocketConnection::FSend>();
			Item->Retention = std::move(Retention);
			Item->bFragment = Fragment;
			Item->bPing = Ping;
			Item->Operation = Op;
			Item->Message = std::move(Message);
			N::Post(P->Context,
			    [P, Item]
			{
				P->Queue(Item);
			});
		}
		return N::FAccess::Wrap(Op);
	}

	Network::FOperation FBoostWebSocketConnection::Receive(Network::FOperationOptions Options)
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
				if (P->bClosed && P->Messages.empty())
				{
					N::Fail(Op, P->FailureCode, "WebSocket closed", P->FailureSystemCode);
					return;
				}
				N::SetAbort(Op,
				    [Weak = std::weak_ptr<FBoostWebSocketConnection>(P), WeakOp = std::weak_ptr<N::FOperation>(Op)]
				{
					if (auto Self = Weak.lock())
					{
						auto Pending = WeakOp.lock();
						Self->Receivers.erase(
						    std::remove(Self->Receivers.begin(), Self->Receivers.end(), Pending),
						    Self->Receivers.end());
					}
				});
				P->Receivers.push_back(Op);
				N::Running(Op);
				P->Deliver();
			});
		}
		return N::FAccess::Wrap(Op);
	}

	Network::FOperation FBoostWebSocketConnection::Close(std::uint16_t Code, std::string Reason, Network::FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (N::Terminal(N::State(Op)))
		{
			return N::FAccess::Wrap(Op);
		}
		if (!ValidClose(Code, Reason))
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Invalid WebSocket close code or reason");
			return N::FAccess::Wrap(Op);
		}
		auto Item = std::make_shared<FBoostWebSocketConnection::FSend>();
		Item->Operation = Op;
		Item->bClose = true;
		Item->Close.code = Code;
		Item->Close.reason = Reason;
		N::Post(P->Context,
		    [P, Item]
		{
			P->Queue(Item);
		});
		return N::FAccess::Wrap(Op);
	}

	FCloseInfo FBoostWebSocketConnection::CloseInfo() const
	{
		std::lock_guard<std::mutex> Lock(InfoMutex);
		return Info;
	}
	std::string FBoostWebSocketConnection::Subprotocol() const
	{
		std::lock_guard<std::mutex> Lock(InfoMutex);
		return SelectedProtocol;
	}
	FCompressionInfo FBoostWebSocketConnection::CompressionInfo() const
	{
		std::lock_guard<std::mutex> Lock(InfoMutex);
		return SelectedCompression;
	}

	struct FBoostWebSocketServer : Backend::IServer, std::enable_shared_from_this<FBoostWebSocketServer>
	{
		FBoostWebSocketServer(std::shared_ptr<N::FContext> C, FOptions O)
		    : Context(std::move(C)), Acceptor(N::Io(Context)), Options(std::move(O))
		{
		}
		void Deliver()
		{
			while (!Ready.empty() && !Waiters.empty())
			{
				auto Op = Waiters.front();
				Waiters.pop_front();
				if (!N::Terminal(N::State(Op)) && N::Complete(Op, Ready.front()))
				{
					Ready.pop_front();
				}
			}
		}
		void Accept()
		{
			if (bClosed)
			{
				return;
			}
			Acceptor.async_accept(
			    [Self = shared_from_this()](N::FSystemError Error, N::FTcp::socket Socket)
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
				auto P = std::make_shared<FBoostWebSocketConnection>(
				    Self->Context, std::move(Socket), Self->Options, Self->Tls);
				if (P->Register())
				{
					Self->Connections.push_back(P);
					Self->Connections.erase(std::remove_if(Self->Connections.begin(),
					                            Self->Connections.end(),
					                            [](const auto& W)
					{
						return W.expired();
					}),
					    Self->Connections.end());
					P->Configure(Beast::role_type::server);
					P->ArmHandshakeDeadline();
					P->Stream.next_layer().Handshake(
					    true,
					    "",
					    [Self, P](N::FSystemError Error)
					{
						if (Error || Self->bClosed)
						{
							P->Abort("TLS handshake failed or server closed");
							return;
						}
						P->AcceptWebSocket(
						    [Self, P](N::FSystemError Ec)
						{
							if (Ec || Self->bClosed ||
							    Self->Ready.size() >= Self->Options.MaxQueuedMessages)
							{
								P->Abort("WebSocket handshake failed or accept queue full");
								return;
							}
							Self->Ready.push_back(P->Wrap());
							if (P->HandshakeTimer)
							{
								P->HandshakeTimer->cancel();
							}
							Self->Deliver();
							P->Read();
						});
					},
					    Self->Options.Operation.Timeout);
				}
				Self->Accept();
			});
		}
		void Close()
		{
			if (bClosed)
			{
				return;
			}
			bClosed = true;
			N::RemoveResource(Context, ResourceId);
			N::FSystemError Ignore;
			Acceptor.close(Ignore);
			for (const auto& W : Connections)
			{
				if (auto P = W.lock())
				{
					P->Abort("WebSocket server closed");
				}
			}
			for (const auto& Op : Waiters)
			{
				N::Fail(Op, Network::EError::Closed, "WebSocket server closed");
			}
			Waiters.clear();
			Ready.clear();
		}
		std::shared_ptr<N::FContext> Context;
		N::FTcp::acceptor Acceptor;
		FOptions Options;
		std::deque<std::shared_ptr<FConnection>> Ready;
		std::shared_ptr<N::FNativeTlsContext> Tls;
		std::deque<std::shared_ptr<N::FOperation>> Waiters;
		std::vector<std::weak_ptr<FBoostWebSocketConnection>> Connections;
		std::uint16_t ListenPort = 0;
		std::uint64_t ResourceId = 0;
		bool bClosed = false;

		void Dispose() noexcept override;
		std::uint16_t Port() const noexcept override;
		Network::FOperation Accept(Network::FOperationOptions Options) override;
		void RequestClose() override;
	};
	std::shared_ptr<FServer> BoostListenWebSocket(
	    Network::FContext& Context, std::string Address, std::uint16_t Port, Network::FError& Error, FOptions Options)
	{
		Error = {};
		auto C = N::FAccess::Context(Context);
		if (!Detail::ValidCompressionOptions(Options))
		{
			Error = {Network::EError::InvalidArgument, "Invalid WebSocket compression options", 0};
			return {};
		}
		if (Options.bCompression && !BoostCompressionSupported())
		{
			Error = {Network::EError::Unsupported, "WebSocket compression is disabled in this build", 0};
			return {};
		}
		if (!Options.ProxyUrl.empty() || !Options.ProxyAuthorization.empty() ||
		    !Detail::ValidProtocols(Options.Subprotocols, Options.bRequireSubprotocol) || Address.empty() ||
		    Address.find('\0') != std::string::npos || !Options.MaxMessageBytes ||
		    (Options.Operation.Timeout && (Options.Operation.Timeout->count() < 0 || *Options.Operation.Timeout > std::chrono::hours(24 * 365))) ||
		    (Options.IdleTimeout &&
		        (Options.IdleTimeout->count() <= 0 || *Options.IdleTimeout > std::chrono::hours(24 * 365))) ||
		    Options.MaxMessageBytes > N::Options(C).MaxBufferBytes || Options.MaxQueuedBytes < Options.MaxMessageBytes ||
		    Options.MaxQueuedBytes > SIZE_MAX || !Options.MaxQueuedMessages)
		{
			Error = {Network::EError::InvalidArgument, "Invalid WebSocket server limits", 0};
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
		auto P = std::make_shared<FBoostWebSocketServer>(C, std::move(Options));
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
		P->ResourceId = N::AddResource(P->Context,
		    [Weak = std::weak_ptr<FBoostWebSocketServer>(P)]
		{
			if (auto Self = Weak.lock())
			{
				Self->Close();
			}
		});
		if (!P->ResourceId)
		{
			Error = {Network::EError::Limit, "Context closed or connection limit exceeded", 0};
			return {};
		}
		N::Post(P->Context,
		    [P]
		{
			P->Accept();
		});
		auto ContextOwner = P->Context;
		return Backend::FAccess::Wrap(ContextOwner, std::move(P));
	}

	void FBoostWebSocketServer::Dispose() noexcept
	{
		N::OnDestroy(Context,
		    [this]
		{
			RequestClose();
		});
	}

	std::uint16_t FBoostWebSocketServer::Port() const noexcept
	{
		return ListenPort;
	}

	Network::FOperation FBoostWebSocketServer::Accept(Network::FOperationOptions Options)
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
				if (P->bClosed)
				{
					N::Fail(Op, Network::EError::Closed, "WebSocket server closed");
					return;
				}
				N::SetAbort(Op,
				    [Weak = std::weak_ptr<FBoostWebSocketServer>(P), WeakOp = std::weak_ptr<N::FOperation>(Op)]
				{
					if (auto Self = Weak.lock())
					{
						auto Pending = WeakOp.lock();
						Self->Waiters.erase(std::remove(Self->Waiters.begin(), Self->Waiters.end(), Pending),
						    Self->Waiters.end());
					}
				});
				P->Waiters.push_back(Op);
				N::Running(Op);
				P->Deliver();
			});
		}
		return N::FAccess::Wrap(Op);
	}

	void FBoostWebSocketServer::RequestClose()
	{
		auto P = shared_from_this();
		N::Post(P->Context,
		    [P]
		{
			P->Close();
		});
	}

} // namespace VeroScript::WebSocket
