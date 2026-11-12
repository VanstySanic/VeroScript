// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttpSse.h"

namespace VeroScript::NetworkBinding
{
	struct FScriptSse
	{
		explicit FScriptSse(Http::FSseOptions Options = {}) : Reader(Options)
		{
		}
		Http::FSseReader Reader;
		Http::FSseEvent Event;
		Http::ESseReadState State = Http::ESseReadState::NeedInput;
	};

	bool RegisterHttpSse(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		if (!RegisterHttpSseClient(S, Api, Error))
		{
			return false;
		}
		FHostTypeId Reader;
		if (!Type(S, "http::sse_reader", Reader, Error))
		{
			return false;
		}
		for (const bool bLimits : {false, true})
		{
			if (!Member(
			        S,
			        Api,
			        bLimits ? "make_sse_reader_with_limits" : "make_sse_reader",
			        T::HostObject,
			        bLimits ? std::vector<T>{T::Int64, T::Int64} : std::vector<T>{},
			        [&S, bLimits](const auto& C, auto& O, auto& E)
			{
				Http::FSseOptions Options;
				if (bLimits)
				{
					Options = {Count(Integer(C, 0)), Count(Integer(C, 1))};
				}
				return Make(S, "http::sse_reader", FScriptSse{Options}, O, E);
			},
			        Error,
			        "http::sse_reader",
			        {},
			        true))
			{
				return false;
			}
		}
		for (const auto* Name : {"event_data", "event_type", "event_id", "last_event_id", "error", "message_id"})
		{
			const std::string_view Field = Name;
			if (!Member(
			        S,
			        Reader,
			        Name,
			        T::String,
			        {},
			        [Field](const auto& C, auto& O, auto&)
			{
				const auto& R = This<FScriptSse>(C);
				if (Field == "last_event_id")
				{
					O = R.Reader.LastEventId();
				}
				else if (Field == "error")
				{
					O = R.Reader.Error().Message;
				}
				else if (Field == "message_id")
				{
					O = std::string(R.Reader.Error().Id());
				}
				else
				{
					if (R.State != Http::ESseReadState::EventReady)
					{
						throw std::logic_error("SSE reader has no current event");
					}
					O = Field == "event_data"   ? R.Event.Data
					    : Field == "event_type" ? R.Event.Event
					                            : R.Event.Id;
				}
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		return Member(
		           S,
		           Reader,
		           "feed",
		           T::Int64,
		           {T::String, T::Bool},
		           [](const auto& C, auto& O, auto& E)
		{
			auto& R = This<FScriptSse>(C);
			const auto& Text = String(C, 0);
			const bool bFinal = std::get<bool>(C.Args[1]);
			const auto Work = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
			std::size_t Offset = 0;
			do
			{
				const auto Count = std::min<std::size_t>(4096, Text.size() - Offset);
				if (Work.Check && !Work.Check(Work.UserData, Count))
				{
					E = "SSE processing cancelled";
					return false;
				}
				const auto Read = R.Reader.Feed(std::string_view(Text).substr(Offset, Count),
				    bFinal && Offset + Count == Text.size(),
				    R.Event);
				R.State = Read.State;
				Offset += Read.Consumed;
				if (R.State != Http::ESseReadState::NeedInput)
				{
					break;
				}
			}
			while (Offset < Text.size());
			O = static_cast<std::int64_t>(Offset);
			return true;
		},
		           Error,
		           "",
		           {},
		           false,
		           true) &&
		    Member(
		        S,
		        Reader,
		        "state",
		        T::Int64,
		        {},
		        [](const auto& C, auto& O, auto&)
		{
			O = static_cast<std::int64_t>(This<FScriptSse>(C).State);
			return true;
		},
		        Error) &&
		    Member(
		        S,
		        Reader,
		        "error_code",
		        T::Int64,
		        {},
		        [](const auto& C, auto& O, auto&)
		{
			O = static_cast<std::int64_t>(This<FScriptSse>(C).Reader.Error().Code);
			return true;
		},
		        Error) &&
		    Member(
		        S,
		        Reader,
		        "retry_ms",
		        T::Int64,
		        {},
		        [](const auto& C, auto& O, auto&)
		{
			const auto Retry = This<FScriptSse>(C).Reader.RetryMilliseconds();
			if (Retry && *Retry > INT64_MAX)
			{
				throw std::range_error("SSE retry interval exceeds int64");
			}
			O = Retry ? static_cast<std::int64_t>(*Retry) : std::int64_t{-1};
			return true;
		},
		        Error) &&
		    Member(
		        S,
		        Reader,
		        "reset",
		        T::Void,
		        {},
		        [](const auto& C, auto&, auto&)
		{
			auto& R = This<FScriptSse>(C);
			R.Reader.Reset();
			R.Event = {};
			R.State = Http::ESseReadState::NeedInput;
			return true;
		},
		        Error,
		        "",
		        {},
		        false,
		        true) &&
		    Member(
		        S,
		        Api,
		        "encode_sse",
		        T::String,
		        {T::String, T::String, T::String},
		        [](const auto& C, auto& O, auto& E)
		{
			Network::FError Failure;
			std::string Text;
			if (!Http::TryEncodeSseEvent({String(C, 0), String(C, 1), String(C, 2)}, Text, Failure))
			{
				E = Failure.Message;
				return false;
			}
			O = std::move(Text);
			return true;
		},
		        Error,
		        "",
		        {},
		        true) &&
		    Member(
		        S,
		        Api,
		        "encode_sse_comment",
		        T::String,
		        {T::String},
		        [](const auto& C, auto& O, auto& E)
		{
			Network::FError Failure;
			std::string Text;
			if (!Http::TryEncodeSseComment(String(C, 0), Text, Failure))
			{
				E = Failure.Message;
				return false;
			}
			O = std::move(Text);
			return true;
		},
		        Error,
		        "",
		        {},
		        true);
	}
} // namespace VeroScript::NetworkBinding
