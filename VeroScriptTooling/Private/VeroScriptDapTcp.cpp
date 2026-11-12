// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptDapTcp.h"

#include "VeroScriptDapStdio.h"

#include <iostream>
#include <mutex>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using FSocketHandle = SOCKET;
using FSocketLength = int;
static constexpr FSocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using FSocketHandle = int;
using FSocketLength = socklen_t;
static constexpr FSocketHandle kInvalidSocket = -1;
#endif

namespace VeroScript::Tooling
{
	namespace
	{
		#if defined(_WIN32)
		struct FWinsockLifetime
		{
			FWinsockLifetime()
			{
				WSADATA Data{};
				bOk = (::WSAStartup(MAKEWORD(2, 2), &Data) == 0);
			}

			~FWinsockLifetime()
			{
				if (bOk)
				{
					::WSACleanup();
				}
			}

			bool bOk = false;

		};
		#endif

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

		struct FSocket
		{
			explicit FSocket(FSocketHandle Handle = kInvalidSocket) : Handle(Handle) {}
			~FSocket() { CloseSocket(Handle); }
			FSocket(const FSocket&) = delete;
			FSocket& operator=(const FSocket&) = delete;

			FSocketHandle Handle;
		};

		bool SetNonBlocking(FSocketHandle Socket)
		{
			#if defined(_WIN32)
			u_long Enabled = 1;
			return ::ioctlsocket(Socket, FIONBIO, &Enabled) == 0;
			#else
			const int Flags = ::fcntl(Socket, F_GETFL);
			return Flags >= 0 && ::fcntl(Socket, F_SETFL, Flags | O_NONBLOCK) == 0;
			#endif
		}

		int SocketError()
		{
			#if defined(_WIN32)
			return ::WSAGetLastError();
			#else
			return errno;
			#endif
		}

		bool IsInterrupted(int Error)
		{
			#if defined(_WIN32)
			return Error == WSAEINTR;
			#else
			return Error == EINTR;
			#endif
		}

		bool WouldBlock(int Error)
		{
			#if defined(_WIN32)
			return Error == WSAEWOULDBLOCK;
			#else
			return Error == EAGAIN || Error == EWOULDBLOCK;
			#endif
		}

		struct FSocketWait
		{
			bool IsStopping() const
			{
				return StopRequested && StopRequested->load();
			}

			template <typename FOperation, typename TResult>
			auto Perform(FSocketHandle Socket, short Events, FOperation Operation, TResult Failure) const
			{
				while (!IsStopping())
				{
					const auto Result = Operation();
					if (Result != Failure)
					{
						return Result;
					}
					const int Error = SocketError();
					if (!IsInterrupted(Error) && (!WouldBlock(Error) || !WaitFor(Socket, Events)))
					{
						break;
					}
				}
				return static_cast<decltype(Operation())>(Failure);
			}

			const std::atomic<bool>* StopRequested;
			FSocketHandle WakeSocket;

		private:
			bool WaitFor(FSocketHandle Socket, short Events) const
			{
				if (!StopRequested)
				{
					return false;
				}
				// Leave the stop datagram readable so it wakes both the reader and writer.
				pollfd Descriptors[] = {{Socket, Events, 0}, {WakeSocket, POLLRDNORM, 0}};
				const unsigned Count = WakeSocket == kInvalidSocket ? 1 : 2;
				while (!IsStopping())
				{
					// The bounded wait also covers an unavailable wake socket or failed send.
					#if defined(_WIN32)
					const int Ready = ::WSAPoll(Descriptors, Count, 250);
					#else
					const int Ready = ::poll(Descriptors, Count, 250);
					#endif
					if (Ready > 0 && Descriptors[0].revents)
					{
						return !IsStopping();
					}
					if (Ready < 0 && !IsInterrupted(SocketError()))
					{
						break;
					}
				}
				return false;
			}
		};

