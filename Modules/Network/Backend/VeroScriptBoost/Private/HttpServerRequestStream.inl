// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FBoostHttpConnection. A request and its response may progress concurrently.

struct FRequestFlow : std::enable_shared_from_this<FRequestFlow>
{
	std::shared_ptr<FBoostHttpConnection> Owner;
	std::shared_ptr<N::FOperation> Op;
	std::shared_ptr<Network::FWritableStream> Writer;
	std::shared_ptr<N::FByteLease> BlockMemory, HeaderMemory;
	Network::FBytes Block;
	std::unordered_map<std::string, std::size_t> InitialFields;
	std::uint64_t Bytes = 0;
	std::size_t PendingBytes = 0;
	Network::EError PendingReadError = Network::EError::None;
	std::atomic<bool> ResumeQueued{false};
	bool bReading = false, bDone = false;

	static std::string FieldKey(Beast::string_view Name)
	{
		std::string Key(Name);
		for (auto& C : Key)
		{
			if (C >= 'A' && C <= 'Z')
			{
				C = static_cast<char>(C - 'A' + 'a');
			}
		}
		return Key;
	}
	void Abort(Network::EError Code, const char* Message)
	{
		const auto Self = shared_from_this();
		if (bDone)
		{
			return;
		}
		bDone = true;
		if (N::Terminal(N::State(Op)))
		{
			const N::FOperationErrorRead Lock(Op);
			if (Lock.Get().Code != Network::EError::None)
			{
				Code = Lock.Get().Code;
			}
		}
		Network::FError E;
		E.Code = Code;
		Writer->Fail(std::move(E));
		N::Fail(Op, Code, Message);
		if (Owner->ActiveRequest.get() == this)
		{
			Owner->ActiveRequest.reset();
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
			Abort(Network::EError::Closed, "HTTP request body stopped");
			return false;
		}
		return true;
	}
	void Finish()
	{
		const auto Self = shared_from_this();
		FRequestBodyResult Result;
		Result.BodyBytes = Bytes;
		Network::FError E;
		auto Memory = N::ReserveBytes(Owner->Context, 0, Op);
		if (!Memory)
		{
			Abort(Network::EError::Limit, "HTTP request trailer retention failed");
			return;
		}
		std::uint64_t HeaderBytes = 0;
		for (const auto& H : Owner->StreamParser->get().base())
		{
			HeaderBytes += H.name_string().size() + H.value().size() + 4;
			if (HeaderBytes > Owner->Options.MaxHeaderBytes)
			{
				Abort(Network::EError::Limit, "HTTP request trailer byte limit exceeded");
				return;
			}
			auto& Initial = InitialFields[FieldKey(H.name_string())];
			if (Initial)
			{
				--Initial;
				continue;
			}
			if (H.name() == Wire::field::content_length || H.name() == Wire::field::transfer_encoding ||
			    H.name() == Wire::field::host || H.name() == Wire::field::connection ||
			    H.name() == Wire::field::trailer || H.name() == Wire::field::upgrade || H.name() == Wire::field::te)
			{
				Abort(Network::EError::Protocol, "Invalid HTTP request trailer field");
				return;
			}
			if (!Memory->TryResize(Memory->Size() + H.name_string().size() + H.value().size() + 64))
			{
				Abort(Network::EError::Limit, "HTTP request trailer retention failed");
				return;
			}
			if (!Result.Trailers.TryAdd(std::string(H.name_string()), std::string(H.value()), E))
			{
				Abort(Network::EError::Protocol, "Invalid HTTP request trailer");
				return;
			}
		}
		if (!N::Complete(Op, std::move(Result), Memory))
		{
			Abort(Network::EError::Closed, "HTTP request body stopped before completion");
			return;
		}
		bDone = true;
		Writer->Finish();
		if (Owner->ActiveRequest.get() == this)
		{
			Owner->ActiveRequest.reset();
		}
		if (Owner->bResponseFinished)
		{
			Owner->ResponseFinished(true);
		}
		else
		{
			Owner->ArmTimeout();
		}
	}
	void Pump()
	{
		if (!Live() || bReading)
		{
			return;
		}
		if (PendingBytes)
		{
			const auto R = Writer->WriteAll(Block.data(), static_cast<std::int64_t>(PendingBytes));
			if (R.State == Network::EStreamState::WouldBlock)
			{
				return;
			}
			if (R.State != Network::EStreamState::Progress || R.Bytes != PendingBytes)
			{
				Abort(R.Error ? R.Error.Code : Network::EError::Internal, "HTTP request body consumer stopped");
				return;
			}
			Bytes += PendingBytes;
			PendingBytes = 0;
			Owner->ArmTimeout();
		}
		if (PendingReadError != Network::EError::None)
		{
			Abort(PendingReadError, "HTTP request body read failed");
			return;
		}
		if (Owner->StreamParser->is_done())
		{
			Finish();
			return;
		}
		auto& Body = Owner->StreamParser->get().body();
		Body.data = Block.data();
		Body.size = Block.size();
		bReading = true;
		Wire::async_read_some(Owner->Stream,
		    Owner->Buffer,
		    *Owner->StreamParser,
		    [Self = shared_from_this()](N::FSystemError Error, std::size_t)
		{
			Self->bReading = false;
			if (!Self->Live())
			{
				return;
			}
			Self->PendingBytes = Self->Block.size() - Self->Owner->StreamParser->get().body().size;
			if (Error && Error != Wire::error::need_buffer)
			{
				Self->PendingReadError =
				    Error == Wire::error::body_limit ? Network::EError::Limit : Network::EError::Protocol;
			}
			Self->Pump();
		});
	}
};

