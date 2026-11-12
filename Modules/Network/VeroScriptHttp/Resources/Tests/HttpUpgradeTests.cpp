// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "HttpTestBackends.h"
#include "../../../VeroScriptWebSocket/Resources/Tests/WebSocketTestBackends.h"
#include "VeroScriptHttp.h"
#include "VeroScriptWebSocket.h"
#include "NetworkProvider.h"
#include "NetworkDiagnostics.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace N = VeroScript::Network;
namespace H = VeroScript::Http;
namespace W = VeroScript::WebSocket;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " + #X); \
		} \
	} \
	while (false)
static void Wait(N::FContext& C, const N::FOperation& Op)
{
	const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(8);
	while (!Op.IsComplete())
	{
		CHECK(std::chrono::steady_clock::now() < End);
		if (C.Mode() == N::EDriveMode::HostDriven)
		{
			N::FError E;
			C.PollIo(E);
			CHECK(!E);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
template <class T>
static T Result(N::FContext& C, const N::FOperation& Op)
{
	Wait(C, Op);
	if (Op.Error())
	{
		throw std::runtime_error(Op.Error().Message);
	}
	auto R = Op.Result<T>();
	CHECK(R);
	return *R;
}
static std::string File(const char* Name)
{
	std::ifstream Input(std::filesystem::u8path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "VeroScriptNetwork/Resources/Tests/Fixtures" / Name, std::ios::binary);
	CHECK(Input);
	return std::string(std::istreambuf_iterator<char>(Input), {});
}
static std::uint64_t Resources(N::FContext& C)
{
	return N::Detail::ResourceCount(N::Detail::FAccess::Context(C));
}
static void Upgrade(N::EDriveMode Mode, bool Stream, bool Tls)
{
	N::FContextOptions Options;
	Options.Mode = Mode;
	Options.MaxConnections = 3;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), Options);
	N::FError E;
	H::FServerOptions ServerOptions;
	ServerOptions.bStreamRequests = Stream;
	if (Tls)
	{
		ServerOptions.Tls.bEnabled = true;
		ServerOptions.Tls.CertificateChainPem = File("loopback-cert.pem");
		ServerOptions.Tls.PrivateKeyPem = File("loopback-key.pem");
	}
	auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, ServerOptions);
	CHECK(Server && !E);
	auto Accept = Server->Accept();
	W::FOptions ClientOptions;
	ClientOptions.Subprotocols = {"other", "binary"};
	if (Tls)
	{
		ClientOptions.Tls.bUseSystemTrust = false;
		ClientOptions.Tls.TrustPem = ServerOptions.Tls.CertificateChainPem;
	}
	auto Connecting = VeroScript::WebSocket::Test::Connect(C,
	    std::string(Tls ? "wss" : "ws") + "://127.0.0.1:" + std::to_string(Server->Port()) + "/same-port",
	    ClientOptions);
	auto Exchange = Result<std::shared_ptr<H::FExchange>>(C, Accept);
	CHECK(Exchange->Request().Target == "/same-port" && Exchange->Request().ProtocolVersion == 11);
	auto Source = Exchange->PrepareUpgrade(E);
	CHECK(Source && !E && !Source->IsConsumed());
	Exchange.reset();
	Accept = {}; // The source must independently preserve the original exchange.
	CHECK(Resources(C) == 3); // Listener and both sides already fill the context's slots.
	N::FContext Other(VeroScript::Boost::CreateNetworkService());
	auto WrongContext = VeroScript::WebSocket::Test::AcceptUpgrade(Other, Source);
	CHECK(WrongContext.Error().Code == N::EError::InvalidArgument && !Source->IsConsumed());
	W::FOptions Bad;
	Bad.MaxQueuedBytes = 1;
	auto Limited = VeroScript::WebSocket::Test::AcceptUpgrade(C, Source, Bad);
	CHECK(Limited.Error().Code == N::EError::Limit && !Source->IsConsumed());
	W::FOptions PeerOptions;
	PeerOptions.Subprotocols = {"binary"};
	PeerOptions.bRequireSubprotocol = true;
	auto Upgrading = VeroScript::WebSocket::Test::AcceptUpgrade(C, Source, PeerOptions);
	auto Peer = Result<std::shared_ptr<W::FConnection>>(C, Upgrading);
	auto Client = Result<std::shared_ptr<W::FConnection>>(C, Connecting);
	CHECK(Source->IsConsumed() && Resources(C) == 3);
	CHECK(Peer->Subprotocol() == "binary" && Client->Subprotocol() == "binary");
	// Source reuse is rejected, and cannot disturb the connection already transferred.
	auto Duplicate = VeroScript::WebSocket::Test::AcceptUpgrade(C, Source, PeerOptions);
	Wait(C, Duplicate);
	CHECK(Duplicate.Error().Code == N::EError::State);
	Source.reset(); // Releasing HTTP metadata cannot close the adopted WebSocket.
	auto Reading = Peer->Receive();
	CHECK(Result<std::uint64_t>(C, Client->Send({{'a', 0, 'b'}, false})) == 3);
	CHECK(Result<W::FMessage>(C, Reading).Bytes == N::FBytes({'a', 0, 'b'}));
	auto Shutdown = Server->Shutdown(std::chrono::milliseconds(10));
	CHECK(!Result<H::FShutdownResult>(C, Shutdown).bForced);
	// HTTP shutdown no longer owns or waits for the upgraded transport.
	Reading = Client->Receive();
	CHECK(Result<std::uint64_t>(C, Peer->Send({{'o', 'k'}, true})) == 2);
	CHECK(Result<W::FMessage>(C, Reading).Bytes == N::FBytes({'o', 'k'}));
	CHECK(Result<W::FCloseInfo>(C, Client->Close()).bClean);
	C.Shutdown();
}
static const std::string Header =
    "GET /first-frame HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n"
    "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nContent-Length: 000\r\n\r\n";