		bool SendAll(FSocketHandle Socket, const char* Data, std::size_t Size, const FSocketWait& Wait)
		{
			std::size_t Sent = 0;
			while (Sent < Size)
			{
				const auto N = Wait.Perform(Socket, POLLWRNORM,
					[&]()
					{
						#if defined(MSG_NOSIGNAL)
						constexpr int SendFlags = MSG_NOSIGNAL;
						#else
						constexpr int SendFlags = 0;
						#endif
						return ::send(Socket, Data + Sent, static_cast<int>(Size - Sent), SendFlags);
					},
					-1);
				if (N <= 0)
				{
					return false;
				}
				Sent += static_cast<std::size_t>(N);
			}
			return true;
		}

		void ConfigureNoSigPipe(const FSocketHandle Socket)
		{
			#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
			const int Enabled = 1;
			(void)::setsockopt(Socket, SOL_SOCKET, SO_NOSIGPIPE, &Enabled, sizeof(Enabled));
			#else
			(void)Socket;
			#endif
		}

		class FSocketStreamBuf final : public std::streambuf
		{
		public:
			~FSocketStreamBuf() override
			{
				sync();
			}

			FSocketStreamBuf(FSocketHandle Socket, const FSocketWait& Wait)
				: Socket(Socket)
				, Wait(Wait)
			{
				setg(InBuf, InBuf, InBuf);
				setp(OutBuf, OutBuf + sizeof(OutBuf));
			}


		protected:
			//~ Begin std::streambuf Interface
			int_type underflow() override
			{
				if (gptr() < egptr())
				{
					return traits_type::to_int_type(*gptr());
				}
				const auto N = Wait.Perform(Socket, POLLRDNORM,
					[&]() { return ::recv(Socket, InBuf, static_cast<int>(sizeof(InBuf)), 0); }, -1);
				if (N <= 0)
				{
					return traits_type::eof();
				}
				setg(InBuf, InBuf, InBuf + N);
				return traits_type::to_int_type(*gptr());
			}

			int_type overflow(int_type Ch) override
			{
				if (pptr() > pbase() && !FlushBuffer())
				{
					return traits_type::eof();
				}
				if (!traits_type::eq_int_type(Ch, traits_type::eof()))
				{
					*pptr() = traits_type::to_char_type(Ch);
					pbump(1);
					if (pptr() == epptr() && !FlushBuffer())
					{
						return traits_type::eof();
					}
				}
				return traits_type::not_eof(Ch);
			}

			int sync() override
			{
				return FlushBuffer() ? 0 : -1;
			}
			//~ End std::streambuf Interface

		private:
			bool FlushBuffer()
			{
				const auto Pending = static_cast<std::size_t>(pptr() - pbase());
				if (Pending == 0)
				{
					return true;
				}
				if (!SendAll(Socket, pbase(), Pending, Wait))
				{
					return false;
				}
				setp(OutBuf, OutBuf + sizeof(OutBuf));
				return true;
			}

			FSocketHandle Socket = kInvalidSocket;
			const FSocketWait& Wait;
			char InBuf[4096]{};
			char OutBuf[4096]{};
		};

		bool ParseIPv4(const std::string& Text, in_addr& Out)
		{
			if (Text.empty() || Text == "0.0.0.0")
			{
				Out.s_addr = htonl(INADDR_ANY);
				return true;
			}
			#if defined(_WIN32)
			return ::InetPtonA(AF_INET, Text.c_str(), &Out) == 1;
			#else
			return ::inet_pton(AF_INET, Text.c_str(), &Out) == 1;
			#endif
		}
	}

	struct FDapTcpRunControl::FState
	{
		struct FRun
		{
			explicit FRun(FState* State) : State(State)
			{
				if (State)
				{
					const std::lock_guard Lock(State->Mutex);
					bAcquired = !State->bRunning;
					State->bRunning = true;
				}
			}

			~FRun()
			{
				if (State && bAcquired)
				{
					const std::lock_guard Lock(State->Mutex);
					State->WakeSocket = kInvalidSocket;
					State->bRunning = false;
				}
			}

