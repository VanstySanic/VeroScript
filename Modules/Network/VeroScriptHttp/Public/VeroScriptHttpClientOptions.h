// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"

namespace VeroScript::Http
{
	struct FClientOptions
	{
		std::string BackendId;
		// Zero uses the context connection limit.
		std::uint32_t MaxConnections = 0;
		std::uint32_t MaxConnectionsPerHost = 8;
		std::uint32_t MaxIdleConnections = 64;
		std::uint32_t MaxPendingRequests = 128;
		std::chrono::seconds MaxConnectionIdleTime{120};
		bool bCookies = false;
		std::uint64_t MaxCookieBytes = 1024 * 1024;
		bool bInheritProxyEnvironment = false;
	};

	struct FRetryOptions
	{
		std::uint32_t MaxAttempts = 1;
		std::chrono::milliseconds Delay{100};
		std::chrono::milliseconds MaxDelay{30000};
		bool bAllowReplay = false;
		bool bAllowUnsafeMethods = false;
		bool bHonorRetryAfter = true;
		bool bRetryTransportErrors = true;
		std::vector<std::uint32_t> Statuses{408, 429, 500, 502, 503, 504};
	};

	struct FResponseHead
	{
		std::uint32_t Status = 0;
		std::string Url, Method;
		// Preserve duplicate and empty Location fields in arrival order.
		std::vector<std::string> Locations;
		bool bRedirectFollowed = false;
		// The selected target is retained even if connecting to it fails. An unobserved method is empty.
		std::string RedirectUrl, RedirectMethod;
	};
	struct FTransferStatistics
	{
		// Backend measurements for this easy-handle attempt, not the logical retry operation.
		// Time values are curl cumulative milestones in microseconds; redirects contribute to them.
		std::optional<std::uint64_t> TotalTimeUs, QueueTimeUs, NameLookupTimeUs, ConnectTimeUs;
		std::optional<std::uint64_t> AppConnectTimeUs, PreTransferTimeUs, StartTransferTimeUs, RedirectTimeUs;
		// curl payload counts concern the last transfer in the redirect chain, exclude headers,
		// and precede content decoding. They are not TCP/TLS wire byte counts.
		std::optional<std::uint64_t> LastTransferDownloadedBytes, LastTransferUploadedBytes;
		std::optional<std::uint64_t> NewConnections, FollowedRedirects;
		// Accepted header lines and decoded body callbacks across all heads in this attempt.
		std::uint64_t ReceivedHeaderBytes = 0, AcceptedBodyBytes = 0;
		bool bFinal = false;
	};
	struct FAttemptDetails
	{
		// Completed HTTP response heads, including informational and final responses.
		// CONNECT proxy heads are excluded. Partial/malformed heads are not published.
		std::vector<FResponseHead> Responses;
		FTransferStatistics Statistics;
	};
	struct FAttempt
	{
		std::uint32_t Number = 0;
		std::uint32_t Status = 0;
		int TransportCode = 0;
		Network::EError ErrorCode = Network::EError::None;
		std::uint64_t UploadedBytes = 0, DownloadedBytes = 0;
		std::string EffectiveUrl;
		// Immutable, independently retained and shared by history/response snapshots.
		std::shared_ptr<const FAttemptDetails> Details;
	};
	struct FAttemptSnapshot
	{
		std::vector<FAttempt> Attempts;
		Network::FError Error;
		bool bFinal = false, bTruncated = false;
	};
	namespace Detail
	{
		struct FAttemptHistoryAccess;
	}
	class FAttemptHistory
	{
		friend struct Detail::FAttemptHistoryAccess;

	public:
		FAttemptHistory();
		~FAttemptHistory();
		std::shared_ptr<const FAttemptSnapshot> Snapshot() const;

	private:
		struct FImpl;
		std::shared_ptr<FImpl> Impl;
	};
	struct FCookies
	{
		// Netscape cookie-file records. Application file adapters may persist these explicitly.
		std::vector<std::string> Records;
	};
} // namespace VeroScript::Http
