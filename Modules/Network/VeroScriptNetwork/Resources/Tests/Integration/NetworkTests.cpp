// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"
#include "../../../../VeroScriptHttp/Resources/Tests/HttpTestBackends.h"
#include "../../../../VeroScriptWebSocket/Resources/Tests/WebSocketTestBackends.h"

#include "VeroScriptNetwork.h"
#include "VeroScriptHttp.h"
#include "VeroScriptWebSocket.h"
#include "VeroScriptHttpSseClient.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace Network = VeroScript::Network;
namespace Http = VeroScript::Http;
namespace Ws = VeroScript::WebSocket;

static void Check(bool Value, const char* Expression, int Line)
{
	if (!Value)
	{
		throw std::runtime_error(std::string("Line ") + std::to_string(Line) + ": " + Expression);
	}
}
#define CHECK(X) Check((X), #X, __LINE__)

template <class T>
static T Result(const Network::FOperation& Op)
{
	CHECK(Op.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed);
	if (Op.Error())
	{
		throw std::runtime_error(Op.Error().Message);
	}
	auto Value = Op.Result<T>();
	CHECK(Value != nullptr);
	return *Value;
}

static void PhysicalBudgetTests(std::uint16_t Port)
{
	Network::FContextOptions Options;
	Options.Mode = Network::EDriveMode::HostDriven;
	Options.MaxConnections = 2;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
	Network::FError Error;
	auto Listener = Network::FTcpListener::Listen(Context, "127.0.0.1", 0, Error);
	CHECK(Listener && !Error);
	auto Pump = [&](const Network::FOperation& Operation)
	{
		const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!Operation.IsComplete() && std::chrono::steady_clock::now() < Until)
		{
			Context.PollIo(Error);
			CHECK(!Error);
			std::this_thread::yield();
		}
		CHECK(Operation.IsComplete());
	};
	Http::FRequest Request;
	Request.Url = "http://127.0.0.1:" + std::to_string(Port) + "/ok";
	auto Blocked = Client.Send(Request);
	Pump(Blocked);
	CHECK(Blocked.Error().Code == Network::EError::Limit);
	Listener->Close();
	auto Allowed = Client.Send(Request);
	Pump(Allowed);
	CHECK(!Allowed.Error() && Allowed.Result<Http::FResponse>());
	// The pooled connection can be reused without reserving another socket.
	auto Reused = Client.Send(Request);
	Pump(Reused);
	CHECK(!Reused.Error() && Reused.Result<Http::FResponse>());
	Context.Shutdown();
}

static void UrlTests()
{
	Network::FError E;
	Network::FUrl Url;
	CHECK(Network::FUrl::TryParse("HTTPS://[::1]:8443/a%20b?q=a+b&q#f", Url, E));
	CHECK(Url.Host == "::1" && Url.Port == 8443 && Url.Scheme == "https");
	CHECK(Url.Target() == "/a%20b?q=a+b&q");
	CHECK(!Network::FUrl::TryParse("http://bad:65536/", Url, E));
	CHECK(!Network::FUrl::TryParse("http://a/\r\n", Url, E));
	CHECK(!Network::FUrl::TryParse("http://a/%xx", Url, E));
	std::vector<Network::FQueryParameter> Query;
	CHECK(Network::TryParseQuery("a=1&a=2&flag&x=+%00", Query, E));
	CHECK(Query.size() == 4 && !Query[2].bHasEquals && Query[3].Value == std::string("+\0", 2));
	CHECK(Network::BuildQuery(Query) == "a=1&a=2&flag&x=%2B%00");
	CHECK(Network::PercentEncode("~* +", true) == "%7E*+%2B");
	CHECK(Network::PercentEncode("~* +", false) == "~%2A%20%2B");
	std::string Value = "unchanged";
	CHECK(!Network::TryPercentDecode("%", Value, E) && Value == "unchanged");
}

static void HttpStreamTests(std::uint16_t Port)
{
	Network::FContextOptions Options;
	Options.MaxBufferBytes = 64 * 1024;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
	const auto Base = "http://127.0.0.1:" + std::to_string(Port);
	Http::FRequest Request;
	Request.Url = Base + "/stream";
	Request.MaxBodyBytes = 2 * 1024 * 1024;
	auto WaitHead = [&](const Http::FStreamingOperation& Task)
	{
		Http::FResponse Head;
		Network::FError E;
		const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!Task.TryHead(Head, E) && !E && std::chrono::steady_clock::now() < Until)
		{
			std::this_thread::yield();
		}
		CHECK(!E && Head.Status == 200 && !Head.bComplete && Head.Body.empty());
	};
	auto Drain = [&](const Http::FStreamingOperation& Task, Network::EError Expected = Network::EError::None)
	{
		Network::FBytes Received;
		const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		for (;;)
		{
			CHECK(std::chrono::steady_clock::now() < Until);
			std::uint8_t Bytes[997];
			const auto Read = Task.Body()->Read(Bytes, sizeof(Bytes));
			CHECK(Task.Body()->BufferedBytes() <= Task.Body()->Capacity());
			Received.insert(Received.end(), Bytes, Bytes + Read.Bytes);
			if (Read.State == Network::EStreamState::Failed)
			{
				CHECK(Read.Error.Code == Expected);
				break;
			}
			if (Read.State == Network::EStreamState::End)
			{
				CHECK(Expected == Network::EError::None);
				break;
			}
			if (!Read.Bytes)
			{
				std::this_thread::yield();
			}
		}
		CHECK(Task.Operation().Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed);
		CHECK(Task.Operation().Error().Code == Expected);
		return Received;
	};
	for (const auto* Path : {"/stream", "/stream-redirect"})
	{
		Request.Url = Base + Path;
		Request.bFollowRedirects = true;
		auto Task = Client.SendStream(Request, 16384);
		WaitHead(Task);
		// Filling the application queue does not block unrelated requests.
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
		CHECK(!Task.Operation().IsComplete());
		Http::FRequest Other;
		Other.Url = Base + "/ok";
		Other.MaxBodyBytes = Options.MaxBufferBytes;
		CHECK(Result<Http::FResponse>(Client.Send(Other)).Status == 200);
		auto Bytes = Drain(Task);
		CHECK(Bytes.size() == 1024 * 1024 + 37);
		for (std::size_t I = 0; I < Bytes.size(); ++I)
		{
			CHECK(Bytes[I] == I % 251);
		}
		const auto Response = Task.Operation().Result<Http::FResponse>();
		CHECK(Response && Response->bComplete && Response->Body.empty() && Response->BodyBytes == Bytes.size());
	}
	Request.Url = Base + "/stream-chunked";
	auto Chunked = Client.SendStream(Request, 16384);
	const auto ChunkBytes = Drain(Chunked);
	CHECK(std::string(ChunkBytes.begin(), ChunkBytes.end()) == std::string("first\0second", 12));
	CHECK(
	    Chunked.Operation().Result<Http::FResponse>()->Trailers.GetAll("X-Final") == std::vector<std::string>{"done"});

	Request.Url = Base + "/stream";
	Request.MaxBodyBytes = 257;
	auto Limited = Client.SendStream(Request, 16384);
	CHECK(Drain(Limited, Network::EError::Limit).size() <= 257);
	Request.MaxBodyBytes = 2 * 1024 * 1024;
	Request.Operation.Timeout = std::chrono::milliseconds(100);
	auto TimedOut = Client.SendStream(Request, 16384);
	CHECK(TimedOut.Operation().Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed);
	CHECK(TimedOut.Operation().Error().Code == Network::EError::TimedOut);
	CHECK(Drain(TimedOut, Network::EError::TimedOut).size() <= 16384);

	Request.Operation.Timeout = std::chrono::seconds(10);
	Request.Url = Base + "/stream-stall";
	auto Cancelled = Client.SendStream(Request, 16384);
	WaitHead(Cancelled);
	Cancelled.Body()->Cancel();
	CHECK(Cancelled.Operation().Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed);
	CHECK(Cancelled.Operation().Error().Code == Network::EError::Cancelled);
	auto Dropped = Client.SendStream(Request, 16384);
	auto Operation = Dropped.Operation();
	Dropped = {};
	CHECK(Operation.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed);
	CHECK(Operation.Error().Code == Network::EError::Cancelled);
	auto Invalid = Client.SendStream(Request, 1);
	CHECK(Invalid.Operation().Error().Code == Network::EError::InvalidArgument && !Invalid.Body());
	Context.Shutdown();
}