			bool OpenWakeSockets()
			{
				if (!State)
				{
					return true;
				}
				WakeReader.Handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				WakeWriter.Handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				sockaddr_in Address{};
				Address.sin_family = AF_INET;
				Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
				FSocketLength AddressLength = sizeof(Address);
				if (WakeReader.Handle == kInvalidSocket || WakeWriter.Handle == kInvalidSocket
					|| ::bind(WakeReader.Handle, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) != 0
					|| ::getsockname(WakeReader.Handle, reinterpret_cast<sockaddr*>(&Address), &AddressLength) != 0
					|| ::connect(WakeWriter.Handle, reinterpret_cast<const sockaddr*>(&Address), AddressLength) != 0
					|| ::getsockname(WakeWriter.Handle, reinterpret_cast<sockaddr*>(&Address), &AddressLength) != 0
					|| ::connect(WakeReader.Handle, reinterpret_cast<const sockaddr*>(&Address), AddressLength) != 0
					|| !SetNonBlocking(WakeWriter.Handle))
				{
					return false;
				}
				const std::lock_guard Lock(State->Mutex);
				State->WakeSocket = WakeWriter.Handle;
				return true;
			}

			FState* State;
			FSocket WakeReader;
			FSocket WakeWriter;
			bool bAcquired = true;
		};

		std::atomic<bool> bStopRequested{false};
		std::mutex Mutex;
		FSocketHandle WakeSocket = kInvalidSocket;
		bool bRunning = false;
	};

	FDapTcpRunControl::FDapTcpRunControl() : State(std::make_unique<FState>()) {}

	FDapTcpRunControl::~FDapTcpRunControl() = default;

	void FDapTcpRunControl::RequestStop() noexcept
	{
		if (!State->bStopRequested.exchange(true))
		{
			const std::lock_guard Lock(State->Mutex);
			if (State->WakeSocket != kInvalidSocket)
			{
				(void)::send(State->WakeSocket, "", 1, 0);
			}
		}
	}

	bool FDapTcpRunControl::IsStopRequested() const noexcept
	{
		return State->bStopRequested.load();
	}

	int VeroScriptDapTcp::Run(const FDapTcpRunOptions& Options)
	{
		return Run(Options, nullptr);
	}

