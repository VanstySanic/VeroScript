// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"
#include "HttpTestBackends.h"
#include "VeroScriptHttp.h"
#include <iostream>
#include <stdexcept>
#include <thread>
namespace N = VeroScript::Network;
namespace H = VeroScript::Http;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error(std::string("Line ") + std::to_string(__LINE__) + ": " + #X); \
		} \
	} \
	while (false)
template <class F>
static void Until(N::FContext& C, F Predicate)
{
	const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!Predicate())
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
static T Get(N::FContext& C, const N::FOperation& Op)
{
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	if (Op.Error())
	{
		throw std::runtime_error(Op.Error().Message);
	}
	const auto R = Op.Result<T>();
	CHECK(R);
	return *R;
}
static void Test(N::EDriveMode Mode)
{
	N::FContextOptions O;
	O.Mode = Mode;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), O);
	N::FError E;
	for (int Case = 0; Case < 4; ++Case)
	{
		auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E);
		CHECK(Server && !E);
		H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
		H::FRequest Q;
		Q.Url = "http://127.0.0.1:" + std::to_string(Server->Port());
		auto Accept = Server->Accept();
		auto Outgoing = Client.Send(Q);
		auto Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
		auto Waiting = Server->Accept();
		auto Stop = Server->Shutdown(Case == 1 ? std::chrono::milliseconds(10) : std::chrono::seconds(5),
		    N::FOperationOptions{Case == 3 ? std::chrono::milliseconds(10) : std::chrono::seconds(10)});
		Until(C,
		    [&]
		{
			return Stop.State() == N::EOperationState::Running || Stop.IsComplete();
		});
		Until(C,
		    [&]
		{
			return Waiting.IsComplete();
		});
		CHECK(Waiting.Error().Code == N::EError::Closed);
		auto Refused = Server->Accept();
		Until(C,
		    [&]
		{
			return Refused.IsComplete();
		});
		CHECK(Refused.Error().Code == N::EError::Closed);
		if (Case == 0)
		{
			CHECK(!Stop.IsComplete());
			H::FResponse R;
			R.Status = 200;
			R.Body = {'o', 'k'};
			Get<std::uint64_t>(C, Exchange->ReplyStream(R).Operation());
			CHECK(Get<H::FResponse>(C, Outgoing).Body == R.Body);
			const auto Result = Get<H::FShutdownResult>(C, Stop);
			CHECK(!Result.bForced && Result.ClosedConnections == 1);
			CHECK(!Get<H::FShutdownResult>(C, Server->Shutdown()).bForced);
		}
		else if (Case == 1)
		{
			const auto Result = Get<H::FShutdownResult>(C, Stop);
			CHECK(Result.bForced && Result.ClosedConnections == 1);
		}
		else
		{
			if (Case == 2)
			{
				CHECK(Stop.Cancel());
			}
			Until(C,
			    [&]
			{
				return Stop.IsComplete();
			});
			CHECK(Stop.Error().Code == (Case == 2 ? N::EError::Cancelled : N::EError::TimedOut));
		}
		Until(C,
		    [&]
		{
			return Outgoing.IsComplete();
		});
	}
	// No active connections: close admission and queued accepts without forcing any exchange.
	{
		auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E);
		CHECK(Server && !E);
		auto Waiting = Server->Accept();
		const auto Result = Get<H::FShutdownResult>(C, Server->Shutdown(std::chrono::milliseconds(0)));
		CHECK(!Result.bForced && Result.ClosedConnections == 0);
		Until(C,
		    [&]
		{
			return Waiting.IsComplete();
		});
		CHECK(Waiting.Error().Code == N::EError::Closed);
	}
	// A request still awaiting application acceptance closes without using the grace interval.
	{
		H::FServerOptions SO;
		SO.bStreamRequests = true;
		auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, SO);
		CHECK(Server && !E);
		auto Socket = Get<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port()));
		const std::string Header =
		    "POST /queued HTTP/1.1\r\nHost: localhost\r\nExpect: 100-continue\r\nContent-Length: 1\r\n\r\n";
		Get<std::uint64_t>(C, Socket->Write(N::FBytes(Header.begin(), Header.end())));
		std::string Info;
		Until(C,
		    [&]
		{
			auto Piece = Get<N::FReadResult>(C, Socket->Read(4096));
			Info.append(Piece.Bytes.begin(), Piece.Bytes.end());
			return Info.find("\r\n\r\n") != Info.npos;
		});
		const auto Result = Get<H::FShutdownResult>(C, Server->Shutdown(std::chrono::seconds(4)));
		CHECK(!Result.bForced && Result.ClosedConnections == 1);
		Socket->Close();
	}
	// An early finished response still grants the request body grace instead of starting another request.
	{
		H::FServerOptions SO;
		SO.bStreamRequests = true;
		auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, SO);
		CHECK(Server && !E);
		auto Accept = Server->Accept();
		auto Socket = Get<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port()));
		const std::string Head = "POST /drain HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\n\r\n";
		Get<std::uint64_t>(C, Socket->Write(N::FBytes(Head.begin(), Head.end())));
		auto Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
		H::FResponse R;
		R.Status = 200;
		Get<std::uint64_t>(C, Exchange->ReplyStream(R).Operation());
		auto Stop = Server->Shutdown(std::chrono::seconds(5));
		Until(C,
		    [&]
		{
			return Stop.State() == N::EOperationState::Running;
		});
		CHECK(!Exchange->Request().BodyOperation.IsComplete());
		Get<std::uint64_t>(C, Socket->Write(N::FBytes{'x'}));
		CHECK(Get<H::FRequestBodyResult>(C, Exchange->Request().BodyOperation).BodyBytes == 1);
		CHECK(!Get<H::FShutdownResult>(C, Stop).bForced);
		Socket->Close();
	}
	C.Shutdown();
}
int main()
{
	try
	{
		Test(N::EDriveMode::NativeBackground);
		Test(N::EDriveMode::HostDriven);
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
	return 0;
}
