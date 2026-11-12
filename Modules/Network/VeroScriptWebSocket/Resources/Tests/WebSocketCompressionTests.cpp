// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "WebSocketTestBackends.h"

#include "VeroScriptWebSocket.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace N = VeroScript::Network;
namespace W = VeroScript::WebSocket;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error(std::string("Line ") + std::to_string(__LINE__) + ": " + #X); \
		} \
	} \
	while (false)

static void Await(N::FContext& C, const N::FOperation& O)
{
	const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
	while (!O.IsComplete() && std::chrono::steady_clock::now() < Until)
	{
		if (C.Mode() == N::EDriveMode::HostDriven)
		{
			N::FError E;
			C.PollIo(E);
			CHECK(!E);
		}
		else
		{
			O.Wait(std::chrono::milliseconds(5));
		}
		std::this_thread::yield();
	}
	CHECK(O.IsComplete());
}
template <class T>
static T Result(N::FContext& C, const N::FOperation& O)
{
	Await(C, O);
	if (O.Error())
	{
		throw std::runtime_error(O.Error().Message);
	}
	CHECK(O.Result<T>());
	return *O.Result<T>();
}
static N::FContextOptions ContextOptions(bool Host)
{
	N::FContextOptions O;
	O.Mode = Host ? N::EDriveMode::HostDriven : N::EDriveMode::NativeBackground;
	return O;
}
static W::FOptions Options(int Bits = 15, int Flags = 0, bool Enabled = true)
{
	W::FOptions O;
	O.bCompression = Enabled;
	O.Compression.ClientMaxWindowBits = O.Compression.ServerMaxWindowBits = Bits;
	O.Compression.bClientNoContextTakeover = (Flags & 1) != 0;
	O.Compression.bServerNoContextTakeover = (Flags & 2) != 0;
	O.MaxMessageBytes = 128 * 1024;
	O.MaxQueuedBytes = 256 * 1024;
	O.Operation.Timeout = std::chrono::seconds(5);
	return O;
}
static std::vector<W::FMessage> Messages()
{
	W::FMessage Text;
	Text.bText = true;
	const char Unit[] = "中文-json-websocket";
	for (unsigned I = 0; I < 2000; ++I)
	{
		Text.Bytes.insert(Text.Bytes.end(), Unit, Unit + sizeof(Unit));
	}
	W::FMessage Binary;
	for (unsigned I = 0; I < 65536; ++I)
	{
		Binary.Bytes.push_back(static_cast<std::uint8_t>(I));
	}
	return {Text, Text, Binary, {{}, true}};
}
static void Local(bool Host, int Bits, int Flags, bool Enabled)
{
	N::FContext C(VeroScript::Boost::CreateNetworkService(), ContextOptions(Host));
	N::FError E;
	auto ServerOptions = Options(Bits, Flags, Enabled);
	ServerOptions.Compression.Level = Bits == 9 ? 0 : 9;
	ServerOptions.Compression.MemoryLevel = Bits == 9 ? 1 : 9;
	auto S = VeroScript::WebSocket::Test::Listen(C, "127.0.0.1", 0, E, ServerOptions);
	CHECK(S && !E);
	auto A = S->Accept();
	auto Client = Result<std::shared_ptr<W::FConnection>>(
	    C, VeroScript::WebSocket::Test::Connect(C, "ws://127.0.0.1:" + std::to_string(S->Port()), Options(15, Flags, Enabled)));
	auto Peer = Result<std::shared_ptr<W::FConnection>>(C, A);
	for (const auto& Connection : {Client, Peer})
	{
		const auto I = Connection->CompressionInfo();
		CHECK(I.bEnabled == Enabled);
		if (Enabled)
		{
			CHECK(I.ClientWindowBits == static_cast<unsigned>(Bits) &&
			    I.ServerWindowBits == static_cast<unsigned>(Bits) &&
			    I.bClientNoContextTakeover == ((Flags & 1) != 0) && I.bServerNoContextTakeover == ((Flags & 2) != 0));
		}
	}
	for (const auto& M : Messages())
	{
		auto Reading = Peer->Receive();
		Result<std::uint64_t>(C, Client->Send(M));
		auto R = Result<W::FMessage>(C, Reading);
		CHECK(R.Bytes == M.Bytes && R.bText == M.bText);
		Reading = Client->Receive();
		Result<std::uint64_t>(C, Peer->Send(R));
		R = Result<W::FMessage>(C, Reading);
		CHECK(R.Bytes == M.Bytes && R.bText == M.bText);
	}
	CHECK(Result<W::FCloseInfo>(C, Client->Close()).bClean);
	CHECK(Client->CompressionInfo().bEnabled == Enabled);
	S->Close();
	C.Shutdown();
}
static void Self()
{
	N::FContext C(VeroScript::Boost::CreateNetworkService(), ContextOptions(true));
	N::FError E;
	for (int Invalid : {-1, 0, 8, 16, INT32_MAX})
	{
		auto O = Options();
		O.Compression.ClientMaxWindowBits = Invalid;
		CHECK(!VeroScript::WebSocket::Test::Listen(C, "127.0.0.1", 0, E, O) && E.Code == N::EError::InvalidArgument);
		CHECK(VeroScript::WebSocket::Test::Connect(C, "ws://127.0.0.1:1", O).Error().Code == N::EError::InvalidArgument);
	}
	if (!VeroScript::WebSocket::Test::CompressionSupported())
	{
		auto O = Options();
		CHECK(!VeroScript::WebSocket::Test::Listen(C, "127.0.0.1", 0, E, O) && E.Code == N::EError::Unsupported);
		CHECK(VeroScript::WebSocket::Test::Connect(C, "ws://127.0.0.1:1", O).Error().Code == N::EError::Unsupported);
		Local(false, 15, 0, false);
		Local(true, 15, 0, false);
		return;
	}
	for (const bool Host : {false, true})
	{
		for (int Bits = 9; Bits <= 15; ++Bits)
		{
			for (int Flags = 0; Flags < 4; ++Flags)
			{
				Local(Host, Bits, Flags, true);
			}
		}
		Local(Host, 15, 0, false);
	}
	N::FContextOptions SmallOptions = ContextOptions(true);
	SmallOptions.MaxBufferBytes = 128 * 1024;
	SmallOptions.MaxRetainedBytes = 1024 * 1024;
	N::FContext Small(VeroScript::Boost::CreateNetworkService(), SmallOptions);
	auto Denied = VeroScript::WebSocket::Test::Connect(Small, "ws://127.0.0.1:1", Options());
	CHECK(Denied.IsComplete() && Denied.Error().Code == N::EError::Limit);
	Small.Shutdown();
	CHECK(Small.MemoryUsage().RetainedBytes == 0);
}
static void Client(
    bool Host, const std::string& Base, const std::string& Trust, int Bits, int Flags, std::string Path, bool Enabled)
{
	N::FContext C(VeroScript::Boost::CreateNetworkService(), ContextOptions(Host));
	auto O = Options(Bits, Flags, Enabled);
	if (Path == "/inflate-limit")
	{
		O.MaxMessageBytes = 1024;
		O.MaxQueuedBytes = 2048;
	}
	O.Tls.TrustPem = Trust;
	auto Open = VeroScript::WebSocket::Test::Connect(C, Base + Path, O);
	Await(C, Open);
	if (Path.rfind("/bad/", 0) == 0)
	{
		CHECK(Open.Error().Code == N::EError::Protocol);
		C.Shutdown();
		return;
	}
	auto Connection = Result<std::shared_ptr<W::FConnection>>(C, Open);
	if (Path == "/inflate-limit" || Path == "/invalid-deflate")
	{
		auto Receiving = Connection->Receive();
		Await(C, Receiving);
		const auto Expected = Path == "/inflate-limit" ? N::EError::Limit : N::EError::Protocol;
		CHECK(Receiving.Error().Code == Expected && !Receiving.Result<W::FMessage>());
		Receiving = Connection->Receive();
		Await(C, Receiving);
		CHECK(Receiving.Error().Code == Expected);
		C.Shutdown();
		return;
	}
	const auto I = Connection->CompressionInfo();
	const bool Compressed = Enabled && Path != "/decline";
	CHECK(I.bEnabled == Compressed);
	if (Compressed)
	{
		CHECK(I.ClientWindowBits == static_cast<unsigned>(Bits) && I.ServerWindowBits == static_cast<unsigned>(Bits) &&
		    I.bClientNoContextTakeover == ((Flags & 1) != 0) && I.bServerNoContextTakeover == ((Flags & 2) != 0));
	}
	for (const auto& M : Messages())
	{
		auto Receiving = Connection->Receive();
		Result<std::uint64_t>(C, Connection->Send(M));
		const auto R = Result<W::FMessage>(C, Receiving);
		CHECK(R.Bytes == M.Bytes && R.bText == M.bText);
	}
	CHECK(Result<W::FCloseInfo>(C, Connection->Close()).bClean);
	C.Shutdown();
}
static void Server(int Argc, char** Argv)
{
	CHECK(Argc == 7 || Argc == 9);
	auto O = Options(std::stoi(Argv[2]), std::stoi(Argv[3]), std::stoi(Argv[5]) != 0);
	O.bReceiveFragments = std::stoi(Argv[6]) != 0;
	if (Argc == 9)
	{
		const auto Read = [](const char* Path)
		{
			std::ifstream File(std::filesystem::u8path(Path), std::ios::binary);
			CHECK(File);
			return std::string(std::istreambuf_iterator<char>(File), {});
		};
		O.Tls.bEnabled = true;
		O.Tls.CertificateChainPem = Read(Argv[7]);
		O.Tls.PrivateKeyPem = Read(Argv[8]);
	}
	N::FContext C(VeroScript::Boost::CreateNetworkService(), ContextOptions(std::stoi(Argv[4]) != 0));
	N::FError E;
	auto S = VeroScript::WebSocket::Test::Listen(C, "127.0.0.1", 0, E, O);
	CHECK(S && !E);
	std::cout << S->Port() << std::endl;
	auto Peer = Result<std::shared_ptr<W::FConnection>>(C, S->Accept());
	const auto Info = Peer->CompressionInfo();
	std::cout << "INFO " << Info.bEnabled << ' ' << Info.ClientWindowBits << ' ' << Info.ServerWindowBits << ' '
	          << Info.bClientNoContextTakeover << ' ' << Info.bServerNoContextTakeover << std::endl;
	for (unsigned Index = 0; Index < 4; ++Index)
	{
		W::FMessage Whole;
		bool First = true;
		for (;;)
		{
			auto Part = Result<W::FMessage>(C, Peer->Receive());
			CHECK(Part.bFirst == First);
			if (First)
			{
				Whole.bText = Part.bText;
			}
			CHECK(Part.bText == Whole.bText && Whole.Bytes.size() + Part.Bytes.size() <= 128 * 1024);
			Whole.Bytes.insert(Whole.Bytes.end(), Part.Bytes.begin(), Part.Bytes.end());
			First = false;
			if (Part.bFinal)
			{
				break;
			}
		}
		Result<std::uint64_t>(C, Peer->Send(std::move(Whole)));
	}
	CHECK(Result<W::FCloseInfo>(C, Peer->Close()).bClean);
	S->Close();
	C.Shutdown();
}
int main(int Argc, char** Argv)
{
	try
	{
		if (Argc == 2 && std::string(Argv[1]) == "--capabilities")
		{
			std::cout << VeroScript::WebSocket::Test::CompressionSupported() << ' ' << VeroScript::Boost::CreateNetworkService()->TlsAvailable() << '\n';
			return 0;
		}
		if (Argc > 1 && std::string(Argv[1]) == "--server")
		{
			Server(Argc, Argv);
			return 0;
		}
		if (Argc == 5 && std::string(Argv[1]) == "--client")
		{
			const auto Plain = "ws://127.0.0.1:" + std::string(Argv[2]);
			const auto Secure = "wss://127.0.0.1:" + std::string(Argv[3]);
			std::ifstream File(std::filesystem::u8path(Argv[4]), std::ios::binary);
			CHECK(File);
			const std::string Trust(std::istreambuf_iterator<char>(File), {});
			for (const bool Host : {false, true})
			{
				if (VeroScript::WebSocket::Test::CompressionSupported())
				{
					for (int Bits = 9; Bits <= 15; ++Bits)
					{
						for (int Flags = 0; Flags < 4; ++Flags)
						{
							Client(Host,
							    Plain,
							    {},
							    Bits,
							    Flags,
							    "/echo/" + std::to_string(Bits) + "/" + std::to_string(Flags),
							    true);
						}
					}
					if (VeroScript::Boost::CreateNetworkService()->TlsAvailable())
					{
						for (int Flags : {0, 3})
						{
							Client(Host, Secure, Trust, 15, Flags, "/echo/15/" + std::to_string(Flags), true);
						}
					}
					Client(Host, Plain, {}, 15, 0, "/quote", true);
					Client(Host, Plain, {}, 15, 0, "/decline", true);
					Client(Host, Plain, {}, 15, 0, "/inflate-limit", true);
					Client(Host, Plain, {}, 15, 0, "/invalid-deflate", true);
				}
				Client(Host, Plain, {}, 15, 0, "/decline", false);
				for (const auto* Bad : {"unknown",
				         "duplicate",
				         "missing",
				         "window8",
				         "window16",
				         "empty",
				         "trailing",
				         "extensions",
				         "headers",
				         "flags"})
				{
					Client(Host, Plain, {}, 15, 0, "/bad/" + std::string(Bad), VeroScript::WebSocket::Test::CompressionSupported());
				}
				Client(Host, Plain, {}, 15, 0, "/bad/unsolicited", false);
			}
		}
		else
		{
			CHECK(Argc == 1);
			Self();
		}
		std::cout << "WebSocket compression qualification passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
