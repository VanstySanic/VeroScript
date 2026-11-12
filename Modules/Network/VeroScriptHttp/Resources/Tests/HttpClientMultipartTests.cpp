// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"
#include "HttpTestBackends.h"
#include "VeroScriptHttp.h"
#include "NetworkProvider.h"
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
	const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(8);
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
static H::FResponse Get(N::FContext& C, const N::FOperation& Op)
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
	auto R = Op.Result<H::FResponse>();
	CHECK(R);
	return *R;
}
static std::string Body(const H::FResponse& R)
{
	return std::string(R.Body.begin(), R.Body.end());
}
static void Test(std::uint16_t Port, N::EDriveMode Mode)
{
	N::FContextOptions O;
	O.Mode = Mode;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), O);
	N::FError E;
	H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
	const auto Base = "http://127.0.0.1:" + std::to_string(Port);
	const auto Scope = Mode == N::EDriveMode::HostDriven ? "host" : "worker";
	H::FRequest Q;
	Q.Method = "POST";
	Q.Url = Base + "/multipart";
	Q.Multipart.emplace();
	unsigned FieldSources = 0, FileSources = 0;
	auto Source = [](std::string Bytes, N::FError& E, bool Known = true)
	{
		N::FBytePipe P;
		CHECK(N::TryCreateBytePipe(64, P, E));
		CHECK(P.Writer->WriteAll(Bytes.data(), static_cast<std::int64_t>(Bytes.size())).Bytes == Bytes.size());
		P.Writer->Finish();
		return H::FBodySource{P.Reader, Known ? std::optional<std::uint64_t>(Bytes.size()) : std::nullopt};
	};
	H::FMultipartPart Field;
	Field.Name = "field";
	Field.UploadFactory = [&](std::uint32_t, N::FError& Error)
	{
		++FieldSources;
		return Source(std::string("alpha\0omega", 11), Error);
	};
	Q.Multipart->push_back(Field);
	H::FMultipartPart Buffered;
	Buffered.Name = "field";
	Buffered.Body = {'s', 'e', 'c', 'o', 'n', 'd'};
	Q.Multipart->push_back(Buffered);
	H::FMultipartPart File;
	File.Name = "file";
	File.Filename = "hello.bin";
	File.ContentType = "application/octet-stream";
	File.UploadFactory = [&](std::uint32_t, N::FError& Error)
	{
		++FileSources;
		return Source(std::string("\0\xff", 2), Error, false);
	};
	Q.Multipart->push_back(File);
	// Mix known/unknown sources and buffers, including repeated names and binary payloads.
	CHECK(Body(Get(C, Client.Send(Q))) == "multipart-ok" && FieldSources == 1 && FileSources == 1);
	Q.Url = Base + "/retry/multipart-" + Scope + "/1";
	Q.Retry.MaxAttempts = 2;
	Q.Retry.bAllowReplay = true;
	Q.Retry.bAllowUnsafeMethods = true;
	Q.Retry.Delay = std::chrono::milliseconds(1);
	CHECK(Get(C, Client.Send(Q)).Attempts.size() == 2 && FieldSources == 3 && FileSources == 3);
	Q.Retry = {};
	Q.bFollowRedirects = true;
	Q.Url = Base + "/redirect-307";
	const auto Echo = Body(Get(C, Client.Send(Q)));
	CHECK(Echo.rfind("POST:", 0) == 0 && Echo.find(std::string("alpha\0omega", 11)) != Echo.npos &&
	    Echo.find(std::string("\0\xff", 2)) != Echo.npos && FieldSources == 5 && FileSources == 5);
	Q.MaxTotalUploadBytes = 38;
	CHECK(Get(C, Client.Send(Q)).Status == 200);
	Q.MaxTotalUploadBytes = 37; // Two payloads require 2 * (11 + 6 + 2) bytes, excluding MIME framing.
	auto Op = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::Limit);
	Q.bFollowRedirects = false;
	Q.MaxTotalUploadBytes = 256 * 1024 * 1024;
	Q.Url = Base + "/multipart";
	Q.MaxUploadBytes = 18;
	Op = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::Limit);

	Q = {};
	Q.Method = "POST";
	Q.Url = Base + "/method-echo";
	Q.Multipart.emplace();
	N::FBytePipe P;
	CHECK(N::TryCreateBytePipe(16, P, E));
	CHECK(P.Writer->WriteAll("x", 1).Bytes == 1);
	P.Writer->Finish();
	File = {};
	File.Name = "file";
	File.Upload = P.Reader;
	File.UploadLength = 3;
	Q.Multipart->push_back(File);
	Op = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::Protocol);
	Q.Multipart->front().UploadLength = 1;
	Op = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::State);
	CHECK(N::TryCreateBytePipe(16, P, E));
	Q.Multipart->front().Upload = P.Reader;
	Q.Multipart->front().UploadLength.reset();
	Q.bFollowRedirects = true;
	Op = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::InvalidArgument);
	CHECK(P.Writer->WriteAll("a", 1).Bytes == 1); // Rejected before claiming the source.
	Q.bFollowRedirects = false;
	Q.Multipart->push_back(Q.Multipart->front());
	Op = Client.Send(Q);
	bool RolledBackBeforeFailure = false;
	if (Mode == N::EDriveMode::HostDriven)
	{
		N::Detail::ObserveTerminal(N::Detail::FAccess::Operation(Op),
		    [Writer = P.Writer, &RolledBackBeforeFailure](auto, const auto&)
		{
			RolledBackBeforeFailure = Writer->WriteAll("a", 1).State == N::EStreamState::Failed;
		});
	}
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::State);
	CHECK(P.Writer->WriteAll("a", 1).State == N::EStreamState::Failed);
	CHECK(Mode != N::EDriveMode::HostDriven || RolledBackBeforeFailure);
	Q.Multipart->resize(1);
	Q.Multipart->front().Upload.reset();
	Q.Multipart->front().UploadFactory = [](std::uint32_t, N::FError&) -> H::FBodySource
	{
		throw std::runtime_error("factory");
	};
	Op = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::Callback);
	Q.Multipart->front().UploadFactory = [](std::uint32_t, N::FError&)
	{
		return H::FBodySource{};
	};
	Op = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::Callback);

	Q.Multipart->front().UploadFactory = [](std::uint32_t, N::FError& Failure)
	{
		Failure = {N::EError::Read, "source-open", 731};
		return H::FBodySource{};
	};
	Op = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::Read && Op.Error().SystemCode == 731);
	auto RawFactory = Q;
	RawFactory.UploadFactory = Q.Multipart->front().UploadFactory;
	RawFactory.Multipart.reset();
	Op = Client.Send(RawFactory);
	Until(C,
	    [&]
	{
		return Op.IsComplete();
	});
	CHECK(Op.Error().Code == N::EError::Read && Op.Error().SystemCode == 731);
	for (bool Multipart : {false, true})
	{
		CHECK(N::TryCreateBytePipe(16, P, E));
		P.Writer->Fail({N::EError::Read, "source-read", 732});
		H::FRequest Failed;
		Failed.Method = "POST";
		Failed.Url = Base + "/method-echo";
		if (Multipart)
		{
			Failed.Multipart.emplace();
			H::FMultipartPart Part;
			Part.Name = "source";
			Part.Upload = P.Reader;
			Failed.Multipart->push_back(Part);
		}
		else
		{
			Failed.Upload = P.Reader;
		}
		Op = Client.Send(Failed);
		Until(C,
		    [&]
		{
			return Op.IsComplete();
		});
		CHECK(Op.Error().Code == N::EError::Read && Op.Error().SystemCode == 732);
	}

	Q.Multipart->front() = {};
	Q.Multipart->front().Body = {'z'};
	const auto EmptyName = Body(Get(C, Client.Send(Q)));
	CHECK(EmptyName.find("name=\"\"") != EmptyName.npos && EmptyName.find("\r\n\r\nz\r\n") != EmptyName.npos);

	// Cancel an upload paused on an empty producer after the server has received its headers.
	H::FServerOptions SO;
	SO.bStreamRequests = true;
	auto Server = VeroScript::Http::Test::Listen(C, "127.0.0.1", 0, E, SO);
	CHECK(Server && !E);
	CHECK(N::TryCreateBytePipe(16, P, E));
	Q.Multipart->front().Body.clear();
	Q.Multipart->front().UploadFactory = {};
	Q.Multipart->front().Upload = P.Reader;
	Q.Url = "http://127.0.0.1:" + std::to_string(Server->Port()) + "/paused";
	N::FBytePipe Second;
	CHECK(N::TryCreateBytePipe(16, Second, E));
	H::FMultipartPart Waiting;
	Waiting.Name = "second";
	Waiting.Upload = Second.Reader;
	Q.Multipart->push_back(Waiting);
	auto Accepted = Server->Accept();
	auto Download = Client.SendStream(Q, 16384);
	Op = Download.Operation();
	Until(C,
	    [&]
	{
		return Accepted.IsComplete();
	});
	CHECK(!Accepted.Error());
	CHECK(!Op.IsComplete() && Op.Cancel());
	CHECK(P.Writer->WriteAll("a", 1).State == N::EStreamState::Failed);
	CHECK(Second.Writer->WriteAll("b", 1).State == N::EStreamState::Failed);
	char TerminalByte;
	const auto TerminalRead = Download.Body()->Read(&TerminalByte, 1);
	CHECK(TerminalRead.State == N::EStreamState::Failed && TerminalRead.Error.Code == N::EError::Cancelled);
	Server->Close();
	Client.Close();
	C.Shutdown();
}
int main(int Argc, char** Argv)
{
	try
	{
		CHECK(Argc >= 2);
		auto Port = static_cast<std::uint16_t>(std::stoi(Argv[1]));
		Test(Port, N::EDriveMode::NativeBackground);
		Test(Port, N::EDriveMode::HostDriven);
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}
