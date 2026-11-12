// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FCurlHttpClient; only the owning I/O executor mutates a transfer's trace.

struct FTraceStorage
{
	std::shared_ptr<N::FContext> Context;
	std::shared_ptr<N::FByteLease> Memory;
	FAttemptDetails Value;
};

bool GrowTrace(FTransfer& T, std::uint64_t Extra)
{
	if (!T.TraceMemory)
	{
		Network::FError Failure;
		T.TraceMemory = N::ReserveSnapshotBytes(Context, sizeof(FTraceStorage), Failure);
		if (!T.TraceMemory && !T.CallbackError)
		{
			T.CallbackError = std::move(Failure);
		}
	}
	const auto Limit = N::Options(Context).MaxRetainedBytes;
	if (!T.TraceMemory || Extra > Limit - std::min(Limit, T.TraceMemory->Size()) ||
		!T.TraceMemory->TryResize(T.TraceMemory->Size() + Extra))
	{
		if (!T.CallbackError)
		{
			T.CallbackError.Code = Network::EError::Limit;
		}
		return false;
	}
	return true;
}

static std::optional<std::uint64_t> OffInfo(CURL* Easy, CURLINFO Info)
{
	curl_off_t Value = 0;
	if (curl_easy_getinfo(Easy, Info, &Value) != CURLE_OK || Value < 0)
	{
		return {};
	}
	return static_cast<std::uint64_t>(Value);
}
static std::optional<std::uint64_t> LongInfo(CURL* Easy, CURLINFO Info)
{
	long Value = 0;
	if (curl_easy_getinfo(Easy, Info, &Value) != CURLE_OK || Value < 0)
	{
		return {};
	}
	return static_cast<std::uint64_t>(Value);
}

bool UpdateRedirects(FTransfer& T, const char* Url, const char* Method)
{
	const auto Count = LongInfo(T.Easy, CURLINFO_REDIRECT_COUNT).value_or(0);
	while (T.NextTraceRedirect < T.Trace.Responses.size())
	{
		auto& Head = T.Trace.Responses[T.NextTraceRedirect];
		if (Head.Status < 300 || Head.Status >= 400 ||
			std::none_of(
				Head.Locations.begin(),
				Head.Locations.end(),
				[](const auto& V)
				{
					return !V.empty();
				}))
		{
			++T.NextTraceRedirect;
			continue;
		}
		if (T.FollowedTraceRedirects >= Count)
		{
			break;
		}
		const std::string_view Target = Url ? Url : "", Verb = Method ? Method : "";
		if (!GrowTrace(T, Target.size() + Verb.size() + 64))
		{
			return false;
		}
		Head.RedirectUrl = Target;
		Head.RedirectMethod = Verb;
		Head.bRedirectFollowed = true;
		++T.FollowedTraceRedirects;
		++T.NextTraceRedirect;
	}
	return true;
}

bool RecordHead(FTransfer& T, std::uint32_t Status)
{
	char *Url = nullptr, *Method = nullptr;
	curl_easy_getinfo(T.Easy, CURLINFO_EFFECTIVE_URL, &Url);
	curl_easy_getinfo(T.Easy, CURLINFO_EFFECTIVE_METHOD, &Method);
	if (!UpdateRedirects(T, Url, Method))
	{
		return false;
	}
	const auto& Fields = T.Response.Headers.Fields();
	std::size_t Locations = 0;
	const std::string_view Address = Url ? Url : "", Verb = Method ? Method : "";
	std::uint64_t Extra = Address.size() + Verb.size() + 64;
	const auto IsLocation = [](std::string_view Name)
	{
		constexpr std::string_view Expected = "location";
		return Name.size() == Expected.size() && std::equal(Name.begin(),
													 Name.end(),
													 Expected.begin(),
													 [](unsigned char A, unsigned char B)
													 {
														 return (A >= 'A' && A <= 'Z' ? A + ('a' - 'A') : A) == B;
													 });
	};
	for (const auto& Field : Fields)
	{
		if (IsLocation(Field.first))
		{
			++Locations;
			Extra += sizeof(std::string) + Field.second.size() + 32;
		}
	}
	auto& Heads = T.Trace.Responses;
	const auto Capacity =
		Heads.size() == Heads.capacity() ? std::max<std::size_t>(1, Heads.capacity() * 2) : Heads.capacity();
	Extra += static_cast<std::uint64_t>(Capacity - Heads.capacity()) * sizeof(FResponseHead);
	if (!GrowTrace(T, Extra))
	{
		return false;
	}
	FResponseHead Head;
	Head.Status = Status;
	Head.Url = Address;
	Head.Method = Verb;
	Head.Locations.reserve(Locations);
	for (const auto& Field : Fields)
	{
		if (IsLocation(Field.first))
		{
			Head.Locations.push_back(Field.second);
		}
	}
	Heads.reserve(Capacity);
	Heads.push_back(std::move(Head));
	return true;
}

