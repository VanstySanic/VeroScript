// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FBoostHttpConnection; all transport state is confined to its I/O executor.

struct FReplyFlow : std::enable_shared_from_this<FReplyFlow>
{
	std::shared_ptr<FBoostHttpConnection> Owner;
	std::shared_ptr<N::FOperation> Op;
	std::shared_ptr<Detail::FServerResponseState> Progress;
	std::shared_ptr<N::FByteLease> SnapshotMemory, WireMemory;
	FBodySource Source;
	Network::FBytes Body, Block;
	std::uint64_t Offset = 0, Written = 0;
	Wire::response<Wire::empty_body> Head;
	std::optional<Wire::response_serializer<Wire::empty_body>> Serializer;
	Wire::fields Trailers;
	std::atomic<bool> ResumeQueued{false};
	bool bChunked = false, bKeepAlive = false, bBodyAllowed = true, bWriting = false, bDone = false;

	void Abort(Network::EError Code, const char* Message)
	{
		const auto Self = shared_from_this();
		if (bDone)
		{
			return;
		}
		bDone = true;
		Progress->State = EResponseState::Aborted;
		if (Source.Stream)
		{
			Source.Stream->Cancel();
		}
		N::Fail(Op, Code, Message);
		if (Owner->ActiveResponse.get() == this)
		{
			Owner->ActiveResponse.reset();
		}
		Owner->Close();
	}

	void Resume()
	{
		if (ResumeQueued.exchange(true))
		{
			return;
		}
		N::Post(Owner->Context,
			[Weak = weak_from_this()]
			{
				if (auto Self = Weak.lock())
				{
					Self->ResumeQueued = false;
					Self->Pump();
				}
			});
	}

	bool Live()
	{
		if (bDone)
		{
			return false;
		}
		if (Owner->bClosed || N::Terminal(N::State(Op)))
		{
			Abort(Network::EError::Closed, "HTTP response stopped");
			return false;
		}
		return true;
	}

	void Finish()
	{
		const auto Self = shared_from_this();
		if (!Live())
		{
			return;
		}
		if (!N::Complete(Op, Written))
		{
			Abort(Network::EError::Closed, "HTTP response stopped before completion");
			return;
		}
		bDone = true;
		Progress->State = EResponseState::Finished;
		Owner->Timer.cancel();
		if (Owner->ActiveResponse.get() == this)
		{
			Owner->ActiveResponse.reset();
		}
		Owner->ResponseFinished(bKeepAlive);
	}

	void EndBody()
	{
		if (Source.Length && Written != *Source.Length)
		{
			Abort(Network::EError::Protocol, "HTTP response body is shorter than its declared length");
			return;
		}
		if (!bChunked)
		{
			Finish();
			return;
		}
		bWriting = true;
		auto Last = std::make_shared<Wire::chunk_last<Wire::fields>>(Trailers);
		N::Asio::async_write(Owner->Stream,
			*Last,
			[Self = shared_from_this(), Last](N::FSystemError Error, std::size_t)
			{
				Self->bWriting = false;
				if (!Self->Live())
				{
					return;
				}
				if (Error)
				{
					Self->Abort(Network::EError::Write, "Failed to write HTTP response trailers");
					return;
				}
				Self->Finish();
			});
	}

	void Wrote(N::FSystemError Error, std::size_t Count)
	{
		bWriting = false;
		if (!Live())
		{
			return;
		}
		if (Error)
		{
			Abort(Network::EError::Write, "Failed to write HTTP response body");
			return;
		}
		Written += Count;
		Progress->Bytes = Written;
		auto State = EResponseState::HeadersSent;
		Progress->State.compare_exchange_strong(State, EResponseState::BodyStreaming);
		Owner->ArmTimeout();
		Pump();
	}