static void HttpTests(std::uint16_t Port, std::uint16_t EchoPort)
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FError E;
	auto Owner = std::this_thread::get_id();
	auto Addresses = Result<std::vector<Network::FAddress>>(Network::Resolve(Context, "localhost", Port));
	CHECK(!Addresses.empty());
	auto Socket = Result<std::shared_ptr<Network::FTcpSocket>>(Network::FTcpSocket::Connect(Context, "127.0.0.1", EchoPort));
	CHECK(Result<std::uint64_t>(Socket->Write({0, 1, 0, 255})) == 4);
	auto Bytes = Result<Network::FReadResult>(Socket->Read(4));
	CHECK(Bytes.Bytes == Network::FBytes({0, 1, 0, 255}));
	Socket->Close();
	Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
	const auto Base = "http://127.0.0.1:" + std::to_string(Port);
	Http::FRequest Request;
	Request.Url = Base + "/ok";
	std::vector<Network::FOperation> Operations;
	unsigned Callbacks = 0;
	for (unsigned I = 0; I < 20; ++I)
	{
		auto Op = Client.Send(Request);
		CHECK(Op.OnComplete(
		    [&](const Network::FOperation& Done)
		{
			CHECK(std::this_thread::get_id() == Owner && Done.IsComplete());
			++Callbacks;
		},
		    E));
		Operations.push_back(Op);
	}
	for (auto& Op : Operations)
	{
		auto Response = Result<Http::FResponse>(Op);
		CHECK(Response.bComplete && Response.Status == 200);
		CHECK(Response.Body == Network::FBytes({'o', 'k'}));
		CHECK(Response.Headers.GetAll("set-cookie").size() == 2);
	}
	CHECK(Callbacks == 0);
	std::size_t Dispatched = 0;
	const auto DispatchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (Callbacks < Operations.size())
	{
		Dispatched += Context.Dispatch(E);
		CHECK(!E && Dispatched == Callbacks && Dispatched <= Operations.size());
		CHECK(std::chrono::steady_clock::now() < DispatchDeadline);
		if (Callbacks < Operations.size())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	CHECK(Dispatched == 20 && Context.Dispatch(E) == 0 && !E && Callbacks == 20);
	Request.Url = Base + "/404";
	CHECK(Result<Http::FResponse>(Client.Send(Request)).Status == 404);
	Request.Url = Base + "/echo";
	Request.Method = "POST";
	Request.Body = {0, 1, 2, 255};
	CHECK(Result<Http::FResponse>(Client.Send(Request)).Body == Request.Body);
	Request.Body.clear();
	Request.Url = Base + "/multipart";
	Request.Multipart.emplace();
	Request.Multipart->push_back(
	    {"field", std::nullopt, {}, Network::FBytes{'a', 'l', 'p', 'h', 'a', 0, 'o', 'm', 'e', 'g', 'a'}});
	Request.Multipart->push_back({"field", std::nullopt, {}, Network::FBytes{'s', 'e', 'c', 'o', 'n', 'd'}});
	Request.Multipart->push_back({"file", "hello.bin", "application/octet-stream", Network::FBytes{0, 255}});
	auto Multipart = Result<Http::FResponse>(Client.Send(Request));
	CHECK(std::string(Multipart.Body.begin(), Multipart.Body.end()) == "multipart-ok");
	Request.Body = {1};
	auto Ambiguous = Client.Send(Request);
	CHECK(Ambiguous.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed &&
	    Ambiguous.Error().Code == Network::EError::InvalidArgument);
	Request.Body.clear();
	CHECK(Request.Headers.TryAdd("Content-Type", "multipart/form-data", E));
	auto WrongBoundary = Client.Send(Request);
	CHECK(WrongBoundary.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed &&
	    WrongBoundary.Error().Code == Network::EError::InvalidArgument);
	Request = {};
	Request.Url = Base + "/ok";
	Request.Method = "HEAD";
	CHECK(Result<Http::FResponse>(Client.Send(Request)).Body.empty());
	Request = {};
	Request.Url = Base + "/truncated";
	auto Truncated = Client.Send(Request);
	CHECK(Truncated.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed && Truncated.Error());
	Request.Url = Base + "/ok";
	Request.MaxBodyBytes = 1;
	auto Limited = Client.Send(Request);
	CHECK(Limited.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed &&
	    Limited.Error().Code == Network::EError::Limit);
	Request = {};
	Request.Url = Base + "/stall";
	Request.Operation.Timeout = std::chrono::milliseconds(100);
	auto Timeout = Client.Send(Request);
	CHECK(Timeout.Wait(std::chrono::milliseconds(1)) == Network::EWaitResult::WaitTimedOut);
	CHECK(Timeout.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed &&
	    Timeout.State() == Network::EOperationState::TimedOut);
	Request.Operation.Timeout = std::nullopt;
	auto Cancel = Client.Send(Request);
	CHECK(Cancel.Cancel());
	CHECK(Cancel.State() == Network::EOperationState::Cancelled && !Cancel.Cancel());
	Context.Shutdown();
	CHECK(Client.Send(Request).IsComplete());
	Client.Close();
	Socket.reset();
}

