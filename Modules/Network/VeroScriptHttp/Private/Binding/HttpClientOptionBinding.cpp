// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttp.h"

namespace VeroScript::NetworkBinding
{
	struct FCookieValue
	{
		std::shared_ptr<const Http::FCookies> Value;
	};
	using FAttemptValue = FHttpAttemptValue;

	struct FHistoryValue
	{
		std::shared_ptr<Http::FAttemptHistory> Value;
	};
	struct FHistorySnapshotValue
	{
		std::shared_ptr<const Http::FAttemptSnapshot> Value;
	};
	static bool RegisterHistory(FState& S, FHostTypeId Api, FHostTypeId Request, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId History, Snapshot;
		if (!Type(S, "http::attempt_history", History, Error) || !Type(S, "http::attempt_snapshot", Snapshot, Error))
		{
			return false;
		}
		if (!Member(
		        S,
		        Api,
		        "make_attempt_history",
		        T::HostObject,
		        {},
		        [&S](const auto&, auto& Out, auto& E)
		{
			return Make(
			    S, "http::attempt_history", FHistoryValue{std::make_shared<Http::FAttemptHistory>()}, Out, E);
		},
		        Error,
		        "http::attempt_history",
		        {},
		        true) ||
		    !Member(
		        S,
		        Request,
		        "set_attempt_history",
		        T::Void,
		        {T::HostObject},
		        [](const auto& C, auto&, auto&)
		{
			This<Http::FRequest>(C).History = Object<FHistoryValue>(C, 0).Value;
			return true;
		},
		        Error,
		        "",
		        {"http::attempt_history"},
		        false,
		        true) ||
		    !Member(
		        S,
		        History,
		        "snapshot",
		        T::HostObject,
		        {},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			return Make(S,
			    "http::attempt_snapshot",
			    FHistorySnapshotValue{This<FHistoryValue>(C).Value->Snapshot()},
			    Out,
			    E);
		},
		        Error,
		        "http::attempt_snapshot"))
		{
			return false;
		}
		for (const std::string Name :
		    {"is_final", "is_truncated", "attempt_count", "error_code", "system_code", "error", "message_id"})
		{
			const auto Kind = Name == "is_final" || Name == "is_truncated" ? T::Bool
			    : Name == "error" || Name == "message_id"                  ? T::String
			                                                               : T::Int64;
			if (!Member(
			        S,
			        Snapshot,
			        Name.c_str(),
			        Kind,
			        {},
			        [Name](const auto& C, auto& Out, auto&)
			{
				const auto& V = *This<FHistorySnapshotValue>(C).Value;
				if (Name == "is_final")
				{
					Out = V.bFinal;
				}
				else if (Name == "is_truncated")
				{
					Out = V.bTruncated;
				}
				else if (Name == "error")
				{
					Out = V.Error.Message;
				}
				else if (Name == "message_id")
				{
					Out = std::string(V.Error.Id());
				}
				else if (Name == "attempt_count")
				{
					Out = static_cast<std::int64_t>(V.Attempts.size());
				}
				else if (Name == "system_code")
				{
					Out = static_cast<std::int64_t>(V.Error.SystemCode);
				}
				else
				{
					Out = static_cast<std::int64_t>(V.Error.Code);
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
		    Snapshot,
		    "attempt",
		    T::HostObject,
		    {T::Int64},
		    [&S](const auto& C, auto& Out, auto& E)
		{
			auto V = This<FHistorySnapshotValue>(C).Value;
			const auto Index = Count(Integer(C, 0));
			if (Index >= V->Attempts.size())
			{
				throw std::range_error("HTTP attempt index is out of range");
			}
			return Make(
			    S, "http::attempt", FAttemptValue{V, &V->Attempts[static_cast<std::size_t>(Index)]}, Out, E);
		},
		    Error,
		    "http::attempt");
	}

	bool RegisterHttpClientOptions(FState& S, FHostTypeId Api, FHostTypeId Client, FHostTypeId Request,
	    FHostTypeId Response, FHostTypeId Op, std::string& Error)
	{
		using T = ENativeType;
		using O = Http::FClientOptions;
		using R = Http::FRetryOptions;
		FHostTypeId OptionsType, RetryType, Cookies, Attempt;
		if (!ValueType<O>(S, "http::client_options", OptionsType, Error) ||
		    !ValueType<R>(S, "http::retry_options", RetryType, Error) || !Type(S, "http::cookies", Cookies, Error) ||
		    !Type(S, "http::attempt", Attempt, Error) || !RegisterHttpTrace(S, Response, Attempt, Error))
		{
			return false;
		}
		if (!Member(
		        S,
		        Api,
		        "make_client_options",
		        T::HostObject,
		        {},
		        [&S](const auto&, auto& Out, auto& E)
		{
			return Make(S, "http::client_options", O{}, Out, E);
		},
		        Error,
		        "http::client_options",
		        {},
		        true) ||
		    !Member(
		        S,
		        Api,
		        "make_retry_options",
		        T::HostObject,
		        {},
		        [&S](const auto&, auto& Out, auto& E)
		{
			return Make(S, "http::retry_options", R{}, Out, E);
		},
		        Error,
		        "http::retry_options",
		        {},
		        true) ||
		    !Member(
		        S,
		        Api,
		        "create_client_with_options",
		        T::HostObject,
		        {T::HostObject, T::HostObject},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			auto Owner = ContextArg(C);
			if (!Lease(S, Owner, E))
			{
				return false;
			}
			auto Backend = HttpClientBackend(S, Object<O>(C, 1).BackendId, E);
			if (!Backend) return false;
			return Make(S,
			    "http::client",
			    FHandle<Http::FClient>{Owner, std::make_shared<Http::FClient>(Context(Owner), std::move(Backend), Object<O>(C, 1))},
			    Out,
			    E);
		},
		        Error,
		        "http::client",
		        {"Network::context", "http::client_options"},
		        true) ||
		    !Member(
		        S,
		        Request,
		        "set_retry_options",
		        T::Void,
		        {T::HostObject},
		        [](const auto& C, auto&, auto&)
		{
			This<Http::FRequest>(C).Retry = Object<R>(C, 0);
			return true;
		},
		        Error,
		        "",
		        {"http::retry_options"},
		        false,
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
		for (const std::string Name : {"cookies", "inherit_proxy_environment"})
		{
			auto Field = [Name](O& V) -> bool&
			{
				return Name == "cookies" ? V.bCookies : V.bInheritProxyEnvironment;
			};
			if (!Member(
			        S,
			        OptionsType,
			        Name.c_str(),
			        T::Bool,
			        {},
			        [Field](const auto& C, auto& Out, auto&)
			{
				Out = Field(This<O>(C));
				return true;
			},
			        Error) ||
			    !Member(
			        S,
			        OptionsType,
			        ("set_" + Name).c_str(),
			        T::Void,
			        {T::Bool},
			        [Field](const auto& C, auto&, auto&)
			{
				Field(This<O>(C)) = std::get<bool>(C.Args[0]);
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
		}
		for (const std::string Name : {"max_connections",
		         "max_connections_per_host",
		         "max_idle_connections",
		         "max_pending_requests",
		         "max_connection_idle_seconds",
		         "max_cookie_bytes"})
		{
			if (!Member(
			        S,
			        OptionsType,
			        Name.c_str(),
			        T::Int64,
			        {},
			        [Name](const auto& C, auto& Out, auto&)
			{
				const auto& V = This<O>(C);
				std::uint64_t N = Name == "max_connections" ? V.MaxConnections
				    : Name == "max_connections_per_host"    ? V.MaxConnectionsPerHost
				    : Name == "max_idle_connections"        ? V.MaxIdleConnections
				    : Name == "max_pending_requests"        ? V.MaxPendingRequests
				    : Name == "max_cookie_bytes"
				    ? V.MaxCookieBytes
				    : static_cast<std::uint64_t>(V.MaxConnectionIdleTime.count());
				if (N > INT64_MAX)
				{
					throw std::range_error("HTTP client option exceeds int64");
				}
				Out = static_cast<std::int64_t>(N);
				return true;
			},
			        Error) ||
			    !Member(
			        S,
			        OptionsType,
			        ("set_" + Name).c_str(),
			        T::Void,
			        {T::Int64},
			        [Name](const auto& C, auto&, auto&)
			{
				const auto N = Integer(C, 0);
				const bool Wide = Name == "max_cookie_bytes" || Name == "max_connection_idle_seconds";
				if (N < 0 || (!Wide && static_cast<std::uint64_t>(N) > UINT32_MAX))
				{
					throw std::range_error("HTTP client option is out of range");
				}
				auto& V = This<O>(C);
				if (Name == "max_connections")
				{
					V.MaxConnections = static_cast<std::uint32_t>(N);
				}
				else if (Name == "max_connections_per_host")
				{
					V.MaxConnectionsPerHost = static_cast<std::uint32_t>(N);
				}
				else if (Name == "max_idle_connections")
				{
					V.MaxIdleConnections = static_cast<std::uint32_t>(N);
				}
				else if (Name == "max_pending_requests")
				{
					V.MaxPendingRequests = static_cast<std::uint32_t>(N);
				}
				else if (Name == "max_cookie_bytes")
				{
					V.MaxCookieBytes = static_cast<std::uint64_t>(N);
				}
				else
				{
					V.MaxConnectionIdleTime = std::chrono::seconds(N);
				}
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
		}
		for (const std::string Name :
		    {"allow_replay", "allow_unsafe_methods", "honor_retry_after", "retry_transport_errors"})
		{
			auto Field = [Name](R& V) -> bool&
			{
				return Name == "allow_replay"        ? V.bAllowReplay
				    : Name == "allow_unsafe_methods" ? V.bAllowUnsafeMethods
				    : Name == "honor_retry_after"    ? V.bHonorRetryAfter
				                                     : V.bRetryTransportErrors;
			};
			if (!Member(
			        S,
			        RetryType,
			        Name.c_str(),
			        T::Bool,
			        {},
			        [Field](const auto& C, auto& Out, auto&)
			{
				Out = Field(This<R>(C));
				return true;
			},
			        Error) ||
			    !Member(
			        S,
			        RetryType,
			        ("set_" + Name).c_str(),
			        T::Void,
			        {T::Bool},
			        [Field](const auto& C, auto&, auto&)
			{
				Field(This<R>(C)) = std::get<bool>(C.Args[0]);
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
		}
		for (const std::string Name : {"max_attempts", "delay_ms", "max_delay_ms"})
		{
			if (!Member(
			        S,
			        RetryType,
			        Name.c_str(),
			        T::Int64,
			        {},
			        [Name](const auto& C, auto& Out, auto&)
			{
				const auto& V = This<R>(C);
				Out = Name == "max_attempts" ? static_cast<std::int64_t>(V.MaxAttempts)
				    : Name == "delay_ms"     ? V.Delay.count()
				                             : V.MaxDelay.count();
				return true;
			},
			        Error) ||
			    !Member(
			        S,
			        RetryType,
			        ("set_" + Name).c_str(),
			        T::Void,
			        {T::Int64},
			        [Name](const auto& C, auto&, auto&)
			{
				const auto N = Integer(C, 0);
				if (N < 0 || N > 24LL * 60 * 60 * 1000 || (Name == "max_attempts" && (N < 1 || N > 32)))
				{
					throw std::range_error("HTTP retry option is out of range");
				}
				auto& V = This<R>(C);
				if (Name == "max_attempts")
				{
					V.MaxAttempts = static_cast<std::uint32_t>(N);
				}
				else if (Name == "delay_ms")
				{
					V.Delay = std::chrono::milliseconds(N);
				}
				else
				{
					V.MaxDelay = std::chrono::milliseconds(N);
				}
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
		}
		if (!Member(
		        S,
		        RetryType,
		        "clear_statuses",
		        T::Void,
		        {},
		        [](const auto& C, auto&, auto&)
		{
			This<R>(C).Statuses.clear();
			return true;
		},
		        Error,
		        "",
		        {},
		        false,
		        true) ||
		    !Member(
		        S,
		        RetryType,
		        "add_status",
		        T::Void,
		        {T::Int64},
		        [](const auto& C, auto&, auto&)
		{
			const auto N = Integer(C, 0);
			if (N < 100 || N > 599)
			{
				throw std::range_error("HTTP status is out of range");
			}
			auto& V = This<R>(C).Statuses;
			if (std::find(V.begin(), V.end(), N) == V.end())
			{
				if (V.size() >= 64)
				{
					throw std::length_error("Too many HTTP retry statuses");
				}
				V.push_back(static_cast<std::uint32_t>(N));
			}
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
		for (const unsigned Command : {0u, 1u, 2u, 3u})
		{
			const auto Name = Command == 0 ? "cookies"
			    : Command == 1             ? "clear_cookies"
			    : Command == 2             ? "import_cookies"
			                               : "import_cookie_record";
			const std::vector<T> Params = Command < 2
			    ? std::vector<T>{T::Int64}
			    : std::vector<T>{Command == 2 ? T::HostObject : T::String, T::Int64};
			if (!Member(
			        S,
			        Client,
			        Name,
			        T::HostObject,
			        Params,
			        [&S, Command](const auto& C, auto& Out, auto& E)
			{
				auto& V = This<FHandle<Http::FClient>>(C);
				const auto Limit = Options(Integer(C, Command < 2 ? 0 : 1));
				return Start(
				    S,
				    "http::operation",
				    V.Context,
				    [&](auto&)
				{
					if (Command == 0)
					{
						return V.Value->Cookies(Limit);
					}
					if (Command == 1)
					{
						return V.Value->ClearCookies(Limit);
					}
					Http::FCookies Input =
					    Command == 2 ? *Object<FCookieValue>(C, 0).Value : Http::FCookies{{String(C, 0)}};
					return V.Value->ImportCookies(std::move(Input), Limit);
				},
				    Out,
				    E);
			},
			        Error,
			        "http::operation",
			        Command == 2 ? std::vector<std::string>{"http::cookies", ""} : std::vector<std::string>{},
			        false,
			        true))
			{
				return false;
			}
		}
		if (!Member(
		        S,
		        Op,
		        "cookies",
		        T::HostObject,
		        {},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			return Make(
			    S, "http::cookies", FCookieValue{Result<Http::FCookies>(This<FOperationValue>(C))}, Out, E);
		},
		        Error,
		        "http::cookies") ||
		    !Member(
		        S,
		        Cookies,
		        "size",
		        T::Int64,
		        {},
		        [](const auto& C, auto& Out, auto&)
		{
			Out = static_cast<std::int64_t>(This<FCookieValue>(C).Value->Records.size());
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Cookies,
		        "record",
		        T::String,
		        {T::Int64},
		        [](const auto& C, auto& Out, auto&)
		{
			const auto& V = This<FCookieValue>(C).Value->Records;
			const auto Index = Count(Integer(C, 0));
			if (Index >= V.size())
			{
				throw std::range_error("Cookie record index is out of range");
			}
			Out = V[static_cast<std::size_t>(Index)];
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Response,
		        "attempt_count",
		        T::Int64,
		        {},
		        [](const auto& C, auto& Out, auto&)
		{
			Out = static_cast<std::int64_t>(This<FHttpResponseValue>(C).Value->Attempts.size());
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Response,
		        "attempt",
		        T::HostObject,
		        {T::Int64},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			auto V = This<FHttpResponseValue>(C).Value;
			const auto Index = Count(Integer(C, 0));
			if (Index >= V->Attempts.size())
			{
				throw std::range_error("HTTP attempt index is out of range");
			}
			return Make(
			    S, "http::attempt", FAttemptValue{V, &V->Attempts[static_cast<std::size_t>(Index)]}, Out, E);
		},
		        Error,
		        "http::attempt"))
		{
			return false;
		}
		for (const std::string Name : {"number",
		         "status",
		         "transport_code",
		         "error_code",
		         "uploaded_bytes",
		         "downloaded_bytes",
		         "effective_url",
		         "message_id"})
		{
			if (!Member(
			        S,
			        Attempt,
			        Name.c_str(),
			        Name == "effective_url" || Name == "message_id" ? T::String : T::Int64,
			        {},
			        [Name](const auto& C, auto& Out, auto&)
			{
				const auto& V = This<FAttemptValue>(C);
				const auto& A = *V.Value;
				if (Name == "effective_url")
				{
					Out = A.EffectiveUrl;
					return true;
				}
				if (Name == "message_id")
				{
					Network::FError Failure;
					Failure.Code = A.ErrorCode;
					Out = std::string(Failure.Id());
					return true;
				}
				const auto N = Name == "number" ? A.Number
				    : Name == "status"          ? A.Status
				    : Name == "transport_code"  ? static_cast<std::uint64_t>(A.TransportCode)
				    : Name == "error_code"      ? static_cast<std::uint64_t>(A.ErrorCode)
				    : Name == "uploaded_bytes"  ? A.UploadedBytes
				                                : A.DownloadedBytes;
				if (N > INT64_MAX)
				{
					throw std::range_error("HTTP attempt count exceeds int64");
				}
				Out = static_cast<std::int64_t>(N);
				return true;
			},
			        Error))
			{
				return false;
			}
		}

		for (const std::string Name :
		    {"set_total_body_limit", "set_total_header_limit", "set_total_upload_limit", "set_redirect_limit"})
		{
			if (!Member(
			        S,
			        Request,
			        Name.c_str(),
			        T::Void,
			        {T::Int64},
			        [Name](const auto& C, auto&, auto&)
			{
				auto& V = This<Http::FRequest>(C);
				const auto N = Count(Integer(C, 0));
				if (Name == "set_total_body_limit")
				{
					V.MaxTotalBodyBytes = N;
				}
				else if (Name == "set_total_header_limit")
				{
					V.MaxTotalHeaderBytes = N;
				}
				else if (Name == "set_redirect_limit")
				{
					if (N > UINT32_MAX)
					{
						throw std::range_error("HTTP redirect limit exceeds uint32");
					}
					V.MaxRedirects = static_cast<std::uint32_t>(N);
				}
				else
				{
					V.MaxTotalUploadBytes = N;
				}
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
		}
		return RegisterHistory(S, Api, Request, Error);
	}
} // namespace VeroScript::NetworkBinding
