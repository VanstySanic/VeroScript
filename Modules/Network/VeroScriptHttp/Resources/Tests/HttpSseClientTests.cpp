// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"
#include "VeroScriptHttpSseClient.h"
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

static H::ESseClientState Poll(N::FContext& Context, H::FSseClient& Client, H::FSseEvent& Event,
    std::uint64_t& Consumed, std::chrono::steady_clock::time_point Deadline)
{
	CHECK(std::chrono::steady_clock::now() < Deadline);
	if (Context.Mode() == N::EDriveMode::HostDriven)
	{
		N::FError Error;
		Context.PollIo(Error);
		CHECK(!Error);
	}
	const auto Before = Consumed;
	const auto State = Client.Poll(Event,
	    {[](void* User, std::uint64_t Units)
	{
		*static_cast<std::uint64_t*>(User) += Units;
		return true;
	},
	        &Consumed});
	if (State == H::ESseClientState::Pending && Before == Consumed)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return State;
}

static void Test(std::uint16_t Port, N::EDriveMode Mode)
{
	N::FContextOptions ContextOptions;
	ContextOptions.Mode = Mode;
	N::FContext Context(VeroScript::Boost::CreateNetworkService(), ContextOptions);
	const auto Base = "http://127.0.0.1:" + std::to_string(Port);
	N::FError Error;
	H::FSseClientOptions Options;
	Options.Service = VeroScript::Curl::CreateHttpClientService();
	Options.Request.Url = Base + "/sse-large-default";
	auto Client = H::FSseClient::Connect(Context, Options, Error);
	CHECK(Client && !Error);
	H::FSseEvent Event;
	std::uint64_t Consumed = 0;
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	for (;;)
	{
		const auto State = Poll(Context, *Client, Event, Consumed, Deadline);
		if (State == H::ESseClientState::Failed)
		{
			throw std::runtime_error("Default SSE failed after " + std::to_string(Consumed) +
			    " bytes: " + Client->Error().Message);
		}
		if (State == H::ESseClientState::EventReady)
		{
			CHECK(Event.Data == "after-64-mib" && Consumed > 64ULL * 1024 * 1024);
			CHECK(Client->ReconnectCount() == 0);
			break;
		}
		CHECK(State == H::ESseClientState::Pending);
	}
	Client->Close();
	Client.reset();
	std::cout << "Default SSE received event after " << Consumed << " bytes\n";

	// Replacing Request must preserve the caller's finite total, even with unlimited SSE defaults.
	Options.Request = H::FRequest{};
	Options.Request.Url = Base + "/sse-explicit-total-limit";
	Options.Request.MaxTotalBodyBytes = 1024;
	Client = H::FSseClient::Connect(Context, Options, Error);
	CHECK(Client && !Error);
	Event.Data = "unchanged";
	Consumed = 0;
	const auto LimitedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	H::ESseClientState State;
	do
	{
		State = Poll(Context, *Client, Event, Consumed, LimitedDeadline);
	}
	while (State == H::ESseClientState::Pending);
	CHECK(State == H::ESseClientState::Failed && Client->Error().Code == N::EError::Limit);
	CHECK(Event.Data == "unchanged" && Client->ReconnectCount() == 0);
	Client.reset();
	Context.Shutdown();
	std::cout << "Explicit SSE request total limit preserved\n";
}

int main(int Argc, char** Argv)
{
	try
	{
		CHECK(Argc >= 2);
		const auto Port = static_cast<std::uint16_t>(std::stoi(Argv[1]));
		Test(Port, N::EDriveMode::NativeBackground);
		Test(Port, N::EDriveMode::HostDriven);
		std::cout << "SSE client long-stream and explicit limit tests passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