	void Pump()
	{
		if (!Live() || bWriting)
		{
			return;
		}
		if (!bBodyAllowed)
		{
			if (Source.Stream)
			{
				Source.Stream->Cancel();
			}
			// HEAD/204/304 finish after their headers; a representation length is not transmitted.
			Source.Length.reset();
			Finish();
			return;
		}
		std::size_t Count = 0;
		if (Source.Stream)
		{
			// At a boundary, read one byte to distinguish EOF from an overlong source.
			const auto Limit = std::min(Owner->Options.MaxStreamBodyBytes, Source.Length.value_or(UINT64_MAX));
			const auto Available = Limit - std::min(Written, Limit);
			const auto ReadSize = std::min<std::uint64_t>(Block.size(), Available ? Available : 1);
			const auto Read = Source.Stream->Read(Block.data(), static_cast<std::int64_t>(ReadSize));
			if (Read.State == Network::EStreamState::WouldBlock)
			{
				return;
			}
			if (Read.State == Network::EStreamState::Failed)
			{
				Abort(Read.Error ? Read.Error.Code : Network::EError::Read, "HTTP response source failed");
				return;
			}
			if (Read.State == Network::EStreamState::End)
			{
				EndBody();
				return;
			}
			if (Read.State != Network::EStreamState::Progress || !Read.Bytes || Read.Bytes > ReadSize)
			{
				Abort(Network::EError::Internal, "Invalid HTTP response source progress");
				return;
			}
			if (Read.Bytes > Available)
			{
				Abort(Source.Length && *Source.Length <= Owner->Options.MaxStreamBodyBytes ? Network::EError::Protocol
																						   : Network::EError::Limit,
					"HTTP response body exceeds its length or byte limit");
				return;
			}
			Count = static_cast<std::size_t>(Read.Bytes);
		}
		else
		{
			if (Offset == Body.size())
			{
				EndBody();
				return;
			}
			Count = static_cast<std::size_t>(std::min<std::uint64_t>(Block.size(), Body.size() - Offset));
			std::copy_n(Body.data() + static_cast<std::size_t>(Offset), Count, Block.data());
			Offset += Count;
		}
		bWriting = true;
		if (bChunked)
		{
			auto Chunk =
				std::make_shared<Wire::chunk_body<N::Asio::const_buffer>>(N::Asio::const_buffer(Block.data(), Count));
			N::Asio::async_write(Owner->Stream,
				*Chunk,
				[Self = shared_from_this(), Chunk, Count](N::FSystemError Error, std::size_t)
				{
					Self->Wrote(Error, Count);
				});
		}
		else
		{
			N::Asio::async_write(Owner->Stream,
				N::Asio::buffer(Block.data(), Count),
				[Self = shared_from_this(), Count](N::FSystemError Error, std::size_t)
				{
					Self->Wrote(Error, Count);
				});
		}
	}

	void Start()
	{
		if (Source.Stream)
		{
			N::FStreamAccess::OnReadable(*Source.Stream,
				[Weak = weak_from_this()]
				{
					if (auto Self = Weak.lock())
					{
						Self->Resume();
					}
				});
		}
		Serializer.emplace(Head);
		bWriting = true;
		Wire::async_write_header(Owner->Stream,
			*Serializer,
			[Self = shared_from_this()](N::FSystemError Error, std::size_t)
			{
				Self->bWriting = false;
				if (!Self->Live())
				{
					return;
				}
				if (Error)
				{
					Self->Abort(Network::EError::Write, "Failed to write HTTP response headers");
					return;
				}
				auto State = EResponseState::Uncommitted;
				Self->Progress->State.compare_exchange_strong(State, EResponseState::HeadersSent);
				Self->Owner->ArmTimeout();
				Self->Pump();
			});
	}
};

