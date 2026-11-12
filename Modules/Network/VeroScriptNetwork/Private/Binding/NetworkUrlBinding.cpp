// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkBinding.h"
#include <utility>

namespace VeroScript::NetworkBinding
{
	bool RegisterNetworkUrl(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Url;
		if (!ValueType<Network::FUrl>(S, "Network::url", Url, Error))
		{
			return false;
		}
		for (bool Resolve : {false, true})
		{
			if (!Member(
					S,
					Api,
					Resolve ? "resolve_url" : "parse_url",
					T::HostObject,
					Resolve ? std::vector<T>{T::String, T::String} : std::vector<T>{T::String},
					[&S, Resolve](const auto& C, auto& O, auto& E)
					{
						Network::FUrl Value;
						Network::FError Failure;
						const bool Ok = Resolve ? Network::FUrl::TryResolve(String(C, 0), String(C, 1), Value, Failure)
												: Network::FUrl::TryParse(String(C, 0), Value, Failure);
						if (!Ok)
						{
							E = Failure.Message;
							return false;
						}
						return Make(S, "Network::url", std::move(Value), O, E);
					},
					Error,
					"Network::url",
					{},
					true))
			{
				return false;
			}
		}
		for (bool Decode : {false, true})
		{
			if (!Member(
					S,
					Api,
					Decode ? "percent_decode" : "percent_encode",
					T::String,
					{T::String, T::Bool},
					[Decode](const auto& C, auto& O, auto& E)
					{
						const bool Form = std::get<bool>(C.Args[1]);
						if (!Decode)
						{
							O = Network::PercentEncode(String(C, 0), Form);
							return true;
						}
						std::string Value;
						Network::FError Failure;
						if (!Network::TryPercentDecode(String(C, 0), Value, Failure, Form))
						{
							E = Failure.Message;
							return false;
						}
						O = std::move(Value);
						return true;
					},
					Error,
					"",
					{},
					true))
			{
				return false;
			}
		}
		using FUrlTextField = std::string Network::FUrl::*;
		const std::pair<const char*, FUrlTextField> Fields[] = {{"scheme", &Network::FUrl::Scheme},
			{"host", &Network::FUrl::Host},
			{"path", &Network::FUrl::Path},
			{"query", &Network::FUrl::Query},
			{"fragment", &Network::FUrl::Fragment}};
		for (const auto& Field : Fields)
		{
			if (!Member(
					S,
					Url,
					Field.first,
					T::String,
					{},
					[Pointer = Field.second](const auto& C, auto& O, auto&)
					{
						O = This<Network::FUrl>(C).*Pointer;
						return true;
					},
					Error))
			{
				return false;
			}
		}
		const std::pair<const char*, bool Network::FUrl::*> Flags[] = {{"has_query", &Network::FUrl::bHasQuery},
			{"has_fragment", &Network::FUrl::bHasFragment},
			{"explicit_port", &Network::FUrl::bExplicitPort},
			{"ipv6", &Network::FUrl::bIpv6}};
		for (const auto& Flag : Flags)
		{
			if (!Member(
					S,
					Url,
					Flag.first,
					T::Bool,
					{},
					[Pointer = Flag.second](const auto& C, auto& O, auto&)
					{
						O = This<Network::FUrl>(C).*Pointer;
						return true;
					},
					Error))
			{
				return false;
			}
		}
		for (int Action = 0; Action < 4; ++Action)
		{
			const char* Name[] = {"authority", "target", "to_string", "without_fragment"};
			if (!Member(
					S,
					Url,
					Name[Action],
					T::String,
					{},
					[Action](const auto& C, auto& O, auto&)
					{
						const auto& Value = This<Network::FUrl>(C);
						O = Action == 0 ? Value.Authority() : Action == 1 ? Value.Target() : Value.String(Action == 2);
						return true;
					},
					Error))
			{
				return false;
			}
		}
		return Member(
			S,
			Url,
			"port",
			T::Int64,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = static_cast<std::int64_t>(This<Network::FUrl>(C).Port);
				return true;
			},
			Error);
	}
} // namespace VeroScript::NetworkBinding