static void HttpUploadTests(std::uint16_t Port)
{
	Network::FContextOptions Options;
	Options.Mode = Network::EDriveMode::HostDriven;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
	Network::FError Error;
	const auto Base = "http://127.0.0.1:" + std::to_string(Port);
	auto Pump = [&](const Network::FOperation& Op)
	{
		const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!Op.IsComplete() && std::chrono::steady_clock::now() < End)
		{
			Context.PollIo(Error);
			CHECK(!Error);
			std::this_thread::yield();
		}
		CHECK(Op.IsComplete());
	};
	for (const bool Known : {false, true})
	{
		Network::FBytePipe Pipe;
		CHECK(Network::TryCreateBytePipe(257, Pipe, Error));
		constexpr std::size_t Total = 256 * 1024 + 37;
		Http::FRequest Request;
		Request.Url = Base + "/echo";
		Request.Method = "POST";
		Request.Upload = Pipe.Reader;
		if (Known)
		{
			Request.UploadLength = Total;
		}
		auto Task = Client.SendStream(Request, 16384);
		std::size_t Sent = 0, Received = 0;
		bool Finished = false, Ended = false;
		const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		while (!Ended && std::chrono::steady_clock::now() < Until)
		{
			Context.PollIo(Error);
			CHECK(!Error);
			std::uint8_t Bytes[113];
			if (Sent < Total)
			{
				const auto Count = std::min(sizeof(Bytes), Total - Sent);
				for (std::size_t I = 0; I < Count; ++I)
				{
					Bytes[I] = static_cast<std::uint8_t>((Sent + I) % 251);
				}
				auto Write = Pipe.Writer->Write(Bytes, Count);
				CHECK(Write.State != Network::EStreamState::Failed);
				Sent += static_cast<std::size_t>(Write.Bytes);
			}
			if (Sent == Total && !Finished)
			{
				Pipe.Writer->Finish();
				Finished = true;
			}
			auto Read = Task.Body()->Read(Bytes, sizeof(Bytes));
			if (Read.State == Network::EStreamState::Failed)
			{
				throw std::runtime_error(Read.Error.Message);
			}
			for (std::size_t I = 0; I < Read.Bytes; ++I)
			{
				CHECK(Bytes[I] == (Received + I) % 251);
			}
			Received += static_cast<std::size_t>(Read.Bytes);
			Ended = Read.State == Network::EStreamState::End;
			if (!Read.Bytes)
			{
				std::this_thread::yield();
			}
		}
		Pump(Task.Operation());
		CHECK(Ended && Received == Total && Sent == Total && !Task.Operation().Error());
		CHECK(Task.Operation().Result<Http::FResponse>()->BodyBytes == Total);
		auto Reused = Client.Send(Request);
		Pump(Reused);
		CHECK(Reused.Error().Code == Network::EError::State);
	}
	for (const bool Limited : {false, true})
	{
		Network::FBytePipe Pipe;
		CHECK(Network::TryCreateBytePipe(16, Pipe, Error));
		CHECK(Pipe.Writer->WriteAll("abc", 3).Bytes == 3);
		Pipe.Writer->Finish();
		Http::FRequest Request;
		Request.Url = Base + "/echo";
		Request.Method = "POST";
		Request.Upload = Pipe.Reader;
		if (Limited)
		{
			Request.MaxUploadBytes = 2;
		}
		else
		{
			Request.UploadLength = 100;
		}
		auto Op = Client.Send(Request);
		Pump(Op);
		CHECK(Op.Error().Code == (Limited ? Network::EError::Limit : Network::EError::Protocol));
	}
	Network::FBytePipe Pipe;
	CHECK(Network::TryCreateBytePipe(16, Pipe, Error));
	Http::FRequest Request;
	Request.Url = Base + "/echo";
	Request.Method = "POST";
	Request.Upload = Pipe.Reader;
	Request.Operation.Timeout = std::chrono::milliseconds(100);
	auto TimedOut = Client.Send(Request);
	Pump(TimedOut);
	CHECK(TimedOut.Error().Code == Network::EError::TimedOut &&
	    Pipe.Writer->Write("x", 1).Error.Code == Network::EError::Cancelled);
	Context.Shutdown();
}

static void TlsWebSocketTests(const std::string& CertFile)
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	if (!VeroScript::Boost::CreateNetworkService()->TlsAvailable())
	{
		auto Unsupported = VeroScript::WebSocket::Test::Connect(Context, "wss://127.0.0.1:1/");
		CHECK(Unsupported.Error().Code == Network::EError::Unsupported);
		return;
	}
	auto ReadFile = [](const std::filesystem::path& Path)
	{
		std::ifstream Input(Path, std::ios::binary);
		CHECK(Input.good());
		return std::string(std::istreambuf_iterator<char>(Input), {});
	};
	const auto Cert = ReadFile(CertFile);
	const auto Key = ReadFile(std::filesystem::path(CertFile).parent_path() / "loopback-key.pem");
	Ws::FOptions ServerOptions;
	ServerOptions.Tls.bEnabled = true;
	ServerOptions.Tls.CertificateChainPem = Cert;
	ServerOptions.Tls.PrivateKeyPem = Key;
	ServerOptions.Operation.Timeout = std::chrono::seconds(5);
	Network::FError Error;
	auto Server = VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, Error, ServerOptions);
	if (!Server)
	{
		throw std::runtime_error(Error.Message);
	}
	auto Accepted = Server->Accept();
	Ws::FOptions ClientOptions;
	ClientOptions.Tls.bUseSystemTrust = false;
	ClientOptions.Tls.TrustPem = Cert;
	auto Connected = VeroScript::WebSocket::Test::Connect(
	    Context, "wss://127.0.0.1:" + std::to_string(Server->Port()) + "/socket", ClientOptions);
	auto Client = Result<std::shared_ptr<Ws::FConnection>>(Connected);
	auto Peer = Result<std::shared_ptr<Ws::FConnection>>(Accepted);
	auto Received = Peer->Receive();
	CHECK(Result<std::uint64_t>(Client->Send({{'a', 0, 'b'}, true})) == 3);
	CHECK(Result<Ws::FMessage>(Received).Bytes == Network::FBytes({'a', 0, 'b'}));
	Received = Client->Receive();
	CHECK(Result<std::uint64_t>(Peer->Send({{0, 255, 1}, false})) == 3);
	CHECK(Result<Ws::FMessage>(Received).Bytes == Network::FBytes({0, 255, 1}));
	CHECK(Result<Ws::FCloseInfo>(Client->Close(1000, "done")).bClean);
	Server->Close();
	Context.Shutdown();
}

static void HttpsServerTests(const std::string& CertFile)
{
	if (!VeroScript::Boost::CreateNetworkService()->TlsAvailable())
	{
		return;
	}
	auto ReadFile = [](const std::filesystem::path& Path)
	{
		std::ifstream Input(Path, std::ios::binary);
		CHECK(Input.good());
		return std::string(std::istreambuf_iterator<char>(Input), {});
	};
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Http::FServerOptions Options;
	Options.Tls.bEnabled = true;
	Options.Tls.CertificateChainPem = ReadFile(CertFile);
	Options.Tls.PrivateKeyPem = ReadFile(std::filesystem::path(CertFile).parent_path() / "loopback-key.pem");
	Network::FError Error;
	auto Server = VeroScript::Http::Test::Listen(Context, "127.0.0.1", 0, Error, Options);
	if (!Server)
	{
		throw std::runtime_error(Error.Message);
	}
	Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
	Http::FRequest Request;
	Request.Url = "https://127.0.0.1:" + std::to_string(Server->Port()) + "/secure";
	Request.CaFile = CertFile;
	for (const auto* Method : {"POST", "HEAD", "GET"})
	{
		Request.Method = Method;
		Request.Body = std::string_view(Method) == "POST" ? Network::FBytes({'a', 0, 'b'}) : Network::FBytes{};
		auto Accepted = Server->Accept();
		auto Sent = Client.Send(Request);
		auto Exchange = Result<std::shared_ptr<Http::FExchange>>(Accepted);
		CHECK(Exchange->Request().Body == Request.Body && Exchange->Request().Method == Method);
		Http::FResponse Reply;
		Reply.Status = std::string_view(Method) == "GET" ? 304 : 200;
		Reply.Body = {'r', 0, 'x'};
		CHECK(Result<std::uint64_t>(Exchange->Reply(Reply)) > 0);
		const auto Received = Result<Http::FResponse>(Sent);
		CHECK(Received.Status == Reply.Status && Received.bComplete);
		if (std::string_view(Method) == "POST")
		{
			CHECK(Received.Body == Reply.Body);
		}
		else
		{
			CHECK(Received.Body.empty());
		}
		if (Reply.Status == 304)
		{
			CHECK(Received.Headers.GetAll("Content-Length").empty());
		}
	}
	Server->Close();
	Client.Close();
	Options.HandshakeTimeout = std::chrono::milliseconds(100);
	auto Slow = VeroScript::Http::Test::Listen(Context, "127.0.0.1", 0, Error, Options);
	CHECK(static_cast<bool>(Slow));
	auto Raw = Result<std::shared_ptr<Network::FTcpSocket>>(Network::FTcpSocket::Connect(Context, "127.0.0.1", Slow->Port()));
	Network::FOperationOptions ReadLimit;
	ReadLimit.Timeout = std::chrono::seconds(3);
	auto Read = Raw->Read(1, ReadLimit);
	CHECK(Read.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed &&
	    Read.Error().Code != Network::EError::TimedOut);
	Raw->Close();
	Slow->Close();
	Context.Shutdown();
}