void StartResponseStream(FResponse Response, FBodySource Source, const std::shared_ptr<N::FOperation>& Op,
	std::uint64_t RequestGeneration, std::shared_ptr<N::FByteLease> Retention,
	std::shared_ptr<Detail::FServerResponseState> Progress)
{
	if (N::Terminal(N::State(Op)))
	{
		return;
	}
	if (bClosed || bReplied || !RequestReady() || Generation != RequestGeneration)
	{
		N::Fail(Op, Network::EError::State, "HTTP exchange closed or already replied");
		return;
	}
	if (Response.Status < 200 || Response.Status > 599 || (Source.Stream && !Response.Body.empty()))
	{
		N::Fail(Op, Network::EError::InvalidArgument, "Invalid HTTP response status or conflicting body sources");
		return;
	}
	if (!Source.Stream)
	{
		if (Source.Length && *Source.Length != Response.Body.size())
		{
			N::Fail(Op, Network::EError::InvalidArgument, "HTTP response body length does not match");
			return;
		}
		Source.Length = Response.Body.size();
	}
	if (Source.Length && *Source.Length > Options.MaxStreamBodyBytes)
	{
		N::Fail(Op, Network::EError::Limit, "HTTP response stream byte limit exceeded");
		return;
	}
	auto Flow = std::make_shared<FReplyFlow>();
	Flow->Owner = shared_from_this();
	Flow->Op = Op;
	Flow->Progress = std::move(Progress);
	Flow->SnapshotMemory = std::move(Retention);
	Flow->Source = std::move(Source);
	Flow->Head.version(RequestVersion());
	Flow->Head.result(Response.Status);
	Flow->bBodyAllowed = !RequestIsHead() && Response.Status != 204 && Response.Status != 304;
	const bool HasTrailers = !Response.Trailers.Fields().empty();
	if (HasTrailers && (!Flow->bBodyAllowed || Flow->Head.version() < 11))
	{
		N::Fail(Op, Network::EError::InvalidArgument, "HTTP trailers require an HTTP/1.1 response body");
		return;
	}
	std::uint64_t HeaderBytes = 64;
	for (const auto& H : Response.Headers.Fields())
	{
		HeaderBytes += H.first.size() + H.second.size() + 4;
		if (HeaderBytes > Options.MaxHeaderBytes)
		{
			N::Fail(Op, Network::EError::Limit, "HTTP response header byte limit exceeded");
			return;
		}
		Flow->Head.insert(H.first, H.second);
	}
	std::string TrailerNames;
	for (const auto& H : Response.Trailers.Fields())
	{
		const auto Field = Wire::string_to_field(H.first);
		if (Field == Wire::field::content_length || Field == Wire::field::transfer_encoding ||
			Field == Wire::field::trailer || Field == Wire::field::connection || Field == Wire::field::host ||
			Field == Wire::field::te || Field == Wire::field::upgrade)
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Invalid HTTP trailer field");
			return;
		}
		HeaderBytes += 2 * H.first.size() + H.second.size() + 8;
		if (HeaderBytes > Options.MaxHeaderBytes)
		{
			N::Fail(Op, Network::EError::Limit, "HTTP response trailer byte limit exceeded");
			return;
		}
		Flow->Trailers.insert(H.first, H.second);
		if (!TrailerNames.empty())
		{
			TrailerNames += ", ";
		}
		TrailerNames += H.first;
	}
	Flow->Head.erase(Wire::field::content_length);
	Flow->Head.erase(Wire::field::transfer_encoding);
	Flow->Head.erase(Wire::field::trailer);
	Flow->bChunked = Flow->bBodyAllowed && Flow->Head.version() >= 11 && (!Flow->Source.Length || HasTrailers);
	Flow->bKeepAlive = RequestKeepAlive() && (!Flow->bBodyAllowed || Flow->bChunked || Flow->Source.Length.has_value());
	Flow->Head.keep_alive(Flow->bKeepAlive);
	if (Flow->bChunked)
	{
		Flow->Head.chunked(true);
	}
	else if (Flow->Source.Length && Response.Status != 204 && Response.Status != 304)
	{
		Flow->Head.content_length(*Flow->Source.Length);
	}
	if (HasTrailers)
	{
		Flow->Head.set(Wire::field::trailer, TrailerNames);
	}
	const auto ChunkBytes = std::min<std::uint64_t>(16384, N::Options(Context).MaxBufferBytes);
	if (!ChunkBytes)
	{
		N::Fail(Op, Network::EError::Limit, "HTTP stream buffer limit is zero");
		return;
	}
	Flow->WireMemory = N::ReserveBytes(Context, ChunkBytes + HeaderBytes, Op);
	if (!Flow->WireMemory)
	{
		return;
	}
	Flow->Block.resize(static_cast<std::size_t>(ChunkBytes));
	Flow->Body = std::move(Response.Body);
	if (Flow->Source.Stream && !N::FStreamAccess::ClaimReader(*Flow->Source.Stream))
	{
		N::Fail(Op, Network::EError::State, "HTTP response source already has a consumer");
		return;
	}
	bReplied = true;
	ActiveResponse = Flow;
	N::SetAbort(Op,
		[Weak = std::weak_ptr<FReplyFlow>(Flow)]
		{
			if (auto P = Weak.lock())
			{
				P->Abort(Network::EError::Closed, "HTTP response stopped");
			}
		});
	if (N::Terminal(N::State(Op)))
	{
		return;
	}
	N::Running(Op);
	ArmTimeout();
	Flow->Start();
}
