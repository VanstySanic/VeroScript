// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "NetworkTaskBinding.h"
#include "VeroScriptHttp.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterHttpServerStreams(FState& S, FHostTypeId Exchange, std::string& Error)
	{
		using T = ENativeType;
		using FReplyValue = FHandle<Http::FResponseOperation>;
		FHostTypeId Stream;
		if (!Type(S, "http::response_stream_operation", Stream, Error))
		{
			return false;
		}
		if (!Member(
				S,
				Exchange,
				"reply_stream",
				T::HostObject,
				{T::HostObject, T::HostObject, T::Int64, T::Int64},
				[&S](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<FHandle<Http::FExchange>>(C);
					const auto& Source = Object<FReadStreamValue>(C, 1);
					const auto Length = Integer(C, 2);
					if (Length < -1)
					{
						E = "Response length must be -1 or nonnegative";
						return false;
					}
					if (Source.Context != V.Context)
					{
						E = "Response stream belongs to another network context";
						return false;
					}
					if (!Lease(S, V.Context, E))
					{
						return false;
					}
					Http::FBodySource Body{Source.Value,
						Length < 0 ? std::nullopt : std::optional<std::uint64_t>(static_cast<std::uint64_t>(Length))};
					return StartTask<Http::FResponseOperation>(
						S,
						"http::response_stream_operation",
						V.Context,
						[&]
						{
							return V.Value->ReplyStream(
								Object<Http::FResponse>(C, 0), std::move(Body), Options(Integer(C, 3)));
						},
						O,
						E);
				},
				Error,
				"http::response_stream_operation",
				{"http::response_builder", "Network::readable_stream", "", ""},
				false,
				true))
		{
			return false;
		}
		return Member(
				   S,
				   Stream,
				   "operation",
				   T::HostObject,
				   {},
				   [&S](const auto& C, auto& O, auto& E)
				   {
					   const auto& V = This<FReplyValue>(C);
					   return Make(S, "http::operation", FOperationValue{V.Context, V.Value->Operation()}, O, E);
				   },
				   Error,
				   "http::operation") &&
			   Member(
				   S,
				   Stream,
				   "state",
				   T::Int64,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   O = static_cast<std::int64_t>(This<FReplyValue>(C).Value->State());
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Stream,
				   "body_bytes",
				   T::Int64,
				   {},
				   [](const auto& C, auto& O, auto& E)
				   {
					   const auto Value = This<FReplyValue>(C).Value->BodyBytes();
					   if (Value > static_cast<std::uint64_t>(INT64_MAX))
					   {
						   E = "Response byte count exceeds int64";
						   return false;
					   }
					   O = static_cast<std::int64_t>(Value);
					   return true;
				   },
				   Error);
	}
} // namespace VeroScript::NetworkBinding