static void WebSocketTests()
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FError E;
	auto Server = VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, E);
	CHECK(Server && !E && Server->Port() != 0);
	auto Accepted = Server->Accept();
	auto Client = Result<std::shared_ptr<Ws::FConnection>>(
	    VeroScript::WebSocket::Test::Connect(Context, "ws://127.0.0.1:" + std::to_string(Server->Port()) + "/"));
	auto Peer = Result<std::shared_ptr<Ws::FConnection>>(Accepted);
	auto CancelledReceive = Peer->Receive();
	CHECK(CancelledReceive.Cancel());
	CHECK(Result<std::uint64_t>(Client->Send({{'h', 'i'}, true})) == 2);
	auto Message = Result<Ws::FMessage>(Peer->Receive());
	CHECK(Message.bText && Message.Bytes == Network::FBytes({'h', 'i'}));
	CHECK(Result<std::uint64_t>(Peer->Send({{}, false})) == 0);
	Message = Result<Ws::FMessage>(Client->Receive());
	CHECK(!Message.bText && Message.Bytes.empty());
	CHECK(Result<std::uint64_t>(Client->Send({{}, true})) == 0);
	Message = Result<Ws::FMessage>(Peer->Receive());
	CHECK(Message.bText && Message.Bytes.empty() && Message.bFinal);
	std::vector<Network::FOperation> Sends;
	for (std::uint8_t I = 0; I < 30; ++I)
	{
		Sends.push_back(Client->Send({{I, 0, 255}, false}));
	}
	for (std::uint8_t I = 0; I < Sends.size(); ++I)
	{
		CHECK(Result<std::uint64_t>(Sends[I]) == 3);
		CHECK(Result<Ws::FMessage>(Peer->Receive()).Bytes == Network::FBytes({I, 0, 255}));
	}
	auto Invalid = Client->Close(1006);
	CHECK(Invalid.IsComplete() && Invalid.Error().Code == Network::EError::InvalidArgument);
	const std::string CloseReason("done\0tail", 9);
	auto Closed = Client->Close(1000, CloseReason);
	CHECK(Result<Ws::FCloseInfo>(Closed).bClean);
	const auto CloseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
	while (!Peer->CloseInfo().bClosed && std::chrono::steady_clock::now() < CloseDeadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const auto PeerClose = Peer->CloseInfo();
	CHECK(PeerClose.bClosed && PeerClose.bClean && PeerClose.Code == 1000 && PeerClose.Reason == CloseReason);
	Server->Close();
	Context.Shutdown();
	Client.reset();
	Peer.reset();
	Server.reset();
}

static void HttpsTests(std::uint16_t Port, const std::string& CaFile)
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
	CHECK(Client.Capabilities().bTls);
	Http::FRequest Request;
	Request.Url = "https://127.0.0.1:" + std::to_string(Port) + "/ok";
	Request.CaFile = CaFile;
	auto Response = Result<Http::FResponse>(Client.Send(Request));
	CHECK(Response.Status == 200 && Response.bComplete && Response.Body == Network::FBytes({'o', 'k'}));
	Request.Proxy = std::string("http://unused\0suffix", 20);
	auto Invalid = Client.Send(Request);
	CHECK(Invalid.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed &&
	    Invalid.Error().Code == Network::EError::InvalidArgument);
	Context.Shutdown();
}

static void HttpServerTests()
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FError E;
	auto Server = VeroScript::Http::Test::Listen(Context, "127.0.0.1", 0, E);
	CHECK(Server && !E);
	Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
	const auto Base = "http://127.0.0.1:" + std::to_string(Server->Port());
	Http::FRequest Request;
	Request.Url = Base + "/one?x=1";
	auto Incoming = Server->Accept();
	auto Outgoing = Client.Send(Request);
	auto Exchange = Result<std::shared_ptr<Http::FExchange>>(Incoming);
	CHECK(Exchange->Request().Target == "/one?x=1");
	Http::FResponse Response;
	Response.Status = 201;
	Response.Body = {'y', 'e', 's'};
	CHECK(Response.Trailers.TryAdd("X-Trailer", "complete", E));
	Result<std::uint64_t>(Exchange->Reply(Response));
	const auto WithTrailers = Result<Http::FResponse>(Outgoing);
	CHECK(WithTrailers.Status == 201 && WithTrailers.Body == Response.Body &&
	    WithTrailers.Trailers.GetAll("X-Trailer") == std::vector<std::string>{"complete"});
	Request.Url = Base + "/two";
	Incoming = Server->Accept();
	Outgoing = Client.Send(Request);
	auto Second = Result<std::shared_ptr<Http::FExchange>>(Incoming);
	CHECK(Exchange->Request().Target == "/one?x=1" && Second->Request().Target == "/two");
	auto StaleReply = Exchange->Reply(Response);
	CHECK(StaleReply.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed && StaleReply.Error());
	Exchange.reset();
	Result<std::uint64_t>(Second->Reply(Response));
	CHECK(Result<Http::FResponse>(Outgoing).Body == Response.Body);
	Context.Shutdown();
}

static void HttpServerBudgetTests()
{
	Network::FContextOptions Limits;
	Limits.MaxRetainedBytes = 128 * 1024;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Limits);
	Network::FContext Sender(VeroScript::Boost::CreateNetworkService());
	Http::FServerOptions ServerLimits;
	ServerLimits.MaxHeaderBytes = 4096;
	Network::FError Error;
	auto Server = VeroScript::Http::Test::Listen(Context, "127.0.0.1", 0, Error, ServerLimits);
	CHECK(Server && !Error);
	Http::FClient Client(Sender, VeroScript::Curl::CreateHttpClientService());
	Http::FRequest Request;
	Request.Url = "http://127.0.0.1:" + std::to_string(Server->Port());
	Request.Method = "POST";
	Request.Body.assign(32768, 42);
	auto Accept = Server->Accept();
	auto Sent = Client.Send(Request);
	auto Exchange = Result<std::shared_ptr<Http::FExchange>>(Accept);
	CHECK(Exchange->Request().Body == Request.Body);
	CHECK(Context.MemoryUsage().RetainedBytes >= Request.Body.size());
	Http::FResponse Reply;
	Reply.Status = 200;
	Reply.Body = {'o', 'k'};
	Result<std::uint64_t>(Exchange->Reply(Reply));
	Result<Http::FResponse>(Sent);
	Accept = {};
	Context.Shutdown();
	CHECK(Context.MemoryUsage().RetainedBytes >= Request.Body.size());
	Exchange.reset();
	CHECK(Context.MemoryUsage().RetainedBytes == 0);
	CHECK(Context.MemoryUsage().PeakRetainedBytes <= Limits.MaxRetainedBytes);
	Sender.Shutdown();
}

