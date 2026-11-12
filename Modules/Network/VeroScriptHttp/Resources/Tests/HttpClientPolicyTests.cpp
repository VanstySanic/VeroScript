// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"

#include "VeroScriptHttp.h"
#include <iostream>
#include <atomic>
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

static void Await(N::FContext& Context, const N::FOperation& Operation)
{
	const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!Operation.IsComplete() && std::chrono::steady_clock::now() < Until)
	{
		if (Context.Mode() == N::EDriveMode::HostDriven)
		{
			N::FError Error;
			Context.PollIo(Error);
			CHECK(!Error);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		else
		{
			CHECK(Operation.Wait(std::chrono::seconds(10)) == N::EWaitResult::Completed);
		}
	}
	CHECK(Operation.IsComplete());
}
template <class T>
static T Result(N::FContext& Context, const N::FOperation& Op)
{
	Await(Context, Op);
	if (Op.Error())
	{
		throw std::runtime_error(Op.Error().Message);
	}
	const auto Value = Op.Result<T>();
	CHECK(Value);
	return *Value;
}
static std::string Body(const H::FResponse& R)
{
	return std::string(R.Body.begin(), R.Body.end());
}

static std::shared_ptr<const H::FAttemptSnapshot> History(
    N::FContext& Context, const std::shared_ptr<H::FAttemptHistory>& Value)
{
	const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (std::chrono::steady_clock::now() < Until)
	{
		auto Snapshot = Value->Snapshot();
		if (Snapshot->bFinal)
		{
			return Snapshot;
		}
		if (Context.Mode() == N::EDriveMode::HostDriven)
		{
			N::FError Error;
			Context.PollIo(Error);
			CHECK(!Error);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	throw std::runtime_error("HTTP history cleanup did not finish");
}

static void Test(std::uint16_t Port, N::EDriveMode Mode)
{
	N::FContextOptions ContextOptions;
	ContextOptions.Mode = Mode;
	N::FContext Context(VeroScript::Boost::CreateNetworkService(), ContextOptions);
	H::FClientOptions Options;
	Options.bCookies = true;
	Options.MaxCookieBytes = 8192;
	H::FClient Client(Context, VeroScript::Curl::CreateHttpClientService(), Options);
	H::FClient Other(Context, VeroScript::Curl::CreateHttpClientService(), Options);
	H::FClient Disabled(Context, VeroScript::Curl::CreateHttpClientService());
	const auto Base = "http://127.0.0.1:" + std::to_string(Port);
	H::FRequest Request;
	Request.Url = Base + "/ok";
	CHECK(Result<H::FResponse>(Context, Client.Send(Request)).Status == 200);
	{
		N::FContextOptions WideOptions;
		WideOptions.Mode = Mode;
		WideOptions.MaxBufferBytes = UINT64_MAX;
		N::FContext WideContext(VeroScript::Boost::CreateNetworkService(), WideOptions);
		H::FClient WideClient(WideContext, VeroScript::Curl::CreateHttpClientService());
		H::FRequest WideRequest;
		WideRequest.Url = Base + "/ok";
		WideRequest.MaxBodyBytes = static_cast<std::uint64_t>(N::FBytes{}.max_size()) + 1;
		auto Buffered = WideClient.Send(WideRequest);
		Await(WideContext, Buffered);
		CHECK(Buffered.Error().Code == N::EError::InvalidArgument);
		auto Streamed = WideClient.SendStream(WideRequest, 16384);
		CHECK(Result<H::FResponse>(WideContext, Streamed.Operation()).Status == 200);
		CHECK(Streamed.Body());
		char Bytes[16];
		const auto Read = Streamed.Body()->Read(Bytes, sizeof(Bytes));
		CHECK(Read.Bytes > 0 && !Read.Error);
		WideClient.Close();
		WideContext.Shutdown();
	}
	H::FRequest Truncated;
	Truncated.Url = Base + "/truncated";
	Truncated.History = std::make_shared<H::FAttemptHistory>();
	auto TruncatedOp = Client.Send(Truncated);
	Await(Context, TruncatedOp);
	CHECK(TruncatedOp.Error().Code == N::EError::Read && TruncatedOp.Error().SystemCode != 0);
	const auto FailedAttempt = History(Context, Truncated.History);
	CHECK(FailedAttempt->Attempts.size() == 1 && FailedAttempt->Error.Code == N::EError::Read &&
	    FailedAttempt->Attempts[0].ErrorCode == N::EError::Read &&
	    FailedAttempt->Attempts[0].TransportCode == TruncatedOp.Error().SystemCode);
	auto Cookies = Result<H::FCookies>(Context, Client.Cookies());
	CHECK(Cookies.Records.size() == 2);
	Request.Url = Base + "/cookie-echo";
	const auto Echo = Body(Result<H::FResponse>(Context, Client.Send(Request)));
	CHECK(Echo.find("a=1") != Echo.npos && Echo.find("b=2") != Echo.npos);
	CHECK(Body(Result<H::FResponse>(Context, Other.Send(Request))).empty());
	CHECK(Body(Result<H::FResponse>(Context, Disabled.Send(Request))).empty());
	auto Unsupported = Disabled.Cookies();
	Await(Context, Unsupported);
	CHECK(Unsupported.Error().Code == N::EError::Unsupported);
	CHECK(Result<std::uint64_t>(Context, Client.ClearCookies()) == 0);
	CHECK(Body(Result<H::FResponse>(Context, Client.Send(Request))).empty());
	CHECK(Result<std::uint64_t>(Context, Client.ImportCookies(Cookies)) == 2);
	CHECK(!Body(Result<H::FResponse>(Context, Client.Send(Request))).empty());
	Cookies.Records.push_back("invalid");
	auto Invalid = Client.ImportCookies(Cookies);
	Await(Context, Invalid);
	CHECK(Invalid.Error().Code == N::EError::InvalidArgument);
	CHECK(Result<H::FCookies>(Context, Client.Cookies()).Records.size() == 2);

	const auto Scope = Mode == N::EDriveMode::HostDriven ? "host" : "worker";
	Request.Url = Base + "/retry/" + Scope + "/2";
	Request.Retry.MaxAttempts = 3;
	Request.Retry.bAllowReplay = true;
	Request.Retry.Delay = std::chrono::milliseconds(1);
	Request.History = std::make_shared<H::FAttemptHistory>();
	const auto Retried = Result<H::FResponse>(Context, Client.Send(Request));
	auto SavedHistory = History(Context, Request.History);
	CHECK(!SavedHistory->Error && !SavedHistory->bTruncated && SavedHistory->Attempts.size() == 3);
	CHECK(Retried.Status == 200 && Body(Retried) == "3" && Retried.Attempts.size() == 3);
	CHECK(Retried.Attempts[0].Number == 1 && Retried.Attempts[0].Status == 503 && Retried.Attempts[2].Number == 3 &&
	    Retried.Attempts[2].Status == 200);
	Request.History.reset();
	Request.Url = Base + "/retry/disabled-" + Scope + "/2";
	Request.Retry = {};
	CHECK(Result<H::FResponse>(Context, Client.Send(Request)).Status == 503);
	Request.Retry.MaxAttempts = 2;
	Invalid = Client.Send(Request);
	Await(Context, Invalid);
	CHECK(Invalid.Error().Code == N::EError::InvalidArgument);
	Request.Retry.bAllowReplay = true;
	Request.Method = "POST";
	Request.Body = {'x'};
	Invalid = Client.Send(Request);
	Await(Context, Invalid);
	CHECK(Invalid.Error().Code == N::EError::InvalidArgument);
	Request.Retry.bAllowUnsafeMethods = true;
	Request.Url = Base + "/retry/post-" + Scope + "/1";
	CHECK(Result<H::FResponse>(Context, Client.Send(Request)).Attempts.size() == 2);
	Request.Method = "GET";
	Request.Body.clear();
	Request.Url = Base + "/retry/limit-" + Scope + "/10";
	const auto Limited = Result<H::FResponse>(Context, Client.Send(Request));
	CHECK(Limited.Status == 503 && Limited.Attempts.size() == 2);
	Request.Url = Base + "/retry/deadline-" + Scope + "/10";
	Request.Retry.MaxAttempts = 3;
	Request.Retry.MaxDelay = std::chrono::seconds(2);
	N::FError Error;
	CHECK(Request.Headers.TryAdd("X-Test-Retry-After", "1", Error));
	Request.Operation.Timeout = std::chrono::milliseconds(75);
	const auto Started = std::chrono::steady_clock::now();
	Request.History = std::make_shared<H::FAttemptHistory>();
	auto Timed = Client.Send(Request);
	Await(Context, Timed);
	CHECK(Timed.Error().Code == N::EError::TimedOut &&
	    std::chrono::steady_clock::now() - Started < std::chrono::seconds(1));
	auto TimedHistory = History(Context, Request.History);
	CHECK(TimedHistory->Error.Code == N::EError::TimedOut && TimedHistory->Attempts.size() == 1 &&
	    !TimedHistory->bTruncated);
	CHECK(SavedHistory->Attempts.size() == 3 && !SavedHistory->Error);
	Request.Operation.Timeout = std::chrono::seconds(5);
	Request.Url = Base + "/retry/cancel-" + Scope + "/10";
	auto Cancelled = Client.Send(Request);
	CHECK(Cancelled.Cancel());
	Await(Context, Cancelled);
	CHECK(Cancelled.Error().Code == N::EError::Cancelled);
	CHECK(History(Context, Request.History)->Error.Code == N::EError::Cancelled);
	Request = {};
	Request.Method = "PUT";
	Request.Body = {'a'};
	Request.bFollowRedirects = true;
	Request.Url = Base + "/redirect-303";
	CHECK(Body(Result<H::FResponse>(Context, Client.Send(Request))) == "GET:");
	Request.Url = Base + "/redirect-307";
	const auto Preserved = Result<H::FResponse>(Context, Client.Send(Request));
	CHECK(Body(Preserved) == "PUT:a");
	CHECK(Preserved.Details && Preserved.Attempts[0].Details == Preserved.Details);
	CHECK(Preserved.Details->Responses.size() == 2 && Preserved.Details->Responses[0].Status == 307 &&
	    Preserved.Details->Responses[0].bRedirectFollowed &&
	    Preserved.Details->Responses[0].RedirectUrl == Base + "/method-echo" &&
	    Preserved.Details->Responses[0].Method == "PUT" && Preserved.Details->Responses[0].RedirectMethod == "PUT");
	CHECK(Preserved.Details->Statistics.bFinal && Preserved.Details->Statistics.FollowedRedirects == 1);
	CHECK(Preserved.Details->Statistics.TotalTimeUs && Preserved.Details->Statistics.QueueTimeUs &&
	    Preserved.Details->Statistics.NameLookupTimeUs && Preserved.Details->Statistics.ConnectTimeUs &&
	    Preserved.Details->Statistics.StartTransferTimeUs && Preserved.Details->Statistics.ReceivedHeaderBytes > 0 &&
	    Preserved.Details->Statistics.LastTransferDownloadedBytes == Preserved.BodyBytes &&
	    Preserved.Details->Statistics.AcceptedBodyBytes == Preserved.BodyBytes);
	CHECK(*Preserved.Details->Statistics.TotalTimeUs >= *Preserved.Details->Statistics.StartTransferTimeUs);
	Request.Method = "POST";
	Request.Body = {'a', 'b', 'c'};
	Request.Url = Base + "/trace/chain/start";
	const auto Chained = Result<H::FResponse>(Context, Client.Send(Request));
	CHECK(Body(Chained) == "GET:");
	const auto& Heads = Chained.Details->Responses;
	CHECK(Heads.size() == 4 && Heads[0].Status == 103 && Heads[1].Status == 302 && Heads[2].Status == 307 &&
	    Heads[3].Status == 200);
	CHECK(!Heads[0].bRedirectFollowed && Heads[1].Locations == std::vector<std::string>{"next?from=start"} &&
	    Heads[1].Url == Request.Url && Heads[1].Method == "POST" && Heads[1].RedirectMethod == "GET" &&
	    Heads[1].RedirectUrl == Base + "/trace/chain/next?from=start" &&
	    Heads[2].Locations == std::vector<std::string>{"../../method-echo?done"} &&
	    Heads[2].RedirectUrl == Base + "/method-echo?done" && Heads[2].bRedirectFollowed &&
	    Heads[3].Method == "GET" && Chained.Details->Statistics.FollowedRedirects == 2);
	Request = {};
	Request.Url = Base + "/trace/duplicate";
	const auto NoFollow = Result<H::FResponse>(Context, Client.Send(Request));
	CHECK(NoFollow.Details->Responses.size() == 1 && NoFollow.Details->Responses[0].Locations.size() == 2 &&
	    !NoFollow.Details->Responses[0].bRedirectFollowed && NoFollow.Details->Statistics.FollowedRedirects == 0);
	Request.bFollowRedirects = true;
	CHECK(Result<H::FResponse>(Context, Client.Send(Request)).Details->Responses[0].Locations.size() == 2);
	Request.Url = Base + "/trace/loop";
	Request.MaxRedirects = 2;
	Request.History = std::make_shared<H::FAttemptHistory>();
	auto Loop = Client.Send(Request);
	Await(Context, Loop);
	CHECK(Loop.Error().Code == N::EError::Protocol);
	const auto LoopHistory = History(Context, Request.History);
	CHECK(!LoopHistory->bTruncated && LoopHistory->Attempts.size() == 1 && LoopHistory->Attempts[0].Details);
	const auto& LoopDetails = *LoopHistory->Attempts[0].Details;
	CHECK(LoopDetails.Statistics.bFinal && LoopDetails.Responses.size() == 3 &&
	    LoopDetails.Statistics.FollowedRedirects == 2 && LoopDetails.Responses[0].bRedirectFollowed &&
	    LoopDetails.Responses[1].bRedirectFollowed && !LoopDetails.Responses[2].bRedirectFollowed);
	Request.Url = Base + "/trace/failed-target";
	auto RefusalListener = N::FTcpListener::Listen(Context, "127.0.0.1", 0, Error);
	CHECK(RefusalListener && !Error);
	const auto RefusalPort = RefusalListener->Port();
	RefusalListener->Close();
	Request.ConnectTimeout = std::chrono::milliseconds(1000);
	CHECK(Request.Headers.TrySet("X-Test-Closed-Port", std::to_string(RefusalPort), Error));
	auto MissingTarget = Client.Send(Request);
	Await(Context, MissingTarget);
	CHECK(MissingTarget.Error().Code == N::EError::Connect || MissingTarget.Error().Code == N::EError::TimedOut);
	const auto MissingHistory = History(Context, Request.History);
	CHECK(!MissingHistory->bTruncated && MissingHistory->Attempts.size() == 1);
	const auto& MissingDetails = *MissingHistory->Attempts[0].Details;
	CHECK(MissingDetails.Responses.size() == 1 && MissingDetails.Responses[0].bRedirectFollowed &&
	    MissingDetails.Responses[0].RedirectUrl ==
	        "http://127.0.0.1:" + std::to_string(RefusalPort) + "/not-listening" &&
	    MissingDetails.Responses[0].RedirectMethod.empty());
	Request = {};
	Request.Method = "GET";
	Request.Body.clear();
	Request.Url = Base + "/empty-location";
	CHECK(Body(Result<H::FResponse>(Context, Client.Send(Request))) == "stay");
	auto EmptyLocation = Client.SendStream(Request, 16384);
	const auto EmptyResponse = Result<H::FResponse>(Context, EmptyLocation.Operation());
	H::FResponse EmptyHead;
	N::FError EmptyError;
	CHECK(EmptyResponse.Status == 302 && EmptyLocation.TryHead(EmptyHead, EmptyError) && !EmptyError &&
	    EmptyHead.Status == 302);
	CHECK(EmptyHead.Details && !EmptyHead.Details->Statistics.bFinal &&
	    EmptyHead.Details->Statistics.AcceptedBodyBytes == 0 && EmptyResponse.Details->Statistics.bFinal &&
	    EmptyResponse.Details->Statistics.AcceptedBodyBytes == 4 &&
	    EmptyResponse.Details->Responses[0].Locations == std::vector<std::string>{""});
	char EmptyBytes[8];
	const auto EmptyRead = EmptyLocation.Body()->Read(EmptyBytes, 8);
	CHECK(EmptyRead.Bytes == 4 && std::string(EmptyBytes, 4) == "stay");

	Request = {};
	Request.Url = Base + "/retry/body-budget-" + Scope + "/10";
	Request.Retry.MaxAttempts = 4;
	Request.Retry.bAllowReplay = true;
	Request.Retry.Delay = std::chrono::milliseconds(1);
	Request.MaxTotalBodyBytes = 2;
	Request.History = std::make_shared<H::FAttemptHistory>();
	auto Flow = Client.Send(Request);
	Await(Context, Flow);
	CHECK(Flow.Error().Code == N::EError::Limit);
	CHECK(History(Context, Request.History)->Attempts.size() == 3);
	Request.MaxTotalBodyBytes = 1024;
	Request.MaxTotalHeaderBytes = 200;
	Request.Url = Base + "/retry/header-budget-" + Scope + "/10";
	Flow = Client.Send(Request);
	Await(Context, Flow);
	CHECK(Flow.Error().Code == N::EError::Limit);
	CHECK(History(Context, Request.History)->Attempts.size() < 4);
	Request = {};
	Request.Url = Base + "/retry/upload-budget-" + Scope + "/1";
	Request.Method = "POST";
	Request.Body = {'a', 'b', 'c'};
	Request.Retry.MaxAttempts = 2;
	Request.Retry.bAllowReplay = true;
	Request.Retry.bAllowUnsafeMethods = true;
	Request.Retry.Delay = std::chrono::milliseconds(1);
	Request.MaxTotalUploadBytes = 5;
	Flow = Client.Send(Request);
	Await(Context, Flow);
	CHECK(Flow.Error().Code == N::EError::Limit);

	unsigned Sources = 0;
	Request = {};
	Request.Url = Base + "/retry/factory-" + Scope + "/1";
	Request.Method = "POST";
	Request.Retry.MaxAttempts = 2;
	Request.Retry.bAllowReplay = true;
	Request.Retry.bAllowUnsafeMethods = true;
	Request.Retry.Delay = std::chrono::milliseconds(1);
	Request.UploadFactory = [&Sources](std::uint32_t Attempt, N::FError& Error)
	{
		++Sources;
		CHECK(Attempt >= 1 && Attempt <= 2);
		N::FBytePipe Pipe;
		CHECK(N::TryCreateBytePipe(16, Pipe, Error));
		CHECK(Pipe.Writer->WriteAll("abc", 3).Bytes == 3);
		Pipe.Writer->Finish();
		return H::FBodySource{Pipe.Reader, 3};
	};
	CHECK(Result<H::FResponse>(Context, Client.Send(Request)).Attempts.size() == 2 && Sources == 2);
	Sources = 0;
	Request.Retry = {};
	Request.Url = Base + "/redirect-307";
	Request.bFollowRedirects = true;
	CHECK(Body(Result<H::FResponse>(Context, Client.Send(Request))) == "POST:abc" && Sources == 2);
	Sources = 0;
	Request.MaxTotalUploadBytes = 5;
	Flow = Client.Send(Request);
	Await(Context, Flow);
	CHECK(Flow.Error().Code == N::EError::Limit && Sources == 2);
	Request.UploadFactory = [](std::uint32_t, N::FError&) -> H::FBodySource
	{
		throw std::runtime_error("factory");
	};
	Flow = Client.Send(Request);
	Await(Context, Flow);
	CHECK(Flow.Error().Code == N::EError::Callback);

	if (Mode == N::EDriveMode::NativeBackground)
	{
		std::atomic<bool> Entered{false}, Release{false};
		struct FRelease
		{
			std::atomic<bool>& Flag;
			~FRelease()
			{
				Flag = true;
			}
		} Guard{Release};
		H::FRequest Slow;
		Slow.Method = "POST";
		Slow.Url = Base + "/method-echo";
		Slow.History = std::make_shared<H::FAttemptHistory>();
		Slow.UploadFactory = [&](std::uint32_t, N::FError& Error)
		{
			Entered = true;
			const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!Release && std::chrono::steady_clock::now() < End)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			if (!Release)
			{
				throw std::runtime_error("history race barrier expired");
			}
			N::FBytePipe Pipe;
			CHECK(N::TryCreateBytePipe(16, Pipe, Error));
			Pipe.Writer->Finish();
			return H::FBodySource{Pipe.Reader, 0};
		};
		auto Previous = Client.Send(Slow);
		const auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!Entered && std::chrono::steady_clock::now() < Until)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		CHECK(Entered && Previous.Cancel());
		const auto OldFinal = Slow.History->Snapshot();
		CHECK(OldFinal->bFinal && OldFinal->Attempts.empty() && OldFinal->Error.Code == N::EError::Cancelled);
		H::FRequest Next;
		Next.Url = Base + "/ok";
		Next.History = Slow.History;
		const auto Current = Client.Send(Next);
		Release = true;
		CHECK(Result<H::FResponse>(Context, Current).Status == 200);
		const auto NewFinal = History(Context, Slow.History);
		CHECK(NewFinal->Attempts.size() == 1 && !NewFinal->Error && !NewFinal->bTruncated);
		CHECK(OldFinal->Attempts.empty() && OldFinal->Error.Code == N::EError::Cancelled);
	}

	{
		N::FContextOptions SmallOptions;
		SmallOptions.Mode = Mode;
		SmallOptions.MaxBufferBytes = 512;
		SmallOptions.MaxRetainedBytes = 2048;
		N::FContext Small(VeroScript::Boost::CreateNetworkService(), SmallOptions);
		H::FClient C(Small, VeroScript::Curl::CreateHttpClientService());
		H::FRequest R;
		R.Url = Base + "/ok";
		R.MaxHeaderBytes = 512;
		R.MaxBodyBytes = 64;
		R.History = std::make_shared<H::FAttemptHistory>();
		CHECK(Result<H::FResponse>(Small, C.Send(R)).Status == 200);
		const auto Truncated = History(Small, R.History);
		CHECK(Truncated->bFinal && Truncated->bTruncated && !Truncated->Error);
		C.Close();
		Small.Shutdown();
		CHECK(Truncated->bTruncated && Truncated->bFinal);
	}

	H::FClientOptions One;
	One.MaxPendingRequests = 1;
	H::FClient Single(Context, VeroScript::Curl::CreateHttpClientService(), One);
	Request = {};
	Request.Url = Base + "/stall";
	auto Pending = Single.Send(Request);
	Request.Url = Base + "/ok";
	auto Queued = Single.Send(Request);
	Await(Context, Queued);
	CHECK(Queued.Error().Code == N::EError::QueueFull);
	CHECK(Pending.Cancel());
	Await(Context, Pending);
	CHECK(Result<H::FResponse>(Context, Single.Send(Request)).Status == 200);

	Request = {};
	Request.Url = Base + "/stall";
	Request.History = std::make_shared<H::FAttemptHistory>();
	auto InFlight = Client.Send(Request);
	// Wait until the transfer is executing so cancellation includes a partial attempt.
	while (InFlight.State() == N::EOperationState::Queued)
	{
		if (Mode == N::EDriveMode::HostDriven)
		{
			N::FError E;
			Context.PollIo(E);
			CHECK(!E);
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	auto Reused = Client.Send(Request);
	Await(Context, Reused);
	CHECK(Reused.Error().Code == N::EError::State);
	CHECK(InFlight.Cancel());
	Await(Context, InFlight);
	auto CancelHistory = History(Context, Request.History);
	CHECK(CancelHistory->Error.Code == N::EError::Cancelled && CancelHistory->Attempts.size() == 1 &&
	    CancelHistory->Attempts[0].ErrorCode == N::EError::Cancelled);
	Request.Url = Base + "/ok";
	CHECK(Result<H::FResponse>(Context, Client.Send(Request)).Status == 200);
	CHECK(History(Context, Request.History)->Attempts.size() == 1);
	CHECK(CancelHistory->Error.Code == N::EError::Cancelled);
	Request.Url = Base + "/stall";
	InFlight = Client.Send(Request);
	while (InFlight.State() == N::EOperationState::Queued)
	{
		if (Mode == N::EDriveMode::HostDriven)
		{
			N::FError E;
			Context.PollIo(E);
			CHECK(!E);
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	Client.Close();
	Await(Context, InFlight);
	CHECK(History(Context, Request.History)->Error.Code == N::EError::Closed);

	Single.Close();
	Client.Close();
	Other.Close();
	Disabled.Close();
	Context.Shutdown();
	const auto AfterShutdown = Request.History->Snapshot();
	CHECK(AfterShutdown && AfterShutdown->bFinal && AfterShutdown->Error.Code == N::EError::Closed);
}
int main(int Argc, char** Argv)
{
	try
	{
		CHECK(Argc >= 2);
		const auto Port = static_cast<std::uint16_t>(std::stoi(Argv[1]));
		Test(Port, N::EDriveMode::NativeBackground);
		Test(Port, N::EDriveMode::HostDriven);
		std::cout << "HTTP cookie, retry and client policy tests passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}
