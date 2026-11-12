// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetworkProviderHost.h"
#include "VeroScriptNetworkBackend.h"
#include "VeroScriptHttpBackend.h"
#include "VeroScriptWebSocketBackend.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace N = VeroScript::Network;
namespace H = VeroScript::Http;
namespace W = VeroScript::WebSocket;
using namespace VeroScript;

static void Check(bool Value, const char* Message)
{
	if (!Value)
	{
		throw std::runtime_error(Message);
	}
}

static void Wait(N::FContext& Context, const N::FOperation& Operation)
{
	const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(8);
	while (!Operation.IsComplete())
	{
		Check(std::chrono::steady_clock::now() < End, "Retirement fixture timed out");
		N::FError Error;
		Context.PollIo(Error);
		Check(!Error, "Retirement fixture polling failed");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const auto Failure = Operation.Error();
	if (Failure)
	{
		throw std::runtime_error("Retirement fixture operation failed: " + Failure.Message);
	}
}

template <class T>
static std::shared_ptr<T> Resolve(const std::shared_ptr<N::FBackendCatalog>& Catalog,
    const char* Service, const char* Backend, const char* Contract)
{
	std::string Error;
	auto Value = Catalog->Resolve(Service, Backend, Contract, "0.1.1", Error);
	if (!Value)
	{
		throw std::runtime_error(Error);
	}
	auto* Pointer = static_cast<T*>(Value.get());
	return std::shared_ptr<T>(std::move(Value), Pointer);
}

static void RunCase(const std::vector<std::string>& Paths, unsigned Case)
{
	std::string Error;
	auto Registry = std::make_unique<FNativeExtensionRegistry>();
	auto Catalog = std::make_shared<N::FBackendCatalog>();
	std::vector<std::shared_ptr<const FLoadedNativeExtension>> Modules;
	std::vector<std::weak_ptr<const FLoadedNativeExtension>> Probes;
	for (std::size_t Index = 0; Index < Paths.size(); ++Index)
	{
		auto Module = LoadNativeExtension(Paths[Index], Error);
		if (!Module)
		{
			throw std::runtime_error(Error);
		}
		FNativeExtensionRegistrationOptions Registration;
		Registration.Origin.ProviderId = Index ? "curl" : "boost";
		Check(Registry->Add(Module, Registration, Error), "Cannot register retirement fixture provider");
		Probes.push_back(Module);
		Modules.push_back(std::move(Module));
	}
	Catalog->Update(Registry->GetProviders());
	N::FContextOptions Options;
	Options.Mode = N::EDriveMode::HostDriven;
	auto Context = std::make_unique<N::FContext>(
	    Resolve<N::Backend::IService>(Catalog, "network", "boost", N::Backend::ContractId), Options);
	std::shared_ptr<const void> Retained;
	N::FError Failure;
	if (Case == 0)
	{
		Retained = N::FTcpListener::Listen(*Context, "127.0.0.1", 0, Failure);
	}
	else if (Case == 1)
	{
		Retained = N::FUdpSocket::Bind(*Context, "127.0.0.1", 0, Failure);
	}
	else if (Case == 2 || Case == 3)
	{
		auto Listener = N::FTcpListener::Listen(*Context, "127.0.0.1", 0, Failure);
		Check(Listener && !Failure, "Cannot create retirement TCP listener");
		auto Incoming = Listener->Accept();
		auto Connected = N::FTcpSocket::Connect(*Context, "127.0.0.1", Listener->Port());
		Wait(*Context, Connected);
		Wait(*Context, Incoming);
		auto Socket = Connected.Result<std::shared_ptr<N::FTcpSocket>>();
		Check(!Connected.Result<std::shared_ptr<N::FUdpSocket>>(), "Wrong result type was accepted");
		Check(Socket && *Socket, "Cannot retrieve retirement TCP socket");
		if (Case == 2)
		{
			Retained = *Socket;
		}
		else
		{
			Retained = std::move(Socket);
		}
	}
	else if (Case == 4)
	{
		auto Invalid = N::FTcpSocket::Connect(*Context, "", 0);
		Check(Invalid.IsComplete() && Invalid.Error(), "Invalid TCP operation did not fail");
		Retained = std::make_shared<N::FOperation>(std::move(Invalid));
	}
	else if (Case == 5)
	{
		Retained = std::make_shared<H::FClient>(*Context,
		    Resolve<H::Backend::IClientService>(Catalog, "http-client", "curl", H::Backend::ClientContractId));
	}
	else if (Case == 6 || Case >= 9)
	{
		H::FServerOptions ServerOptions;
		ServerOptions.Service = Resolve<H::Backend::IServerService>(Catalog,
		    "http-server", "boost", H::Backend::ServerContractId);
		auto Server = H::FServer::Listen(*Context, "127.0.0.1", 0, Failure, ServerOptions);
		Check(Server && !Failure, "Cannot create retirement HTTP server");
		if (Case == 6)
		{
			Retained = Server;
		}
		else
		{
			H::FClient Client(*Context,
			    Resolve<H::Backend::IClientService>(Catalog, "http-client", "curl", H::Backend::ClientContractId));
			auto Incoming = Server->Accept();
			H::FRequest Request;
			Request.Url = "http://127.0.0.1:" + std::to_string(Server->Port()) + "/";
			auto Transfer = Client.SendStream(std::move(Request), 65536);
			Wait(*Context, Incoming);
			auto ExchangeResult = Incoming.Result<std::shared_ptr<H::FExchange>>();
			Check(ExchangeResult && *ExchangeResult, "Cannot retrieve retirement HTTP exchange");
			auto Exchange = *ExchangeResult;
			auto SharedExchange = Exchange->shared_from_this();
			Check(!SharedExchange.owner_before(Exchange) && !Exchange.owner_before(SharedExchange),
			    "HTTP exchange lost its shared owner");
			if (Case == 9)
			{
				Retained = std::move(SharedExchange);
			}
			else if (Case == 10)
			{
				H::FResponse Response;
				Response.Status = 200;
				Response.Body = {'o', 'k'};
				auto Reply = Exchange->Reply(std::move(Response));
				Wait(*Context, Reply);
				Wait(*Context, Transfer.Operation());
				Retained = Transfer.Body();
			}
			else
			{
				Retained = Exchange->PrepareUpgrade(Failure);
			}
		}
	}
	else
	{
		W::FOptions WebSocketOptions;
		WebSocketOptions.Service = Resolve<W::Backend::IService>(Catalog,
		    "websocket-server", "boost", W::Backend::ContractId);
		auto Server = W::FServer::Listen(*Context, "127.0.0.1", 0, Failure, WebSocketOptions);
		Check(Server && !Failure, "Cannot create retirement WebSocket server");
		if (Case == 7)
		{
			Retained = Server;
		}
		else
		{
			auto Incoming = Server->Accept();
			auto Connected = W::FConnection::Connect(*Context,
			    "ws://127.0.0.1:" + std::to_string(Server->Port()) + "/", WebSocketOptions);
			Wait(*Context, Connected);
			Wait(*Context, Incoming);
			auto Socket = Connected.Result<std::shared_ptr<W::FConnection>>();
			Check(Socket && *Socket, "Cannot retrieve retirement WebSocket connection");
			Retained = *Socket;
		}
	}
	Check(Retained && !Failure, "Retirement fixture did not retain its selected object");
	Context->Shutdown();
	Modules.clear();
	Registry.reset();
	Catalog.reset();
	Context.reset();
	Check(!Probes.front().expired(), "Provider code retired before its last object");
	Retained.reset();
	for (const auto& Probe : Probes)
	{
		Check(Probe.expired(), "Provider code lease leaked after final release");
	}
}

static int Run(int Argc, char** Argv)
{
	try
	{
		Check(Argc == 3, "Expected Boost and curl module paths");
		for (unsigned Case = 0; Case < 12; ++Case)
		{
			std::cerr << "Backend retirement case " << Case << '\n';
			RunCase({Argv[1], Argv[2]}, Case);
		}
		std::cout << "Dynamic backend objects and results retire safely after their public context\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}

#ifdef _WIN32
int wmain(int Argc, wchar_t** Argv)
{
	std::vector<std::string> Utf8;
	for (int Index = 0; Index < Argc; ++Index)
	{
		Utf8.push_back(PathUtil::ToUtf8(std::filesystem::path(Argv[Index])));
	}
	std::vector<char*> Arguments;
	for (auto& Value : Utf8)
	{
		Arguments.push_back(Value.data());
	}
	return Run(Argc, Arguments.data());
}
#else
int main(int Argc, char** Argv)
{
	return Run(Argc, Argv);
}
#endif
