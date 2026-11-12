// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FCurlHttpClient. Factories and all curl callbacks run on its native executor.

bool PrepareMultipart(
	FRequest& Request, std::uint32_t Attempt, const std::shared_ptr<N::FOperation>& Op, FInputClaims& Claims)
{
	if (!Request.Multipart)
	{
		return true;
	}
	for (auto& Part : *Request.Multipart)
	{
		if ((!Part.Body.empty() && (Part.Upload || Part.UploadFactory)) ||
			(Part.UploadLength && !Part.Upload && !Part.UploadFactory) ||
			(Attempt == 1 && Part.Upload && Part.UploadFactory) ||
			(Part.Upload && !Part.UploadFactory && (Request.bFollowRedirects || Request.Retry.MaxAttempts > 1)))
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Multipart sources are exclusive and replay requires a factory");
			return false;
		}
		if (Part.UploadFactory)
		{
			if (N::Terminal(N::State(Op)))
			{
				return false;
			}
			Network::FError E;
			try
			{
				auto Source = Part.UploadFactory(Attempt, E);
				if (E || !Source.Stream)
				{
					N::Fail(Op,
						E ? E.Code : Network::EError::Callback,
						E ? E.Message : "Multipart factory returned no source",
						E ? E.SystemCode : 0);
					return false;
				}
				Part.Upload = std::move(Source.Stream);
				Part.UploadLength = Source.Length;
			}
			catch (const std::bad_alloc&)
			{
				N::Fail(Op, Network::EError::Allocation, "Multipart factory allocation failed");
				return false;
			}
			catch (...)
			{
				N::Fail(Op, Network::EError::Callback, "Multipart source factory threw");
				return false;
			}
		}
		if (Part.UploadLength &&
			(*Part.UploadLength > static_cast<std::uint64_t>(INT64_MAX) || *Part.UploadLength > Request.MaxUploadBytes))
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Multipart source length is out of range");
			return false;
		}
	}
	std::uint64_t Known = 0;
	for (const auto& Part : *Request.Multipart)
	{
		const auto Bytes = Part.Upload ? Part.UploadLength.value_or(0) : Part.Body.size();
		if (Bytes > Request.MaxUploadBytes - std::min(Known, Request.MaxUploadBytes))
		{
			N::Fail(Op, Network::EError::Limit, "Multipart known payload exceeds the upload limit");
			return false;
		}
		Known += Bytes;
	}
	Claims.Readers.reserve(Request.Multipart->size());
	for (const auto& Part : *Request.Multipart)
	{
		if (Part.Upload)
		{
			if (N::Terminal(N::State(Op)))
			{
				return false;
			}
			if (!N::FStreamAccess::ClaimReader(*Part.Upload))
			{
				// Publish failure only after already-claimed sources have reached their terminal state.
				Claims.Rollback();
				N::Fail(Op, Network::EError::State, "Multipart source has already been consumed or cancelled");
				return false;
			}
			Claims.Readers.push_back(Part.Upload);
		}
	}
	return true;
}
static std::size_t ReadMime(char* Data, std::size_t Size, std::size_t Count, void* User) noexcept
{
	auto& S = *static_cast<FMimeUpload*>(User);
	auto& T = *S.Transfer;
	if (N::Terminal(N::State(T.Operation)))
	{
		return CURL_READFUNC_ABORT;
	}
	if (Size && Count > SIZE_MAX / Size)
	{
		T.CallbackError.Code = Network::EError::Limit;
		return CURL_READFUNC_ABORT;
	}
	const auto Capacity = Size * Count;
	if (!Capacity)
	{
		return 0;
	}
	try
	{
		auto& Part = (*T.Request.Multipart)[S.Index];
		if (Part.UploadLength && S.ReadBytes == *Part.UploadLength)
		{
			return 0;
		}
		const auto PerBody = T.Request.MaxUploadBytes - std::min(T.MimeBodyBytes, T.Request.MaxUploadBytes);
		const auto Total = T.Request.MaxTotalUploadBytes - std::min(T.Total.Upload, T.Request.MaxTotalUploadBytes);
		const auto Left = Part.UploadLength ? *Part.UploadLength - S.ReadBytes : UINT64_MAX;
		const auto Amount =
			std::min<std::uint64_t>({Capacity, PerBody, Total, Left, static_cast<std::uint64_t>(INT64_MAX)});
		char Extra;
		const auto R = Part.Upload->Read(Amount ? Data : &Extra, static_cast<std::int64_t>(Amount ? Amount : 1));
		if (R.State == Network::EStreamState::WouldBlock)
		{
			T.bUploadPaused = true;
			return CURL_READFUNC_PAUSE;
		}
		if (R.State == Network::EStreamState::Failed)
		{
			T.CallbackError = R.Error;
			return CURL_READFUNC_ABORT;
		}
		if (R.State == Network::EStreamState::End)
		{
			if (Part.UploadLength && S.ReadBytes != *Part.UploadLength)
			{
				T.CallbackError = {Network::EError::Protocol, "Multipart source ended before its declared length", 0};
				return CURL_READFUNC_ABORT;
			}
			return 0;
		}
		if (R.State != Network::EStreamState::Progress || !R.Bytes || R.Bytes > (Amount ? Amount : 1))
		{
			T.CallbackError.Code = Network::EError::Internal;
			return CURL_READFUNC_ABORT;
		}
		if (!Amount)
		{
			T.CallbackError = {Network::EError::Limit, "Multipart source upload byte limit exceeded", 0};
			return CURL_READFUNC_ABORT;
		}
		S.ReadBytes += R.Bytes;
		S.Admission = T.MimeAdmission;
		T.MimeBodyBytes += R.Bytes;
		T.Total.Upload += R.Bytes;
		return static_cast<std::size_t>(R.Bytes);
	}
	catch (const std::bad_alloc&)
	{
		T.CallbackError.Code = Network::EError::Allocation;
	}
	catch (...)
	{
		T.CallbackError.Code = Network::EError::Internal;
	}
	return CURL_READFUNC_ABORT;
}
static int SeekMime(void* User, curl_off_t Offset, int Origin) noexcept
{
	auto& S = *static_cast<FMimeUpload*>(User);
	auto& T = *S.Transfer;
	if (Offset || Origin != SEEK_SET || N::Terminal(N::State(T.Operation)))
	{
		return CURL_SEEKFUNC_CANTSEEK;
	}
	if (!S.ReadBytes)
	{
		return CURL_SEEKFUNC_OK;
	}
	try
	{
		auto& Part = (*T.Request.Multipart)[S.Index];
		if (!Part.UploadFactory)
		{
			return CURL_SEEKFUNC_CANTSEEK;
		}
		if (S.Admission == T.MimeAdmission && !T.Client->AdmitUpload(T))
		{
			return CURL_SEEKFUNC_FAIL;
		}
		Network::FError E;
		auto Source = Part.UploadFactory(T.Attempt, E);
		if (E || !Source.Stream || Source.Length != Part.UploadLength || !N::FStreamAccess::ClaimReader(*Source.Stream))
		{
			T.CallbackError =
				E ? std::move(E)
				  : Network::FError{
						Network::EError::State, "Multipart factory returned an incompatible or consumed rewind source", 0};
			return CURL_SEEKFUNC_FAIL;
		}
		Part.Upload->Cancel();
		Part.Upload = std::move(Source.Stream);
		S.ReadBytes = 0;
		const auto It = T.Client->Transfers.find(T.Easy);
		if (It != T.Client->Transfers.end())
		{
			T.Client->BindInputSource(It->second, Part.Upload, S.Index);
		}
		return CURL_SEEKFUNC_OK;
	}
	catch (const std::bad_alloc&)
	{
		T.CallbackError.Code = Network::EError::Allocation;
	}
	catch (...)
	{
		T.CallbackError.Code = Network::EError::Callback;
	}
	return CURL_SEEKFUNC_FAIL;
}