std::shared_ptr<const FAttemptDetails> TraceSnapshot(FTransfer& T, bool Final)
{
	char* Url = nullptr;
	curl_easy_getinfo(T.Easy, CURLINFO_EFFECTIVE_URL, &Url);
	// A completed head supplied the actual target method. A connection failure has no observed method.
	if (!UpdateRedirects(T, Url, nullptr) || !GrowTrace(T, 0))
	{
		return {};
	}
	auto& S = T.Trace.Statistics;
	S.TotalTimeUs = OffInfo(T.Easy, CURLINFO_TOTAL_TIME_T);
	S.QueueTimeUs = OffInfo(T.Easy, CURLINFO_QUEUE_TIME_T);
	S.NameLookupTimeUs = OffInfo(T.Easy, CURLINFO_NAMELOOKUP_TIME_T);
	S.ConnectTimeUs = OffInfo(T.Easy, CURLINFO_CONNECT_TIME_T);
	S.AppConnectTimeUs = OffInfo(T.Easy, CURLINFO_APPCONNECT_TIME_T);
	S.PreTransferTimeUs = OffInfo(T.Easy, CURLINFO_PRETRANSFER_TIME_T);
	S.StartTransferTimeUs = OffInfo(T.Easy, CURLINFO_STARTTRANSFER_TIME_T);
	S.RedirectTimeUs = OffInfo(T.Easy, CURLINFO_REDIRECT_TIME_T);
	S.LastTransferDownloadedBytes = OffInfo(T.Easy, CURLINFO_SIZE_DOWNLOAD_T);
	S.LastTransferUploadedBytes = OffInfo(T.Easy, CURLINFO_SIZE_UPLOAD_T);
	S.FollowedRedirects = LongInfo(T.Easy, CURLINFO_REDIRECT_COUNT);
	S.NewConnections = LongInfo(T.Easy, CURLINFO_NUM_CONNECTS);
	S.ReceivedHeaderBytes = T.HeaderBytes;
	S.AcceptedBodyBytes = T.AttemptBodyBytes;
	S.bFinal = Final;
	// Allocate the owner before moving the mutable trace, preserving diagnostics if allocation fails.
	auto Owner = std::make_shared<FTraceStorage>();
	Owner->Context = Context;
	if (Final)
	{
		Owner->Memory = std::move(T.TraceMemory);
		Owner->Value = std::move(T.Trace);
	}
	else
	{
		Network::FError Failure;
		Owner->Memory = N::ReserveSnapshotBytes(Context, T.TraceMemory->Size(), Failure);
		if (!Owner->Memory)
		{
			if (!T.CallbackError)
			{
				T.CallbackError = std::move(Failure);
			}
			return {};
		}
		Owner->Value = T.Trace;
	}
	const auto* Value = &Owner->Value;
	return std::shared_ptr<const FAttemptDetails>(std::move(Owner), Value);
}
