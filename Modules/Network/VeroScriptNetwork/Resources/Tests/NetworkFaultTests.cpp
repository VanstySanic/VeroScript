// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"

#include "NetworkInternal.h"
#include "VeroScriptNetworkStream.h"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace VeroScript;

static bool RetainedResultTest()
{
	Network::FOperation Operation;
	std::shared_ptr<const std::vector<Network::FAddress>> Result;
	std::weak_ptr<int> CodeOwner;
	{
		Network::FContext Context(VeroScript::Boost::CreateNetworkService());
		auto Owner = std::make_shared<int>(7);
		CodeOwner = Owner;
		Network::FError Error;
		if (!Context.RetainCodeOwner(Owner, Error))
		{
			return false;
		}
		Owner.reset();
		Operation = Network::Resolve(Context, "localhost", 80);
		if (Operation.Wait(std::chrono::seconds(5)) != Network::EWaitResult::Completed || Operation.Error())
		{
			std::cerr << "Retained resolve: " << static_cast<int>(Operation.State()) << ' ' << Operation.Error().Message
			          << '\n';
			return false;
		}
		Result = Operation.Result<std::vector<Network::FAddress>>();
	}
	if (!Result || Result->empty() || CodeOwner.expired())
	{
		std::cerr << "Result missing or owner expired while the operation is retained\n";
		return false;
	}
	Operation = {};
	if (CodeOwner.expired())
	{
		std::cerr << "Owner expired while the result is retained\n";
		return false;
	}
	Result.reset();
	if (!CodeOwner.expired())
	{
		std::cerr << "Owner remains after releasing the operation and result\n";
	}
	return CodeOwner.expired();
}

static bool CodeOwnerIdentityTest()
{
	std::weak_ptr<int> FirstProbe, SecondProbe, NullProbe;
	{
		Network::FContextOptions Options;
		Options.Mode = Network::EDriveMode::HostDriven;
		Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
		auto Impl = Network::Detail::FAccess::Context(Context);
		auto First = std::make_shared<int>(1);
		auto Second = std::make_shared<int>(2);
		auto Null = std::make_shared<int>(3);
		FirstProbe = First;
		SecondProbe = Second;
		NullProbe = Null;
		int Address = 0, OtherAddress = 0;
		Network::FError Error;
		if (!Context.RetainCodeOwner(std::shared_ptr<const void>(First, &Address), Error) ||
		    !Context.RetainCodeOwner(std::shared_ptr<const void>(Second, &Address), Error))
		{
			return false;
		}
		First.reset();
		Second.reset();
		if (FirstProbe.expired() || SecondProbe.expired())
		{
			return false;
		}
		if (!Context.RetainCodeOwner(std::shared_ptr<const void>(Null, nullptr), Error))
		{
			return false;
		}
		Null.reset();
		if (NullProbe.expired() || Impl->CodeOwners.size() != 3)
		{
			return false;
		}
		// Different alias addresses sharing one control block need only one pin.
		if (!Context.RetainCodeOwner(std::shared_ptr<const void>(FirstProbe.lock(), &OtherAddress), Error) ||
		    Impl->CodeOwners.size() != 3)
		{
			return false;
		}
		// A non-null alias without a control block cannot retain any code.
		if (Context.RetainCodeOwner(std::shared_ptr<const void>(std::shared_ptr<int>{}, &Address), Error) || !Error)
		{
			return false;
		}
	}
	return FirstProbe.expired() && SecondProbe.expired() && NullProbe.expired();
}

static bool Run(Network::EDriveMode Mode, bool bAllocation)
{
	Network::FContextOptions Options;
	Options.Mode = Mode;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	Network::FError Error;
	auto Listener = Network::FTcpListener::Listen(Context, "127.0.0.1", 0, Error);
	if (!Listener || Error)
	{
		return false;
	}
	Network::FOperationOptions Infinite;
	Infinite.Timeout = std::nullopt;
	auto Pending = Listener->Accept(Infinite);
	Network::FBytePipe Pipe;
	if (!Network::TryCreateBytePipe(16, Pipe, Error))
	{
		return false;
	}
	Network::Detail::ObserveTerminal(Network::Detail::FAccess::Operation(Pending),
	    [Weak = std::weak_ptr<Network::FWritableStream>(Pipe.Writer)](auto, const auto& Failure)
	{
		if (auto W = Weak.lock())
		{
			Network::FError E;
			E.Code = Failure.Code;
			W->Fail(std::move(E));
		}
	});
	auto CallbackOwner = std::make_shared<int>(1);
	std::weak_ptr<int> CallbackProbe = CallbackOwner;
	if (!Pending.OnComplete(
	        [Keep = Pending, Owner = CallbackOwner](const auto&)
	{
		(void)Keep;
		(void)Owner;
	},
	        Error))
	{
		return false;
	}
	CallbackOwner.reset();
	// Inject at the I/O executor boundary. No script callback runs on this thread.
	Network::Detail::FAccess::Context(Context)->Post(
	    [bAllocation]
	{
		if (bAllocation)
		{
			throw std::bad_alloc();
		}
		throw std::runtime_error("Injected I/O failure");
	});
	if (Mode == Network::EDriveMode::HostDriven)
	{
		Context.PollIo(Error);
		if (Error.Code != (bAllocation ? Network::EError::Allocation : Network::EError::Internal))
		{
			return false;
		}
	}
	if (Pending.Wait(std::chrono::seconds(5)) != Network::EWaitResult::Completed || !Context.IsClosed() ||
	    Pending.State() != Network::EOperationState::Failed ||
	    Pending.Error().Code != (bAllocation ? Network::EError::Allocation : Network::EError::Internal))
	{
		return false;
	}
	// A faulted context still joins its worker and drains resource cancellation.
	char Byte;
	const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	Network::FStreamResult Read;
	do
	{
		Read = Pipe.Reader->Read(&Byte, 1);
		if (Read.State == Network::EStreamState::WouldBlock)
		{
			std::this_thread::yield();
		}
	}
	while (Read.State == Network::EStreamState::WouldBlock && std::chrono::steady_clock::now() < Until);
	if (Read.Error.Code != (bAllocation ? Network::EError::Allocation : Network::EError::Internal))
	{
		return false;
	}
	Context.Shutdown();
	Context.Shutdown();
	return CallbackProbe.expired();
}