static void HostDrivenTest()
{
	Network::FContextOptions Options;
	Options.Mode = Network::EDriveMode::HostDriven;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	Network::FError E;
	auto Op = Network::Resolve(Context, "127.0.0.1", 80);
	CHECK(Op.Wait(std::chrono::milliseconds(0)) == Network::EWaitResult::RequiresPoll);
	auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!Op.IsComplete() && std::chrono::steady_clock::now() < End)
	{
		Context.PollIo(E);
		std::this_thread::yield();
	}
	CHECK(Op.IsComplete() && !Op.Error());
	Context.Shutdown();
}

static void UdpTests()
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FError E;
	auto A = Network::FUdpSocket::Bind(Context, "127.0.0.1", 0, E);
	auto B = Network::FUdpSocket::Bind(Context, "127.0.0.1", 0, E);
	CHECK(A && B);
	CHECK(Result<bool>(A->SetBroadcast(true)));
	CHECK(Result<bool>(A->SetBroadcast(false)));
	auto InvalidGroup = A->JoinMulticast({"127.0.0.1", "", 0});
	CHECK(InvalidGroup.IsComplete() && InvalidGroup.Error().Code == Network::EError::InvalidArgument);
	CHECK(Result<bool>(A->JoinMulticast({"239.255.42.1", "127.0.0.1", 0})));
	CHECK(Result<bool>(A->LeaveMulticast({"239.255.42.1", "127.0.0.1", 0})));
	auto Receive = B->Receive();
	CHECK(Result<std::uint64_t>(A->SendTo({}, {"127.0.0.1", B->Port(), false})) == 0);
	auto Datagram = Result<Network::FDatagram>(Receive);
	CHECK(Datagram.Bytes.empty() && Datagram.OriginalBytes == 0 && !Datagram.bTruncated &&
	    Datagram.Peer.Port == A->Port());
	Receive = B->Receive(2);
	CHECK(Result<std::uint64_t>(A->SendTo({1, 2, 3}, {"127.0.0.1", B->Port(), false})) == 3);
	Datagram = Result<Network::FDatagram>(Receive);
	CHECK(Datagram.Bytes == Network::FBytes({1, 2}) && Datagram.OriginalBytes == 3 && Datagram.bTruncated);
	Context.Shutdown();
}

static void TcpTests()
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FError E;
	auto Server = Network::FTcpListener::Listen(Context, "127.0.0.1", 0, E);
	CHECK(Server && !E && Server->Port());
	auto Accept = Server->Accept();
	auto Client =
	    Result<std::shared_ptr<Network::FTcpSocket>>(Network::FTcpSocket::Connect(Context, "127.0.0.1", Server->Port()));
	auto Peer = Result<std::shared_ptr<Network::FTcpSocket>>(Accept);
	std::vector<Network::FOperation> Writes;
	for (std::uint8_t I = 0; I < 40; ++I)
	{
		Writes.push_back(Client->Write({I, 0, 255}));
	}
	for (const auto& Op : Writes)
	{
		CHECK(Result<std::uint64_t>(Op) == 3);
	}
	Network::FBytes Received;
	while (Received.size() != 120)
	{
		auto Read = Result<Network::FReadResult>(Peer->Read(120 - Received.size()));
		CHECK(!Read.bEnd && !Read.Bytes.empty());
		Received.insert(Received.end(), Read.Bytes.begin(), Read.Bytes.end());
	}
	for (std::uint8_t I = 0; I < 40; ++I)
	{
		CHECK(Received[I * 3] == I && Received[I * 3 + 1] == 0 && Received[I * 3 + 2] == 255);
	}
	// Closed listeners fail promptly; cancellation does not destroy the listening socket.
	auto Cancel = Server->Accept();
	CHECK(Cancel.Cancel());
	auto Again = Server->Accept();
	auto Next =
	    Result<std::shared_ptr<Network::FTcpSocket>>(Network::FTcpSocket::Connect(Context, "127.0.0.1", Server->Port()));
	CHECK(Result<std::shared_ptr<Network::FTcpSocket>>(Again) != nullptr);
	Server->Close();
	auto Closed = Server->Accept();
	CHECK(Closed.Wait(std::chrono::seconds(5)) == Network::EWaitResult::Completed && Closed.Error());
	Context.Shutdown();
}

static void ControlTests()
{
	{
		Network::FContext Context(VeroScript::Boost::CreateNetworkService());
		Network::FError E;
		const auto Host = std::string("127.0.0.1\0suffix", 16);
		CHECK(Network::Resolve(Context, Host, 80).Error().Code == Network::EError::InvalidArgument);
		CHECK(Network::FTcpSocket::Connect(Context, Host, 80).Error().Code == Network::EError::InvalidArgument);
		CHECK(Network::FTcpSocket::Connect(Context, "127.0.0.1", 0).Error().Code == Network::EError::InvalidArgument);
		CHECK(!Network::FTcpListener::Listen(Context, Host, 0, E) && E.Code == Network::EError::InvalidArgument);
		CHECK(!Network::FUdpSocket::Bind(Context, Host, 0, E) && E.Code == Network::EError::InvalidArgument);
		CHECK(!VeroScript::Http::Test::Listen(Context, Host, 0, E) && E.Code == Network::EError::InvalidArgument);
		CHECK(!VeroScript::WebSocket::Test::Listen(Context, Host, 0, E) && E.Code == Network::EError::InvalidArgument);
		Context.Shutdown();
	}
	if (sizeof(long) == 4)
	{
		Network::FContextOptions Large;
		Large.MaxConnections = UINT32_MAX;
		Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Large);
		bool Rejected = false;
		try
		{
			Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
		}
		catch (const std::invalid_argument&)
		{
			Rejected = true;
		}
		CHECK(Rejected);
	}
	Network::FContextOptions Options;
	Options.Mode = Network::EDriveMode::HostDriven;
	Options.MaxOperations = 1;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	auto Pending = Network::Resolve(Context, "127.0.0.1", 80);
	auto Rejected = Network::Resolve(Context, "127.0.0.1", 80);
	CHECK(Rejected.IsComplete() && Rejected.Error().Code == Network::EError::QueueFull);
	CHECK(Pending.Cancel() && Pending.IsComplete());
	Network::FError E;
	bool Called = false;
	CHECK(Pending.OnComplete(
	    [&](const auto&)
	{
		Network::FError Nested;
		CHECK(Context.Dispatch(Nested) == 0 && Nested.Code == Network::EError::State);
		Context.Shutdown();
		Called = true;
	},
	    E));
	CHECK(Context.Dispatch(E) == 1 && Called && !E && Context.IsClosed());
}

