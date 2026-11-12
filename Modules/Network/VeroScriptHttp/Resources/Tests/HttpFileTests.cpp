// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"
#include "VeroScriptHttpFiles.h"
#include "../../../VeroScriptNetwork/Resources/Tests/NetworkTestPaths.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace N = VeroScript::Network;
namespace H = VeroScript::Http;
namespace Fs = std::filesystem;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " + #X); \
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
static void Retired(N::FContext& C, const H::FDownloadOperation& O)
{
	Until(C,
	    [&]
	{
		return O.Operation().IsComplete() && O.Progress().File.bPhysicalComplete;
	});
}
static void Save(const Fs::path& P, const std::string& S)
{
	std::ofstream F(P, std::ios::binary | std::ios::trunc);
	CHECK(F && F.write(S.data(), S.size()));
}
static std::string Load(const Fs::path& P)
{
	std::ifstream F(P, std::ios::binary);
	CHECK(F);
	return std::string(std::istreambuf_iterator<char>(F), {});
}
static void Success(N::FContext& C, const H::FDownloadOperation& O)
{
	Retired(C, O);
	if (O.Operation().Error())
	{
		throw std::runtime_error(
		    O.Operation().Error().Message + " (" + std::to_string(static_cast<int>(O.Operation().Error().Code)) + ")");
	}
	CHECK(O.Operation().Result<H::FDownloadResult>()->File.bCommitted);
}
static void Test(const Fs::path& Root, std::uint16_t Port, N::EDriveMode Mode)
{
	N::FContextOptions ContextOptions;
	ContextOptions.Mode = Mode;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), ContextOptions);
	auto Files = std::make_shared<N::FFileExecutor>(C);
	H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
	const auto Base = "http://127.0.0.1:" + std::to_string(Port);
	std::string Bytes(40001, ' ');
	for (std::size_t I = 0; I < Bytes.size(); ++I)
	{
		Bytes[I] = static_cast<char>(I % 251);
	}
	H::FRequest Q;
	Q.Url = Base + "/download/full";
	H::FDownloadOptions O;
	O.File.Path = N::Test::PathToUtf8(Root / "full.bin");
	O.File.ExpectedLength = Bytes.size();
	auto Download = H::DownloadFile(C, Client, *Files, Q, O);
	Success(C, Download);
	CHECK(Load(Fs::u8path(O.File.Path)) == Bytes);
	CHECK(Download.Progress().ReceivedBytes == Bytes.size() && Download.Progress().RepresentationBytes == Bytes.size());
	CHECK(Download.ResponseHead()->Status == 200 &&
	    Download.Operation().Result<H::FDownloadResult>()->Response.bComplete);
	CHECK(Download.Operation().Result<H::FDownloadResult>()->Response.Body.empty());
	N::FFileReadOptions Source;
	Source.Path = O.File.Path;
	Q.Url = Base + "/method-echo";
	Q.Method = "POST";
	Q.UploadFactory = H::MakeFileUploadFactory(Files, Source);
	auto Upload = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Upload.IsComplete();
	});
	CHECK(!Upload.Error());
	auto Response = Upload.Result<H::FResponse>();
	CHECK(std::string(Response->Body.begin(), Response->Body.end()) == "POST:" + Bytes);
	const auto Scope = Mode == N::EDriveMode::HostDriven ? "host" : "worker";
	Q.Url = Base + "/retry/file-" + Scope + "/1";
	Q.Retry.MaxAttempts = 2;
	Q.Retry.bAllowReplay = Q.Retry.bAllowUnsafeMethods = true;
	Q.Retry.Delay = std::chrono::milliseconds(1);
	std::atomic<unsigned> Sources{0};
	auto Factory = Q.UploadFactory;
	Q.UploadFactory = [Factory, &Sources](std::uint32_t Attempt, N::FError& E)
	{
		++Sources;
		return Factory(Attempt, E);
	};
	Upload = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Upload.IsComplete();
	});
	CHECK(!Upload.Error() && Sources == 2 && Upload.Result<H::FResponse>()->Attempts.size() == 2);
	Q.Retry = {};
	Q.Url = Base + "/method-echo";
	Source.Path = N::Test::PathToUtf8(Root / "absent.bin");
	Source.Length = 0;
	Q.UploadFactory = H::MakeFileUploadFactory(Files, Source);
	Upload = Client.Send(Q);
	Until(C,
	    [&]
	{
		return Upload.IsComplete();
	});
	CHECK(Upload.Error().Code == N::EError::Read && Upload.Error().SystemCode != 0);
	Q = {};
	Q.Url = Base + "/download/range";
	const auto Partial = Root / "partial.bin";
	Save(Partial, Bytes.substr(0, 17001));
	O.File.Path = N::Test::PathToUtf8(Root / "resumed.bin");
	O.Resume = H::FDownloadResume{N::Test::PathToUtf8(Partial), 17001, H::EResumeValidator::StrongETag, "\"file-v1\""};
	Download = H::DownloadFile(C, Client, *Files, Q, O);
	Success(C, Download);
	CHECK(Load(Fs::u8path(O.File.Path)) == Bytes && Load(Partial) == Bytes.substr(0, 17001));
	CHECK(Download.Progress().bResumed && !Download.Progress().bAlreadyComplete &&
	    Download.Progress().ReceivedBytes == 23000);
	O.File.Path = N::Test::PathToUtf8(Root / "date-resumed.bin");
	O.Resume->Kind = H::EResumeValidator::LastModified;
	O.Resume->Validator = "Sun, 06 Nov 1994 08:49:37 GMT";
	Download = H::DownloadFile(C, Client, *Files, Q, O);
	Success(C, Download);
	CHECK(Load(Fs::u8path(O.File.Path)) == Bytes);
	O.Resume->Kind = H::EResumeValidator::StrongETag;
	O.Resume->Validator = "\"file-v1\"";
	O.File.Path = N::Test::PathToUtf8(Root / "protected.bin");
	O.File.bReplaceExisting = true;
	Save(Fs::u8path(O.File.Path), "original");
	for (const char* Endpoint :
	    {"/download/full", "/download/bad-range", "/download/bad-validator", "/download/encoded"})
	{
		Q.Url = Base + Endpoint;
		Download = H::DownloadFile(C, Client, *Files, Q, O);
		Retired(C, Download);
		CHECK(Download.Operation().Error().Code == N::EError::Protocol);
		CHECK(Download.ResponseHead() && Load(Fs::u8path(O.File.Path)) == "original");
		CHECK(Load(Partial) == Bytes.substr(0, 17001) && Download.Progress().File.PartialPath.empty());
	}
	// A 416 entity is not file data. A validated complete prefix can be atomically committed.
	Save(Partial, Bytes);
	O.Resume->Offset = Bytes.size();
	O.File.Path = N::Test::PathToUtf8(Root / "complete.bin");
	Q.Url = Base + "/download/416-body";
	Download = H::DownloadFile(C, Client, *Files, Q, O);
	Success(C, Download);
	CHECK(Load(Fs::u8path(O.File.Path)) == Bytes && Download.Progress().bAlreadyComplete);
	CHECK(Download.ResponseHead()->Status == 416 &&
	    Download.Progress().ReceivedBytes == std::string_view("already complete").size());
	O.Resume->Offset = Bytes.size() + 1;
	Save(Partial, Bytes + "x");
	O.File.Path = N::Test::PathToUtf8(Root / "protected.bin");
	O.File.ExpectedLength.reset();
	Download = H::DownloadFile(C, Client, *Files, Q, O);
	Retired(C, Download);
	CHECK(Download.Operation().Error().Code == N::EError::Protocol && Load(Fs::u8path(O.File.Path)) == "original");
	O.Resume.reset();
	for (const char* Endpoint : {"/download/missing", "/download/truncated", "/download/encoded"})
	{
		Q.Url = Base + Endpoint;
		Download = H::DownloadFile(C, Client, *Files, Q, O);
		Retired(C, Download);
		CHECK(Download.Operation().Error().Code ==
		    (std::string_view(Endpoint) == "/download/truncated" ? N::EError::Read : N::EError::Protocol));
		CHECK(Load(Fs::u8path(O.File.Path)) == "original" && Download.Progress().File.PartialPath.empty());
	}
	O.File.MaxBytes = 100;
	Q.Url = Base + "/download/full";
	Download = H::DownloadFile(C, Client, *Files, Q, O);
	Retired(C, Download);
	CHECK(Download.Operation().Error().Code == N::EError::Limit && Load(Fs::u8path(O.File.Path)) == "original");
	O.File.MaxBytes = 1024 * 1024;
	Q.Url = Base + "/stall";
	Q.Operation.Timeout = std::chrono::milliseconds(60);
	Download = H::DownloadFile(C, Client, *Files, Q, O);
	Retired(C, Download);
	CHECK(Download.Operation().Error().Code == N::EError::TimedOut && Load(Fs::u8path(O.File.Path)) == "original");
	Q.Operation.Timeout = std::chrono::seconds(8);
	Download = H::DownloadFile(C, Client, *Files, Q, O);
	Until(C,
	    [&]
	{
		return !Download.Progress().File.PartialPath.empty();
	});
	CHECK(Download.Operation().Cancel());
	Retired(C, Download);
	CHECK(Download.Operation().Error().Code == N::EError::Cancelled && Load(Fs::u8path(O.File.Path)) == "original");
	Download = H::DownloadFile(C, Client, *Files, Q, O);
	C.Shutdown();
	CHECK(Download.Operation().IsComplete() && Download.Progress().File.bPhysicalComplete);
	CHECK(Load(Fs::u8path(O.File.Path)) == "original");
}
int main(int Argc, char** Argv)
{
	Fs::path Root;
	try
	{
		CHECK(Argc == 6);
		const auto Base = Fs::absolute(Fs::u8path(Argv[5])).lexically_normal();
		Fs::create_directories(Base);
		Root = Base / Fs::u8path("download-文件 " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		CHECK(Root.parent_path() == Base && Fs::create_directory(Root));
		for (const auto Mode : {N::EDriveMode::NativeBackground, N::EDriveMode::HostDriven})
		{
			const auto Directory = Root / (Mode == N::EDriveMode::HostDriven ? "host" : "worker");
			CHECK(Fs::create_directory(Directory));
			Test(Directory, static_cast<std::uint16_t>(std::stoi(Argv[1])), Mode);
		}
		Fs::remove_all(Root);
		std::cout << "File upload/replay, validated downloads/resume, atomic commit and cancellation passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << " (files retained in " << N::Test::PathToUtf8(Root) << ")\n";
		return 1;
	}
}