static bool WorkerShutdownReentryTest()
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	auto Impl = Network::Detail::FAccess::Context(Context);
	std::atomic<bool> Entered{false};
	Impl->Post(
	    [Impl, &Entered]
	{
		Entered.store(true);
		while (!Impl->Closed.load())
		{
			std::this_thread::yield();
		}
		Impl->Shutdown();
	});
	while (!Entered.load())
	{
		std::this_thread::yield();
	}
	// Joins the handler above while that handler reenters shutdown.
	Context.Shutdown();
	return Context.IsClosed() && !Impl->Worker.joinable();
}

static bool ConcurrentCancelShutdownTest()
{
	for (int I = 0; I < 256; ++I)
	{
		Network::FContextOptions Options;
		Options.Mode = Network::EDriveMode::HostDriven;
		Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
		auto Impl = Network::Detail::FAccess::Context(Context);
		Network::FOperationOptions Deadline;
		Deadline.Timeout = std::chrono::hours(1);
		auto Op = Network::Detail::FAccess::Wrap(Network::Detail::Begin(Impl, Deadline));
		Network::FError Error;
		Context.PollIo(Error);
		std::atomic<bool> Ready{false}, Go{false};
		std::thread Cancel(
		    [&]
		{
			Ready.store(true);
			while (!Go.load())
			{
				std::this_thread::yield();
			}
			Op.Cancel();
		});
		while (!Ready.load())
		{
			std::this_thread::yield();
		}
		Go.store(true);
		Context.Shutdown();
		Cancel.join();
		if (Op.State() != Network::EOperationState::Cancelled || !Impl->Active.empty())
		{
			return false;
		}
	}
	return true;
}

static bool WorkerOwnerReleaseTest()
{
	auto Context = std::make_shared<Network::FContext>(VeroScript::Boost::CreateNetworkService());
	auto Impl = Network::Detail::FAccess::Context(*Context);
	Network::FOperationOptions Infinite;
	Infinite.Timeout = std::nullopt;
	auto Pending = Network::Detail::FAccess::Wrap(Network::Detail::Begin(Impl, Infinite));
	std::atomic<bool> Go{false};
	Impl->Post(
	    [Keep = Context, &Go]() mutable
	{
		while (!Go.load())
		{
			std::this_thread::yield();
		}
		Keep.reset();
	});
	Context.reset();
	Go.store(true);
	const bool Reclaimed = Pending.Wait(std::chrono::seconds(1)) == Network::EWaitResult::Completed;
	// Also let the old implementation fail cleanly instead of leaking the probe.
	Impl->Shutdown();
	return Reclaimed && Pending.State() == Network::EOperationState::Cancelled;
}

static bool HostDrivenShutdownOrderTest()
{
	Network::FContextOptions Options;
	Options.Mode = Network::EDriveMode::HostDriven;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	auto Impl = Network::Detail::FAccess::Context(Context);
	std::array<int, 3> Order{};
	std::size_t Count = 0;
	Impl->Post(
	    [&]
	{
		Order[Count++] = 1;
		Context.Shutdown();
		Order[Count++] = 2;
	});
	Impl->Post(
	    [&]
	{
		Order[Count++] = 3;
	});
	Network::FError Error;
	Context.PollIo(Error, 1);
	return !Error && Count == 3 && Order == std::array<int, 3>{1, 2, 3};
}