static void CloseQueueTest()
{
	Network::FContextOptions Options;
	Options.Mode = Network::EDriveMode::HostDriven;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	Network::FError E;
	auto Server = VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, E);
	CHECK(Server && !E);
	auto Accepted = Server->Accept();
	Ws::FOptions Limits;
	Limits.MaxQueuedMessages = 1;
	auto Connected = VeroScript::WebSocket::Test::Connect(Context, "ws://127.0.0.1:" + std::to_string(Server->Port()), Limits);
	auto Pump = [&](const Network::FOperation& Op)
	{
		auto End = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!Op.IsComplete() && std::chrono::steady_clock::now() < End)
		{
			Context.PollIo(E);
			CHECK(!E);
			std::this_thread::yield();
		}
		CHECK(Op.IsComplete());
	};
	Pump(Connected);
	Pump(Accepted);
	auto Client = Result<std::shared_ptr<Ws::FConnection>>(Connected);
	auto Peer = Result<std::shared_ptr<Ws::FConnection>>(Accepted);
	auto Sent = Client->Send({Network::FBytes(65536, 42), false});
	auto Closed = Client->Close();
	Pump(Closed);
	CHECK(!Closed.Error() && Result<Ws::FCloseInfo>(Closed).bClean);
	CHECK(Sent.IsComplete());
	Context.Shutdown();
}

static void RetainedBudgetTests(std::uint16_t Port)
{
	Network::FContextOptions Options;
	Options.MaxRetainedBytes = 64;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
	Network::FContext Sender(VeroScript::Boost::CreateNetworkService());
	Network::FError Error;
	auto Listener = Network::FTcpListener::Listen(Context, "127.0.0.1", 0, Error);
	CHECK(Listener && !Error);
	auto Accepted = Listener->Accept();
	auto Outgoing =
	    Result<std::shared_ptr<Network::FTcpSocket>>(Network::FTcpSocket::Connect(Sender, "127.0.0.1", Listener->Port()));
	auto Incoming = Result<std::shared_ptr<Network::FTcpSocket>>(Accepted);
	auto Read = Incoming->Read(64);
	CHECK(Incoming->Read(1).Error().Code == Network::EError::Limit);
	Result<std::uint64_t>(Outgoing->Write({42}));
	CHECK(Read.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed && !Read.Error());
	auto Retained = Read.Result<Network::FReadResult>();
	CHECK(Retained && Retained->Bytes == Network::FBytes{42});
	Read = {};
	CHECK(Context.MemoryUsage().RetainedBytes == 64);
	Context.Shutdown();
	CHECK(Context.MemoryUsage().RetainedBytes == 64);
	Retained.reset();
	CHECK(Context.MemoryUsage().RetainedBytes == 0);
	Sender.Shutdown();

	Options.MaxRetainedBytes = 48 * 1024;
	Network::FContext HttpContext(VeroScript::Boost::CreateNetworkService(), Options);
	Http::FClient Client(HttpContext, VeroScript::Curl::CreateHttpClientService());
	Http::FRequest Request;
	Request.Url = "http://127.0.0.1:" + std::to_string(Port) + "/stream";
	auto TooLarge = Client.Send(Request);
	CHECK(TooLarge.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed);
	CHECK(TooLarge.Error().Code == Network::EError::Limit);
	HttpContext.Shutdown();
	CHECK(HttpContext.MemoryUsage().RetainedBytes == 0);
	CHECK(HttpContext.MemoryUsage().PeakRetainedBytes <= Options.MaxRetainedBytes);
}

