// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttp.h"
#include "VeroScriptHttpRouter.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterHttpRouter(FState& S, FHostTypeId Api, FHostTypeId Exchange, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Router, Snapshot, Match, Limits;
		if (!Type(S, "http::router", Router, Error) || !Type(S, "http::route_snapshot", Snapshot, Error) ||
			!Type(S, "http::route_match", Match, Error) ||
			!ValueType<Http::FRouteLimits>(S, "http::route_limits", Limits, Error))
		{
			return false;
		}

		if (!Member(
				S,
				Api,
				"make_route_limits",
				T::HostObject,
				{},
				[&S](const auto&, auto& Out, auto& E)
				{
					return Make(S, "http::route_limits", Http::FRouteLimits{}, Out, E);
				},
				Error,
				"http::route_limits",
				{},
				true) ||
			!Member(
				S,
				Api,
				"create_router_with_limits",
				T::HostObject,
				{T::HostObject},
				[&S](const auto& C, auto& Out, auto& E)
				{
					return Make(S, "http::router", Http::FRouter(Object<Http::FRouteLimits>(C, 0)), Out, E);
				},
				Error,
				"http::router",
				{"http::route_limits"},
				true))
		{
			return false;
		}
		for (const std::string Name : {"max_routes",
				 "max_segments",
				 "max_query_parameters",
				 "max_method_bytes",
				 "max_pattern_bytes",
				 "max_total_pattern_bytes",
				 "max_target_bytes"})
		{
			if (!Member(
					S,
					Limits,
					Name.c_str(),
					T::Int64,
					{},
					[Name](const auto& C, auto& Out, auto&)
					{
						const auto& L = This<Http::FRouteLimits>(C);
						const auto N = Name == "max_routes"				   ? L.MaxRoutes
									   : Name == "max_segments"			   ? L.MaxSegments
									   : Name == "max_query_parameters"	   ? L.MaxQueryParameters
									   : Name == "max_method_bytes"		   ? L.MaxMethodBytes
									   : Name == "max_pattern_bytes"	   ? L.MaxPatternBytes
									   : Name == "max_total_pattern_bytes" ? L.MaxTotalPatternBytes
																		   : L.MaxTargetBytes;
						if (N > INT64_MAX)
						{
							throw std::range_error("Route limit exceeds int64");
						}
						Out = static_cast<std::int64_t>(N);
						return true;
					},
					Error) ||
				!Member(
					S,
					Limits,
					("set_" + Name).c_str(),
					T::Void,
					{T::Int64},
					[Name](const auto& C, auto&, auto&)
					{
						auto& L = This<Http::FRouteLimits>(C);
						const auto N = Count(Integer(C, 0));
						const bool Narrow = Name == "max_routes" || Name == "max_segments" ||
											Name == "max_query_parameters" || Name == "max_method_bytes";
						if (Narrow && N > UINT32_MAX)
						{
							throw std::range_error("Route limit exceeds uint32");
						}
						if (Name == "max_routes")
						{
							L.MaxRoutes = static_cast<std::uint32_t>(N);
						}
						else if (Name == "max_segments")
						{
							L.MaxSegments = static_cast<std::uint32_t>(N);
						}
						else if (Name == "max_query_parameters")
						{
							L.MaxQueryParameters = static_cast<std::uint32_t>(N);
						}
						else if (Name == "max_method_bytes")
						{
							L.MaxMethodBytes = static_cast<std::uint32_t>(N);
						}
						else if (Name == "max_pattern_bytes")
						{
							L.MaxPatternBytes = N;
						}
						else if (Name == "max_total_pattern_bytes")
						{
							L.MaxTotalPatternBytes = N;
						}
						else
						{
							L.MaxTargetBytes = N;
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
				Api,
				"create_router",
				T::HostObject,
				{},
				[&S](const auto&, auto& Out, auto& E)
				{
					return Make(S, "http::router", Http::FRouter{}, Out, E);
				},
				Error,
				"http::router",
				{},
				true) ||
			!Member(
				S,
				Router,
				"add",
				T::Void,
				{T::String, T::String, T::Int64},
				[](const auto& C, auto&, auto& E)
				{
					Network::FError Failure;
					const bool Good =
						This<Http::FRouter>(C).TryAdd({Count(Integer(C, 2)), String(C, 0), String(C, 1)}, Failure);
					E = Failure.Message;
					return Good;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				Router,
				"remove",
				T::Bool,
				{T::Int64},
				[](const auto& C, auto& Out, auto& E)
				{
					Network::FError Failure;
					bool Removed = false;
					const bool Good = This<Http::FRouter>(C).TryRemove(Count(Integer(C, 0)), Removed, Failure);
					Out = Removed;
					E = Failure.Message;
					return Good;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				Router,
				"snapshot",
				T::HostObject,
				{},
				[&S](const auto& C, auto& Out, auto& E)
				{
					return Make(S, "http::route_snapshot", This<Http::FRouter>(C).Snapshot(), Out, E);
				},
				Error,
				"http::route_snapshot") ||
			!Member(S,
				Exchange,
				"route",
				T::HostObject,
				{T::HostObject},
				[&S](const auto& C, auto& Out, auto& E)
				{
					const auto& Request = This<FHandle<Http::FExchange>>(C).Value->Request();
					Http::FRouteMatch Result;
					Network::FError Failure;
					if (!Object<Http::FRouter>(C, 0).Snapshot().TryMatch(
							Request.Method, Request.Target, Result, Failure))
					{
						E = Failure.Message;
						return false;
					}
					return Make(S, "http::route_match", std::move(Result), Out, E);
				},
				Error,
				"http::route_match",
				{"http::router"}) ||
			!Member(
				S,
				Snapshot,
				"match",
				T::HostObject,
				{T::String, T::String},
				[&S](const auto& C, auto& Out, auto& E)
				{
					Http::FRouteMatch Result;
					Network::FError Failure;
					if (!This<Http::FRouteSnapshot>(C).TryMatch(String(C, 0), String(C, 1), Result, Failure))
					{
						E = Failure.Message;
						return false;
					}
					return Make(S, "http::route_match", std::move(Result), Out, E);
				},
				Error,
				"http::route_match") ||
			!Member(
				S,
				Snapshot,
				"size",
				T::Int64,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = static_cast<std::int64_t>(This<Http::FRouteSnapshot>(C).Size());
					return true;
				},
				Error))
		{
			return false;
		}
		for (const std::string Name : {"found", "id", "raw_target", "path", "parameter_count", "query_count"})
		{
			const auto Kind = Name == "found" ? T::Bool : Name == "raw_target" || Name == "path" ? T::String : T::Int64;
			if (!Member(
					S,
					Match,
					Name.c_str(),
					Kind,
					{},
					[Name](const auto& C, auto& Out, auto&)
					{
						const auto& M = This<Http::FRouteMatch>(C);
						if (Name == "found")
						{
							Out = M.bFound;
						}
						else if (Name == "raw_target")
						{
							Out = M.RawTarget;
						}
						else if (Name == "path")
						{
							Out = M.Path;
						}
						else if (Name == "parameter_count")
						{
							Out = static_cast<std::int64_t>(M.Parameters.size());
						}
						else if (Name == "query_count")
						{
							Out = static_cast<std::int64_t>(M.Query.size());
						}
						else
						{
							if (M.Id > INT64_MAX)
							{
								throw std::range_error("Route id exceeds int64");
							}
							Out = static_cast<std::int64_t>(M.Id);
						}
						return true;
					},
					Error))
			{
				return false;
			}
		}
		for (const std::string Name :
			{"parameter_name", "parameter_value", "query_name", "query_value", "query_has_equals"})
		{
			if (!Member(
					S,
					Match,
					Name.c_str(),
					Name == "query_has_equals" ? T::Bool : T::String,
					{T::Int64},
					[Name](const auto& C, auto& Out, auto&)
					{
						const auto& M = This<Http::FRouteMatch>(C);
						const auto I = Count(Integer(C, 0));
						if (Name.rfind("parameter_", 0) == 0)
						{
							if (I >= M.Parameters.size())
							{
								throw std::range_error("Route parameter index is out of range");
							}
							Out = Name == "parameter_name" ? M.Parameters[static_cast<std::size_t>(I)].first
														   : M.Parameters[static_cast<std::size_t>(I)].second;
						}
						else
						{
							if (I >= M.Query.size())
							{
								throw std::range_error("Route query index is out of range");
							}
							const auto& Q = M.Query[static_cast<std::size_t>(I)];
							if (Name == "query_has_equals")
							{
								Out = Q.bHasEquals;
							}
							else
							{
								Out = Name == "query_name" ? Q.Name : Q.Value;
							}
						}
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