static bool LastOwnerReleaseTest(Network::EDriveMode Mode)
{
	Network::FContextOptions Options;
	Options.Mode = Mode;
	auto Context = std::make_shared<Network::FContext>(VeroScript::Boost::CreateNetworkService(), Options);
	auto Impl = Network::Detail::FAccess::Context(*Context);
	std::weak_ptr<Network::Detail::FContext> Probe = Impl;
	std::atomic<bool> Go{false};
	Impl->Post(
	    [Keep = Context, &Go]() mutable
	{
		while (!Go.load())
		{
			std::this_thread::yield();
		}
		Keep.reset();
	});
	Impl.reset();
	auto* Raw = Context.get();
	Context.reset();
	Go.store(true);
	Network::FError Error;
	if (Mode == Network::EDriveMode::HostDriven)
	{
		Raw->PollIo(Error, 1);
	}
	const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!Probe.expired() && std::chrono::steady_clock::now() < Until)
	{
		std::this_thread::yield();
	}
	return !Error && Probe.expired();
}

static bool RecursivePollTest()
{
	Network::FContextOptions Options;
	Options.Mode = Network::EDriveMode::HostDriven;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	auto Impl = Network::Detail::FAccess::Context(Context);
	Network::FError Outer, Inner;
	std::size_t Nested = 1;
	int Called = 0;
	Impl->Post(
	    [&]
	{
		Nested = Context.PollIo(Inner, 1);
	});
	Impl->Post(
	    [&]
	{
		++Called;
	});
	const auto First = Context.PollIo(Outer, 1);
	if (First != 1 || Outer || Nested != 0 || Inner.Code != Network::EError::State || Called != 0)
	{
		return false;
	}
	return Context.PollIo(Outer, 1) == 1 && !Outer && Called == 1;
}

static bool AcceptReplacementTest(Network::EDriveMode Mode)
{
	Network::FContextOptions Options;
	Options.Mode = Mode;
	// Exactly one listener and the two sockets of a loopback connection.
	Options.MaxConnections = 3;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	Network::FError Error;
	auto Listener = Network::FTcpListener::Listen(Context, "127.0.0.1", 0, Error);
	if (!Listener || Error)
	{
		return false;
	}
	Network::FOperationOptions Infinite;
	Infinite.Timeout = std::nullopt;
	const auto PumpUntil = [&](const auto& Ready)
	{
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!Ready() && std::chrono::steady_clock::now() < Deadline)
		{
			if (Mode == Network::EDriveMode::HostDriven)
			{
				Context.PollIo(Error);
				if (Error)
				{
					return false;
				}
			}
			std::this_thread::yield();
		}
		return Ready();
	};
	for (unsigned Iteration = 0; Iteration < 20; ++Iteration)
	{
		auto Old = Listener->Accept(Infinite);
		if (!PumpUntil(
		        [&]
		{
			return Old.State() == Network::EOperationState::Running || Old.IsComplete();
		}) ||
		    Old.IsComplete())
		{
			return false;
		}
		Network::FOperation Next, Connect;
		// Finish publishes cancellation before it posts physical I/O cleanup. Force a
		// waiting caller's replacement into that interval, with no timing dependency.
		Network::Detail::ObserveTerminal(Network::Detail::FAccess::Operation(Old),
		    [&](auto, const auto&)
		{
			Next = Listener->Accept(Infinite);
			Connect = Network::FTcpSocket::Connect(Context, "127.0.0.1", Listener->Port(), Infinite);
		});
		if (!Old.Cancel() || Old.Error().Code != Network::EError::Cancelled ||
		    !PumpUntil(
		        [&]
		{
			return Next.IsComplete() && Connect.IsComplete();
		}) ||
		    Next.Error() || Connect.Error())
		{
			std::cerr << "Cancelled accept consumed a replacement connection\n";
			return false;
		}
		auto Peer = Next.Result<std::shared_ptr<Network::FTcpSocket>>();
		auto Client = Connect.Result<std::shared_ptr<Network::FTcpSocket>>();
		if (!Peer || !*Peer || !Client || !*Client)
		{
			return false;
		}
		(*Peer)->Close();
		(*Client)->Close();
	}
	Context.Shutdown();
	return true;
}

int main()
{
	try
	{
		if (!CodeOwnerIdentityTest())
		{
			std::cerr << "Code owner control-block retention failed\n";
			return 1;
		}
		const auto WorkerRelease = WorkerOwnerReleaseTest();
		const auto HostOrder = HostDrivenShutdownOrderTest();
		if (!WorkerRelease || !HostOrder || !LastOwnerReleaseTest(Network::EDriveMode::NativeBackground) ||
		    !LastOwnerReleaseTest(Network::EDriveMode::HostDriven) || !RecursivePollTest())
		{
			std::cerr << "Shutdown from I/O handling failed: worker owner release=" << WorkerRelease
			          << ", host-driven order=" << HostOrder << '\n';
			return 1;
		}
		if (!RetainedResultTest() || !WorkerShutdownReentryTest() || !ConcurrentCancelShutdownTest())
		{
			std::cerr << "Retained result or code owner lifetime failed\n";
			return 1;
		}
		for (const auto Mode : {Network::EDriveMode::HostDriven, Network::EDriveMode::NativeBackground})
		{
			if (!AcceptReplacementTest(Mode) || !Run(Mode, false) || !Run(Mode, true))
			{
				std::cerr << "I/O fault propagation or cleanup failed\n";
				return 1;
			}
		}
		std::cout << "I/O fault propagation and shutdown tests passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