static void SseClientTests(std::uint16_t Port)
{
	{
		Network::FContextOptions Limits;
		Limits.MaxConnections = 1;
		Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Limits);
		Http::FClient Existing(Context, VeroScript::Curl::CreateHttpClientService());
		Network::FError Error;
		CHECK(!VeroScript::Http::Test::ConnectSse(Context, {}, Error) && Error.Code == Network::EError::Limit);
		Context.Shutdown();
		CHECK(!VeroScript::Http::Test::ConnectSse(Context, {}, Error) && Error.Code == Network::EError::Closed);
	}
	for (const auto Mode : {Network::EDriveMode::NativeBackground, Network::EDriveMode::HostDriven})
	{
		Network::FContextOptions ContextOptions;
		ContextOptions.Mode = Mode;
		Network::FContext Context(VeroScript::Boost::CreateNetworkService(), ContextOptions);
		Network::FError Error;
		Http::FSseClientOptions Options;
		Options.Request.Url = "http://127.0.0.1:" + std::to_string(Port) + "/sse-reconnect";
		auto Invalid = Options;
		const auto FactoryCalled = std::make_shared<std::atomic<bool>>(false);
		Invalid.Request.UploadFactory = [FactoryCalled](std::uint32_t, Network::FError&)
		{
			FactoryCalled->store(true);
			return Http::FBodySource{};
		};
		CHECK(!VeroScript::Http::Test::ConnectSse(Context, Invalid, Error) && Error.Code == Network::EError::InvalidArgument &&
		    !FactoryCalled->load());
		Invalid.Request.UploadFactory = {};
		Invalid.Request.UploadLength = 0;
		CHECK(!VeroScript::Http::Test::ConnectSse(Context, Invalid, Error) && Error.Code == Network::EError::InvalidArgument);
		Options.MaxPollBytes = 2;
		Options.ReconnectDelay = std::chrono::milliseconds(15);
		Options.MaxReconnectDelay = std::chrono::milliseconds(20);
		Options.MaxReconnects = 2;
		auto Client = VeroScript::Http::Test::ConnectSse(Context, Options, Error);
		CHECK(Client && !Error);
		Http::FSseEvent Event;
		std::vector<Http::FSseEvent> Events;
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		Http::ESseClientState State;
		do
		{
			if (Mode == Network::EDriveMode::HostDriven)
			{
				Context.PollIo(Error);
				CHECK(!Error);
			}
			State = Client->Poll(Event);
			if (State == Http::ESseClientState::Failed)
			{
				throw std::runtime_error("SSE reconnect failed (" +
				    std::to_string(static_cast<unsigned>(Client->Error().Code)) +
				    "): " + Client->Error().Message);
			}
			CHECK(State != Http::ESseClientState::Failed);
			if (State == Http::ESseClientState::Reconnecting)
			{
				CHECK(Client->ReconnectRemaining() <= std::chrono::milliseconds(1));
			}
			if (State == Http::ESseClientState::EventReady)
			{
				Events.push_back(Event);
			}
			CHECK(std::chrono::steady_clock::now() < Deadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		while (State != Http::ESseClientState::Closed);
		CHECK(Events.size() == 2 && Events[0].Data == "one" && Events[1].Data == "two");
		CHECK(Events[0].Id == "first" && Events[1].Id == "first");
		CHECK(Client->LastEventId() == "silent" && Client->ReconnectCount() == 2);
		CHECK(Client->ResponseHead() && Client->ResponseHead()->Status == 204);
		Client.reset();
		Options.Request.Url = "http://127.0.0.1:" + std::to_string(Port) + "/sse-disconnect";
		Client = VeroScript::Http::Test::ConnectSse(Context, Options, Error);
		CHECK(Client && !Error);
		unsigned RecoveredEvents = 0;
		const auto RecoveryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		do
		{
			if (Mode == Network::EDriveMode::HostDriven)
			{
				Context.PollIo(Error);
				CHECK(!Error);
			}
			State = Client->Poll(Event);
			CHECK(State != Http::ESseClientState::Failed);
			if (State == Http::ESseClientState::EventReady)
			{
				CHECK(Event.Data == "before-close" && Event.Id == "resume");
				++RecoveredEvents;
			}
			CHECK(std::chrono::steady_clock::now() < RecoveryDeadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		while (State != Http::ESseClientState::Closed);
		CHECK(RecoveredEvents == 1 && Client->ReconnectCount() == 1 && Client->ResponseHead() &&
		    Client->ResponseHead()->Status == 204);
		Client.reset();
		Options.MaxReconnects = 0;
		Options.MaxPollBytes = 65536;
		Options.Request.Url = "http://127.0.0.1:" + std::to_string(Port) + "/sse-stop";
		for (unsigned Attempt = 0; Attempt < 32; ++Attempt)
		{
			Client = VeroScript::Http::Test::ConnectSse(Context, Options, Error);
			CHECK(Client && !Error);
			const auto StopDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			do
			{
				if (Mode == Network::EDriveMode::HostDriven)
				{
					Context.PollIo(Error);
					CHECK(!Error);
				}
				State = Client->Poll(Event);
				CHECK(State != Http::ESseClientState::Failed);
				CHECK(std::chrono::steady_clock::now() < StopDeadline);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			while (State != Http::ESseClientState::Closed);
			CHECK(Client->ResponseHead() && Client->ResponseHead()->Status == 204);
			Client.reset();
		}
		Options.Request.Url = "http://127.0.0.1:" + std::to_string(Port) + "/sse-batch";
		Client = VeroScript::Http::Test::ConnectSse(Context, Options, Error);
		CHECK(Client && !Error);
		std::uint64_t Charged = 0;
		std::size_t EventCount = 0;
		const Http::FSseWork CountWork{[](void* User, std::uint64_t Units)
		{
			*static_cast<std::uint64_t*>(User) += Units;
			return true;
		},
		    &Charged};
		const auto BatchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		do
		{
			if (Mode == Network::EDriveMode::HostDriven)
			{
				Context.PollIo(Error);
				CHECK(!Error);
			}
			State = Client->Poll(Event, CountWork);
			CHECK(State != Http::ESseClientState::Failed);
			if (State == Http::ESseClientState::EventReady)
			{
				CHECK(Event.Data == "x");
				++EventCount;
			}
			CHECK(std::chrono::steady_clock::now() < BatchDeadline);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		while (State != Http::ESseClientState::Closed);
		CHECK(EventCount == 64 && Charged == 64 * std::string_view("data: x\n\n").size());
		Client.reset();
		for (const char* Path : {"/ok", "/sse-limit", "/sse-stall"})
		{
			Options.Request.Url = "http://127.0.0.1:" + std::to_string(Port) + Path;
			Options.Parser.MaxLineBytes = 32;
			Options.IdleTimeout = std::chrono::milliseconds(80);
			Client = VeroScript::Http::Test::ConnectSse(Context, Options, Error);
			CHECK(Client && !Error);
			Event.Data = "unchanged";
			const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			do
			{
				if (Mode == Network::EDriveMode::HostDriven)
				{
					Context.PollIo(Error);
					CHECK(!Error);
				}
				State = Client->Poll(Event);
				CHECK(std::chrono::steady_clock::now() < Until);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			while (State == Http::ESseClientState::Pending);
			CHECK(State == Http::ESseClientState::Failed && Event.Data == "unchanged");
			CHECK(Client->ResponseHead() && Client->ResponseHead()->Status == 200);
			const auto Expected = std::string_view(Path) == "/ok" ? Network::EError::Protocol
			    : std::string_view(Path) == "/sse-limit"          ? Network::EError::Limit
			                                                      : Network::EError::TimedOut;
			CHECK(Client->Error().Code == Expected && Client->Poll(Event) == Http::ESseClientState::Failed);
			Client.reset();
		}
		for (bool Throw : {false, true})
		{
			Options.Request.Url = "http://127.0.0.1:" + std::to_string(Port) + "/sse-limit";
			Client = VeroScript::Http::Test::ConnectSse(Context, Options, Error);
			CHECK(Client && !Error);
			const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			const Http::FSseWork Work{[](void* User, std::uint64_t Units)
			{
				if (!Units)
				{
					return true;
				}
				if (*static_cast<bool*>(User))
				{
					throw std::runtime_error("work callback");
				}
				return false;
			},
			    &Throw};
			do
			{
				if (Mode == Network::EDriveMode::HostDriven)
				{
					Context.PollIo(Error);
					CHECK(!Error);
				}
				State = Client->Poll(Event, Work);
				CHECK(std::chrono::steady_clock::now() < Until);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			while (State == Http::ESseClientState::Pending);
			CHECK(State == Http::ESseClientState::Failed &&
			    Client->Error().Code == (Throw ? Network::EError::Callback : Network::EError::Cancelled));
			Client.reset();
		}
		Context.Shutdown();
		CHECK(Context.MemoryUsage().RetainedBytes == 0);
	}
}

static void WebSocketProtocolTests(std::uint16_t Port)
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FError Error;
	Ws::FOptions ServerOptions;
	ServerOptions.Subprotocols = {"game.v2", "game.v1"};
	ServerOptions.bRequireSubprotocol = true;
	auto Server = VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, Error, ServerOptions);
	CHECK(Server && !Error);
	Ws::FOptions ClientOptions;
	ClientOptions.Subprotocols = {"game.v1", "game.v2"};
	ClientOptions.bRequireSubprotocol = true;
	const auto Url = "ws://127.0.0.1:" + std::to_string(Server->Port());
	auto Accept = Server->Accept();
	auto Client = Result<std::shared_ptr<Ws::FConnection>>(VeroScript::WebSocket::Test::Connect(Context, Url, ClientOptions));
	auto Peer = Result<std::shared_ptr<Ws::FConnection>>(Accept);
	CHECK(Client->Subprotocol() == "game.v2" && Peer->Subprotocol() == "game.v2");
	CHECK(Result<Ws::FCloseInfo>(Client->Close()).bClean);
	ClientOptions.Subprotocols = {"different"};
	auto Rejected = VeroScript::WebSocket::Test::Connect(Context, Url, ClientOptions);
	CHECK(Rejected.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed && Rejected.Error());
	for (const auto& Tokens :
	    std::vector<std::vector<std::string>>{{""}, {"a b"}, {"a,b"}, {"a", "a"}, {std::string(16385, 'x')}})
	{
		ClientOptions.Subprotocols = Tokens;
		CHECK(VeroScript::WebSocket::Test::Connect(Context, Url, ClientOptions).Error().Code == Network::EError::InvalidArgument);
		CHECK(!VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, Error, ClientOptions) &&
		    Error.Code == Network::EError::InvalidArgument);
	}
	ClientOptions.Subprotocols = {"game.v1"};
	for (const char* Path : {"unoffered", "multiple"})
	{
		auto Op = VeroScript::WebSocket::Test::Connect(
		    Context, "ws://127.0.0.1:" + std::to_string(Port) + "/ws-protocol-" + Path, ClientOptions);
		CHECK(Op.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed &&
		    Op.Error().Code == Network::EError::Protocol);
	}
	auto External = Result<std::shared_ptr<Ws::FConnection>>(VeroScript::WebSocket::Test::Connect(
	    Context, "ws://127.0.0.1:" + std::to_string(Port) + "/ws-protocol-valid", ClientOptions));
	CHECK(External->Subprotocol() == "game.v1");
	CHECK(Result<Ws::FMessage>(External->Receive()).Bytes == Network::FBytes({'o', 'k'}));
	// A complete first masked frame shares the request write. The server must
	// preserve every read-ahead byte when passing the upgrade to Beast.
	auto Raw = Result<std::shared_ptr<Network::FTcpSocket>>(Network::FTcpSocket::Connect(Context, "127.0.0.1", Server->Port()));
	auto RawAccept = Server->Accept();
	std::string Request = "GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
	                      "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: "
	                      "dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Protocol: game.v1\r\n\r\n";
	Network::FBytes Bytes(Request.begin(), Request.end());
	Bytes.insert(Bytes.end(), {0x81, 0x82, '1', '2', '3', '4', 'o' ^ '1', 'k' ^ '2'});
	Result<std::uint64_t>(Raw->Write(std::move(Bytes)));
	auto RawPeer = Result<std::shared_ptr<Ws::FConnection>>(RawAccept);
	CHECK(RawPeer->Subprotocol() == "game.v1");
	CHECK(Result<Ws::FMessage>(RawPeer->Receive()).Bytes == Network::FBytes({'o', 'k'}));
	const auto ShutdownStart = std::chrono::steady_clock::now();
	Context.Shutdown();
	CHECK(std::chrono::steady_clock::now() - ShutdownStart < std::chrono::seconds(5));
}

static void WebSocketControlLimitTests()
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FError Error;
	Ws::FOptions Options;
	Options.MaxQueuedMessages = 1;
	auto Server = VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, Error, Options);
	CHECK(Server && !Error);
	auto Accepted = Server->Accept();
	auto Client = Result<std::shared_ptr<Ws::FConnection>>(
	    VeroScript::WebSocket::Test::Connect(Context, "ws://127.0.0.1:" + std::to_string(Server->Port())));
	auto Peer = Result<std::shared_ptr<Ws::FConnection>>(Accepted);
	auto Cancelled = Peer->Receive();
	CHECK(Cancelled.Cancel());
	auto Pending = Peer->Receive();
	Result<std::uint64_t>(Client->Ping("first"));
	auto Sending = Peer->Send({Network::FBytes(256 * 1024, 42), false});
	auto Overflow = Client->Ping("second");
	CHECK(Overflow.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed);
	CHECK(Pending.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed);
	CHECK(Pending.Error().Code == Network::EError::Limit);
	CHECK(Sending.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed);
	if (Sending.State() != Network::EOperationState::Succeeded)
	{
		CHECK(Sending.Error().Code == Network::EError::Limit && Sending.Error().SystemCode == 0);
	}
	CHECK(Cancelled.State() == Network::EOperationState::Cancelled &&
	    Cancelled.Error().Code == Network::EError::Cancelled);
	const auto Closed = Peer->CloseInfo();
	CHECK(Closed.bClosed && !Closed.bClean && Closed.Reason == "WebSocket control queue limit exceeded");
	auto Late = Peer->Receive();
	CHECK(Late.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed &&
	    Late.Error().Code == Network::EError::Limit);
	Context.Shutdown();
}

static void WebSocketFragmentTests()
{
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FError Error;
	Ws::FOptions Options;
	Options.IdleTimeout = std::chrono::milliseconds(0);
	CHECK(!VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, Error, Options) && Error.Code == Network::EError::InvalidArgument);
	Options.IdleTimeout = std::chrono::seconds(60);
	Options.bReceiveFragments = true;
	Options.bCompression = VeroScript::WebSocket::Test::CompressionSupported();
	auto Server = VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, Error, Options);
	CHECK(Server && !Error);
	auto Accept = Server->Accept();
	Options.bReceiveFragments = false;
	auto Client = Result<std::shared_ptr<Ws::FConnection>>(
	    VeroScript::WebSocket::Test::Connect(Context, "ws://127.0.0.1:" + std::to_string(Server->Port()), Options));
	auto Peer = Result<std::shared_ptr<Ws::FConnection>>(Accept);
	Result<std::uint64_t>(Client->SendFragment({{0xe4}, true, false}));
	auto Wrong = Client->Send({{'x'}, true});
	CHECK(Wrong.Wait(std::chrono::seconds(10)) == Network::EWaitResult::Completed &&
	    Wrong.Error().Code == Network::EError::State);
	Result<std::uint64_t>(Client->Ping("probe"));
	Result<std::uint64_t>(Client->SendFragment({{0xb8, 0xad}, true, true}));
	Network::FBytes Combined;
	bool Final = false;
	bool FirstPiece = true;
	while (!Final)
	{
		const auto Piece = Result<Ws::FMessage>(Peer->Receive());
		CHECK(Piece.bText);
		CHECK(Piece.bFirst == FirstPiece);
		FirstPiece = false;
		Combined.insert(Combined.end(), Piece.Bytes.begin(), Piece.Bytes.end());
		Final = Piece.bFinal;
	}
	CHECK(Combined == Network::FBytes({0xe4, 0xb8, 0xad}));
	auto PongDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	Ws::FControlEvent Event;
	while (!Client->TryTakeControl(Event) && std::chrono::steady_clock::now() < PongDeadline)
	{
		std::this_thread::yield();
	}
	CHECK(Event.Type == Ws::EControlType::Pong && Event.Payload == "probe");
	CHECK(Peer->TryTakeControl(Event) && Event.Type == Ws::EControlType::Ping && Event.Payload == "probe");
	CHECK(Client->Ping(std::string(126, 'x')).Error().Code == Network::EError::InvalidArgument);
	Result<std::uint64_t>(Peer->SendFragment({{0, 1}, false, false}));
	Result<std::uint64_t>(Peer->SendFragment({{2, 0}, false, true}));
	const auto Whole = Result<Ws::FMessage>(Client->Receive());
	CHECK(!Whole.bText && Whole.bFinal && Whole.Bytes == Network::FBytes({0, 1, 2, 0}));
	CHECK(Result<Ws::FCloseInfo>(Client->Close()).bClean);
	Context.Shutdown();
}

