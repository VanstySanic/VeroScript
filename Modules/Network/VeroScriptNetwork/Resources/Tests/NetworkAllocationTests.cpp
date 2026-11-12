// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"


#include "NetworkInternal.h"
#include "NetworkStream.h"
#include "VeroScriptNetworkFiles.h"
#include "NetworkTestPaths.h"
#if defined(VEROSCRIPT_NETWORK_TEST_PROTOCOLS)
#include "VeroScriptCurl.h"
#include "../../../VeroScriptHttp/Resources/Tests/HttpTestBackends.h"
#include "../../../VeroScriptWebSocket/Resources/Tests/WebSocketTestBackends.h"
#include "VeroScriptHttp.h"
#include "VeroScriptHttpFiles.h"
#include "VeroScriptWebSocket.h"
#include "VeroScriptWebSocketReconnect.h"
#endif
#include <cstdlib>
#include <iostream>
#include <new>
#include <filesystem>

namespace
{
	thread_local bool FailAllocation = false;
	thread_local int AllocationCountdown = -1;
	void* Allocate(std::size_t Size)
	{
		if (FailAllocation || AllocationCountdown == 0)
		{
			throw std::bad_alloc();
		}
		if (AllocationCountdown > 0)
		{
			--AllocationCountdown;
		}
		if (void* Block = std::malloc(Size ? Size : 1))
		{
			return Block;
		}
		throw std::bad_alloc();
	}

	struct FFailureScope
	{
		FFailureScope()
		{
			FailAllocation = true;
		}
		~FFailureScope()
		{
			FailAllocation = false;
		}
	};

	void Check(bool Ok, const char* Text)
	{
		if (!Ok)
		{
			throw std::runtime_error(Text);
		}
	}

	VeroScript::Network::FContextOptions Options()
	{
		VeroScript::Network::FContextOptions Value;
		Value.Mode = VeroScript::Network::EDriveMode::HostDriven;
		return Value;
	}