void ResponseFinished(bool KeepAlive)
{
	if (!KeepAlive)
	{
		Close();
		return;
	}
	bResponseFinished = true;
	if (!ActiveRequest)
	{
		if (bStopAfterResponse)
		{
			Close();
		}
		else
		{
			Read();
		}
	}
	else
	{
		ArmTimeout();
	}
}
bool RequestReady() const
{
	return Options.bStreamRequests ? (StreamParser && StreamParser->is_header_done()) : (Parser && Parser->is_done());
}
unsigned RequestVersion() const
{
	return Options.bStreamRequests ? StreamParser->get().version() : Parser->get().version();
}
bool RequestIsHead() const
{
	return Options.bStreamRequests ? StreamParser->get().method() == Wire::verb::head
	                               : Parser->get().method() == Wire::verb::head;
}
bool RequestKeepAlive() const
{
	return Options.bStreamRequests ? bRequestKeepAlive : Parser->get().keep_alive();
}

void StreamHeadersReady()
{
	auto Flow = std::make_shared<FRequestFlow>();
	Flow->Owner = shared_from_this();
	Flow->Op = N::Begin(Context, Network::FOperationOptions{std::nullopt});
	if (N::Terminal(N::State(Flow->Op)))
	{
		Close();
		return;
	}
	auto& Request = StreamParser->get();
	const auto Chunk = std::min<std::uint64_t>(16384, Options.RequestStreamBufferBytes);
	Flow->BlockMemory = N::ReserveBytes(Context, Chunk, Flow->Op);
	auto RingMemory = N::ReserveBytes(Context, Options.RequestStreamBufferBytes, Flow->Op);
	Network::FBytePipe Pipe;
	Network::FError E;
	if (!Flow->BlockMemory || !RingMemory ||
	    !N::FStreamAccess::CreatePipe(Context, Options.RequestStreamBufferBytes, Pipe, E, RingMemory))
	{
		N::Fail(Flow->Op, E ? E.Code : Network::EError::Limit, "HTTP request pipe allocation failed");
		Close();
		return;
	}
	Flow->Block.resize(static_cast<std::size_t>(Chunk));
	Flow->Writer = std::move(Pipe.Writer);
	std::uint64_t HeaderBytes = Request.method_string().size() + Request.target().size() + 64;
	for (const auto& H : Request.base())
	{
		HeaderBytes += H.name_string().size() + H.value().size() + 64;
	}
	Flow->HeaderMemory = N::ReserveBytes(Context, HeaderBytes * 3, Flow->Op);
	if (!Flow->HeaderMemory)
	{
		Close();
		return;
	}
	Incoming = {};
	Incoming.Method = std::string(Request.method_string());
	Incoming.Target = std::string(Request.target());
	Incoming.ProtocolVersion = Request.version();
	for (const auto& H : Request.base())
	{
		++Flow->InitialFields[FRequestFlow::FieldKey(H.name_string())];
		if (!Incoming.Headers.TryAdd(std::string(H.name_string()), std::string(H.value()), E))
		{
			N::Fail(Flow->Op, E.Code, "Invalid HTTP request header");
			Close();
			return;
		}
	}
	Incoming.BodyStream = std::move(Pipe.Reader);
	Incoming.BodyOperation = N::FAccess::Wrap(Flow->Op);
	IncomingMemory = Flow->HeaderMemory;
	bRequestKeepAlive = Request.keep_alive();
	++Generation;
	bReplied = false;
	bResponseFinished = false;
	ActiveRequest = Flow;
	N::FStreamAccess::OnWritable(*Flow->Writer,
	    [Weak = std::weak_ptr<FRequestFlow>(Flow)]
	{
		if (auto P = Weak.lock())
		{
			P->Resume();
		}
	});
	N::FStreamAccess::OnCancel(*Incoming.BodyStream,
	    [Weak = std::weak_ptr<N::FOperation>(Flow->Op)]
	{
		if (auto Op = Weak.lock())
		{
			N::FAccess::Wrap(Op).Cancel();
		}
	});
	N::SetAbort(Flow->Op,
	    [Weak = std::weak_ptr<FRequestFlow>(Flow)]
	{
		if (auto P = Weak.lock())
		{
			P->Abort(Network::EError::Closed, "HTTP request body stopped");
		}
	});
	if (N::Terminal(N::State(Flow->Op)))
	{
		return;
	}
	N::Running(Flow->Op);
	ArmTimeout();
	OnRequest(shared_from_this());
	Flow->Pump();
}
void ReadStream()
{
	Network::FError E;
	if (!TransportMemory)
	{
		TransportMemory = N::ReserveBytes(Context, Options.MaxHeaderBytes + 8192, E);
	}
	if (!TransportMemory)
	{
		Close();
		return;
	}
	StreamParser.emplace();
	StreamParser->body_limit(Options.MaxStreamBodyBytes);
	StreamParser->header_limit(Options.MaxHeaderBytes);
	bResponseFinished = false;
	ArmTimeout();
	Wire::async_read_header(Stream,
	    Buffer,
	    *StreamParser,
	    [Self = shared_from_this()](N::FSystemError Error, std::size_t)
	{
		if (Self->bClosed)
		{
			return;
		}
		if (Error)
		{
			Self->Close();
		}
		else
		{
			Self->ContinueRequest(Self->StreamParser->get(), Self->StreamParser->is_done(),
			    [Self]
			{
				Self->StreamHeadersReady();
			});
		}
	});
}