int main(int Argc, char** Argv)
{
	try
	{
		CHECK(Argc == 5);
		UrlTests();
		RetainedBudgetTests(static_cast<std::uint16_t>(std::stoi(Argv[1])));
		WebSocketControlLimitTests();
		WebSocketFragmentTests();
		WebSocketProtocolTests(static_cast<std::uint16_t>(std::stoi(Argv[1])));
		SseClientTests(static_cast<std::uint16_t>(std::stoi(Argv[1])));
		HttpStreamTests(static_cast<std::uint16_t>(std::stoi(Argv[1])));
		HttpUploadTests(static_cast<std::uint16_t>(std::stoi(Argv[1])));
		PhysicalBudgetTests(static_cast<std::uint16_t>(std::stoi(Argv[1])));
		HttpTests(static_cast<std::uint16_t>(std::stoi(Argv[1])), static_cast<std::uint16_t>(std::stoi(Argv[2])));
		HttpsTests(static_cast<std::uint16_t>(std::stoi(Argv[3])), Argv[4]);
		TlsWebSocketTests(Argv[4]);
		HttpsServerTests(Argv[4]);
		WebSocketTests();
		HttpServerTests();
		HttpServerBudgetTests();
		HostDrivenTest();
		UdpTests();
		TcpTests();
		ControlTests();
		CloseQueueTest();
		std::cout << "Network loopback tests passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