	void RegistrationFailure()
	{
		namespace N = VeroScript::Network;
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
		auto Impl = N::Detail::FAccess::Context(Context);
		N::FOperationOptions Infinite;
		Infinite.Timeout = std::nullopt;
		bool Observed = false;
		for (int I = 0; I < 1024 && !Observed; ++I)
		{
			auto Op = N::Detail::Begin(Impl, Infinite);
			Impl->Finish(Op, N::EOperationState::Succeeded);
			auto Value = N::Detail::FAccess::Wrap(Op);
			N::FError Error;
			const auto Before = Impl->CallbackCount;
			{
				FFailureScope Failure;
				Observed = !Value.OnComplete(
							   [](const auto&)
							   {
							   },
							   Error) &&
						   Error.Code == N::EError::Allocation;
			}
			if (Observed)
			{
				Check(Impl->CallbackCount == Before && !Op->bCallbackRegistered && !Op->Callback,
					"Failed callback registration committed partial state");
				Check(Value.OnComplete(
						  [](const auto&)
						  {
						  },
						  Error) &&
						  !Error,
					"Callback registration could not be retried after allocation failure");
			}
		}
		Check(Observed, "Callback queue allocation boundary was not exercised");
		Context.Shutdown();
	}
	#if defined(VEROSCRIPT_NETWORK_TEST_PROTOCOLS)
	void ReconnectAllocationFailure()
	{
		namespace N = VeroScript::Network;
		namespace W = VeroScript::WebSocket;
		bool AdmissionFailed = false, IoFailed = false, SnapshotFailed = false, Started = false;
		for (int Phase = 0; Phase < 3; ++Phase)
		{
			for (int Boundary = 0; Boundary < 96; ++Boundary)
			{
				N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
				W::FReconnectPolicy Policy;
				Policy.MaxAttempts = 3;
				Policy.Delay = Policy.MaxDelay = std::chrono::seconds(30);
				Policy.MaxElapsed = std::chrono::seconds(60);
				W::FReconnectTask Task;
				N::FError Error;
				std::shared_ptr<const W::FReconnectSnapshot> Snapshot;
				try
				{
					if (Phase == 0)
					{
						AllocationCountdown = Boundary;
					}
					Task = VeroScript::WebSocket::Test::ConnectWithRetry(Context, "not-a-websocket-url", Policy);
					AllocationCountdown = -1;
					Started = Started || Task.Operation().IsValid();
					if (Phase == 1)
					{
						AllocationCountdown = Boundary;
					}
					Context.PollIo(Error);
					AllocationCountdown = -1;
					if (Phase == 1)
					{
						IoFailed = IoFailed || Context.IsClosed() || Error.Code == N::EError::Allocation ||
								   Task.Operation().Error().Code == N::EError::Allocation;
					}
					if (Phase == 2)
					{
						for (int Step = 0; Step < 16 && !Task.Operation().IsComplete(); ++Step)
						{
							Context.PollIo(Error);
						}
						AllocationCountdown = Boundary;
						Snapshot = Task.Snapshot(Error);
						AllocationCountdown = -1;
						SnapshotFailed = SnapshotFailed || Error.Code == N::EError::Allocation;
					}
				}
				catch (const std::bad_alloc&)
				{
					AllocationCountdown = -1;
					if (Phase == 0)
					{
						AdmissionFailed = true;
					}
					else if (Phase == 1)
					{
						IoFailed = true;
					}
					else
					{
						SnapshotFailed = true;
					}
				}
				catch (...)
				{
					AllocationCountdown = -1;
					throw;
				}
				if (Phase == 0)
				{
					AdmissionFailed = AdmissionFailed || Task.Operation().Error().Code == N::EError::Allocation;
				}
				{
					FFailureScope Failure;
					Task.Operation().Cancel();
					Context.Shutdown();
				}
				Snapshot.reset();
				Task = {};
				if (const auto Retained = Context.MemoryUsage().RetainedBytes)
				{
					std::cerr << "Reconnect phase " << Phase << ", boundary " << Boundary << ": " << Retained << " bytes retained\n";
				}
				Check(
					Context.MemoryUsage().RetainedBytes == 0, "Reconnect retained storage after shutdown and release");
			}
		}
		Check(AdmissionFailed && IoFailed && SnapshotFailed && Started,
			"Reconnect allocation boundaries were not exercised");
		// Reach a real refused connection and a pending retry timer, then remove
		// the heap before logical cancellation and physical shutdown.
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
		N::FError Error;
		auto Listener = N::FTcpListener::Listen(Context, "127.0.0.1", 0, Error);
		Check(Listener && !Error, "Could not reserve a refusal fixture port");
		const auto Port = Listener->Port();
		Listener->Close();
		Context.PollIo(Error);
		W::FReconnectPolicy Policy;
		Policy.MaxAttempts = 3;
		Policy.Delay = Policy.MaxDelay = std::chrono::seconds(30);
		Policy.MaxElapsed = std::chrono::seconds(60);
		auto Task = VeroScript::WebSocket::Test::ConnectWithRetry(Context, "ws://127.0.0.1:" + std::to_string(Port), Policy);
		W::FReconnectSnapshot Snapshot;
		const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < Until)
		{
			Context.PollIo(Error);
			Check(!Error && Task.TrySnapshot(Snapshot, Error), "Reconnect backoff did not make progress");
			if (Snapshot.Attempts.size() == 1 && Snapshot.Attempts[0].bComplete)
			{
				break;
			}
		}
		Check(Snapshot.Attempts.size() == 1 && Snapshot.Attempts[0].bComplete && !Snapshot.bFinal,
			"A pending reconnect backoff was not exercised");
		{
			FFailureScope Failure;
			Task.Operation().Cancel();
			Context.Shutdown();
		}
		Check(Task.TrySnapshot(Snapshot, Error) && Snapshot.bFinal && Snapshot.Error.Code == N::EError::Cancelled,
			"Reconnect cancellation failed without heap storage");
		Task = {};
		Listener.reset();
		Check(Context.MemoryUsage().RetainedBytes == 0, "Cancelled reconnect kept physical/diagnostic storage");
	}
	#endif

	void CompletionFailure()
	{
		namespace N = VeroScript::Network;
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
		auto Impl = N::Detail::FAccess::Context(Context);
		N::FOperationOptions Infinite;
		Infinite.Timeout = std::nullopt;
		std::vector<std::weak_ptr<int>> Probes;
		N::FError Error;
		for (int I = 0; I < 1024 && !Context.IsClosed(); ++I)
		{
			auto Op = N::Detail::Begin(Impl, Infinite);
			auto Value = N::Detail::FAccess::Wrap(Op);
			auto Marker = std::make_shared<int>(I);
			Probes.push_back(Marker);
			Check(Value.OnComplete(
					  [Keep = Value, Marker](const auto&)
					  {
						  (void)Keep;
						  (void)Marker;
					  },
					  Error),
				"Could not register completion probe");
			Marker.reset();
			Impl->Post(
				[Impl, Op]
				{
					FFailureScope Failure;
					Impl->Finish(Op, N::EOperationState::Succeeded);
				});
			Context.PollIo(Error, 1);
		}
		Check(Context.IsClosed() && Error.Code == N::EError::Allocation,
			"Completion queue allocation failure did not reach the I/O fault boundary");
		Context.Shutdown();
		for (const auto& Probe : Probes)
		{
			Check(Probe.expired(), "Completion queue allocation failure leaked a self-captured operation");
		}
	}

	void DispatchFailure()
	{
		namespace N = VeroScript::Network;
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
		auto Impl = N::Detail::FAccess::Context(Context);
		N::FOperationOptions Infinite;
		Infinite.Timeout = std::nullopt;
		N::FError Error;
		unsigned Called = 0;
		for (int I = 0; I < 65; ++I)
		{
			auto Op = N::Detail::Begin(Impl, Infinite);
			Impl->Finish(Op, N::EOperationState::Succeeded);
			Check(N::Detail::FAccess::Wrap(Op).OnComplete(
					  [&](const auto&)
					  {
						  ++Called;
					  },
					  Error),
				"Could not register dispatch probe");
		}
		{
			FFailureScope Failure;
			Check(Context.Dispatch(Error) == 0 && Error.Code == N::EError::Allocation,
				"Dispatch did not preserve its queue when preparing the batch failed");
		}
		Check(!Called && Impl->CallbackCount == 65 && Impl->Completed.size() == 65,
			"Failed dispatch changed callback accounting");
		Check(Context.Dispatch(Error, 65) == 65 && !Error && Called == 65,
			"Dispatch could not be retried after allocation failure");
		Context.Shutdown();
	}

	void ShutdownFailure(bool FaultFirst)
	{
		namespace N = VeroScript::Network;
		auto Context = std::make_unique<N::FContext>(VeroScript::Boost::CreateNetworkService(), Options());
		auto Impl = N::Detail::FAccess::Context(*Context);
		auto Op = N::Detail::Begin(Impl, {});
		auto Sibling = N::Detail::Begin(Impl, {});
		auto Value = N::Detail::FAccess::Wrap(Op);
		auto Marker = std::make_shared<int>(1);
		std::weak_ptr<int> Probe = Marker;
		N::FError Error;
		Check(Value.OnComplete(
				  [Keep = Value, Marker](const auto&)
				  {
					  (void)Keep;
					  (void)Marker;
				  },
				  Error),
			"Could not register shutdown probe");
		Marker.reset();
		bool Aborted = false, Closed = false, SiblingsTerminal = false;
		N::Detail::SetAbort(Op,
			[&]
			{
				Aborted = true;
				SiblingsTerminal = N::Detail::Terminal(Sibling->Status);
			});
		Check(Impl->AddResource(
				  [&]
				  {
					  Closed = true;
				  }) != 0,
			"Could not register shutdown resource");
		// Start the deadline so an orphaned timer would otherwise keep shutdown alive.
		Context->PollIo(Error);
		{
			FFailureScope Failure;
			if (FaultFirst)
			{
				Impl->Fault(N::EError::Allocation);
			}
			Context->Shutdown();
			Context.reset();
		}
		Check(Impl->Closed && Impl->Active.empty() && !Impl->FaultedOperations && Impl->Completed.empty() &&
				  Impl->Resources.empty() && Impl->CallbackCount == 0,
			"Shutdown left queued operations or resources after allocation failure");
		Check(Aborted && Closed && SiblingsTerminal && Probe.expired(),
			"Shutdown did not publish sibling states and reclaim abort/resource/callback ownership");
		Check(Value.State() == (FaultFirst ? N::EOperationState::Failed : N::EOperationState::Cancelled),
			"Shutdown changed the operation terminal state");
	}

	void HandleDestructionFailure()
	{
		namespace N = VeroScript::Network;
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
		N::FError Error;
		auto Listener = N::FTcpListener::Listen(Context, "127.0.0.1", 0, Error);
		Check(Listener && !Error, "Could not create destructor probe");
		{
			FFailureScope Failure;
			Listener.reset();
			Context.Shutdown();
		}
		Check(Context.IsClosed(), "Handle destruction did not survive a failed post");
	}
	void CancelWithoutHeap()
	{
		namespace N = VeroScript::Network;
		N::FContext C(VeroScript::Boost::CreateNetworkService(), Options());
		auto Impl = N::Detail::FAccess::Context(C);
		auto Op = N::Detail::Begin(Impl, N::FOperationOptions{std::nullopt});
		auto Handle = N::Detail::FAccess::Wrap(Op);
		bool Aborted = false;
		N::Detail::SetAbort(Op,
			[&]
			{
				Aborted = true;
			});
		{
			FFailureScope Failure;
			Check(Handle.Cancel(), "Cancellation did not publish its terminal state without heap");
			C.Shutdown();
		}
		Check(Handle.State() == N::EOperationState::Cancelled && Aborted,
			"Failed cancellation dispatch lost its pending native abort");
	}
	void TcpShutdownWithoutHeap()
	{
		namespace N = VeroScript::Network;
		N::FContext C(VeroScript::Boost::CreateNetworkService(), Options());
		N::FError E;
		auto Listener = N::FTcpListener::Listen(C, "127.0.0.1", 0, E);
		Check(Listener && !E, "Cannot prepare heapless TCP shutdown");
		auto Accepted = Listener->Accept();
		auto Connected = N::FTcpSocket::Connect(C, "127.0.0.1", Listener->Port());
		auto Poll = [&](const N::FOperation& Op)
		{
			const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!Op.IsComplete())
			{
				Check(std::chrono::steady_clock::now() < End, "Heapless TCP fixture timed out");
				C.PollIo(E);
				Check(!E, "Heapless TCP fixture failed");
			}
		};
		Poll(Accepted);
		Poll(Connected);
		auto A = *Accepted.Result<std::shared_ptr<N::FTcpSocket>>();
		auto B = *Connected.Result<std::shared_ptr<N::FTcpSocket>>();
		auto ReadA = A->Read(16), ReadB = B->Read(16);
		C.PollIo(E);
		Check(!E && !ReadA.IsComplete() && !ReadB.IsComplete(), "TCP reads did not enter their pending state");
		{
			FFailureScope Failure;
			C.Shutdown();
		}
		Check(ReadA.IsComplete() && ReadB.IsComplete(), "Heapless TCP shutdown stranded an active read");
		A.reset();
		B.reset();
		Listener.reset();
		Accepted = {};
		Connected = {};
		ReadA = {};
		ReadB = {};
		Check(C.MemoryUsage().RetainedBytes == 0, "Heapless TCP shutdown retained unreachable buffers");
	}

	void FileAdmissionFailure()
	{
		namespace N = VeroScript::Network;
		const auto Root =
			std::filesystem::current_path() /
			std::filesystem::u8path(
				"file-allocation-文件 " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		Check(std::filesystem::create_directory(Root), "Could not create file allocation fixture");
		unsigned Failures = 0, Claimed = 0;
		for (int Boundary = 0; Boundary < 64; ++Boundary)
		{
			N::FContext C(VeroScript::Boost::CreateNetworkService(), Options());
			N::FFileExecutor Files(C);
			N::FBytePipe P;
			N::FError E;
			Check(N::TryCreateBytePipe(16, P, E), "Could not create file allocation source");
			N::FFileWriteOptions W;
			W.Path = N::Test::PathToUtf8(Root / "output.bin", true);
			N::FFileOperation Task;
			AllocationCountdown = Boundary;
			try
			{
				Task = Files.Write(std::move(W), P.Reader);
			}
			catch (const std::bad_alloc&)
			{
				++Failures;
			}
			catch (...)
			{
				AllocationCountdown = -1;
				throw;
			}
			AllocationCountdown = -1;
			if (Task.Operation().Error().Code == N::EError::Allocation)
			{
				++Failures;
			}
			// Exercise shutdown of admitted jobs as well as allocation failures before/after reader claim.
			C.PollIo(E, 8);
			{
				FFailureScope Failure;
				C.Shutdown();
			}
			Check(Task.Operation().IsComplete(), "File allocation failure left a pending operation");
			if (!N::Detail::FStreamAccess::ClaimReader(*P.Reader))
			{
				++Claimed;
				Check(P.Writer->WriteAll("x", 1).State == N::EStreamState::Failed,
					"File allocation failure stranded a claimed source");
			}
			Check(Files.Statistics().PendingFiles == 0 && Files.Statistics().ActiveWorkers == 0,
				"File allocation shutdown retained physical jobs");
		}
		Check(Failures && Claimed, "File allocation boundaries were not exercised");
		Check(std::filesystem::is_empty(Root), "File allocation failure retained a temporary output");
		std::filesystem::remove(Root);
	}

	#if defined(VEROSCRIPT_NETWORK_TEST_PROTOCOLS)
	void DownloadAdmissionFailure()
	{
		namespace N = VeroScript::Network;
		namespace H = VeroScript::Http;
		const auto Root =
			std::filesystem::current_path() /
			std::filesystem::u8path("download-allocation-文件 " +
									std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		Check(std::filesystem::create_directory(Root), "Could not create download allocation fixture");
		unsigned Failures = 0;
		for (int Boundary = 0; Boundary < 96; ++Boundary)
		{
			N::FContext C(VeroScript::Boost::CreateNetworkService(), Options());
			N::FFileExecutor Files(C);
			H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
			H::FRequest Q;
			Q.Url = "http://127.0.0.1:1/unused";
			H::FDownloadOptions D;
			D.File.Path = N::Test::PathToUtf8(Root / "output.bin", true);
			H::FDownloadOperation Task;
			AllocationCountdown = Boundary;
			try
			{
				Task = H::DownloadFile(C, Client, Files, std::move(Q), std::move(D));
			}
			catch (const std::bad_alloc&)
			{
				++Failures;
			}
			catch (...)
			{
				AllocationCountdown = -1;
				throw;
			}
			AllocationCountdown = -1;
			if (Task.Operation().Error().Code == N::EError::Allocation)
			{
				++Failures;
			}
			N::FError E;
			C.PollIo(E, 8);
			{
				FFailureScope Failure;
				C.Shutdown();
			}
			Check(Task.Operation().IsComplete(), "Download setup failure left a pending operation");
			Check(Files.Statistics().PendingFiles == 0 && Files.Statistics().ActiveWorkers == 0,
				"Download setup failure stranded file work");
		}
		Check(Failures > 0, "Download allocation boundaries were not exercised");
		Check(std::filesystem::is_empty(Root), "Download setup failure retained a temporary output");
		std::filesystem::remove(Root);
	}

	void InputAdmissionFailure(bool Multipart)
	{
		namespace N = VeroScript::Network;
		namespace H = VeroScript::Http;
		unsigned Claimed = 0, Failures = 0;
		for (int Boundary = 0; Boundary < 96; ++Boundary)
		{
			N::FContext C(VeroScript::Boost::CreateNetworkService(), Options());
			H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
			N::FError E;
			N::FBytePipe P;
			Check(N::TryCreateBytePipe(16, P, E), "Could not create input admission probe");
			H::FRequest Q;
			Q.Method = "POST";
			Q.Url = "http://127.0.0.1:1/unused";
			if (Multipart)
			{
				Q.Multipart.emplace();
				H::FMultipartPart Part;
				Part.Name = "source";
				Part.Upload = P.Reader;
				Q.Multipart->push_back(std::move(Part));
			}
			else
			{
				Q.Upload = P.Reader;
			}
			auto Op = Client.Send(Q);
			AllocationCountdown = Boundary;
			try
			{
				C.PollIo(E, 64);
			}
			catch (...)
			{
				AllocationCountdown = -1;
				throw;
			}
			AllocationCountdown = -1;
			if (E.Code == N::EError::Allocation || Op.Error().Code == N::EError::Allocation)
			{
				++Failures;
			}
			Client.Close();
			C.Shutdown();
			Check(Op.IsComplete(), "Input admission failure left a pending operation");
			// Unclaimed sources still belong to the caller. Once claimed, every exit must cancel it.
			if (!N::Detail::FStreamAccess::ClaimReader(*P.Reader))
			{
				++Claimed;
				Check(P.Writer->WriteAll("x", 1).State == N::EStreamState::Failed,
					"Failed admission stranded a claimed input source");
			}
		}
		Check(Claimed > 0 && Failures > 0, "Input admission failure boundaries were not exercised");
	}
	void UpgradeAdmissionFailure()
	{
		namespace N = VeroScript::Network;
		namespace H = VeroScript::Http;
		namespace W = VeroScript::WebSocket;
		unsigned Before = 0, After = 0;
		for (const bool DuringIo : {false, true})
		{
			for (int Boundary = 0; Boundary < 96; ++Boundary)
			{
				if (std::getenv("VEROSCRIPT_TRACE_UPGRADE_ALLOCATIONS"))
				{
					std::cerr << "upgrade phase " << DuringIo << " boundary " << Boundary << '\n';
				}
				N::FContext C(VeroScript::Boost::CreateNetworkService(), Options());
				N::FError E;
				auto Poll = [&](const N::FOperation& Op)
				{
					const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
					while (!Op.IsComplete())
					{
						Check(std::chrono::steady_clock::now() < End, "Upgrade allocation fixture timed out");
						C.PollIo(E);
						Check(!E, "Upgrade allocation fixture setup failed");
					}
				};
				auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E);
				Check(Server && !E, "Cannot create upgrade allocation listener");
				auto Accept = Server->Accept();
				auto Connecting = N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port());
				Poll(Connecting);
				auto Socket = *Connecting.Result<std::shared_ptr<N::FTcpSocket>>();
				const std::string Head =
					"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n"
					"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
				auto Sent = Socket->Write(N::FBytes(Head.begin(), Head.end()));
				Poll(Sent);
				Poll(Accept);
				auto Exchange = *Accept.Result<std::shared_ptr<H::FExchange>>();
				auto Source = Exchange->PrepareUpgrade(E);
				Check(Source && !E, "Cannot prepare upgrade allocation source");
				N::FOperation Upgrading;
				if (DuringIo)
				{
					Upgrading = VeroScript::WebSocket::Test::AcceptUpgrade(C, Source);
				}
				bool Threw = false;
				AllocationCountdown = Boundary;
				try
				{
					if (DuringIo)
					{
						C.PollIo(E, 64);
					}
					else
					{
						Upgrading = VeroScript::WebSocket::Test::AcceptUpgrade(C, Source);
					}
				}
				catch (const std::bad_alloc&)
				{
					Threw = true;
				}
				catch (...)
				{
					AllocationCountdown = -1;
					throw;
				}
				AllocationCountdown = -1;
				if (Threw || E.Code == N::EError::Allocation || Upgrading.Error().Code == N::EError::Allocation)
				{
					if (Source->IsConsumed())
					{
						++After;
					}
					else
					{
						++Before;
					}
				}
				{
					FFailureScope Failure;
					C.Shutdown();
				}
				Check(Upgrading.IsComplete(), "Upgrade allocation failure left a pending task");
				Source.reset();
				Exchange.reset();
				Socket.reset();
				Server.reset();
				Accept = {};
				Connecting = {};
				Sent = {};
				Upgrading = {};
				Check(C.MemoryUsage().RetainedBytes == 0,
					"Upgrade allocation failure retained an unreachable transport or quota");
			}
		}
		Check(Before && After, "Both sides of the upgrade transfer allocation boundary must be exercised");
	}

	void ProtocolShutdownFailure(bool FaultFirst)
	{
		namespace N = VeroScript::Network;
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
		N::FError Error;
		auto Client = std::make_unique<VeroScript::Http::FClient>(Context, VeroScript::Curl::CreateHttpClientService());
		auto Http = VeroScript::Http::Test::Listen(Context, "127.0.0.1", 0, Error);
		Check(Http && !Error, "Could not create HTTP shutdown probe");
		auto Ws = VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, Error);
		Check(Ws && !Error, "Could not create WebSocket shutdown probe");
		auto HttpAccept = Http->Accept();
		auto WsAccept = Ws->Accept();
		Context.PollIo(Error);
		{
			FFailureScope Failure;
			if (FaultFirst)
			{
				N::Detail::FAccess::Context(Context)->Fault(N::EError::Allocation);
			}
			Context.Shutdown();
			Client.reset();
			Http.reset();
			Ws.reset();
		}
		Check(HttpAccept.IsComplete() && WsAccept.IsComplete() && Context.IsClosed(),
			"Protocol shutdown left pending operations after allocation failure");
	}

	void HttpStreamShutdownFailure(bool FaultFirst)
	{
		namespace N = VeroScript::Network;
		namespace H = VeroScript::Http;
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
		N::FError Error;
		H::FServerOptions Limits;
		Limits.bStreamRequests = true;
		auto Server = VeroScript::Http::Test::Listen(Context, "127.0.0.1", 0, Error, Limits);
		Check(Server && !Error, "Could not create stream shutdown listener");
		auto Accept = Server->Accept();
		auto Connecting = N::FTcpSocket::Connect(Context, "127.0.0.1", Server->Port());
		auto Poll = [&](auto Done)
		{
			const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!Done())
			{
				Check(std::chrono::steady_clock::now() < End, "Stream shutdown fixture timed out");
				Context.PollIo(Error);
				Check(!Error, "Stream shutdown fixture poll failed");
			}
		};
		Poll(
			[&]
			{
				return Connecting.IsComplete();
			});
		auto Socket = *Connecting.Result<std::shared_ptr<N::FTcpSocket>>();
		const std::string Header = "POST /pending HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4096\r\n\r\n";
		auto Sent = Socket->Write(N::FBytes(Header.begin(), Header.end()));
		Poll(
			[&]
			{
				return Sent.IsComplete() && Accept.IsComplete();
			});
		auto Exchange = *Accept.Result<std::shared_ptr<H::FExchange>>();
		auto Body = Exchange->Request().BodyOperation;
		N::FBytePipe Output;
		Check(N::TryCreateBytePipe(Context, 64, Output, Error), "Could not create response shutdown pipe");
		H::FResponse Response;
		Response.Status = 200;
		auto Reply = Exchange->ReplyStream(Response, {Output.Reader, {}});
		Poll(
			[&]
			{
				return Reply.State() == H::EResponseState::HeadersSent || Reply.Operation().IsComplete();
			});
		Check(!Body.IsComplete() && !Reply.Operation().IsComplete(), "Stream shutdown fixture was not active");
		auto Shutdown = Server->Shutdown(std::chrono::seconds(5), N::FOperationOptions{std::nullopt});
		Poll(
			[&]
			{
				return Shutdown.State() == N::EOperationState::Running;
			});
		{
			FFailureScope Failure;
			if (FaultFirst)
			{
				N::Detail::FAccess::Context(Context)->Fault(N::EError::Allocation);
			}
			Context.Shutdown();
			Exchange.reset();
			Server.reset();
			Socket.reset();
			Output = {};
			Accept = {};
			Connecting = {};
			Sent = {};
		}
		Check(Context.IsClosed() && Body.IsComplete() && Reply.Operation().IsComplete() && Shutdown.IsComplete(),
			"Stream shutdown left a pending operation");
		Check(Context.MemoryUsage().RetainedBytes == 0, "Stream shutdown retained an unreachable body/response cycle");
	}
	#endif

	void CallbackDiagnosticFailure()
	{
		namespace N = VeroScript::Network;
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options());
		auto Impl = N::Detail::FAccess::Context(Context);
		N::FOperationOptions Infinite;
		Infinite.Timeout = std::nullopt;
		auto Op = N::Detail::Begin(Impl, Infinite);
		Impl->Finish(Op, N::EOperationState::Succeeded);
		N::FError Error;
		const std::runtime_error Failure("Callback failure whose diagnostic exceeds small string storage");
		Check(N::Detail::FAccess::Wrap(Op).OnComplete(
				  [Failure](const auto&)
				  {
					  FailAllocation = true;
					  throw Failure;
				  },
				  Error),
			"Could not register diagnostic probe");
		std::size_t Count;
		try
		{
			Count = Context.Dispatch(Error);
			FailAllocation = false;
		}
		catch (...)
		{
			FailAllocation = false;
			throw;
		}
		Check(Count == 1 && Error.Code == N::EError::Callback && !Impl->bDispatching,
			"Failed callback diagnostic left dispatch active");
		Check(Context.Dispatch(Error) == 0 && !Error, "Dispatch did not recover after a callback diagnostic failure");
		Context.Shutdown();
	}
} // namespace

