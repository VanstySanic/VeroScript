// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "WebSocketTestBackends.h"
#include "VeroScriptWebSocket.h"
#include "VeroScriptWebSocketReconnect.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#if VEROSCRIPT_PROXY_BINDINGS
#include "VeroScriptTestVm.h"
#include "../../../VeroScriptNetwork/Resources/Tests/NetworkTestProviders.h"
#include "VeroScriptNetworkModule.h"
#include "VeroScriptWebSocketModule.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include <sstream>
#endif

namespace Network = VeroScript::Network;
namespace Ws = VeroScript::WebSocket;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error(#X); \
		} \
	} \
	while (false)

static void Finish(Network::FContext& Context, const Network::FOperation& Operation)
{
	const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
	while (!Operation.IsComplete() && std::chrono::steady_clock::now() < Until)
	{
		if (Context.Mode() == Network::EDriveMode::HostDriven)
		{
			Network::FError Error;
			Context.PollIo(Error);
			CHECK(!Error);
		}
		else
		{
			Operation.Wait(std::chrono::milliseconds(5));
		}
		std::this_thread::yield();
	}
	CHECK(Operation.IsComplete());
}

static void Run(Network::EDriveMode Mode, const std::string& Proxy, const std::string& Target, const std::string& Trust)
{
	Network::FContextOptions ContextOptions;
	ContextOptions.Mode = Mode;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), ContextOptions);
	Ws::FOptions Options;
	Options.Operation.Timeout = std::chrono::seconds(3);
	Options.ProxyUrl = Proxy;
	Options.ProxyAuthorization = "Basic dXNlcjpwYXNz";
	Options.Tls.TrustPem = Trust;
	auto Open = VeroScript::WebSocket::Test::Connect(Context, Target, Options);
	Finish(Context, Open);
	if (Open.Error())
	{
		throw std::runtime_error(Open.Error().Message);
	}
	auto Connection = Open.Result<std::shared_ptr<Ws::FConnection>>();
	CHECK(Connection && *Connection);
	for (auto Message : {Ws::FMessage{{'h', 'i'}, true}, Ws::FMessage{{0, 255, 0}, false}, Ws::FMessage{{}, true}})
	{
		auto Receive = (*Connection)->Receive();
		auto Send = (*Connection)->Send(Message);
		Finish(Context, Send);
		Finish(Context, Receive);
		CHECK(!Send.Error() && !Receive.Error());
		auto Reply = Receive.Result<Ws::FMessage>();
		CHECK(Reply && Reply->Bytes == Message.Bytes && Reply->bText == Message.bText);
	}
	auto Close = (*Connection)->Close();
	Finish(Context, Close);
	if (Close.Error())
	{
		throw std::runtime_error(Target + ": close: " + Close.Error().Message);
	}

	Options.ProxyAuthorization = "wrong";
	auto Rejected = VeroScript::WebSocket::Test::Connect(Context, Target, Options);
	Finish(Context, Rejected);
	CHECK(Rejected.Error().Code == Network::EError::Protocol);
	Options.ProxyAuthorization = "Test stall";
	Options.Operation.Timeout = std::chrono::milliseconds(75);
	auto Timed = VeroScript::WebSocket::Test::Connect(Context, Target, Options);
	Finish(Context, Timed);
	CHECK(Timed.Error().Code == Network::EError::TimedOut);
	Options.Operation.Timeout = std::chrono::seconds(3);
	auto Cancelled = VeroScript::WebSocket::Test::Connect(Context, Target, Options);
	CHECK(Cancelled.Cancel());
	Finish(Context, Cancelled);
	CHECK(Cancelled.Error().Code == Network::EError::Cancelled);

	Options.ProxyUrl = "socks5://127.0.0.1:8080";
	auto Invalid = VeroScript::WebSocket::Test::Connect(Context, Target, Options);
	CHECK(Invalid.Error().Code == Network::EError::InvalidArgument);
	Options.ProxyUrl = Proxy;
	Options.ProxyAuthorization = "bad\r\nheader";
	Invalid = VeroScript::WebSocket::Test::Connect(Context, Target, Options);
	CHECK(Invalid.Error().Code == Network::EError::InvalidArgument);
	Context.Shutdown();
}

