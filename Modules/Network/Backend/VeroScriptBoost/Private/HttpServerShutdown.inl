// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FBoostHttpServer. Shutdown reports logical transport closure.

void CheckShutdown(bool Force = false)
{
	if (bClosed || bClosingConnections || !ShutdownOperation)
	{
		return;
	}
	if (N::Terminal(N::State(ShutdownOperation)))
	{
		Close();
		return;
	}
	if (!Force)
	{
		for (const auto& Weak : Connections)
		{
			if (auto P = Weak.lock(); P && !P->bClosed)
			{
				return;
			}
		}
	}
	bClosingConnections = true;
	if (Force)
	{
		for (const auto& Weak : Connections)
		{
			if (auto P = Weak.lock())
			{
				P->Close();
			}
		}
	}
	auto Op = std::move(ShutdownOperation);
	ShutdownTimer.cancel();
	bClosed = true;
	bClosingConnections = false;
	N::RemoveResource(Context, ResourceId);
	N::Complete(Op, FShutdownResult{Force, ShutdownConnections});
}

void Shutdown(const std::shared_ptr<N::FOperation>& Op, std::chrono::milliseconds Grace)
{
	if (N::Terminal(N::State(Op)))
	{
		return;
	}
	if (Grace.count() < 0 || Grace > std::chrono::hours(24 * 365))
	{
		N::Fail(Op, Network::EError::InvalidArgument, "HTTP shutdown grace period is out of range");
		return;
	}
	if (bClosed)
	{
		N::Complete(Op, FShutdownResult{});
		return;
	}
	if (bStopping)
	{
		N::Fail(Op, Network::EError::State, "HTTP server is already shutting down");
		return;
	}
	bStopping = true;
	bClosingConnections = true;
	ShutdownOperation = Op;
	for (const auto& Weak : Connections)
	{
		if (auto P = Weak.lock(); P && !P->bClosed)
		{
			++ShutdownConnections;
		}
	}
	N::SetAbort(Op,
		[Self = shared_from_this()]
		{
			Self->Close();
		});
	if (N::Terminal(N::State(Op)))
	{
		bClosingConnections = false;
		return;
	}
	N::Running(Op);
	N::FSystemError Ignore;
	Acceptor.close(Ignore);
	for (const auto& Pending : Waiters)
	{
		N::Fail(Pending, Network::EError::Closed, "HTTP server is shutting down");
	}
	Waiters.clear();
	// Queued exchanges have not entered application handling; only delivered work receives grace.
	for (const auto& Exchange : Ready)
	{
		Backend::FAccess::Implementation(*Exchange).Discard();
	}
	Ready.clear();
	for (const auto& Weak : Connections)
	{
		if (auto P = Weak.lock())
		{
			P->bStopAfterResponse = true;
			if (!P->RequestReady() || !P->bExchangeDelivered)
			{
				P->Close();
			}
		}
	}
	ShutdownTimer.expires_after(Grace);
	ShutdownTimer.async_wait(
		[Weak = weak_from_this()](N::FSystemError Error)
		{
			if (!Error)
			{
				if (auto P = Weak.lock())
				{
					P->CheckShutdown(true);
				}
			}
		});
	bClosingConnections = false;
	CheckShutdown();
}
