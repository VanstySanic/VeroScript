// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttpSseClient.h"

namespace VeroScript::NetworkBinding
{
	struct FSseClientValue
	{
		FHostObjectHandle Context;
		std::shared_ptr<Http::FSseClient> Client;
		Http::FSseEvent Event;
		Http::ESseClientState State = Http::ESseClientState::Pending;
	};
	bool RegisterHttpSseClient(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		using O = Http::FSseClientOptions;
		FHostTypeId OptionsType, Client;
		if (!ValueType<O>(S, "http::sse_options", OptionsType, Error) || !Type(S, "http::sse_client", Client, Error))
		{
			return false;
		}
		if (!Member(
		        S,
		        Api,
		        "make_sse_options",
		        T::HostObject,
		        {},
		        [&S](const auto&, auto& Out, auto& E)
		{
			return Make(S, "http::sse_options", O{}, Out, E);
		},
		        Error,
		        "http::sse_options",
		        {},
		        true))
		{
			return false;
		}
		if (!Member(S, OptionsType, "set_backend", T::Void, {T::String},
		        [](const auto& C, auto&, auto&)
		{
			This<O>(C).BackendId = String(C, 0);
			return true;
		},
		        Error, "", {}, false, true))
		{
			return false;
		}
		for (const bool WithOptions : {false, true})
		{
			if (!Member(
			        S,
			        Api,
			        WithOptions ? "connect_sse_with_options" : "connect_sse",
			        T::HostObject,
			        WithOptions ? std::vector<T>{T::HostObject, T::String, T::HostObject}
			                    : std::vector<T>{T::HostObject, T::String},
			        [&S, WithOptions](const auto& C, auto& Out, auto& E)
			{
				auto Owner = ContextArg(C);
				if (!Lease(S, Owner, E))
				{
					return false;
				}
				auto Options = WithOptions ? Object<O>(C, 2) : O{};
				Options.Request.Url = String(C, 1);
				Options.Service = HttpClientBackend(S, Options.BackendId, E);
				if (!Options.Service) return false;
				Network::FError Failure;
				auto Value = Http::FSseClient::Connect(Context(Owner), std::move(Options), Failure);
				if (!Value)
				{
					E = Failure.Message.empty() ? "Cannot create SSE client" : Failure.Message;
					return false;
				}
				return Make(S, "http::sse_client", FSseClientValue{Owner, std::move(Value), {}}, Out, E);
			},
			        Error,
			        "http::sse_client",
			        WithOptions ? std::vector<std::string>{"Network::context", "", "http::sse_options"}
			                    : std::vector<std::string>{"Network::context", ""},
			        true))
			{
				return false;
			}
		}
		if (!Member(
		        S,
		        OptionsType,
		        "set_request",
		        T::Void,
		        {T::HostObject},
		        [](const auto& C, auto&, auto&)
		{
			This<O>(C).Request = Object<Http::FRequest>(C, 0);
			return true;
		},
		        Error,
		        "",
		        {"http::request"},
		        false,
		        true) ||
		    !Member(
		        S,
		        OptionsType,
		        "set_limits",
		        T::Void,
		        {T::Int64, T::Int64, T::Int64, T::Int64},
		        [](const auto& C, auto&, auto&)
		{
			auto& V = This<O>(C);
			V.Parser = {Count(Integer(C, 0)), Count(Integer(C, 1))};
			V.BufferBytes = Count(Integer(C, 2));
			V.MaxPollBytes = Count(Integer(C, 3));
			return true;
		},
		        Error,
		        "",
		        {},
		        false,
		        true) ||
		    !Member(
		        S,
		        OptionsType,
		        "set_reconnect",
		        T::Void,
		        {T::Int64, T::Int64, T::Int64},
		        [](const auto& C, auto&, auto& E)
		{
			const auto Delay = Integer(C, 0), Maximum = Integer(C, 1), Attempts = Integer(C, 2);
			if (Delay < 0 || Maximum < 0 || Maximum > 365LL * 24 * 60 * 60 * 1000 || Attempts < 0 ||
			    Attempts > UINT32_MAX)
			{
				E = "Invalid SSE reconnect options";
				return false;
			}
			auto& V = This<O>(C);
			V.ReconnectDelay = std::chrono::milliseconds(Delay);
			V.MaxReconnectDelay = std::chrono::milliseconds(Maximum);
			V.MaxReconnects = static_cast<std::uint32_t>(Attempts);
			return true;
		},
		        Error,
		        "",
		        {},
		        false,
		        true) ||
		    !Member(
		        S,
		        OptionsType,
		        "set_idle_timeout",
		        T::Void,
		        {T::Int64},
		        [](const auto& C, auto&, auto& E)
		{
			const auto Ms = Integer(C, 0);
			if (Ms != -1 && (Ms <= 0 || Ms > 365LL * 24 * 60 * 60 * 1000))
			{
				E = "Invalid SSE idle timeout";
				return false;
			}
			This<O>(C).IdleTimeout =
			    Ms == -1 ? std::nullopt
			             : std::optional<std::chrono::milliseconds>(std::chrono::milliseconds(Ms));
			return true;
		},
		        Error,
		        "",
		        {},
		        false,
		        true))
		{
			return false;
		}
		if (!Member(
		        S,
		        Client,
		        "poll",
		        T::Int64,
		        {},
		        [](const auto& C, auto& Out, auto& E)
		{
			struct FCheckState
			{
				FMemoryWork Work;
				bool bStopped = false;
			};
			FCheckState Check{C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{}};
			const auto Continue = [](void* Data, std::uint64_t Units)
			{
				auto& State = *static_cast<FCheckState*>(Data);
				const bool Result = !State.Work.Check || State.Work.Check(State.Work.UserData, Units);
				State.bStopped = !Result;
				return Result;
			};
			if (!Continue(&Check, 0))
			{
				E = "SSE processing cancelled";
				return false;
			}
			auto& V = This<FSseClientValue>(C);
			V.State = V.Client->Poll(V.Event, {Continue, &Check});
			if (Check.bStopped)
			{
				E = "SSE processing cancelled";
				return false;
			}
			Out = static_cast<std::int64_t>(V.State);
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Client,
		        "close",
		        T::Void,
		        {},
		        [](const auto& C, auto&, auto&)
		{
			auto& V = This<FSseClientValue>(C);
			V.Client->Close();
			V.State = Http::ESseClientState::Closed;
			return true;
		},
		        Error))
		{
			return false;
		}
		for (const char* Name : {"event_data", "event_type", "event_id", "last_event_id", "error", "message_id"})
		{
			if (!Member(
			        S,
			        Client,
			        Name,
			        T::String,
			        {},
			        [Name](const auto& C, auto& Out, auto& E)
			{
				const auto& V = This<FSseClientValue>(C);
				const std::string_view Field = Name;
				if (Field == "last_event_id")
				{
					Out = V.Client->LastEventId();
				}
				else if (Field == "error")
				{
					Out = V.Client->Error().Message;
				}
				else if (Field == "message_id")
				{
					Out = std::string(V.Client->Error().Id());
				}
				else
				{
					if (V.State != Http::ESseClientState::EventReady)
					{
						E = "SSE client has no current event";
						return false;
					}
					Out = Field == "event_data" ? V.Event.Data
					    : Field == "event_type" ? V.Event.Event
					                            : V.Event.Id;
				}
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		for (const char* Name : {"error_code", "response_status", "reconnect_count", "reconnect_remaining"})
		{
			if (!Member(
			        S,
			        Client,
			        Name,
			        T::Int64,
			        {},
			        [Name](const auto& C, auto& Out, auto&)
			{
				const auto& V = *This<FSseClientValue>(C).Client;
				const std::string_view Field = Name;
				Out = Field == "error_code" ? static_cast<std::int64_t>(V.Error().Code)
				    : Field == "response_status"
				    ? static_cast<std::int64_t>(V.ResponseHead() ? V.ResponseHead()->Status : 0)
				    : Field == "reconnect_count" ? static_cast<std::int64_t>(V.ReconnectCount())
				                                 : V.ReconnectRemaining().count();
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::NetworkBinding