#if VEROSCRIPT_PROXY_BINDINGS
static void Script(const std::vector<std::string>& Paths, const std::string& Proxy,
    const std::vector<std::string>& Targets, const std::string& Trust, bool Reconnect)
{
	using namespace VeroScript;
	std::string Error;
	FNativeExtensionRegistry Registry;
	auto BackendCatalog = Network::Test::AddProviders(Registry, Error, !Paths.empty());
	CHECK(BackendCatalog);
	const FNativeExtensionQuery Queries[] = {Network::QueryModule, Ws::QueryModule};
	const char* Identities[] = {Network::GetImplementationIdentity(), Ws::GetImplementationIdentity()};
	const char* Providers[] = {"network", "websocket"};
	for (unsigned I = 0; I < 2; ++I)
	{
		FNativeExtensionLoadOptions Load;
		Load.ContentIdentity = Identities[I];
		auto Module =
		    Paths.empty() ? QueryNativeExtension(Queries[I], {}, Error, Load) : LoadNativeExtension(Paths[I], Error);
		FNativeExtensionRegistrationOptions Registration;
		Registration.Services = {BackendCatalog->Binding()};
		Registration.Origin.ProviderId = Providers[I];
		CHECK(Module && Registry.Add(Module, Registration, Error));
	}
	FModuleEnvironmentDesc Desc;
	Desc.EntryOwnerId = "project";
	Desc.Scopes = {{"project", {"network", "websocket"}}, {"network", {}}, {"websocket", {"network"}}};
	const auto Root = std::filesystem::current_path().generic_string();
	Desc.SourceRoots = {{Root, "project"}};
	Desc.Bindings = Registry.GetBindings();
	Desc.Owners = Registry.GetOwners();
	Desc.AvailableDependencies = Registry.GetDependencies();
	auto Environment = FModuleEnvironment::Create(std::move(Desc), Error);
	CHECK(Environment);
	const auto Example = Reconnect ? std::filesystem::path(VEROSCRIPT_PROXY_EXAMPLE_PATH).parent_path() / "reconnect.vs"
	                               : std::filesystem::path(VEROSCRIPT_PROXY_EXAMPLE_PATH);
	std::ifstream Input(Example, std::ios::binary);
	const std::string Source{std::istreambuf_iterator<char>(Input), {}};
	CHECK(!Source.empty());
	FCompileOptions Options;
	VeroScript::Test::Configure(Options);
	Options.ModuleEnvironment = Environment;
	auto Code = VeroScriptCompile::CompileSource(Source, Root + "/proxy-example.vs", Options);
	if (!Code.bSuccess)
	{
		throw std::runtime_error(std::to_string(Code.Error.Location.Line) + ":" +
		    std::to_string(Code.Error.Location.Column) + " " + Code.Error.Message);
	}
	for (const auto& Original : Targets)
	{
		const auto Nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
		const auto Target =
		    Reconnect ? Original.substr(0, Original.size() - 5) + "/retry/script-" + Nonce + "/2" : Original;
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionBackend(EExecutionBackend::Vm);
		CHECK(Vm.SetModuleEnvironment(Environment, Error));
		std::vector<std::string_view> Args{Proxy, Target, "Basic dXNlcjpwYXNz"};
		if (Target.rfind("wss:", 0) == 0)
		{
			Args.push_back(Trust);
		}
		if (!Vm.Execute(*Code.Module, ERunMode::Script, Args) || Vm.GetProcessExitCode() != 0)
		{
			throw std::runtime_error(
			    Vm.GetError().Message + " script proxy exit=" + std::to_string(Vm.GetProcessExitCode()));
		}
	}
}
#endif

