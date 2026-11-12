// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpSseClient.h"
#include "NetworkProvider.h"
#include "VeroScriptHttpBackend.h"
#include <array>
#include <algorithm>
#include <stdexcept>

namespace VeroScript::Http
{
	namespace N = Network::Detail;
	using FClock = std::chrono::steady_clock;
	FSseClientOptions::FSseClientOptions()
	{
		Request.Operation.Timeout = std::nullopt;
		Request.MaxBodyBytes = UINT64_MAX;
		Request.MaxTotalBodyBytes = UINT64_MAX;
	}
	static bool EventStreamType(const FHeaders& Headers)
	{
		const auto Values = Headers.GetAll("Content-Type");
		if (Values.size() != 1)
		{
			return false;
		}
		std::string_view Type = Values[0];
		Type = Type.substr(0, Type.find(';'));
		while (!Type.empty() && (Type.back() == ' ' || Type.back() == '\t'))
		{
			Type.remove_suffix(1);
		}
		while (!Type.empty() && (Type.front() == ' ' || Type.front() == '\t'))
		{
			Type.remove_prefix(1);
		}
		constexpr std::string_view Expected = "text/event-stream";
		return Type.size() == Expected.size() && std::equal(Type.begin(),
													 Type.end(),
													 Expected.begin(),
													 [](char A, char B)
													 {
														 return (A >= 'A' && A <= 'Z' ? A + ('a' - 'A') : A) == B;
													 });
	}
	struct FSseClient::FImpl
	{
		FImpl(Network::FContext& Context, FSseClientOptions Value)
			: State(N::FAccess::Context(Context)), Client(Context, Value.Service), Options(std::move(Value)), Reader(Options.Parser)
		{
			RetryDelay = Options.ReconnectDelay;
		}
		void Start()
		{
			const auto LastId = Reader.LastEventId();
			Reader.Reset(LastId);
			Pending.clear();
			Offset = 0;
			Head.reset();
			auto Request = Options.Request;
			Network::FError Error;
			if (!Request.Headers.TrySet("Accept", "text/event-stream", Error))
			{
				throw std::invalid_argument("Invalid SSE request");
			}
			Request.Headers.Erase("Last-Event-ID");
			if (!LastId.empty() && !Request.Headers.TrySet("Last-Event-ID", LastId, Error))
			{
				throw std::invalid_argument("Invalid Last-Event-ID");
			}
			Stream = Client.SendStream(std::move(Request), Options.BufferBytes);
			LastActivity = FClock::now();
			Status = ESseClientState::Pending;
		}
		ESseClientState Fail(Network::FError Error)
		{
			Failure = std::move(Error);
			Status = ESseClientState::Failed;
			Stream.Operation().Cancel();
			if (auto Body = Stream.Body())
			{
				Body->Cancel();
			}
			Client.Close();
			return Status;
		}
		ESseClientState End(Network::FError Error)
		{
			const auto Code = Error.Code;
			const bool Retry = !Error || Code == Network::EError::Connect || Code == Network::EError::Resolve ||
							   Code == Network::EError::Read || Code == Network::EError::Write || Code == Network::EError::TimedOut;
			if (N::IsClosed(State) || Code == Network::EError::Closed || Code == Network::EError::Cancelled)
			{
				Status = ESseClientState::Closed;
				return Status;
			}
			if (!Retry || Reconnects >= Options.MaxReconnects)
			{
				if (Error)
				{
					return Fail(std::move(Error));
				}
				Status = ESseClientState::Closed;
				return Status;
			}
			Stream.Operation().Cancel();
			if (auto Body = Stream.Body())
			{
				Body->Cancel();
			}
			if (const auto ServerRetry = Reader.RetryMilliseconds())
			{
				RetryDelay =
					std::chrono::milliseconds(std::min<std::uint64_t>(*ServerRetry, Options.MaxReconnectDelay.count()));
			}
			Next = FClock::now() + std::min(RetryDelay, Options.MaxReconnectDelay);
			Status = ESseClientState::Reconnecting;
			return Status;
		}
		bool CheckWork(FSseWork Work, std::uint64_t Units)
		{
			if (!Work.Check)
			{
				return true;
			}
			try
			{
				if (Work.Check(Work.UserData, Units))
				{
					return true;
				}
			}
			catch (const std::bad_alloc&)
			{
				throw;
			}
			catch (...)
			{
				Fail({Network::EError::Callback, {}, 0});
				return false;
			}
			Fail({Network::EError::Cancelled, {}, 0});
			return false;
		}
		ESseClientState Poll(FSseEvent& Out, FSseWork Check)
		{
			if (Status == ESseClientState::Closed || Status == ESseClientState::Failed)
			{
				return Status;
			}
			if (N::IsClosed(State))
			{
				Close();
				return Status;
			}
			if (!CheckWork(Check, 0))
			{
				return Status;
			}
			if (Status == ESseClientState::Reconnecting)
			{
				if (FClock::now() < Next)
				{
					return Status;
				}
				++Reconnects;
				Start();
			}
			if (!Head)
			{
				FResponse Value;
				Network::FError Error;
				// Read completion first: a head may be published between TryHead
				// and a later completion check, especially for an empty 204 reply.
				const bool Complete = Stream.Operation().IsComplete();
				if (!Stream.TryHead(Value, Error))
				{
					if (Error)
					{
						return End(std::move(Error));
					}
					if (Complete)
					{
						return Fail({Network::EError::Protocol, "SSE response has no head", 0});
					}
					if (Options.IdleTimeout && FClock::now() - LastActivity >= *Options.IdleTimeout)
					{
						return End({Network::EError::TimedOut, "SSE response head timeout", 0});
					}
					return ESseClientState::Pending;
				}
				Head = std::move(Value);
				if (Head->Status == 204)
				{
					Close();
					return Status;
				}
				if (Head->Status != 200 || !EventStreamType(Head->Headers))
				{
					return Fail({Network::EError::Protocol, "SSE requires HTTP 200 and text/event-stream", 0});
				}
			}
			std::uint64_t Work = 0;
			while (Work < Options.MaxPollBytes)
			{
				if (Offset < Pending.size())
				{
					const auto Count = std::min<std::uint64_t>(Pending.size() - Offset, Options.MaxPollBytes - Work);
					if (!CheckWork(Check, 0))
					{
						return Status;
					}
					FSseEvent Event;
					const auto Read = Reader.Feed(std::string_view(Pending).substr(Offset, Count), false, Event);
					Offset += Read.Consumed;
					Work += Read.Consumed;
					// Feed may stop at the first event in this block. Charge only
					// consumed bytes so subsequent Poll calls do not charge them again.
					if (!CheckWork(Check, Read.Consumed))
					{
						return Status;
					}
					if (Read.State == ESseReadState::Failed)
					{
						return Fail(Reader.Error());
					}
					if (Read.State == ESseReadState::EventReady)
					{
						Out = std::move(Event);
						return ESseClientState::EventReady;
					}
					if (Read.Consumed == 0)
					{
						return Fail({Network::EError::State, "SSE parser made no progress", 0});
					}
					continue;
				}
				Pending.clear();
				Offset = 0;
				std::array<char, 8192> Bytes;
				auto Body = Stream.Body();
				if (!Body)
				{
					return End(Stream.Operation().Error());
				}
				const auto Read = Body->Read(Bytes.data(),
					static_cast<std::int64_t>(std::min<std::uint64_t>(Bytes.size(), Options.MaxPollBytes - Work)));
				if (Read.Bytes)
				{
					Pending.assign(Bytes.data(), static_cast<std::size_t>(Read.Bytes));
					LastActivity = FClock::now();
					continue;
				}
				if (Read.State == Network::EStreamState::End || Read.State == Network::EStreamState::Failed)
				{
					Reader.Feed({}, true, Out);
					return Reader.Error() ? Fail(Reader.Error()) : End(Read.Error);
				}
				if (Options.IdleTimeout && FClock::now() - LastActivity >= *Options.IdleTimeout)
				{
					return End({Network::EError::TimedOut, "SSE idle timeout", 0});
				}
				return ESseClientState::Pending;
			}
			return ESseClientState::Pending;
		}
		void Close() noexcept
		{
			if (Status != ESseClientState::Failed)
			{
				Status = ESseClientState::Closed;
			}
			try
			{
				Stream.Operation().Cancel();
				if (auto Body = Stream.Body())
				{
					Body->Cancel();
				}
				Client.Close();
			}
			catch (...)
			{
			}
		}
		std::shared_ptr<N::FContext> State;
		std::shared_ptr<N::FByteLease> Retention;
		FClient Client;
		FSseClientOptions Options;
		FSseReader Reader;
		FStreamingOperation Stream;
		std::optional<FResponse> Head;
		std::string Pending;
		std::chrono::milliseconds RetryDelay{0};
		std::size_t Offset = 0;
		std::uint32_t Reconnects = 0;
		FClock::time_point LastActivity{}, Next{};
		ESseClientState Status = ESseClientState::Pending;
		Network::FError Failure;
	};
	std::shared_ptr<FSseClient> FSseClient::Connect(
		Network::FContext& Context, FSseClientOptions Options, Network::FError& Error)
	{
		Error = {};
		try
		{
			if (Options.Service && !Options.BackendId.empty() && Options.BackendId != Options.Service->GetBackendId())
			{
				throw std::invalid_argument("SSE backend selection mismatch");
			}
			if (Options.Request.Method != "GET" || !Options.Request.Body.empty() || Options.Request.Upload ||
				Options.Request.UploadFactory || Options.Request.UploadLength || Options.Request.Multipart ||
				!Options.MaxPollBytes || Options.MaxPollBytes > 16 * 1024 * 1024 || Options.BufferBytes < 16384 ||
				Options.ReconnectDelay.count() < 0 || Options.MaxReconnectDelay.count() < 0 ||
				Options.MaxReconnectDelay > std::chrono::hours(24 * 365) ||
				(Options.IdleTimeout &&
					(Options.IdleTimeout->count() <= 0 || *Options.IdleTimeout > std::chrono::hours(24 * 365))))
			{
				throw std::invalid_argument("Invalid SSE client options");
			}
			auto Impl = std::make_unique<FImpl>(Context, std::move(Options));
			const auto Limit = N::Options(Impl->State).MaxRetainedBytes;
			std::uint64_t Bytes = 8192;
			const auto Add = [&](std::uint64_t Count)
			{
				if (Count > Limit || Bytes > Limit - Count)
				{
					return false;
				}
				Bytes += Count;
				return true;
			};
			// Conservative capacities for line/ID buffers, event data and head snapshots.
			for (const auto [Count, Copies] : {std::pair<std::uint64_t, unsigned>{Impl->Options.Parser.MaxLineBytes, 6},
					 {Impl->Options.Parser.MaxEventBytes, 2},
					 {Impl->Options.Request.MaxHeaderBytes, 2}})
			{
				if (Count > Limit / Copies || !Add(Count * Copies))
				{
					Error.Code = Network::EError::Limit;
					return {};
				}
			}
			for (const auto Count : {Impl->Options.Request.Url.capacity(),
					 Impl->Options.Request.Method.capacity(),
					 Impl->Options.Request.Proxy.capacity(),
					 Impl->Options.Request.CaFile.capacity()})
			{
				if (!Add(Count))
				{
					Error.Code = Network::EError::Limit;
					return {};
				}
			}
			for (const auto& Field : Impl->Options.Request.Headers.Fields())
			{
				if (!Add(Field.first.capacity()) || !Add(Field.second.capacity()))
				{
					Error.Code = Network::EError::Limit;
					return {};
				}
			}
			Impl->Retention = N::ReserveBytes(Impl->State, Bytes, Error);
			if (!Impl->Retention)
			{
				return {};
			}
			Impl->Start();
			return std::shared_ptr<FSseClient>(new FSseClient(std::move(Impl)));
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = Network::EError::Allocation;
		}
		catch (const std::invalid_argument&)
		{
			Error.Code = Network::EError::InvalidArgument;
		}
		catch (const Detail::FClientCreationError& Failure)
		{
			Error.Code = Failure.Code;
		}
		catch (const std::length_error&)
		{
			Error.Code = Network::EError::Limit;
		}
		catch (const std::exception&)
		{
			Error.Code = Network::EError::Internal;
		}
		return {};
	}

