// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"
#include "VeroScriptHttpFiles.h"
#include "VeroScriptWebSocketReconnect.h"
#include "../NetworkTestPaths.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

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

// Keep real transports and the real executor; decline just one selected timer.
struct FTimerGate
{
	std::size_t Calls = 0, Reject = 0;
};
class FRuntime final : public N::Backend::IRuntime
{
public:
	FRuntime(std::shared_ptr<IRuntime> Inner, std::shared_ptr<FTimerGate> Gate)
	    : Inner(std::move(Inner)), Gate(std::move(Gate))
	{
	}
	const char* GetBackendId() const noexcept override
	{
		return Inner->GetBackendId();
	}
	void Run() override
	{
		Inner->Run();
	}
	void Stop() noexcept override
	{
		Inner->Stop();
	}
	void Restart() override
	{
		Inner->Restart();
	}
	std::size_t PollOne() override
	{
		return Inner->PollOne();
	}
	void ReleaseWork() noexcept override
	{
		Inner->ReleaseWork();
	}
	void Post(std::function<void()> Function) override
	{
		Inner->Post(std::move(Function));
	}
	std::shared_ptr<N::Backend::ITimer> CreateTimer() override
	{
		return ++Gate->Calls == Gate->Reject ? nullptr : Inner->CreateTimer();
	}
	std::shared_ptr<N::Backend::ISocketWatch> WatchSocket(std::uintptr_t Socket, std::error_code& Error) override
	{
		return Inner->WatchSocket(Socket, Error);
	}
	void* QueryInterface(std::string_view Contract) noexcept override
	{
		return Inner->QueryInterface(Contract);
	}

private:
	std::shared_ptr<IRuntime> Inner;
	std::shared_ptr<FTimerGate> Gate;
};
class FService final : public N::Backend::IService
{
public:
	explicit FService(std::shared_ptr<FTimerGate> Gate)
	    : Inner(VeroScript::Boost::CreateNetworkService()), Gate(std::move(Gate))
	{
	}
	const char* GetBackendId() const noexcept override
	{
		return Inner->GetBackendId();
	}
	const char* GetFoundationIdentity() const noexcept override
	{
		return Inner->GetFoundationIdentity();
	}
	bool TlsAvailable() const noexcept override
	{
		return Inner->TlsAvailable();
	}
	std::shared_ptr<N::Backend::IRuntime> CreateRuntime() override
	{
		return std::make_shared<FRuntime>(Inner->CreateRuntime(), Gate);
	}
	N::FOperation Resolve(N::FContext& C, std::string Host, std::uint16_t Port, N::FOperationOptions O) override
	{
		return Inner->Resolve(C, std::move(Host), Port, O);
	}
	N::FOperation ConnectTcp(N::FContext& C, std::string Host, std::uint16_t Port, N::FOperationOptions O) override
	{
		return Inner->ConnectTcp(C, std::move(Host), Port, O);
	}
	std::shared_ptr<N::FTcpListener> ListenTcp(
	    N::FContext& C, std::string Address, std::uint16_t Port, N::FError& Error, int Backlog) override
	{
		return Inner->ListenTcp(C, std::move(Address), Port, Error, Backlog);
	}
	std::shared_ptr<N::FUdpSocket> BindUdp(
	    N::FContext& C, std::string Address, std::uint16_t Port, N::FError& Error) override
	{
		return Inner->BindUdp(C, std::move(Address), Port, Error);
	}

private:
	std::shared_ptr<IService> Inner;
	std::shared_ptr<FTimerGate> Gate;
};
static void Await(N::FContext& C, const N::FOperation& Op)
{
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
	while (!Op.IsComplete())
	{
		CHECK(std::chrono::steady_clock::now() < Deadline);
		N::FError Error;
		C.PollIo(Error);
		CHECK(!Error);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
template <class T>
static T Result(N::FContext& C, const N::FOperation& Op)
{
	Await(C, Op);
	if (Op.Error())
	{
		throw std::runtime_error(Op.Error().Message);
	}
	const auto Value = Op.Result<T>();
	CHECK(Value);
	return *Value;
}
static N::FOperationOptions Unlimited()
{
	N::FOperationOptions O;
	O.Timeout = std::nullopt;
	return O;
}
static void Reconnect(N::FContext& C, FTimerGate& Gate)
{
	N::FError Error;
	auto Listener = N::FTcpListener::Listen(C, "127.0.0.1", 0, Error);
	CHECK(Listener && !Error);
	const auto Port = Listener->Port();
	Listener->Close();
	C.PollIo(Error);
	CHECK(!Error);
	// The parent deadline timer succeeds; its retry timer is unavailable.
	Gate.Reject = Gate.Calls + 2;
	W::FReconnectPolicy Policy;
	Policy.MaxAttempts = 2;
	Policy.Delay = std::chrono::milliseconds(1);
	W::FOptions Options;
	Options.Service = VeroScript::Boost::CreateWebSocketService();
	auto Task = W::ConnectWithRetry(C, "ws://127.0.0.1:" + std::to_string(Port), Policy, Options);
	Await(C, Task.Operation());
	CHECK(Task.Operation().Error().Code == N::EError::Unsupported);
	CHECK(Gate.Calls == Gate.Reject);
	auto Snapshot = Task.Snapshot(Error);
	CHECK(Snapshot && !Error && Snapshot->bFinal && Snapshot->Attempts.empty());
}
static void Download(N::FContext& C, FTimerGate& Gate)
{
	N::FFileExecutor Files(C);
	H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
	struct FTemporaryDirectory
	{
		std::filesystem::path Path;
		~FTemporaryDirectory()
		{
			std::error_code Ignored;
			std::filesystem::remove_all(Path, Ignored);
		}
	} Temp{std::filesystem::temp_directory_path() /
	    ("veroscript-download-timer-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
	CHECK(std::filesystem::create_directory(Temp.Path));
	H::FRequest Request;
	Request.Url = "http://127.0.0.1:1/";
	Request.Operation = Unlimited();
	H::FDownloadOptions Options;
	Options.File.Path = N::Test::PathToUtf8(Temp.Path / "output.bin");
	Gate.Reject = Gate.Calls + 1;
	auto Task = H::DownloadFile(C, Client, Files, Request, Options);
	Await(C, Task.Operation());
	CHECK(Task.Operation().Error().Code == N::EError::Unsupported);
	CHECK(Gate.Calls == Gate.Reject);
	CHECK(std::filesystem::is_empty(Temp.Path));
	C.Shutdown();
}
static void Retry(N::FContext& C, FTimerGate& Gate)
{
	N::FError Error;
	H::FServerOptions Options;
	Options.Service = VeroScript::Boost::CreateHttpServerService();
	auto Server = H::FServer::Listen(C, "127.0.0.1", 0, Error, Options);
	CHECK(Server && !Error);
	H::FClient Client(C, VeroScript::Curl::CreateHttpClientService());
	H::FRequest Request;
	Request.Url = "http://127.0.0.1:" + std::to_string(Server->Port()) + "/";
	Request.Operation = Unlimited();
	Request.Retry.MaxAttempts = 2;
	Request.Retry.bAllowReplay = true;
	Request.Retry.Delay = std::chrono::milliseconds(1);
	Request.History = std::make_shared<H::FAttemptHistory>();
	Gate.Reject = Gate.Calls + 1;
	auto Accepted = Server->Accept(Unlimited());
	auto Sent = Client.Send(Request);
	auto Exchange = Result<std::shared_ptr<H::FExchange>>(C, Accepted);
	H::FResponse Response;
	Response.Status = 503;
	Result<std::uint64_t>(C, Exchange->Reply(Response, Unlimited()));
	Await(C, Sent);
	CHECK(Sent.Error().Code == N::EError::Unsupported);
	CHECK(Gate.Calls == Gate.Reject);
	CHECK(Request.History->Snapshot()->Attempts.size() == 1);
	// A missing optional timer must not poison the client or the context.
	Request.Retry = {};
	Request.History.reset();
	Accepted = Server->Accept(Unlimited());
	Sent = Client.Send(Request);
	Exchange = Result<std::shared_ptr<H::FExchange>>(C, Accepted);
	Response.Status = 200;
	Result<std::uint64_t>(C, Exchange->Reply(Response, Unlimited()));
	CHECK(Result<H::FResponse>(C, Sent).Status == 200);
	Server->Close();
}
int main(int Argc, char** Argv)
{
	try
	{
		CHECK(Argc == 2);
		auto Gate = std::make_shared<FTimerGate>();
		N::FContextOptions Options;
		Options.Mode = N::EDriveMode::HostDriven;
		N::FContext C(std::make_shared<FService>(Gate), Options);
		const std::string Mode = Argv[1];
		if (Mode == "reconnect")
		{
			Reconnect(C, *Gate);
		}
		else if (Mode == "download")
		{
			Download(C, *Gate);
		}
		else if (Mode == "retry")
		{
			Retry(C, *Gate);
		}
		else
		{
			throw std::runtime_error("Unknown timer test mode");
		}
		C.Shutdown();
		std::cout << "PASS: " << Mode << '\n';
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
