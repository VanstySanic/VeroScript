// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"

#include "VeroScriptHostBinding.h"
#include "VeroScriptNative.h"
#include "VeroScriptPath.h"
#include "VeroScriptValue.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <variant>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using FSocketHandle = SOCKET;
static constexpr FSocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
using FSocketHandle = int;
static constexpr FSocketHandle kInvalidSocket = -1;
#endif

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptDapTcpTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	std::string Frame(const std::string& Body)
	{
		return "Content-Length: " + std::to_string(Body.size()) + "\r\n\r\n" + Body;
	}

	std::string Request(const int Seq, const std::string& Command, const std::string& ArgsJson = "{}")
	{
		return Frame(
			std::string("{\"seq\":") + std::to_string(Seq)
				+ ",\"type\":\"request\",\"command\":\"" + Command
				+ "\",\"arguments\":" + ArgsJson + "}");
	}

	void CloseSocket(FSocketHandle Socket)
	{
		if (Socket == kInvalidSocket)
		{
			return;
		}
		#if defined(_WIN32)
		::closesocket(Socket);
		#else
		::close(Socket);
		#endif
	}

	bool SendAll(FSocketHandle Socket, const std::string& Data)
	{
		std::size_t Sent = 0;
		while (Sent < Data.size())
		{
			#if defined(_WIN32)
			const int N = ::send(
				Socket,
				Data.data() + Sent,
				static_cast<int>(Data.size() - Sent),
				0);
			#else
			#if defined(MSG_NOSIGNAL)
			constexpr int Flags = MSG_NOSIGNAL;
			#else
			constexpr int Flags = 0;
			#endif
			const ssize_t N = ::send(Socket, Data.data() + Sent, Data.size() - Sent, Flags);
			if (N < 0 && errno == EINTR)
			{
				continue;
			}
			#endif
			if (N <= 0)
			{
				return false;
			}
			Sent += static_cast<std::size_t>(N);
		}
		return true;
	}

	bool RecvUntil(FSocketHandle Socket, std::string& Acc, const std::string& Needle, int TimeoutMs)
	{
		const auto Deadline =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(TimeoutMs);
		char Buf[2048];
		while (Acc.find(Needle) == std::string::npos)
		{
			const auto Remaining = std::chrono::duration_cast<std::chrono::microseconds>(
				Deadline - std::chrono::steady_clock::now());
			if (Remaining.count() <= 0)
			{
				return false;
			}
			fd_set Readable;
			FD_ZERO(&Readable);
			FD_SET(Socket, &Readable);
			timeval Timeout{};
			Timeout.tv_sec = static_cast<decltype(Timeout.tv_sec)>(Remaining.count() / 1000000);
			Timeout.tv_usec = static_cast<decltype(Timeout.tv_usec)>(Remaining.count() % 1000000);
			#if defined(_WIN32)
			const int Ready = ::select(0, &Readable, nullptr, nullptr, &Timeout);
			#else
			const int Ready = ::select(Socket + 1, &Readable, nullptr, nullptr, &Timeout);
			#endif
			if (Ready <= 0)
			{
				return false;
			}
			#if defined(_WIN32)
			const int N = ::recv(Socket, Buf, static_cast<int>(sizeof(Buf)), 0);
			#else
			const ssize_t N = ::recv(Socket, Buf, sizeof(Buf), 0);
			#endif
			if (N <= 0)
			{
				return false;
			}
			Acc.append(Buf, static_cast<std::size_t>(N));
		}
		return true;
	}

	FSocketHandle ConnectLocal(std::uint16_t Port, int ReceiveBuffer = 0)
	{
		const FSocketHandle Socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (Socket == kInvalidSocket)
		{
			return kInvalidSocket;
		}
		if (ReceiveBuffer > 0 && ::setsockopt(Socket, SOL_SOCKET, SO_RCVBUF,
			reinterpret_cast<const char*>(&ReceiveBuffer), sizeof(ReceiveBuffer)) != 0)
		{
			CloseSocket(Socket);
			return kInvalidSocket;
		}
		sockaddr_in Addr{};
		Addr.sin_family = AF_INET;
		Addr.sin_port = htons(Port);
		#if defined(_WIN32)
		::InetPtonA(AF_INET, "127.0.0.1", &Addr.sin_addr);
		#else
		::inet_pton(AF_INET, "127.0.0.1", &Addr.sin_addr);
		#endif
		if (::connect(Socket, reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr)) != 0)
		{
			CloseSocket(Socket);
			return kInvalidSocket;
		}
		return Socket;
	}

	struct FLiveHostState
	{
		int CallCount = 0;
		int LastSum = 0;
	};

	bool LiveHostAdd(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		auto* State = static_cast<FLiveHostState*>(Context.UserData);
		const int Left = std::get<std::int32_t>(Context.Args[0]);
		const int Right = std::get<std::int32_t>(Context.Args[1]);
		const int Sum = Left + Right;
		if (State)
		{
			++State->CallCount;
			State->LastSum = Sum;
		}
		OutReturn = static_cast<std::int32_t>(Sum);
		return true;
	}

	class FPausedDomain final : public VeroScript::Tooling::IVeroScriptLiveDebugDomain
	{
	public:
		using FLiveDebugDomainInfo = VeroScript::Tooling::FLiveDebugDomainInfo;
		using FLiveDebugBreakpoint = VeroScript::Tooling::FLiveDebugBreakpoint;
		using FDapEvent = VeroScript::Tooling::FDapEvent;
		using FDapContinueResult = VeroScript::Tooling::FDapContinueResult;

		FLiveDebugDomainInfo Info() const override
		{
			FLiveDebugDomainInfo Info;
			Info.HostId = "tcp-host";
			Info.DomainId = "tcp-domain";
			return Info;
		}

		bool Connect(FEventHandler Handler, std::string&) override
		{
			Callback = std::move(Handler);
			return true;
		}

		bool Activate(std::string&) override
		{
			bStopped = true;
			FDapEvent Event;
			Event.Kind = VeroScript::Tooling::EDapEventKind::Stopped;
			Callback(Event);
			return true;
		}

		void Disconnect() noexcept override
		{
			++Disconnects;
			bStopped = false;
			Callback = {};
		}

		VeroScript::Tooling::FDapSetBreakpointsResult SetBreakpoints(
			std::string_view, const std::vector<FLiveDebugBreakpoint>&) override { return {}; }
		FDapContinueResult Resume(VeroScript::Tooling::EDebugRunMode) override { return {}; }
		FDapContinueResult Pause() override { return {}; }
		void RequestCancel() override { ++Cancels; }
		VeroScript::Tooling::FDapStackTraceResult StackTrace() const override
		{
			VeroScript::Tooling::FDapStackTraceResult Result;
			Result.StackFrames.push_back({0, std::string(16 * 1024 * 1024, 'x'), "live.vs", 2, 1});
			return Result;
		}
		VeroScript::Tooling::FDapScopesResult Scopes(int) const override { return {}; }
		VeroScript::Tooling::FDapVariablesResult Variables(int) const override { return {}; }
		VeroScript::Tooling::FDapEvaluateResult Evaluate(std::string_view, int, std::string_view) const override { return {}; }
		VeroScript::Tooling::FDapSetVariableResult SetVariable(int, std::string_view, std::string_view) override { return {}; }
		bool IsStopped() const override { return bStopped; }
		bool IsContextRunning() const override { return bStopped; }

		FEventHandler Callback;
		int Disconnects = 0;
		int Cancels = 0;
		bool bStopped = false;
	};

	bool StopEndpointTests()
	{
		using namespace VeroScript::Tooling;
		FDapTcpRunOptions Options;
		Options.Port = 0;
		Options.MaxSessions = 0;
		Options.bAnnounceListen = false;
		Options.Stdio.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Control = std::make_shared<FDapTcpRunControl>();
		bool bListening = false;
		Options.OnListening = [&](std::uint16_t) { bListening = true; };
		Options.Control->RequestStop();
		if (!Expect(VeroScriptDapTcp::Run(Options) == 0 && !bListening, "pre-stopped endpoint never binds"))
		{
			return false;
		}

		enum class EStopAt { Accept, IdleClient, Header, Payload, OwnedPause,
			OwnedDetachRunning, OwnedDetachPaused, DomainPause, SlowWrite };
		std::ofstream Trace;
		const char* TracePath = std::getenv("VEROSCRIPT_TEST_DAP_TCP_TRACE");
		if (TracePath)
		{
			Trace.open(VeroScript::PathUtil::FromUtf8(TracePath), std::ios::binary);
			if (!Expect(Trace.is_open(), "open TCP trace"))
			{
				return false;
			}
		}
		for (const auto StopAt : {EStopAt::Accept, EStopAt::IdleClient, EStopAt::Header,
			EStopAt::Payload, EStopAt::OwnedPause, EStopAt::OwnedDetachRunning,
			EStopAt::OwnedDetachPaused, EStopAt::DomainPause, EStopAt::SlowWrite})
		{
			const bool bDomain = StopAt == EStopAt::DomainPause || StopAt == EStopAt::SlowWrite;
			const bool bDetach = StopAt == EStopAt::OwnedDetachRunning || StopAt == EStopAt::OwnedDetachPaused;
			FPausedDomain Domain;
			std::promise<std::uint16_t> PortPromise;
			std::promise<int> ExitPromise;
			auto Bound = PortPromise.get_future();
			auto Exited = ExitPromise.get_future();
			Options.Control = std::make_shared<FDapTcpRunControl>();
			Options.OnListening = [&](std::uint16_t Port) { PortPromise.set_value(Port); };
			std::thread Server([&]()
			{
				ExitPromise.set_value(VeroScriptDapTcp::Run(Options, bDomain ? &Domain : nullptr));
			});

			const auto Timeout = std::chrono::seconds(5);
			bool bOk = Expect(Bound.wait_for(Timeout) == std::future_status::ready, "controlled endpoint is listening");
			FSocketHandle Client = kInvalidSocket;
			std::string Responses;
			const auto Send = [&](const std::string& Request)
			{
				if (TracePath)
				{
					Trace << "Client bytes (" << Request.size() << "):\n" << Request << '\n';
				}
				return SendAll(Client, Request);
			};
			if (TracePath)
			{
				Trace << "Stop scenario " << static_cast<int>(StopAt) << '\n';
			}
			if (bOk && StopAt != EStopAt::Accept)
			{
				Client = ConnectLocal(Bound.get(), StopAt == EStopAt::SlowWrite ? 4096 : 0);
				bOk = Expect(Client != kInvalidSocket, "connect to controlled endpoint");
			}
			if (bOk && Client != kInvalidSocket)
			{
				const auto Initialize = Request(1, "initialize");
				bOk = Send(Initialize) && RecvUntil(Client, Responses, "\"command\":\"initialize\"", 5000);
				if (bOk && (StopAt == EStopAt::Header || StopAt == EStopAt::Payload))
				{
					const auto Partial = Request(2, "threads");
					bOk = Send(Partial.substr(0, StopAt == EStopAt::Header ? 8 : Partial.size() - 1));
				}
				if (bOk && (bDomain || StopAt == EStopAt::OwnedPause || bDetach))
				{
					const auto Start = bDomain
						? Request(2, "attach", R"({"attachMode":"domain","domainId":"tcp-domain"})")
						: bDetach
							? Request(2, "launch", std::string(R"({"source":"void main(){print(\"owned_detach_running\");while(true){}}","fileLabel":"tcp-detach-stop.vs","stopOnEntry":)")
								+ (StopAt == EStopAt::OwnedDetachPaused ? "true}" : "false}"))
						: Request(2, "launch", R"({"source":"void main(){print(1);}","fileLabel":"tcp-stop.vs","stopOnEntry":true})");
					bOk = Send(Start)
						&& Send(Request(3, "configurationDone"))
						&& RecvUntil(Client, Responses, StopAt == EStopAt::OwnedDetachRunning
							? "owned_detach_running" : "\"event\":\"stopped\"", 5000);
				}
				if (bOk && bDetach)
				{
					bOk = Send(Request(4, "disconnect", R"({"terminateDebuggee":false})"))
						&& RecvUntil(Client, Responses, "\"command\":\"disconnect\"", 5000);
					// The disconnect resumes a paused call and preserves execution.
					// Give shutdown time to enter its owned-worker wait before the
					// separate host stop arrives; it must still be able to cancel.
					bOk = bOk && Expect(Exited.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
						"non-terminating disconnect preserves the owned program until host stop");
				}
				if (bOk && StopAt == EStopAt::SlowWrite)
				{
					// Observe the start of a reply larger than the socket buffers,
					// then leave its body unread while requesting stop.
					bOk = Send(Request(4, "stackTrace", R"({"threadId":1})"))
						&& RecvUntil(Client, Responses, "Content-Length: 16777", 5000);
				}
			}
			Options.Control->RequestStop();
			Options.Control->RequestStop();
			if (Exited.wait_for(Timeout) != std::future_status::ready)
			{
				CloseSocket(Client);
				std::exit(Fail("controlled endpoint did not stop"));
			}
			Server.join();
			CloseSocket(Client);
			if (TracePath)
			{
				Trace << "Server bytes read (" << Responses.size() << "):\n" << Responses << '\n';
			}
			if (!Expect(bOk && Exited.get() == 0 && Options.Control->IsStopRequested(), "controlled endpoint stops cleanly")
				|| !Expect(!bDomain || (Domain.Disconnects == 1 && Domain.Cancels == 0 && !Domain.bStopped),
					"stopping detaches the domain and resumes it without cancellation"))
			{
				return false;
			}
		}
		return true;
	}
}

int main()
{
	using namespace VeroScript::Tooling;

	#if defined(_WIN32)
	WSADATA Wsa{};
	if (::WSAStartup(MAKEWORD(2, 2), &Wsa) != 0)
	{
		return Fail("WSAStartup failed");
	}
	#endif
	#if !defined(_WIN32)
	// Do not restart interrupted syscalls in the signal handler: the transport
	// must preserve the listening socket and partially read protocol frames.
	struct sigaction SignalAction{};
	SignalAction.sa_handler = [](int)
	{
	};
	sigemptyset(&SignalAction.sa_mask);
	if (::sigaction(SIGUSR1, &SignalAction, nullptr) != 0)
	{
		return Fail("install interruption signal handler");
	}
	#endif

	VeroScript::FHostBindingRegistry Registry;
	std::string ErrorId;
	FLiveHostState LiveState;
	if (!Registry.RegisterNative(
			"host_add",
			VeroScript::ENativeType::Int,
			{VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
			LiveHostAdd,
				&LiveState,
			ErrorId))
	{
		return Fail("RegisterNative host_add");
	}

	std::mutex Mu;
	std::condition_variable Cv;
	std::uint16_t BoundPort = 0;
	bool bReady = false;
	int ServerExit = -1;

	std::thread Server(
		[&]()
		{
			FDapTcpRunOptions Opt;
		Opt.Stdio.VmBackendFactory = VeroScript::Test::VmFactory();
			Opt.BindAddress = "127.0.0.1";
			Opt.Port = 0;
			Opt.bAnnounceListen = false;
			Opt.Stdio.LiveBindings = &Registry;
			Opt.OnListening = [&](std::uint16_t Port)
			{
				{
					std::lock_guard<std::mutex> Lock(Mu);
					BoundPort = Port;
					bReady = true;
				}
				Cv.notify_one();
			};
			ServerExit = VeroScriptDapTcp::Run(Opt);
		});

	{
		std::unique_lock<std::mutex> Lock(Mu);
		if (!Cv.wait_for(Lock, std::chrono::seconds(5), [&]
		{
			return bReady;
		}))
		{
			Server.detach();
			return Fail("TCP DAP server did not become ready");
		}
	}

	#if !defined(_WIN32)
	// Interrupt accept() after the listener has become ready. Repeated signals
	// avoid depending on a single scheduling instant between callback/accept.
	for (int Index = 0; Index < 5; ++Index)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		::pthread_kill(Server.native_handle(), SIGUSR1);
	}
	#endif
	const FSocketHandle Client = ConnectLocal(BoundPort);
	if (!Expect(Client != kInvalidSocket, "connect to TCP DAP"))
	{
		Server.join();
		return EXIT_FAILURE;
	}

	constexpr const char* Source =
		"void main() {\\n"
		"    print(host_add(20, 22));\\n"
		"}\\n";

	std::string Out;
	#if !defined(_WIN32)
	// Leave the final byte of a framed request unread, interrupt recv(), then
	// finish the request. It must still produce one complete initialize reply.
	const std::string Initialize = Request(1, "initialize");
	const bool bPartialSent = SendAll(Client, Initialize.substr(0, Initialize.size() - 1));
	for (int Index = 0; Index < 5; ++Index)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		::pthread_kill(Server.native_handle(), SIGUSR1);
	}
	if (!Expect(bPartialSent && SendAll(Client, Initialize.substr(Initialize.size() - 1))
		&& RecvUntil(Client, Out, "\"command\":\"initialize\"", 5000),
		"interrupted protocol read preserves a partial initialize request"))
	{
		CloseSocket(Client);
		Server.join();
		return EXIT_FAILURE;
	}
	#else
	if (!Expect(SendAll(Client, Request(1, "initialize")), "send initialize request"))
	{
		CloseSocket(Client);
		Server.join();
		return EXIT_FAILURE;
	}
	#endif
	if (!Expect(
			SendAll(
					Client,
					Request(
						2,
						"attach",
						std::string("{\"source\":\"") + Source
							+ "\",\"fileLabel\":\"tcp-live.vs\",\"stopOnEntry\":false}"))
							&& SendAll(Client, Request(3, "configurationDone"))
							&& SendAll(
								Client,
								Request(4, "disconnect", "{\"terminateDebuggee\":false}")),
			"send DAP requests"))
	{
		CloseSocket(Client);
		Server.join();
		return EXIT_FAILURE;
	}

	if (!Expect(RecvUntil(Client, Out, "\"command\":\"attach\"", 10000),
			"recv attach response"))
	{
		CloseSocket(Client);
		Server.join();
		return EXIT_FAILURE;
	}

	if (!Expect(RecvUntil(Client, Out, "\"command\":\"configurationDone\"", 10000),
			"recv configurationDone response"))
	{
		CloseSocket(Client);
		Server.join();
		return EXIT_FAILURE;
	}
	if (!Expect(RecvUntil(Client, Out, "\"event\":\"terminated\"", 10000),
			"recv terminated event"))
	{
		CloseSocket(Client);
		Server.join();
		return EXIT_FAILURE;
	}

	CloseSocket(Client);
	Server.join();

	if (!Expect(ServerExit == 0, "TCP DAP server exit 0"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(LiveState.CallCount == 1 && LiveState.LastSum == 42, "live host_add via TCP DAP"))
	{
		std::cerr << "calls=" << LiveState.CallCount << " sum=" << LiveState.LastSum
			<< " out=\n"
			<< Out;
		return EXIT_FAILURE;
	}
	if (!Expect(Out.find("42") != std::string::npos, "DAP stream should mention 42"))
	{
		std::cerr << "out=\n"
				  << Out;
		return EXIT_FAILURE;
	}

	{
		// terminateDebuggee=true must stop an unlimited MaxSessions listen loop.
		VeroScript::FHostBindingRegistry TermRegistry;
		std::mutex Mu2;
		std::condition_variable Cv2;
		std::uint16_t TermPort = 0;
		bool bTermReady = false;
		int TermExit = -1;
		std::thread TermServer(
			[&]()
			{
				FDapTcpRunOptions Opt;
		Opt.Stdio.VmBackendFactory = VeroScript::Test::VmFactory();
				Opt.BindAddress = "127.0.0.1";
				Opt.Port = 0;
				Opt.MaxSessions = 0;
				Opt.bAnnounceListen = false;
				Opt.Stdio.LiveBindings = &TermRegistry;
				Opt.OnListening = [&](std::uint16_t Port)
				{
					{
						std::lock_guard<std::mutex> Lock(Mu2);
						TermPort = Port;
						bTermReady = true;
					}
					Cv2.notify_one();
				};
				TermExit = VeroScriptDapTcp::Run(Opt);
			});

		{
			std::unique_lock<std::mutex> Lock(Mu2);
			if (!Cv2.wait_for(Lock, std::chrono::seconds(5), [&]
			{
				return bTermReady;
			}))
			{
				TermServer.detach();
				return Fail("terminate TCP server not ready");
			}
		}

		const FSocketHandle TermClient = ConnectLocal(TermPort);
		if (!Expect(TermClient != kInvalidSocket, "connect terminate TCP DAP"))
		{
			TermServer.detach();
			return EXIT_FAILURE;
		}
		if (!Expect(
				SendAll(TermClient, Request(1, "initialize"))
					&& SendAll(
						TermClient,
						Request(2, "disconnect", "{\"terminateDebuggee\":true}")),
				"send terminate disconnect"))
		{
			CloseSocket(TermClient);
			TermServer.detach();
			return EXIT_FAILURE;
		}
		std::string TermOut;
		(void)RecvUntil(TermClient, TermOut, "\"command\":\"disconnect\"", 5000);
		CloseSocket(TermClient);
		TermServer.join();
		if (!Expect(TermExit == 0, "terminateDebuggee should stop listen cleanly"))
		{
			std::cerr << "TermExit=" << TermExit << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!StopEndpointTests())
	{
		return EXIT_FAILURE;
	}
	#if defined(_WIN32)
	::WSACleanup();
	#endif
	std::cout << "VeroScriptDapTcpTests OK\n";
	return EXIT_SUCCESS;
}
