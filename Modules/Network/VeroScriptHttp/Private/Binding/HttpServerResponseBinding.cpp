// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttp.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterHttpServerResponses(FState& S, FHostTypeId Api, FHostTypeId Exchange, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Response;
		if (!ValueType<Http::FResponse>(S, "http::response_builder", Response, Error))
		{
			return false;
		}
		if (!Member(
				S,
				Api,
				"create_response",
				T::HostObject,
				{T::Int64, T::String},
				[&S](const auto& C, auto& Out, auto& E)
				{
					const auto Status = Integer(C, 0);
					if (Status < 100 || Status > 599)
					{
						E = "HTTP status is out of range";
						return false;
					}
					Http::FResponse Value;
					Value.Status = static_cast<std::uint32_t>(Status);
					Value.Body = Bytes(String(C, 1));
					return Make(S, "http::response_builder", std::move(Value), Out, E);
				},
				Error,
				"http::response_builder",
				{},
				true))
		{
			return false;
		}
		for (const bool Trailer : {false, true})
		{
			for (const bool Set : {false, true})
			{
				if (!Member(
						S,
						Response,
						Trailer ? (Set ? "set_trailer" : "add_trailer") : (Set ? "set_header" : "add_header"),
						T::Void,
						{T::String, T::String},
						[Set, Trailer](const auto& C, auto&, auto& E)
						{
							Network::FError Error;
							auto& Value = This<Http::FResponse>(C);
							auto& Headers = Trailer ? Value.Trailers : Value.Headers;
							const bool Ok = Set ? Headers.TrySet(String(C, 0), String(C, 1), Error)
												: Headers.TryAdd(String(C, 0), String(C, 1), Error);
							if (!Ok)
							{
								E = Error.Message;
							}
							return Ok;
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
		}
		return Member(
			S,
			Exchange,
			"reply_response",
			T::HostObject,
			{T::HostObject, T::Int64},
			[&S](const auto& C, auto& Out, auto& E)
			{
				auto& V = This<FHandle<Http::FExchange>>(C);
				return Start(
					S,
					"http::operation",
					V.Context,
					[&](auto&)
					{
						return V.Value->Reply(Object<Http::FResponse>(C, 0), Options(Integer(C, 1)));
					},
					Out,
					E);
			},
			Error,
			"http::operation",
			{"http::response_builder", ""},
			false,
			true);
	}
} // namespace VeroScript::NetworkBinding
