// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "WebSocketBinding.h"
#include "WebSocketBinding.h"
#include "NetworkTaskBinding.h"
#include "VeroScriptWebSocketReconnect.h"

namespace VeroScript::NetworkBinding
{
	struct FReconnectSnapshotValue
	{
		std::shared_ptr<const WebSocket::FReconnectSnapshot> Value;
	};
	struct FReconnectAttemptValue
	{
		std::shared_ptr<const WebSocket::FReconnectSnapshot> Owner;
		std::size_t Index = 0;
	};

	bool RegisterWebSocketReconnect(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		using P = WebSocket::FReconnectPolicy;
		using Task = WebSocket::FReconnectTask;
		FHostTypeId Policy, TaskType, Snapshot, Attempt;
		if (!ValueType<P>(S, "websocket::reconnect_policy", Policy, Error) ||
			!Type(S, "websocket::reconnect_task", TaskType, Error) ||
			!Type(S, "websocket::reconnect_snapshot", Snapshot, Error) ||
			!Type(S, "websocket::reconnect_attempt", Attempt, Error))
		{
			return false;
		}
		if (!Member(
				S,
				Api,
				"make_reconnect_policy",
				T::HostObject,
				{},
				[&S](const auto&, auto& Out, auto& E)
				{
					return Make(S, "websocket::reconnect_policy", P{}, Out, E);
				},
				Error,
				"websocket::reconnect_policy",
				{},
				true))
		{
			return false;
		}
		static constexpr std::int64_t Year = 365LL * 24 * 60 * 60 * 1000;
		if (!Member(
				S,
				Policy,
				"set_limits",
				T::Void,
				{T::Int64, T::Int64},
				[](const auto& C, auto&, auto& E)
				{
					const auto Attempts = Integer(C, 0), Elapsed = Integer(C, 1);
					if (Attempts < 1 || Attempts > 32 || Elapsed <= 0 || Elapsed > Year)
					{
						E = "Invalid WebSocket reconnect limits";
						return false;
					}
					auto& Value = This<P>(C);
					Value.MaxAttempts = static_cast<std::uint32_t>(Attempts);
					Value.MaxElapsed = std::chrono::milliseconds(Elapsed);
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				Policy,
				"set_backoff",
				T::Void,
				{T::Int64, T::Int64, T::Bool},
				[](const auto& C, auto&, auto& E)
				{
					const auto Delay = Integer(C, 0), Maximum = Integer(C, 1);
					if (Delay < 0 || Maximum < Delay || Maximum > Year)
					{
						E = "Invalid WebSocket reconnect backoff";
						return false;
					}
					auto& Value = This<P>(C);
					Value.Delay = std::chrono::milliseconds(Delay);
					Value.MaxDelay = std::chrono::milliseconds(Maximum);
					Value.bExponentialBackoff = std::get<bool>(C.Args[2]);
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				Policy,
				"set_retry_protocol_errors",
				T::Void,
				{T::Bool},
				[](const auto& C, auto&, auto&)
				{
					This<P>(C).bRetryProtocolErrors = std::get<bool>(C.Args[0]);
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				Policy,
				"max_attempts",
				T::Int64,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = std::int64_t(This<P>(C).MaxAttempts);
					return true;
				},
				Error) ||
			!Member(
				S,
				Policy,
				"max_elapsed_ms",
				T::Int64,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = std::int64_t(This<P>(C).MaxElapsed.count());
					return true;
				},
				Error))
		{
			return false;
		}
		if (!Member(
				S,
				Api,
				"connect_with_retry",
				T::HostObject,
				{T::HostObject, T::String, T::HostObject, T::HostObject},
				[&S](const auto& C, auto& Out, auto& E)
				{
					auto Owner = ContextArg(C);
					if (!Lease(S, Owner, E) || !CheckWork(C, String(C, 1).size(), E))
					{
						return false;
					}
					return StartTask<Task>(
						S,
						"websocket::reconnect_task",
						Owner,
						[&]
						{
							return WebSocket::ConnectWithRetry(
								Context(Owner), String(C, 1), Object<P>(C, 3), WebSocketBackend(S, Object<WebSocket::FOptions>(C, 2), false, E));
						},
						Out,
						E);
				},
				Error,
				"websocket::reconnect_task",
				{"Network::context", "", "websocket::options", "websocket::reconnect_policy"},
				true) ||
			!Member(
				S,
				TaskType,
				"operation",
				T::HostObject,
				{},
				[&S](const auto& C, auto& Out, auto& E)
				{
					const auto& Value = This<FHandle<Task>>(C);
					return Make(
						S, "websocket::operation", FOperationValue{Value.Context, Value.Value->Operation()}, Out, E);
				},
				Error,
				"websocket::operation") ||
			!Member(
				S,
				TaskType,
				"snapshot",
				T::HostObject,
				{},
				[&S](const auto& C, auto& Out, auto& E)
				{
					Network::FError Failure;
					auto Value = This<FHandle<Task>>(C).Value->Snapshot(Failure);
					if (!Value)
					{
						E = "WebSocket reconnect snapshot is unavailable";
						return false;
					}
					std::uint64_t Work = 1;
					for (const auto& A : Value->Attempts)
					{
						Work += 1 + A.Error.Message.size();
					}
					if (!CheckWork(C, Work + Value->Error.Message.size(), E))
					{
						return false;
					}
					return Make(S, "websocket::reconnect_snapshot", FReconnectSnapshotValue{std::move(Value)}, Out, E);
				},
				Error,
				"websocket::reconnect_snapshot"))
		{
			return false;
		}
		for (const auto* Name : {"generation", "size", "error_code", "system_code"})
		{
			if (!Member(
					S,
					Snapshot,
					Name,
					T::Int64,
					{},
					[Name](const auto& C, auto& Out, auto&)
					{
						const auto& V = *This<FReconnectSnapshotValue>(C).Value;
						const auto N = std::string_view(Name);
						Out = N == "generation"	   ? Count(V.Generation)
							  : N == "size"		   ? Count(V.Attempts.size())
							  : N == "system_code" ? std::int64_t(V.Error.SystemCode)
												   : std::int64_t(V.Error.Code);
						return true;
					},
					Error))
			{
				return false;
			}
		}
		if (!Member(
				S,
				Snapshot,
				"is_final",
				T::Bool,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = This<FReconnectSnapshotValue>(C).Value->bFinal;
					return true;
				},
				Error) ||
			!Member(
				S,
				Snapshot,
				"error",
				T::String,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = This<FReconnectSnapshotValue>(C).Value->Error.Message;
					return true;
				},
				Error) ||
			!Member(
				S,
				Snapshot,
				"message_id",
				T::String,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = std::string(This<FReconnectSnapshotValue>(C).Value->Error.Id());
					return true;
				},
				Error) ||
			!Member(
				S,
				Snapshot,
				"message_truncated",
				T::Bool,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = This<FReconnectSnapshotValue>(C).Value->bMessageTruncated;
					return true;
				},
				Error) ||
			!Member(
				S,
				Snapshot,
				"at",
				T::HostObject,
				{T::Int64},
				[&S](const auto& C, auto& Out, auto& E)
				{
					auto Owner = This<FReconnectSnapshotValue>(C).Value;
					const auto Index = Integer(C, 0);
					if (Index < 0 || static_cast<std::uint64_t>(Index) >= Owner->Attempts.size())
					{
						E = "WebSocket reconnect attempt index is out of range";
						return false;
					}
					return Make(S,
						"websocket::reconnect_attempt",
						FReconnectAttemptValue{std::move(Owner), static_cast<std::size_t>(Index)},
						Out,
						E);
				},
				Error,
				"websocket::reconnect_attempt"))
		{
			return false;
		}
		for (const auto* Name : {"number", "error_code", "system_code"})
		{
			if (!Member(
					S,
					Attempt,
					Name,
					T::Int64,
					{},
					[Name](const auto& C, auto& Out, auto&)
					{
						const auto& V = This<FReconnectAttemptValue>(C);
						const auto& A = V.Owner->Attempts[V.Index];
						const auto N = std::string_view(Name);
						Out = N == "number"		   ? std::int64_t(A.Number)
							  : N == "system_code" ? std::int64_t(A.Error.SystemCode)
												   : std::int64_t(A.Error.Code);
						return true;
					},
					Error))
			{
				return false;
			}
		}
		for (const auto* Name : {"complete", "connected", "message_truncated"})
		{
			if (!Member(
					S,
					Attempt,
					Name,
					T::Bool,
					{},
					[Name](const auto& C, auto& Out, auto&)
					{
						const auto& V = This<FReconnectAttemptValue>(C);
						const auto& A = V.Owner->Attempts[V.Index];
						const auto N = std::string_view(Name);
						Out = N == "complete" ? A.bComplete : N == "connected" ? A.bConnected : A.bMessageTruncated;
						return true;
					},
					Error))
			{
				return false;
			}
		}
		return Member(
				   S,
				   Attempt,
				   "message_id",
				   T::String,
				   {},
				   [](const auto& C, auto& Out, auto&)
				   {
					   const auto& V = This<FReconnectAttemptValue>(C);
					   Out = std::string(V.Owner->Attempts[V.Index].Error.Id());
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Attempt,
				   "error",
				   T::String,
				   {},
				   [](const auto& C, auto& Out, auto&)
				   {
					   const auto& V = This<FReconnectAttemptValue>(C);
					   Out = V.Owner->Attempts[V.Index].Error.Message;
					   return true;
				   },
				   Error);
	}
} // namespace VeroScript::NetworkBinding
