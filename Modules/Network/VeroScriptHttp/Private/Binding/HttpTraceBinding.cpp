// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttp.h"

namespace VeroScript::NetworkBinding
{
	struct FTraceValue
	{
		std::shared_ptr<const Http::FAttemptDetails> Value;
	};
	struct FHeadValue
	{
		std::shared_ptr<const Http::FAttemptDetails> Owner;
		const Http::FResponseHead* Value;
	};
	static std::int64_t TraceCount(std::optional<std::uint64_t> Value)
	{
		if (!Value)
		{
			return -1;
		}
		if (*Value > INT64_MAX)
		{
			throw std::range_error("HTTP measurement exceeds int64");
		}
		return static_cast<std::int64_t>(*Value);
	}
	bool RegisterHttpTrace(FState& S, FHostTypeId Response, FHostTypeId Attempt, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Details, Statistics, Head;
		if (!Type(S, "http::transfer_details", Details, Error) ||
			!Type(S, "http::transfer_statistics", Statistics, Error) || !Type(S, "http::response_head", Head, Error))
		{
			return false;
		}
		for (const bool IsAttempt : {false, true})
		{
			if (!Member(
					S,
					IsAttempt ? Attempt : Response,
					"details",
					T::HostObject,
					{},
					[&S, IsAttempt](const auto& C, auto& Out, auto& E)
					{
						auto Value = IsAttempt ? This<FHttpAttemptValue>(C).Value->Details
											   : This<FHttpResponseValue>(C).Value->Details;
						if (!Value)
						{
							E = "HTTP transfer details are unavailable for this response";
							return false;
						}
						return Make(S, "http::transfer_details", FTraceValue{std::move(Value)}, Out, E);
					},
					Error,
					"http::transfer_details"))
			{
				return false;
			}
		}
		if (!Member(
				S,
				Details,
				"response_count",
				T::Int64,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = TraceCount(This<FTraceValue>(C).Value->Responses.size());
					return true;
				},
				Error) ||
			!Member(
				S,
				Details,
				"response",
				T::HostObject,
				{T::Int64},
				[&S](const auto& C, auto& Out, auto& E)
				{
					auto Owner = This<FTraceValue>(C).Value;
					const auto Index = Count(Integer(C, 0));
					if (Index >= Owner->Responses.size())
					{
						throw std::range_error("HTTP response head index is out of range");
					}
					const auto* Value = &Owner->Responses[static_cast<std::size_t>(Index)];
					return Make(S, "http::response_head", FHeadValue{std::move(Owner), Value}, Out, E);
				},
				Error,
				"http::response_head") ||
			!Member(
				S,
				Details,
				"statistics",
				T::HostObject,
				{},
				[&S](const auto& C, auto& Out, auto& E)
				{
					return Make(S, "http::transfer_statistics", This<FTraceValue>(C), Out, E);
				},
				Error,
				"http::transfer_statistics") ||
			!Member(
				S,
				Statistics,
				"is_final",
				T::Bool,
				{},
				[](const auto& C, auto& Out, auto&)
				{
					Out = This<FTraceValue>(C).Value->Statistics.bFinal;
					return true;
				},
				Error))
		{
			return false;
		}
		using FMetric = std::optional<std::uint64_t> Http::FTransferStatistics::*;
		const std::pair<const char*, FMetric> Metrics[] = {{"total_time_us", &Http::FTransferStatistics::TotalTimeUs},
			{"queue_time_us", &Http::FTransferStatistics::QueueTimeUs},
			{"name_lookup_time_us", &Http::FTransferStatistics::NameLookupTimeUs},
			{"connect_time_us", &Http::FTransferStatistics::ConnectTimeUs},
			{"app_connect_time_us", &Http::FTransferStatistics::AppConnectTimeUs},
			{"pre_transfer_time_us", &Http::FTransferStatistics::PreTransferTimeUs},
			{"start_transfer_time_us", &Http::FTransferStatistics::StartTransferTimeUs},
			{"redirect_time_us", &Http::FTransferStatistics::RedirectTimeUs},
			{"last_transfer_downloaded_bytes", &Http::FTransferStatistics::LastTransferDownloadedBytes},
			{"last_transfer_uploaded_bytes", &Http::FTransferStatistics::LastTransferUploadedBytes},
			{"new_connections", &Http::FTransferStatistics::NewConnections},
			{"followed_redirects", &Http::FTransferStatistics::FollowedRedirects}};
		for (const auto& Metric : Metrics)
		{
			if (!Member(
					S,
					Statistics,
					Metric.first,
					T::Int64,
					{},
					[Field = Metric.second](const auto& C, auto& Out, auto&)
					{
						Out = TraceCount(This<FTraceValue>(C).Value->Statistics.*Field);
						return true;
					},
					Error))
			{
				return false;
			}
		}
		for (const bool Headers : {true, false})
		{
			if (!Member(
					S,
					Statistics,
					Headers ? "received_header_bytes" : "accepted_body_bytes",
					T::Int64,
					{},
					[Headers](const auto& C, auto& Out, auto&)
					{
						const auto& V = This<FTraceValue>(C).Value->Statistics;
						Out = TraceCount(Headers ? V.ReceivedHeaderBytes : V.AcceptedBodyBytes);
						return true;
					},
					Error))
			{
				return false;
			}
		}
		for (const std::string Name :
			{"url", "method", "redirect_url", "redirect_method", "status", "redirect_followed", "location_count"})
		{
			const bool StringResult =
				Name == "url" || Name == "method" || Name == "redirect_url" || Name == "redirect_method";
			if (!Member(
					S,
					Head,
					Name.c_str(),
					StringResult				  ? T::String
					: Name == "redirect_followed" ? T::Bool
												  : T::Int64,
					{},
					[Name](const auto& C, auto& Out, auto&)
					{
						const auto& V = *This<FHeadValue>(C).Value;
						if (Name == "url")
						{
							Out = V.Url;
						}
						else if (Name == "method")
						{
							Out = V.Method;
						}
						else if (Name == "redirect_url")
						{
							Out = V.RedirectUrl;
						}
						else if (Name == "redirect_method")
						{
							Out = V.RedirectMethod;
						}
						else if (Name == "redirect_followed")
						{
							Out = V.bRedirectFollowed;
						}
						else
						{
							Out = TraceCount(Name == "status" ? V.Status : V.Locations.size());
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
			Head,
			"location",
			T::String,
			{T::Int64},
			[](const auto& C, auto& Out, auto&)
			{
				const auto& V = This<FHeadValue>(C).Value->Locations;
				const auto Index = Count(Integer(C, 0));
				if (Index >= V.size())
				{
					throw std::range_error("HTTP Location index is out of range");
				}
				Out = V[static_cast<std::size_t>(Index)];
				return true;
			},
			Error);
	}
} // namespace VeroScript::NetworkBinding
