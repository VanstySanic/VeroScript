// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FCurlHttpClient. Cookie parsing and matching remain owned by curl.

bool CookieSnapshot(CURL* Easy, FCookies& Out, std::shared_ptr<N::FByteLease>& Memory, Network::FError& Error)
{
	curl_slist* Raw = nullptr;
	const auto Code = curl_easy_getinfo(Easy, CURLINFO_COOKIELIST, &Raw);
	std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> List(Raw, curl_slist_free_all);
	if (Code != CURLE_OK)
	{
		Error = {Network::EError::Allocation, curl_easy_strerror(Code), Code};
		return false;
	}
	std::uint64_t Size = 0, Count = 0;
	for (auto* At = Raw; At; At = At->next)
	{
		const auto Bytes = std::strlen(At->data) + sizeof(std::string) + 32;
		if (Bytes > Configuration.MaxCookieBytes - std::min(Size, Configuration.MaxCookieBytes))
		{
			Error = {Network::EError::Limit, "HTTP cookie store byte limit exceeded", 0};
			return false;
		}
		Size += Bytes;
		++Count;
	}
	Memory = N::ReserveBytes(Context, Size, Error);
	if (!Memory)
	{
		return false;
	}
	FCookies Candidate;
	Candidate.Records.reserve(static_cast<std::size_t>(Count));
	for (auto* At = Raw; At; At = At->next)
	{
		Candidate.Records.emplace_back(At->data);
	}
	Out = std::move(Candidate);
	return true;
}
static bool CookieRecord(std::string_view Record)
{
	if (Record.empty() || Record.find_first_of("\r\n\0", 0, 3) != Record.npos)
	{
		return false;
	}
	std::string_view Fields[7];
	std::size_t Start = 0;
	for (unsigned I = 0; I < 6; ++I)
	{
		const auto End = Record.find('\t', Start);
		if (End == Record.npos)
		{
			return false;
		}
		Fields[I] = Record.substr(Start, End - Start);
		Start = End + 1;
	}
	Fields[6] = Record.substr(Start);
	if (Fields[0].empty() || Fields[2].empty() || Fields[2].front() != '/' || Fields[5].empty() ||
		Fields[6].find('\t') != Record.npos || (Fields[1] != "TRUE" && Fields[1] != "FALSE") ||
		(Fields[3] != "TRUE" && Fields[3] != "FALSE"))
	{
		return false;
	}
	std::uint64_t Expiry = 0;
	const auto Parsed = std::from_chars(Fields[4].data(), Fields[4].data() + Fields[4].size(), Expiry);
	return Parsed.ec == std::errc{} && Parsed.ptr == Fields[4].data() + Fields[4].size() &&
		   Expiry <= static_cast<std::uint64_t>(INT64_MAX);
}
void CookieCommand(const std::shared_ptr<N::FOperation>& Op, unsigned Command, FCookies Input)
{
	if (N::Terminal(N::State(Op)))
	{
		return;
	}
	if (Closed)
	{
		N::Fail(Op, Network::EError::Closed, "HTTP client closed");
		return;
	}
	if (!CookieShare)
	{
		N::Fail(Op, Network::EError::Unsupported, "HTTP cookie storage is disabled");
		return;
	}
	N::Running(Op);
	for (const auto& Record : Input.Records)
	{
		if (!CookieRecord(Record))
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Expected Netscape cookie-file records");
			return;
		}
	}
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> Easy(curl_easy_init(), curl_easy_cleanup);
	if (!Easy)
	{
		N::Fail(Op, Network::EError::Allocation, "Cannot allocate cookie operation");
		return;
	}
	auto Code = curl_easy_setopt(Easy.get(), CURLOPT_SHARE, CookieShare);
	if (Code == CURLE_OK)
	{
		Code = curl_easy_setopt(Easy.get(), CURLOPT_COOKIEFILE, "");
	}
	if (Code == CURLE_OK && Command == 1)
	{
		Code = curl_easy_setopt(Easy.get(), CURLOPT_COOKIELIST, "ALL");
	}
	if (Command == 2)
	{
		for (const auto& Record : Input.Records)
		{
			if (Code != CURLE_OK || N::Terminal(N::State(Op)))
			{
				break;
			}
			Code = curl_easy_setopt(Easy.get(), CURLOPT_COOKIELIST, Record.c_str());
		}
	}
	if (Code != CURLE_OK)
	{
		N::Fail(Op, Network::EError::Allocation, curl_easy_strerror(Code), Code);
		return;
	}
	FCookies Snapshot;
	std::shared_ptr<N::FByteLease> Memory;
	Network::FError Failure;
	if (!CookieSnapshot(Easy.get(), Snapshot, Memory, Failure))
	{
		if (Failure.Code == Network::EError::Limit)
		{
			curl_easy_setopt(Easy.get(), CURLOPT_COOKIELIST, "ALL");
		}
		N::Fail(Op, Failure.Code, Failure.Message, Failure.SystemCode);
		return;
	}
	if (Command == 0)
	{
		N::Complete(Op, std::move(Snapshot), std::move(Memory));
	}
	else
	{
		N::Complete(Op, static_cast<std::uint64_t>(Snapshot.Records.size()));
	}
}
