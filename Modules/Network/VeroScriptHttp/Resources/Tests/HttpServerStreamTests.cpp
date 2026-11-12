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
static void Await(N::FContext& C, const N::FOperation& Op)
{
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
}
template <class T>
static T Get(N::FContext& C, const N::FOperation& Op)
{
	Await(C, Op);
	if (Op.Error())
	{
		throw std::runtime_error(Op.Error().Message);
	}
	const auto R = Op.Result<T>();
	CHECK(R);
	return *R;
}
static N::FBytePipe Pipe(N::FContext& C)
{
	N::FBytePipe P;
	N::FError E;
	CHECK(N::TryCreateBytePipe(C, 64, P, E));
	return P;
}

static void Test(N::EDriveMode Mode)
{
	N::FContextOptions O;
	O.Mode = Mode;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), O);
	N::FError E;
	H::FServerOptions SO;
	SO.MaxStreamBodyBytes = 8;
	auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, SO);
	CHECK(Server && !E);
	H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
	H::FRequest Q;
	Q.Url = "http://127.0.0.1:" + std::to_string(Server->Port()) + "/stream";
	auto Accept = Server->Accept();
	auto Incoming = Client.SendStream(Q, 16384);
	auto Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	auto P = Pipe(C);
	H::FResponse R;
	R.Status = 200;
	CHECK(R.Trailers.TryAdd("X-Checksum", "five", E));
	auto Reply = Exchange->ReplyStream(R, {P.Reader, {}});
	H::FResponse Head;
	Until(C,
	    [&]
	{
		return Incoming.TryHead(Head, E);
	});
	CHECK(!E && Head.Status == 200);
	CHECK(!Reply.Operation().IsComplete() && Reply.State() == H::EResponseState::HeadersSent);
	CHECK(Head.Headers.GetAll("Trailer") == std::vector<std::string>{"X-Checksum"});
	CHECK(P.Writer->WriteAll("hello", 5).Bytes == 5);
	Until(C,
	    [&]
	{
		return Incoming.Body()->BufferedBytes() == 5;
	});
	CHECK(!Incoming.Operation().IsComplete() && !Reply.Operation().IsComplete());
	char Data[32]{};
	CHECK(Incoming.Body()->Read(Data, 32).Bytes == 5 && std::string(Data, 5) == "hello");
	P.Writer->Finish();
	Await(C, Reply.Operation());
	const auto Received = Get<H::FResponse>(C, Incoming.Operation());
	CHECK(Reply.State() == H::EResponseState::Finished && Reply.BodyBytes() == 5);
	CHECK(Received.BodyBytes == 5 && Received.Trailers.GetAll("X-Checksum") == std::vector<std::string>{"five"});
	CHECK(Incoming.Body()->Read(Data, 32).State == N::EStreamState::End);

	// A finished stream leaves a keep-alive connection available for the next exchange.
	Accept = Server->Accept();
	auto Buffered = Client.Send(Q);
	auto Next = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	auto Stale = Exchange->ReplyStream(R);
	Await(C, Stale.Operation());
	CHECK(Stale.Operation().Error().Code == N::EError::State && Stale.State() == H::EResponseState::Aborted);
	R.Body = {'o', 'k'};
	Get<std::uint64_t>(C, Next->Reply(R));
	CHECK(Get<H::FResponse>(C, Buffered).Body == R.Body);

	// Invalid trailers fail before commitment; the same exchange can still send a valid response.
	Accept = Server->Accept();
	Buffered = Client.Send(Q);
	Next = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	H::FResponse Invalid;
	Invalid.Status = 200;
	CHECK(Invalid.Trailers.TryAdd("Content-Length", "2", E));
	auto Failed = Next->ReplyStream(Invalid);
	Await(C, Failed.Operation());
	CHECK(Failed.Operation().Error().Code == N::EError::InvalidArgument);
	R.Trailers = {};
	Get<std::uint64_t>(C, Next->Reply(R));
	CHECK(Get<H::FResponse>(C, Buffered).Body == R.Body);

	// Known-length underflow, overflow, unknown-length limit, and source failure close their connection.
	for (int Case = 0; Case < 5; ++Case)
	{
		Accept = Server->Accept();
		Incoming = Client.SendStream(Q, 16384);
		Next = Get<std::shared_ptr<H::FExchange>>(C, Accept);
		P = Pipe(C);
		H::FResponse Response;
		Response.Status = 200;
		const auto Length = Case < 2 ? std::optional<std::uint64_t>(2) : std::nullopt;
		Failed = Next->ReplyStream(Response, {P.Reader, Length});
		if (Case == 0)
		{
			CHECK(P.Writer->WriteAll("a", 1).Bytes == 1);
			P.Writer->Finish();
		}
		else if (Case == 1)
		{
			CHECK(P.Writer->WriteAll("abc", 3).Bytes == 3);
			P.Writer->Finish();
		}
		else if (Case == 2)
		{
			CHECK(P.Writer->WriteAll("123456789", 9).Bytes == 9);
			P.Writer->Finish();
		}
		else if (Case == 3)
		{
			P.Writer->Fail({N::EError::Read, "test producer", 0});
		}
		else
		{
			Until(C,
			    [&]
			{
				return Incoming.TryHead(Head, E);
			});
			P.Reader->Cancel();
		}
		Await(C, Failed.Operation());
		CHECK(Failed.Operation().Error().Code == (Case < 2 ? N::EError::Protocol : Case == 2 ? N::EError::Limit
		                                                 : Case == 3                         ? N::EError::Read
		                                                                                     : N::EError::Cancelled));
		CHECK(Failed.State() == H::EResponseState::Aborted);
		Await(C, Incoming.Operation());
	}

	// The native reader is single-consumer, and a failed claim doesn't commit headers.
	Accept = Server->Accept();
	Incoming = Client.SendStream(Q, 16384);
	Next = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	P = Pipe(C);
	R.Body.clear();
	auto First = Next->ReplyStream(R, {P.Reader, {}});
	H::FClient Other(C, VeroScript::Curl::CreateHttpClientService());
	auto AcceptOther = Server->Accept();
	auto OtherRequest = Other.Send(Q);
	auto OtherExchange = Get<std::shared_ptr<H::FExchange>>(C, AcceptOther);
	auto Shared = OtherExchange->ReplyStream(R, {P.Reader, {}});
	Await(C, Shared.Operation());
	CHECK(Shared.Operation().Error().Code == N::EError::State);
	Get<std::uint64_t>(C, OtherExchange->Reply(R));
	Get<H::FResponse>(C, OtherRequest);
	CHECK(First.Operation().Cancel());
	Await(C, First.Operation());
	Until(C,
	    [&]
	{
		return P.Writer->Write("x", 1).State == N::EStreamState::Failed;
	});
	CHECK(First.State() == H::EResponseState::Aborted);
	Await(C, Incoming.Operation());

	// HEAD advertises the representation size and does not consume its producer.
	Q.Method = "HEAD";
	Accept = Server->Accept();
	Buffered = Client.Send(Q);
	Next = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	P = Pipe(C);
	auto HeadReply = Next->ReplyStream(R, {P.Reader, 5});
	Get<std::uint64_t>(C, HeadReply.Operation());
	const auto HeadResponse = Get<H::FResponse>(C, Buffered);
	CHECK(HeadResponse.Body.empty() && HeadResponse.Headers.GetAll("Content-Length") == std::vector<std::string>{"5"});
	CHECK(HeadReply.BodyBytes() == 0 && P.Writer->Write("x", 1).State == N::EStreamState::Failed);

	// HTTP/1.0 unknown-length responses end by closing the connection without chunk framing.
	{
		auto Socket = Get<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port()));
		Accept = Server->Accept();
		const std::string Raw = "GET /legacy HTTP/1.0\r\nHost: localhost\r\n\r\n";
		Get<std::uint64_t>(C, Socket->Write(N::FBytes(Raw.begin(), Raw.end())));
		Next = Get<std::shared_ptr<H::FExchange>>(C, Accept);
		P = Pipe(C);
		auto Legacy = Next->ReplyStream(R, {P.Reader, {}});
		CHECK(P.Writer->WriteAll("old", 3).Bytes == 3);
		P.Writer->Finish();
		std::string Wire;
		for (;;)
		{
			const auto Piece = Get<N::FReadResult>(C, Socket->Read(4096));
			Wire.append(Piece.Bytes.begin(), Piece.Bytes.end());
			if (Piece.bEnd)
			{
				break;
			}
		}
		Get<std::uint64_t>(C, Legacy.Operation());
		CHECK(Wire.find("HTTP/1.0 200") == 0 && Wire.find("Transfer-Encoding") == Wire.npos &&
		    Wire.find("Content-Length") == Wire.npos && Wire.substr(Wire.size() - 3) == "old");
	}
	// Idle timeout remains independent of the total operation deadline.
	{
		H::FServerOptions IdleOptions;
		IdleOptions.ResponseTimeout = std::chrono::milliseconds(500);
		auto IdleServer = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, IdleOptions);
		CHECK(IdleServer && !E);
		H::FRequest IdleRequest;
		IdleRequest.Url = "http://127.0.0.1:" + std::to_string(IdleServer->Port());
		auto IdleAccept = IdleServer->Accept();
		auto IdleIncoming = Client.SendStream(IdleRequest, 16384);
		auto IdleExchange = Get<std::shared_ptr<H::FExchange>>(C, IdleAccept);
		P = Pipe(C);
		auto IdleReply = IdleExchange->ReplyStream(R, {P.Reader, {}}, N::FOperationOptions{std::nullopt});
		Await(C, IdleReply.Operation());
		CHECK(IdleReply.Operation().Error().Code == N::EError::TimedOut);
		Await(C, IdleIncoming.Operation());
		IdleServer->Close();
	}
	// Shutdown wakes a response stalled waiting for source data.
	Q.Method = "GET";
	Accept = Server->Accept();
	Incoming = Client.SendStream(Q, 16384);
	Next = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	P = Pipe(C);
	auto Closing = Next->ReplyStream(R, {P.Reader, {}});
	Until(C,
	    [&]
	{
		return Incoming.TryHead(Head, E);
	});
	C.Shutdown();
	CHECK(Closing.Operation().IsComplete() && Closing.State() == H::EResponseState::Aborted);
	CHECK(P.Writer->Write("x", 1).State == N::EStreamState::Failed);
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