static void Reconnect(Network::EDriveMode Mode, const std::string& Proxy, const std::string& Base, const std::string& Trust)
{
	Network::FContextOptions COptions;
	COptions.Mode = Mode;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService(), COptions);
	Ws::FOptions Options;
	Options.ProxyUrl = Proxy;
	Options.ProxyAuthorization = "Basic dXNlcjpwYXNz";
	Options.Tls.TrustPem = Trust;
	Options.Operation.Timeout = std::chrono::seconds(2);
	Ws::FReconnectPolicy Policy;
	Policy.MaxAttempts = 3;
	Policy.Delay = std::chrono::milliseconds(50);
	Policy.MaxDelay = std::chrono::milliseconds(100);
	Policy.MaxElapsed = std::chrono::seconds(4);
	Policy.bRetryProtocolErrors = true;
	const auto Nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto Root = Base.substr(0, Base.size() - 5);
	auto Target = [&](const std::string& Name, int Failures)
	{
		return Root + "/retry/" + Nonce + Name + "/" + std::to_string(Failures);
	};
	auto First = [&](const Ws::FReconnectTask& Task)
	{
		const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		Ws::FReconnectSnapshot Snapshot;
		Network::FError Error;
		while (std::chrono::steady_clock::now() < Until)
		{
			CHECK(Task.TrySnapshot(Snapshot, Error));
			if (!Snapshot.Attempts.empty() && Snapshot.Attempts[0].bComplete)
			{
				return Snapshot;
			}
			if (Context.Mode() == Network::EDriveMode::HostDriven)
			{
				Context.PollIo(Error);
				CHECK(!Error);
			}
			else
			{
				Task.Operation().Wait(std::chrono::milliseconds(1));
			}
		}
		throw std::runtime_error("first reconnect attempt timed out");
	};
	auto Task = VeroScript::WebSocket::Test::ConnectWithRetry(Context, Target("success", 2), Policy, Options);
	const auto Earlier = First(Task);
	CHECK(!Earlier.Attempts.empty());
	Finish(Context, Task.Operation());
	CHECK(!Task.Operation().Error());
	Network::FError Error;
	auto History = Task.Snapshot(Error);
	CHECK(History && !Error && History->bFinal && !History->Error && History->Attempts.size() == 3);
	CHECK(History->Attempts[0].bComplete && !History->Attempts[0].bConnected &&
	    History->Attempts[0].Error.Code == Network::EError::Protocol);
	CHECK(History->Attempts[2].bConnected && History->Attempts[2].Number == 3);
	CHECK(Earlier.Attempts.size() <= 3);
	const auto FirstGeneration = History->Generation;
	auto Connected = Task.Operation().Result<std::shared_ptr<Ws::FConnection>>();
	CHECK(Connected && *Connected);
	// A second explicit generation is independent from the retained old handle.
	auto Next = VeroScript::WebSocket::Test::ConnectWithRetry(Context, Base, Policy, Options);
	Finish(Context, Next.Operation());
	CHECK(!Next.Operation().Error());
	auto NewHistory = Next.Snapshot(Error);
	CHECK(NewHistory && NewHistory->Generation != FirstGeneration && NewHistory->Attempts.size() == 1);
	auto New = Next.Operation().Result<std::shared_ptr<Ws::FConnection>>();
	CHECK(New && *New);
	auto OldClose = (*Connected)->Close();
	Finish(Context, OldClose);
	CHECK(!OldClose.Error());
	auto Receive = (*New)->Receive();
	auto Send = (*New)->Send({{'n', 'e', 'w'}, true});
	Finish(Context, Send);
	Finish(Context, Receive);
	CHECK(!Send.Error() && !Receive.Error() && Receive.Result<Ws::FMessage>() &&
	    Receive.Result<Ws::FMessage>()->Bytes == Network::FBytes({'n', 'e', 'w'}));
	auto NewClose = (*New)->Close();
	Finish(Context, NewClose);
	CHECK(!NewClose.Error());
	auto Exhausted = VeroScript::WebSocket::Test::ConnectWithRetry(Context, Target("exhausted", 99), Policy, Options);
	Finish(Context, Exhausted.Operation());
	Ws::FReconnectSnapshot Result;
	CHECK(Exhausted.TrySnapshot(Result, Error) && Result.bFinal && Result.Attempts.size() == 3 &&
	    Result.Error.Code == Network::EError::Protocol);
	auto Disabled = Policy;
	Disabled.MaxAttempts = 1;
	auto Once = VeroScript::WebSocket::Test::ConnectWithRetry(Context, Target("once", 99), Disabled, Options);
	Finish(Context, Once.Operation());
	CHECK(Once.TrySnapshot(Result, Error) && Result.bFinal && Result.Attempts.size() == 1);
	auto Restricted = Policy;
	Restricted.bRetryProtocolErrors = false;
	auto NoProtocol = VeroScript::WebSocket::Test::ConnectWithRetry(Context, Target("protocol", 99), Restricted, Options);
	Finish(Context, NoProtocol.Operation());
	CHECK(NoProtocol.TrySnapshot(Result, Error) && Result.bFinal && Result.Attempts.size() == 1);
	auto Backoff = Policy;
	Backoff.Delay = Backoff.MaxDelay = std::chrono::seconds(30);
	auto Cancelled = VeroScript::WebSocket::Test::ConnectWithRetry(Context, Target("cancel", 99), Backoff, Options);
	CHECK(First(Cancelled).Attempts.size() == 1);
	CHECK(Cancelled.Operation().Cancel());
	CHECK(Cancelled.TrySnapshot(Result, Error) && Result.bFinal && Result.Error.Code == Network::EError::Cancelled &&
	    Result.Attempts.size() == 1);
	Backoff.MaxElapsed = std::chrono::milliseconds(125);
	auto Timed = VeroScript::WebSocket::Test::ConnectWithRetry(Context, Target("timeout", 99), Backoff, Options);
	Finish(Context, Timed.Operation());
	CHECK(Timed.Operation().Error().Code == Network::EError::TimedOut);
	CHECK(Timed.TrySnapshot(Result, Error) && Result.bFinal && Result.Attempts.size() == 1);
	auto InvalidPolicy = Policy;
	InvalidPolicy.MaxElapsed = std::chrono::milliseconds(INT64_MAX);
	auto Invalid = VeroScript::WebSocket::Test::ConnectWithRetry(Context, Base, InvalidPolicy, Options);
	CHECK(Invalid.Operation().Error().Code == Network::EError::InvalidArgument);
	Context.Shutdown();
	CHECK(Task.TrySnapshot(Result, Error) && Result.bFinal && Result.Attempts.size() == 3);
}

