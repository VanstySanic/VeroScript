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
static std::string ReadHead(N::FContext& C, const std::shared_ptr<N::FTcpSocket>& Socket)
{
	std::string Text;
	while (Text.find("\r\n\r\n") == Text.npos)
	{
		const auto Read = Get<N::FReadResult>(C, Socket->Read(1024));
		CHECK(!Read.bEnd && !Read.Bytes.empty());
		Text.append(Read.Bytes.begin(), Read.Bytes.end());
	}
	return Text;
}
static void ExpectRequests(N::EDriveMode Mode, bool Stream)
{
	N::FContextOptions O;
	O.Mode = Mode;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), O);
	N::FError E;
	H::FServerOptions SO;
	SO.bStreamRequests = Stream;
	SO.MaxHeaderBytes = 256;
	SO.ResponseTimeout = std::chrono::milliseconds(500);
	auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, SO);
	CHECK(Server && !E);
	// No-body, body read ahead, HTTP/1.0, rejected expectation, header limit and idle deadline.
	for (int Case = 0; Case < 7; ++Case)
	{
		auto Socket = Get<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port()));
		auto Accept = Server->Accept();
		const bool NoBody = Case == 1, InlineBody = Case == 2 || Case == 3;
		std::string Head = std::string("POST /expect HTTP/") + (Case == 3 ? "1.0" : "1.1") +
		    "\r\nHost: localhost\r\nExpect: " + (Case == 4 ? "unsupported" : "100-CoNtInUe") +
		    "\r\nContent-Length: " + (NoBody ? "0" : "3") + "\r\n";
		if (Case == 5)
		{
			Head += "X-Large: " + std::string(300, 'x') + "\r\n";
		}
		Head += "\r\n";
		if (InlineBody)
		{
			Head += "abc";
		}
		Get<std::uint64_t>(C, Socket->Write(N::FBytes(Head.begin(), Head.end())));
		if (Case == 4 || Case == 5)
		{
			if (Case == 4)
			{
				CHECK(ReadHead(C, Socket).find("HTTP/1.1 417 ") == 0);
			}
			CHECK(Get<N::FReadResult>(C, Socket->Read(1024)).bEnd);
			CHECK(!Accept.IsComplete());
			CHECK(Accept.Cancel());
			continue;
		}
		if (!NoBody && Case != 3)
		{
			CHECK(ReadHead(C, Socket) == "HTTP/1.1 100 Continue\r\n\r\n");
			if (!Stream && !InlineBody)
			{
				CHECK(!Accept.IsComplete());
			}
		}
		if (Case == 6)
		{
			if (Stream)
			{
				auto Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
				Until(C, [&]
				{
					return Exchange->Request().BodyOperation.IsComplete();
				});
				CHECK(Exchange->Request().BodyOperation.Error().Code == N::EError::TimedOut);
			}
			CHECK(Get<N::FReadResult>(C, Socket->Read(1024)).bEnd);
			if (!Stream)
			{
				CHECK(!Accept.IsComplete());
				CHECK(Accept.Cancel());
			}
			continue;
		}
		if (!NoBody && !InlineBody)
		{
			Get<std::uint64_t>(C, Socket->Write({'a', 'b', 'c'}));
		}
		auto Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
		if (Stream)
		{
			CHECK(Get<H::FRequestBodyResult>(C, Exchange->Request().BodyOperation).BodyBytes == (NoBody ? 0 : 3));
			char Body[3]{};
			CHECK(Exchange->Request().BodyStream->Read(Body, 3).Bytes == (NoBody ? 0 : 3));
			if (!NoBody)
			{
				CHECK(std::string(Body, 3) == "abc");
			}
		}
		else
		{
			CHECK(Exchange->Request().Body == (NoBody ? N::FBytes{} : N::FBytes{'a', 'b', 'c'}));
		}
		H::FResponse Response;
		Response.Status = 200;
		Get<std::uint64_t>(C, Exchange->Reply(Response));
		CHECK(ReadHead(C, Socket).find(Case == 3 ? "HTTP/1.0 200 " : "HTTP/1.1 200 ") == 0);
		Socket->Close();
	}
	Server->Close();
	C.Shutdown();
}
static void Test(N::EDriveMode Mode)
{
	N::FContextOptions O;
	O.Mode = Mode;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), O);
	N::FError E;
	H::FServerOptions SO;
	SO.bStreamRequests = true;
	SO.RequestStreamBufferBytes = 64;
	SO.MaxStreamBodyBytes = 8192;
	auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, SO);
	CHECK(Server && !E);
	H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
	H::FRequest Q;
	Q.Url = "http://127.0.0.1:" + std::to_string(Server->Port()) + "/upload";
	Q.Method = "POST";
	N::FBytePipe Upload;
	CHECK(N::TryCreateBytePipe(C, 64, Upload, E));
	Q.Upload = Upload.Reader;
	CHECK(Q.Headers.TryAdd("Expect", "100-continue", E));
	auto Accept = Server->Accept();
	auto Outgoing = Client.Send(Q);
	auto Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	CHECK(Exchange->Request().Body.empty() && Exchange->Request().BodyStream &&
	    !Exchange->Request().BodyOperation.IsComplete());
	auto Reader = Exchange->Request().BodyStream;
	const auto BodyOp = Exchange->Request().BodyOperation;
	CHECK(Upload.Writer->WriteAll("first", 5).Bytes == 5);
	Until(C,
	    [&]
	{
		return Reader->BufferedBytes() == 5;
	});
	char Data[128]{};
	CHECK(Reader->Read(Data, 5).Bytes == 5 && std::string(Data, 5) == "first" && !BodyOp.IsComplete());
	// Transfer much more than the server ring capacity, verifying every byte before EOF.
	std::uint64_t Sent = 0, Received = 0;
	Until(C,
	    [&]
	{
		if (Sent < 4096)
		{
			char Piece[31];
			for (std::size_t I = 0; I < 31; ++I)
			{
				Piece[I] = static_cast<char>((Sent + I) % 127);
			}
			const auto R =
			    Upload.Writer->Write(Piece, static_cast<std::int64_t>(std::min<std::uint64_t>(31, 4096 - Sent)));
			CHECK(R.State != N::EStreamState::Failed);
			Sent += R.Bytes;
		}
		const auto R = Reader->Read(Data, sizeof(Data));
		CHECK(R.State != N::EStreamState::Failed);
		for (std::uint64_t I = 0; I < R.Bytes; ++I)
		{
			CHECK(Data[I] == static_cast<char>((Received + I) % 127));
		}
		Received += R.Bytes;
		return Received == 4096;
	});
	CHECK(!BodyOp.IsComplete());
	Upload.Writer->Finish();
	const auto BodyResult = Get<H::FRequestBodyResult>(C, BodyOp);
	CHECK(BodyResult.BodyBytes == 4101 && BodyResult.Trailers.Fields().empty());
	H::FResponse Response;
	Response.Status = 200;
	Response.Body = {'d', 'o', 'n', 'e'};
	Get<std::uint64_t>(C, Exchange->Reply(Response));
	CHECK(Get<H::FResponse>(C, Outgoing).Body == Response.Body);

	// Chunked trailers and the subsequent keep-alive request share one physical transport.
	auto Socket = Get<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port()));
	Accept = Server->Accept();
	const std::string Raw = "POST /trailers HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nTrailer: "
	                        "X-End\r\n\r\n3\r\nabc\r\n0\r\nX-End: yes\r\n\r\n";
	Get<std::uint64_t>(C, Socket->Write(N::FBytes(Raw.begin(), Raw.end())));
	auto Previous = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	const auto Trailers = Get<H::FRequestBodyResult>(C, Previous->Request().BodyOperation);
	CHECK(Trailers.BodyBytes == 3 && Trailers.Trailers.GetAll("X-End") == std::vector<std::string>{"yes"});
	CHECK(Previous->Request().BodyStream->Read(Data, sizeof(Data)).Bytes == 3 && std::string(Data, 3) == "abc");
	Get<std::uint64_t>(C, Previous->Reply(Response));
	std::string Wire;
	Until(C,
	    [&]
	{
		auto Piece = Get<N::FReadResult>(C, Socket->Read(4096));
		Wire.append(Piece.Bytes.begin(), Piece.Bytes.end());
		return Wire.find("\r\n\r\ndone") != Wire.npos;
	});
	Accept = Server->Accept();
	const std::string GetRequest = "GET /next HTTP/1.1\r\nHost: localhost\r\n\r\n";
	Get<std::uint64_t>(C, Socket->Write(N::FBytes(GetRequest.begin(), GetRequest.end())));
	Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	CHECK(Get<H::FRequestBodyResult>(C, Exchange->Request().BodyOperation).BodyBytes == 0);
	auto Stale = Previous->Reply(Response);
	Until(C,
	    [&]
	{
		return Stale.IsComplete();
	});
	CHECK(Stale.Error().Code == N::EError::State);
	Get<std::uint64_t>(C, Exchange->Reply(Response));
	Socket->Close();

	// A full request ring doesn't complete the body until the consumer makes room.
	Socket = Get<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port()));
	Accept = Server->Accept();
	const std::string Pending =
	    "POST /backpressure HTTP/1.1\r\nHost: localhost\r\nContent-Length: 512\r\n\r\n" + std::string(512, 'x');
	Get<std::uint64_t>(C, Socket->Write(N::FBytes(Pending.begin(), Pending.end())));
	Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
	Reader = Exchange->Request().BodyStream;
	Until(C,
	    [&]
	{
		return Reader->BufferedBytes() == 64;
	});
	CHECK(!Exchange->Request().BodyOperation.IsComplete());
	Reader->Cancel();
	Until(C,
	    [&]
	{
		return Exchange->Request().BodyOperation.IsComplete();
	});
	CHECK(Exchange->Request().BodyOperation.Error().Code == N::EError::Cancelled);
	Socket->Close();

	// Malformed/truncated input and the total byte bound end the body operation.
	for (int Case = 0; Case < 2; ++Case)
	{
		Socket = Get<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", Server->Port()));
		Accept = Server->Accept();
		const std::string Head = Case == 0
		    ? "POST /short HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nabc"
		    : "POST /limit HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n";
		Get<std::uint64_t>(C, Socket->Write(N::FBytes(Head.begin(), Head.end())));
		Exchange = Get<std::shared_ptr<H::FExchange>>(C, Accept);
		if (Case == 0)
		{
			Socket->Close();
		}
		else
		{
			const std::string Excess = "2001\r\n";
			Get<std::uint64_t>(C, Socket->Write(N::FBytes(Excess.begin(), Excess.end())));
		}
		Until(C,
		    [&]
		{
			return Exchange->Request().BodyOperation.IsComplete();
		});
		CHECK(Exchange->Request().BodyOperation.Error().Code == (Case == 0 ? N::EError::Protocol : N::EError::Limit));
		if (Case == 0)
		{
			CHECK(Exchange->Request().BodyStream->Read(Data, sizeof(Data)).Bytes == 3 && std::string(Data, 3) == "abc");
			CHECK(Exchange->Request().BodyStream->Read(Data, 1).Error.Code == N::EError::Protocol);
		}
		Socket->Close();
	}
	// An early completed response must not disable the unfinished request's idle timer.
	{
		H::FServerOptions Idle = SO;
		Idle.ResponseTimeout = std::chrono::milliseconds(500);
		auto IdleServer = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, Idle);
		CHECK(IdleServer && !E);
		Socket = Get<std::shared_ptr<N::FTcpSocket>>(C, N::FTcpSocket::Connect(C, "127.0.0.1", IdleServer->Port()));
		auto IdleAccept = IdleServer->Accept();
		const std::string Header = "POST /idle HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\n\r\n";
		Get<std::uint64_t>(C, Socket->Write(N::FBytes(Header.begin(), Header.end())));
		auto IdleExchange = Get<std::shared_ptr<H::FExchange>>(C, IdleAccept);
		Get<std::uint64_t>(C, IdleExchange->ReplyStream(Response).Operation());
		Until(C,
		    [&]
		{
			return IdleExchange->Request().BodyOperation.IsComplete();
		});
		CHECK(IdleExchange->Request().BodyOperation.Error().Code == N::EError::TimedOut);
		Until(C,
		    [&]
		{
			return IdleExchange->Request().BodyStream->Read(Data, 1).State == N::EStreamState::Failed;
		});
		CHECK(IdleExchange->Request().BodyStream->Read(Data, 1).Error.Code == N::EError::TimedOut);
		Socket->Close();
		IdleServer->Close();
	}
	Server->Close();
	Client.Close();
	C.Shutdown();
}
int main()
{
	try
	{
		Test(N::EDriveMode::NativeBackground);
		Test(N::EDriveMode::HostDriven);
		for (const auto Mode : {N::EDriveMode::NativeBackground, N::EDriveMode::HostDriven})
		{
			ExpectRequests(Mode, false);
			ExpectRequests(Mode, true);
		}
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
	return 0;
}