	FSseClient::~FSseClient()
	{
		Close();
	}

	ESseClientState FSseClient::Poll(FSseEvent& Out, FSseWork Work)
	{
		try
		{
			return Impl->Poll(Out, Work);
		}
		catch (const std::bad_alloc&)
		{
			return Impl->Fail({Network::EError::Allocation, {}, 0});
		}
		catch (const std::length_error&)
		{
			return Impl->Fail({Network::EError::Limit, {}, 0});
		}
		catch (const std::invalid_argument&)
		{
			return Impl->Fail({Network::EError::InvalidArgument, {}, 0});
		}
	}

	void FSseClient::Close() noexcept
	{
		Impl->Close();
	}

	const Network::FError& FSseClient::Error() const noexcept
	{
		return Impl->Failure;
	}

	const std::optional<FResponse>& FSseClient::ResponseHead() const noexcept
	{
		return Impl->Head;
	}

	const std::string& FSseClient::LastEventId() const noexcept
	{
		return Impl->Reader.LastEventId();
	}

	std::uint32_t FSseClient::ReconnectCount() const noexcept
	{
		return Impl->Reconnects;
	}

	std::chrono::milliseconds FSseClient::ReconnectRemaining() const noexcept
	{
		return Impl->Status == ESseClientState::Reconnecting
				   ? std::max(std::chrono::milliseconds(0),
						 std::chrono::duration_cast<std::chrono::milliseconds>(Impl->Next - FClock::now()))
				   : std::chrono::milliseconds(0);
	}

	FSseClient::FSseClient(std::unique_ptr<FImpl> Value) : Impl(std::move(Value))
	{
	}
} // namespace VeroScript::Http