static void PreRead(N::EDriveMode Mode, bool Stream, bool Invalid, bool Cancel = false)
{
	N::FContextOptions O;
	O.Mode = Mode;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), O);
	N::FError E;
	H::FServerOptions SO;
	SO.bStreamRequests = Stream;
	auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, SO);
	CHECK(Server && !E);
	auto Accept = Server->Accept();
	auto Socket = Result<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port()));
	std::string Text = Header;
	if (Invalid)
	{
		Text.replace(Text.find("dGhlIHNhbXBsZSBub25jZQ=="), 24, "not-a-valid-key");
	}
	N::FBytes Bytes(Text.begin(), Text.end());
	if (!Invalid && !Cancel)
	{
		// The entire first masked frame shares the single TCP write with the HTTP headers.
		Bytes.insert(Bytes.end(),
		    {0x81, 0x82, 1, 2, 3, 4, static_cast<std::uint8_t>('o' ^ 1), static_cast<std::uint8_t>('k' ^ 2)});
	}
	CHECK(Result<std::uint64_t>(C, Socket->Write(std::move(Bytes))) > Header.size() - 24);
	auto Exchange = Result<std::shared_ptr<H::FExchange>>(C, Accept);
	auto Source = Exchange->PrepareUpgrade(E);
	CHECK(Source && !E);
	auto Upgrading = VeroScript::WebSocket::Test::AcceptUpgrade(C, Source);
	if (Cancel)
	{
		CHECK(Mode == N::EDriveMode::HostDriven && Upgrading.Cancel());
		CHECK(Upgrading.Error().Code == N::EError::Cancelled);
	}
	if (Invalid || Cancel)
	{
		CHECK((Cancel || Upgrading.Error().Code == N::EError::Protocol) && !Source->IsConsumed());
		H::FResponse Response;
		Response.Status = Cancel ? 409 : 400;
		Response.Body = {'n', 'o'};
		Result<std::uint64_t>(C, Exchange->Reply(std::move(Response)));
		std::string Wire;
		while (Wire.find("\r\n\r\n") == Wire.npos)
		{
			auto Read = Result<N::FReadResult>(C, Socket->Read(4096));
			CHECK(!Read.bEnd && !Read.Bytes.empty());
			Wire.append(Read.Bytes.begin(), Read.Bytes.end());
		}
		CHECK(Wire.find(Cancel ? "HTTP/1.1 409" : "HTTP/1.1 400") == 0);
	}
	else
	{
		auto Peer = Result<std::shared_ptr<W::FConnection>>(C, Upgrading);
		CHECK(Result<W::FMessage>(C, Peer->Receive()).Bytes == N::FBytes({'o', 'k'}));
		Socket->Close();
	}
	C.Shutdown();
}
int main()
{
	try
	{
		for (const auto Mode : {N::EDriveMode::NativeBackground, N::EDriveMode::HostDriven})
		{
			for (const bool Stream : {false, true})
			{
				Upgrade(Mode, Stream, false);
				if (VeroScript::Boost::CreateNetworkService()->TlsAvailable())
				{
					Upgrade(Mode, Stream, true);
				}
				PreRead(Mode, Stream, false);
				PreRead(Mode, Stream, true);
				if (Mode == N::EDriveMode::HostDriven)
				{
					PreRead(Mode, Stream, false, true);
				}
			}
		}
		std::cout << "HTTP/HTTPS upgrade, slot transfer, first-frame preservation and preflight failure passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}