int main(int Argc, char** Argv)
{
	try
	{
		const bool IsReconnect = Argc > 1 && std::string_view(Argv[Argc - 1]) == "--reconnect";
		if (IsReconnect)
		{
			--Argc;
		}
		CHECK(Argc == 5 || Argc == 7);
		std::ifstream Input(std::filesystem::u8path(Argv[4]), std::ios::binary);
		const std::string Trust{std::istreambuf_iterator<char>(Input), {}};
		CHECK(!Trust.empty());
		const auto Proxy = std::string("http://127.0.0.1:") + Argv[1];
		for (auto Mode : {Network::EDriveMode::HostDriven, Network::EDriveMode::NativeBackground})
		{
			auto Test = IsReconnect ? Reconnect : Run;
			Test(Mode, Proxy, std::string("ws://127.0.0.1:") + Argv[2] + "/echo", {});
			#if VEROSCRIPT_PROXY_TLS
			Test(Mode, Proxy, std::string("wss://127.0.0.1:") + Argv[3] + "/echo", Trust);
			#endif
		}
		#if VEROSCRIPT_PROXY_BINDINGS
		std::vector<std::string> Targets{std::string("ws://127.0.0.1:") + Argv[2] + "/echo"};
		#if VEROSCRIPT_PROXY_TLS
		Targets.push_back(std::string("wss://127.0.0.1:") + Argv[3] + "/echo");
		#endif
		Script({}, Proxy, Targets, Trust, IsReconnect);
		if (Argc == 7)
		{
			Script({std::filesystem::absolute(Argv[5]).generic_string(),
			           std::filesystem::absolute(Argv[6]).generic_string()},
			    Proxy,
			    Targets,
			    Trust,
			    IsReconnect);
		}
		#endif
		std::cout << "HTTP CONNECT proxy, independent peers, both drive modes passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
