// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FCurlHttpClient. A logical operation retains its original deadline.

bool RecordAttempt(FTransfer& T, CURLcode Code)
{
	if (T.bRecorded)
	{
		return true;
	}
	const auto Extra = T.Response.EffectiveUrl.capacity() + 1;
	if (Extra >
			N::Options(Context).MaxRetainedBytes - std::min(T.HistoryMemory->Size(), N::Options(Context).MaxRetainedBytes) ||
		!T.HistoryMemory->TryResize(T.HistoryMemory->Size() + Extra))
	{
		Detail::FAttemptHistoryAccess::Lost(T.Request.History);
		N::Fail(T.Operation, Network::EError::Limit, "HTTP attempt history byte limit exceeded");
		return false;
	}
	curl_off_t Uploaded = 0;
	curl_easy_getinfo(T.Easy, CURLINFO_SIZE_UPLOAD_T, &Uploaded);
	FAttempt A;
	A.Number = T.Attempt;
	A.Status = T.Response.Status;
	A.TransportCode = Code;
	A.ErrorCode = T.CallbackError ? T.CallbackError.Code : Detail::TransportError(Code);
	if (N::Terminal(N::State(T.Operation)))
	{
		const N::FOperationErrorRead Lock(T.Operation);
		A.ErrorCode = Lock.Get().Code;
	}
	A.UploadedBytes = Uploaded > 0 ? static_cast<std::uint64_t>(Uploaded) : 0;
	A.DownloadedBytes = T.AttemptBodyBytes;
	A.EffectiveUrl = T.Response.EffectiveUrl;
	A.Details = TraceSnapshot(T, true);
	if (!A.Details)
	{
		Detail::FAttemptHistoryAccess::Lost(T.Request.History);
		N::Fail(T.Operation, T.CallbackError.Code, "HTTP attempt details byte limit exceeded");
		return false;
	}
	T.Response.Details = A.Details;
	Detail::FAttemptHistoryAccess::Append(T.Request.History, A);
	T.Previous.push_back(std::move(A));
	T.bRecorded = true;
	return true;
}
void RecordAborted(FTransfer& T) noexcept
{
	if (!T.Request.History || T.bRecorded)
	{
		return;
	}
	try
	{
		Describe(T);
		RecordAttempt(T, CURLE_ABORTED_BY_CALLBACK);
	}
	catch (...)
	{
		Detail::FAttemptHistoryAccess::Lost(T.Request.History);
	}
}
bool Retry(const std::shared_ptr<FTransfer>& T, CURLcode Code)
{
	const auto& Policy = T->Request.Retry;
	if (T->CallbackError || N::Terminal(N::State(T->Operation)) || T->Attempt >= Policy.MaxAttempts ||
		!Policy.bAllowReplay || (T->Request.Upload && !T->Request.UploadFactory))
	{
		return false;
	}
	// Published stream headers/data belong to this attempt and cannot be withdrawn.
	if (T->Head)
	{
		std::lock_guard<std::mutex> Lock(T->Head->Mutex);
		if (T->Head->Head || T->Response.BodyBytes)
		{
			return false;
		}
	}
	const bool Transport = Policy.bRetryTransportErrors &&
						   (Code == CURLE_COULDNT_RESOLVE_HOST || Code == CURLE_COULDNT_RESOLVE_PROXY ||
							   Code == CURLE_COULDNT_CONNECT || Code == CURLE_SEND_ERROR || Code == CURLE_RECV_ERROR ||
							   Code == CURLE_PARTIAL_FILE || Code == CURLE_HTTP2_STREAM);
	const bool Status =
		Code == CURLE_OK && !T->Output &&
		std::find(Policy.Statuses.begin(), Policy.Statuses.end(), T->Response.Status) != Policy.Statuses.end();
	if (!Transport && !Status)
	{
		return false;
	}
	auto Delay = Policy.Delay;
	for (std::uint32_t I = 1; I < T->Attempt && Delay < Policy.MaxDelay; ++I)
	{
		Delay = Delay.count() > Policy.MaxDelay.count() / 2 ? Policy.MaxDelay : Delay * 2;
	}
	if (Policy.bHonorRetryAfter)
	{
		const auto Values = T->Response.Headers.GetAll("Retry-After");
		if (Values.size() == 1)
		{
			const auto& Value = Values.front();
			std::uint64_t Seconds = 0;
			const auto Parsed = std::from_chars(Value.data(), Value.data() + Value.size(), Seconds);
			if (Parsed.ec == std::errc{} && Parsed.ptr == Value.data() + Value.size())
			{
				const auto Bounded =
					std::min<std::uint64_t>(Seconds, static_cast<std::uint64_t>(Policy.MaxDelay.count() / 1000 + 1));
				Delay = std::max(Delay, std::chrono::milliseconds(Bounded * 1000));
			}
			else
			{
				const auto Date = curl_getdate(Value.c_str(), nullptr), Now = std::time(nullptr);
				if (Date >= 0 && Date > Now)
				{
					const auto SecondsLeft = std::min<std::uint64_t>(static_cast<std::uint64_t>(Date - Now),
						static_cast<std::uint64_t>(Policy.MaxDelay.count() / 1000 + 1));
					Delay = std::max(Delay, std::chrono::milliseconds(SecondsLeft * 1000));
				}
			}
		}
	}
	Delay = std::min(Delay, Policy.MaxDelay);
	T->RetryTimer = N::Runtime(Context)->CreateTimer();
	if (!T->RetryTimer)
	{
		T->CallbackError = {Network::EError::Unsupported, "HTTP backend did not provide a retry timer", 0};
		return false;
	}
	if (T->Request.Upload)
	{
		T->Request.Upload->Cancel();
	}
	if (T->Request.Multipart)
	{
		for (const auto& Part : *T->Request.Multipart)
		{
			if (Part.Upload)
			{
				Part.Upload->Cancel();
			}
		}
	}
	T->Response = {};
	T->ResponseMemory->TryResize(0);
	T->RetryTimer->ExpiresAt(std::chrono::steady_clock::now() + Delay);
	curl_multi_remove_handle(Multi, T->Easy);
	T->bInMulti = false;
	T->RetryTimer->AsyncWait(
		[Self = shared_from_this(), T](std::error_code Error) mutable
		{
			if (Error || Self->Closed || N::Terminal(N::State(T->Operation)))
			{
				Self->Remove(T->Easy);
				return;
			}
			Detail::FAttemptHistoryAccess::End(T->Request.History, T->Operation, T->Attempt);
			auto Request = std::move(T->Request);
			auto RequestMemory = std::move(T->RequestMemory);
			auto Output = std::move(T->Output);
			auto Head = std::move(T->Head);
			auto Previous = std::move(T->Previous);
			auto History = std::move(T->HistoryMemory);
			Self->Remove(T->Easy);
			Self->Start(std::move(Request),
				T->Operation,
				std::move(RequestMemory),
				std::move(Output),
				std::move(Head),
				T->Attempt + 1,
				std::move(Previous),
				std::move(History),
				T->Total);
		});
	return true;
}