void* operator new(std::size_t Size)
{
	return Allocate(Size);
}
void* operator new[](std::size_t Size)
{
	return Allocate(Size);
}
void operator delete(void* Block) noexcept
{
	std::free(Block);
}
void operator delete[](void* Block) noexcept
{
	std::free(Block);
}
void operator delete(void* Block, std::size_t) noexcept
{
	std::free(Block);
}
void operator delete[](void* Block, std::size_t) noexcept
{
	std::free(Block);
}

int main()
{
	#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
	std::cout << "Global allocation injection requires the MSVC release STL; skipped\n";
	return 77;
	#endif
	try
	{
		RegistrationFailure();
		CompletionFailure();
		DispatchFailure();
		CallbackDiagnosticFailure();
		ShutdownFailure(false);
		ShutdownFailure(true);
		HandleDestructionFailure();
		CancelWithoutHeap();
		TcpShutdownWithoutHeap();
		FileAdmissionFailure();
		#if defined(VEROSCRIPT_NETWORK_TEST_PROTOCOLS)
		DownloadAdmissionFailure();
		ReconnectAllocationFailure();
		UpgradeAdmissionFailure();
		InputAdmissionFailure(false);
		InputAdmissionFailure(true);
		ProtocolShutdownFailure(false);
		ProtocolShutdownFailure(true);
		HttpStreamShutdownFailure(false);
		HttpStreamShutdownFailure(true);
		#endif
		std::cout << "Network allocation failure tests passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