	int VeroScriptDapTcp::Run(
		const FDapTcpRunOptions& Options,
		IVeroScriptLiveDebugDomain* LiveDomain)
	{
		const std::shared_ptr<FDapTcpRunControl> Control = Options.Control;
		if (Control && Control->IsStopRequested())
		{
			return VeroScriptDapStdio::ExitOk;
		}
		#if defined(_WIN32)
		FWinsockLifetime Winsock;
		if (!Winsock.bOk)
		{
			std::cerr << "veroscript-dap-tcp: WSAStartup failed\n";
			return 1;
		}
		#endif
		FDapTcpRunControl::FState::FRun Run(Control ? Control->State.get() : nullptr);
		if (!Run.bAcquired)
		{
			std::cerr << "veroscript-dap-tcp: endpoint stop control is already in use\n";
			return 1;
		}
		const bool bWakeReady = Run.OpenWakeSockets();
		if (!bWakeReady && Options.bAnnounceListen)
		{
			std::cerr << "veroscript-dap-tcp: using bounded stop waits (wake socket error " << SocketError() << ")\n";
		}
		const FSocketWait Wait{Control ? &Control->State->bStopRequested : nullptr,
			bWakeReady ? Run.WakeReader.Handle : kInvalidSocket};

		in_addr Address{};
		if (!ParseIPv4(Options.BindAddress, Address))
		{
			std::cerr << "veroscript-dap-tcp: invalid bind address: " << Options.BindAddress << '\n';
			return 1;
		}

		const FSocket Listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
		const FSocketHandle ListenSocket = Listener.Handle;
		if (ListenSocket == kInvalidSocket || (Control && !SetNonBlocking(ListenSocket)))
		{
			std::cerr << "veroscript-dap-tcp: cannot configure listener socket\n";
			return 1;
		}

		int Reuse = 1;
		#if defined(_WIN32)
		::setsockopt(
			ListenSocket,
			SOL_SOCKET,
			SO_REUSEADDR,
			reinterpret_cast<const char*>(&Reuse),
			sizeof(Reuse));
		#else
		::setsockopt(ListenSocket, SOL_SOCKET, SO_REUSEADDR, &Reuse, sizeof(Reuse));
		#endif

		sockaddr_in BindAddr{};
		BindAddr.sin_family = AF_INET;
		BindAddr.sin_port = htons(Options.Port);
		BindAddr.sin_addr = Address;
		if (::bind(ListenSocket, reinterpret_cast<sockaddr*>(&BindAddr), sizeof(BindAddr)) != 0)
		{
			std::cerr << "veroscript-dap-tcp: bind(" << Options.BindAddress << ':' << Options.Port
				<< ") failed\n";
			return 1;
		}
		if (::listen(ListenSocket, 1) != 0)
		{
			std::cerr << "veroscript-dap-tcp: listen() failed\n";
			return 1;
		}

		std::uint16_t BoundPort = Options.Port;
		{
			sockaddr_in BoundAddr{};
			FSocketLength BoundLen = sizeof(BoundAddr);
			if (::getsockname(
					ListenSocket,
					reinterpret_cast<sockaddr*>(&BoundAddr),
						&BoundLen)
						== 0)
			{
				BoundPort = ntohs(BoundAddr.sin_port);
			}
		}

		if (Options.OnListening && !Wait.IsStopping())
		{
			Options.OnListening(BoundPort);
		}

		if (Options.bAnnounceListen)
		{
			std::cerr << "veroscript-dap-tcp: listening on " << Options.BindAddress << ':'
				<< BoundPort
				<< (Options.Stdio.LiveBindings ? " (live bindings)\n" : "\n");
		}

		int LastExit = 0;
		int SessionsDone = 0;
		const int SessionLimit = Options.MaxSessions < 0 ? 1 : Options.MaxSessions;
		while (SessionLimit == 0 || SessionsDone < SessionLimit)
		{
			const FSocket Connection(Wait.Perform(ListenSocket, POLLRDNORM,
				[&]() { return ::accept(ListenSocket, nullptr, nullptr); }, kInvalidSocket));
			const FSocketHandle Client = Connection.Handle;
			if (Wait.IsStopping())
			{
				break;
			}
			if (Client == kInvalidSocket || (Control && !SetNonBlocking(Client)))
			{
				std::cerr << "veroscript-dap-tcp: cannot accept client socket\n";
				LastExit = 1;
				break;
			}
			ConfigureNoSigPipe(Client);

			if (Options.bAnnounceListen && SessionsDone > 0)
			{
				std::cerr << "veroscript-dap-tcp: client connected (session "
					<< (SessionsDone + 1) << ")\n";
			}

			{
				// The protocol reader and execution worker may call recv()/send()
				// concurrently. Give each direction its own streambuf as well as its own
				// basic_ios state: standard streambuf objects do not promise that
				// simultaneous underflow/overflow calls on one instance are race-free,
				// even when their get/put buffers are otherwise independent. A connected
				// socket itself supports one reader and one writer concurrently.
				FSocketStreamBuf InputBuffer(Client, Wait);
				FSocketStreamBuf OutputBuffer(Client, Wait);
				std::istream Input(&InputBuffer);
				std::ostream Output(&OutputBuffer);
				LastExit = VeroScriptDapStdio::Run(Input, Output, Options.Stdio, LiveDomain, Wait.StopRequested);
			}
			++SessionsDone;
			if (LastExit == VeroScriptDapStdio::ExitTerminateHost)
			{
				if (Options.bAnnounceListen)
				{
					std::cerr << "veroscript-dap-tcp: host terminate requested — stopping listen\n";
				}
				LastExit = VeroScriptDapStdio::ExitOk;
				break;
			}
			if (LastExit != VeroScriptDapStdio::ExitOk)
			{
				break;
			}
		}

		return Wait.IsStopping() ? VeroScriptDapStdio::ExitOk : LastExit;
	}
}
