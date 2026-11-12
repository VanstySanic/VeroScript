// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"
#include "HttpTestBackends.h"

#include "NetworkProvider.h"
#include "VeroScriptHttp.h"
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
	thread_local int Countdown = -1;
	void* Allocate(std::size_t Size)
	{
		if (Countdown == 0)
		{
			throw std::bad_alloc();
		}
		if (Countdown > 0)
		{
			--Countdown;
		}
		if (auto* Result = std::malloc(Size ? Size : 1))
		{
			return Result;
		}
		throw std::bad_alloc();
	}
	void Check(bool Value, const char* Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	void Run(bool Stream)
	{
		namespace N = VeroScript::Network;
		namespace H = VeroScript::Http;
		N::FContext Peer(VeroScript::Boost::CreateNetworkService());
		N::FError Error;
		auto Server = VeroScript::Http::Test::Listen(Peer, "127.0.0.1", 0, Error);
		Check(Server && !Error, "Cannot create HTTP trace allocation peer");
		unsigned Failed = 0, Succeeded = 0, SnapshotFailed = 0;
		for (int Boundary = 0; Boundary < 96; ++Boundary)
		{
			N::FContextOptions Options;
			Options.Mode = N::EDriveMode::HostDriven;
			N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
			H::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
			H::FRequest Request;
			Request.Url = "http://127.0.0.1:" + std::to_string(Server->Port()) + "/trace";
			Request.History = std::make_shared<H::FAttemptHistory>();
			auto Accept = Server->Accept();
			H::FStreamingOperation Streaming;
			N::FOperation Operation;
			if (Stream)
			{
				Streaming = Client.SendStream(Request, 16384);
				Operation = Streaming.Operation();
			}
			else
			{
				Operation = Client.Send(Request);
			}
			const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!Accept.IsComplete() && std::chrono::steady_clock::now() < Until)
			{
				Context.PollIo(Error);
				Check(!Error, "HTTP trace allocation setup failed");
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			Check(Accept.IsComplete() && !Accept.Error(), "HTTP trace allocation peer did not accept");
			auto Exchange = *Accept.Result<std::shared_ptr<H::FExchange>>();
			H::FResponse Response;
			Response.Status = 200;
			Response.Body = {'p', 'a', 'y', 'l', 'o', 'a', 'd'};
			Check(Response.Headers.TryAdd("Location", "/one", Error) && Response.Headers.TryAdd("Location", "", Error),
				"Cannot prepare HTTP trace allocation headers");
			auto Reply = Exchange->Reply(std::move(Response));
			Check(Reply.Wait(std::chrono::seconds(5)) == N::EWaitResult::Completed && !Reply.Error(),
				"HTTP trace allocation peer did not send");
			Countdown = Boundary;
			try
			{
				while (!Operation.IsComplete() && std::chrono::steady_clock::now() < Until)
				{
					Context.PollIo(Error);
					if (Error)
					{
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			catch (...)
			{
				Countdown = -1;
				throw;
			}
			Countdown = -1;
			if (Operation.Error().Code == N::EError::Allocation || Error.Code == N::EError::Allocation)
			{
				++Failed;
			}
			else
			{
				Check(Operation.IsComplete() && !Operation.Error(), "HTTP trace operation did not complete");
				auto Result = Operation.Result<H::FResponse>();
				Check(Result && Result->Details && Result->Details->Statistics.bFinal &&
						  Result->Details->Statistics.AcceptedBodyBytes == 7 &&
						  Result->Details->Responses.size() == 1 && Result->Details->Responses[0].Locations.size() == 2,
					"HTTP trace allocation changed the completed response");
				if (Stream)
				{
					H::FResponse Head;
					Check(Streaming.TryHead(Head, Error) && !Error && Head.Details &&
							  !Head.Details->Statistics.bFinal && Head.Details->Statistics.AcceptedBodyBytes == 0,
						"HTTP early details snapshot was changed by finalization");
				}
				++Succeeded;
				// Copying public history can fail, but it must not mutate the completed details.
				Countdown = Boundary % 8;
				try
				{
					auto Copy = Request.History->Snapshot();
				}
				catch (const std::bad_alloc&)
				{
					++SnapshotFailed;
				}
				catch (...)
				{
					Countdown = -1;
					throw;
				}
				Countdown = -1;
				Check(
					Result->Details->Responses[0].Locations[0] == "/one", "Failed history copy damaged shared details");
			}
			Client.Close();
			Context.Shutdown();
			Check(Operation.IsComplete(), "HTTP trace allocation left an operation pending");
			Streaming = {};
			Operation = {};
			Request.History.reset();
			Check(Context.MemoryUsage().RetainedBytes == 0, "HTTP trace allocation retained an unreachable snapshot");
		}
		Check(Failed && Succeeded && SnapshotFailed, "HTTP trace allocation boundaries were not exercised");
		Server->Close();
		Peer.Shutdown();
	}
} // namespace

void* operator new(std::size_t Size)
{
	return Allocate(Size);
}
void* operator new[](std::size_t Size)
{
	return Allocate(Size);
}
void operator delete(void* Value) noexcept
{
	std::free(Value);
}
void operator delete[](void* Value) noexcept
{
	std::free(Value);
}
void operator delete(void* Value, std::size_t) noexcept
{
	std::free(Value);
}
void operator delete[](void* Value, std::size_t) noexcept
{
	std::free(Value);
}
int main()
{
	try
	{
		Run(false);
		Run(true);
		std::cout << "HTTP trace allocation boundaries passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		Countdown = -1;
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
