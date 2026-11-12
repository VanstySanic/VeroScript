// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#if TEST_BOOST
#include "VeroScriptBoost.h"
#endif
#if TEST_HTTP
#include "VeroScriptCurl.h"
#include "../../../../../VeroScriptHttp/Resources/Tests/HttpTestBackends.h"
#endif
#if TEST_WEBSOCKET
#include "../../../../../VeroScriptWebSocket/Resources/Tests/WebSocketTestBackends.h"
#endif

#include <VeroScriptNetwork.h>
#include <VeroScriptNetworkStream.h>
#include <VeroScriptNetworkFiles.h>
#include "../../NetworkTestPaths.h"
#include <filesystem>
#if TEST_HTTP
#include <VeroScriptHttp.h>
#include <VeroScriptHttpMultipart.h>
#include <VeroScriptHttpFiles.h>
#endif
#if TEST_WEBSOCKET
#include <VeroScriptWebSocket.h>
#include <VeroScriptWebSocketReconnect.h>
#endif

int main()
{
	#if !TEST_BOOST
	VeroScript::Network::FError Error;
	VeroScript::Network::FUrl Url;
	VeroScript::Network::FBytePipe Pipe;
	return VeroScript::Network::FUrl::TryParse("https://localhost/path", Url, Error) &&
		VeroScript::Network::TryCreateBytePipe(8, Pipe, Error) &&
		Pipe.Writer->WriteAll("ok", 2).Bytes == 2 ? 0 : 1;
	#else
	using namespace VeroScript;
	Network::FError Error;
	Network::FContext Context(VeroScript::Boost::CreateNetworkService());
	Network::FBytePipe Pipe;
	if (!Network::TryCreateBytePipe(Context, 8, Pipe, Error) || Pipe.Writer->WriteAll("ok", 2).Bytes != 2 ||
		Context.MemoryUsage().RetainedBytes != 8)
	{
		return 7;
	}
	char Bytes[2];
	if (Pipe.Reader->Read(Bytes, 2).Bytes != 2 || std::string(Bytes, 2) != "ok")
	{
		return 8;
	}
	auto Owner = std::make_shared<int>(0);
	if (!Context.RetainCodeOwner(Owner, Error))
	{
		return 1;
	}
	auto OtherOwner = std::make_shared<int>(1);
	auto NullOwner = std::make_shared<int>(2);
	std::weak_ptr<int> OtherProbe = OtherOwner, NullProbe = NullOwner;
	if (!Context.RetainCodeOwner(std::shared_ptr<const void>(OtherOwner, Owner.get()), Error) ||
		!Context.RetainCodeOwner(std::shared_ptr<const void>(NullOwner, nullptr), Error))
	{
		return 9;
	}
	OtherOwner.reset();
	NullOwner.reset();
	if (OtherProbe.expired() || NullProbe.expired())
	{
		return 10;
	}
	Network::FUrl Url;
	if (!Network::FUrl::TryParse("https://localhost/path", Url, Error) || Url.Port != 443)
	{
		return 2;
	}
	if (!Network::FUrl::TryResolve(Url.String(), "../other?", Url, Error) || Url.String() != "https://localhost/other?" ||
		!Url.bHasQuery)
	{
		return 6;
	}
	const auto FileRoot =
		std::filesystem::current_path() /
		std::filesystem::u8path(
			"consumer-文件 " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	if (!std::filesystem::create_directory(FileRoot))
	{
		return 14;
	}
	auto Files = std::make_shared<Network::FFileExecutor>(Context);
	Network::FBytePipe FilePipe;
	if (!Network::TryCreateBytePipe(Context, 32, FilePipe, Error) ||
		FilePipe.Writer->WriteAll("file\0bytes", 10).Bytes != 10)
	{
		return 15;
	}
	FilePipe.Writer->Finish();
	Network::FFileWriteOptions Output;
	Output.Path = "input.bin";
	Output.BaseDirectory = Network::Test::PathToUtf8(FileRoot, true);
	Output.ExpectedLength = 10;
	auto Written = Files->Write(Output, FilePipe.Reader);
	if (Written.Operation().Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed ||
		Written.Operation().Error() || !Written.Operation().Result<Network::FFileResult>()->bCommitted)
	{
		return 16;
	}
	Network::FFileReadOptions Input;
	Input.Path = "input.bin";
	Input.BaseDirectory = Output.BaseDirectory;
	auto Read = Files->Read(Input);
	// The file fits in one pipe block; this bounded SDK smoke test can observe completion before draining.
	if (Read.Operation().Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed || Read.Operation().Error())
	{
		return 17;
	}
	char FileBytes[10];
	if (Read.Body()->Read(FileBytes, 10).Bytes != 10 || std::string(FileBytes, 10) != std::string("file\0bytes", 10))
	{
		return 18;
	}
	#if TEST_HTTP
	Http::FClient Client(Context, VeroScript::Curl::CreateHttpClientService());
	auto HttpServer = VeroScript::Http::Test::Listen(Context, "127.0.0.1", 0, Error);
	if (!HttpServer || Error)
	{
		return 19;
	}
	Http::FRequest Request;
	Request.Url = "http://127.0.0.1:" + std::to_string(HttpServer->Port()) + "/download";
	auto Accepted = HttpServer->Accept();
	Http::FDownloadOptions DownloadOptions;
	DownloadOptions.File.Path = "download.bin";
	DownloadOptions.File.BaseDirectory = Output.BaseDirectory;
	auto Download = Http::DownloadFile(Context, Client, *Files, Request, DownloadOptions);
	if (Accepted.Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed || Accepted.Error())
	{
		return 20;
	}
	auto Exchange = Accepted.Result<std::shared_ptr<Http::FExchange>>();
	if (!Exchange || !*Exchange)
	{
		return 21;
	}
	Http::FResponse Response;
	Response.Status = 200;
	Response.Body = {'o', 'k'};
	auto Replied = (*Exchange)->Reply(std::move(Response));
	if (Replied.Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed || Replied.Error() ||
		Download.Operation().Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed ||
		Download.Operation().Error())
	{
		return 22;
	}
	auto Downloaded = Download.Operation().Result<Http::FDownloadResult>();
	if (!Downloaded || Downloaded->File.Bytes != 2 || !Downloaded->File.bCommitted ||
		!Downloaded->Response.Body.empty() || !Downloaded->Response.Details ||
		!Downloaded->Response.Details->Statistics.bFinal || Downloaded->Response.Details->Responses.size() != 1 ||
		Downloaded->Response.Details->Statistics.LastTransferDownloadedBytes != 2 ||
		Downloaded->Response.Attempts[0].Details != Downloaded->Response.Details)
	{
		return 23;
	}
	Network::FError FactoryError;
	auto UploadFactory = Http::MakeFileUploadFactory(Files, Input);
	auto Upload = UploadFactory(1, FactoryError);
	if (FactoryError || !Upload.Stream)
	{
		return 24;
	}
	Upload.Stream->Cancel();
	HttpServer->Close();
	std::shared_ptr<Http::FMultipartReader> Multipart;
	if (!Http::FMultipartReader::TryCreate("multipart/form-data; boundary=consumer", Multipart, Error, {}, &Context))
	{
		return 11;
	}
	Http::FMultipartEvent Event;
	const std::string Empty = "--consumer--\r\n";
	auto Feed = Multipart->Feed(Empty, true, Event);
	if (Feed.State != Http::EMultipartReadState::EventReady || Event.Type != Http::EMultipartEvent::End)
	{
		return 12;
	}
	Feed = Multipart->Feed(std::string_view(Empty).substr(Feed.Consumed), true, Event);
	if (Feed.State != Http::EMultipartReadState::Done)
	{
		return 13;
	}
	Http::FMultipartPart Part;
	Part.Name = "stream";
	Part.UploadFactory = [](std::uint32_t, Network::FError& E)
	{
		Network::FBytePipe P;
		if (!Network::TryCreateBytePipe(8, P, E))
		{
			return Http::FBodySource{};
		}
		P.Writer->Finish();
		return Http::FBodySource{P.Reader, 0};
	};
	if (Client.Capabilities().Backend.empty())
	{
		return 3;
	}
	#endif
	#if TEST_WEBSOCKET
	#if TEST_HTTP
	auto UpgradeServer = VeroScript::Http::Test::Listen(Context, "127.0.0.1", 0, Error);
	if (!UpgradeServer || Error)
	{
		return 25;
	}
	auto UpgradeAccepted = UpgradeServer->Accept();
	WebSocket::FOptions CompressionOptions;
	CompressionOptions.bCompression = VeroScript::WebSocket::Test::CompressionSupported();
	CompressionOptions.Compression.ClientMaxWindowBits = 12;
	CompressionOptions.Compression.ServerMaxWindowBits = 12;
	CompressionOptions.Compression.bServerNoContextTakeover = true;
	auto WsConnecting = VeroScript::WebSocket::Test::Connect(
		Context, "ws://127.0.0.1:" + std::to_string(UpgradeServer->Port()) + "/upgrade", CompressionOptions);
	if (UpgradeAccepted.Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed || UpgradeAccepted.Error())
	{
		return 26;
	}
	auto UpgradeExchange = *UpgradeAccepted.Result<std::shared_ptr<Http::FExchange>>();
	auto Source = UpgradeExchange->PrepareUpgrade(Error);
	if (!Source || Error)
	{
		return 27;
	}
	auto Upgrading = VeroScript::WebSocket::Test::AcceptUpgrade(Context, Source, CompressionOptions);
	if (Upgrading.Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed || Upgrading.Error() ||
		WsConnecting.Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed || WsConnecting.Error())
	{
		return 28;
	}
	auto Upgraded = *Upgrading.Result<std::shared_ptr<WebSocket::FConnection>>();
	auto WsClient = *WsConnecting.Result<std::shared_ptr<WebSocket::FConnection>>();
	if (WsClient->CompressionInfo().bEnabled != VeroScript::WebSocket::Test::CompressionSupported() ||
		Upgraded->CompressionInfo().bEnabled != VeroScript::WebSocket::Test::CompressionSupported() ||
		(VeroScript::WebSocket::Test::CompressionSupported() && (WsClient->CompressionInfo().ClientWindowBits != 12 ||
			WsClient->CompressionInfo().ServerWindowBits != 12 || !WsClient->CompressionInfo().bServerNoContextTakeover)))
	{
		return 32;
	}
	auto UpgradeRead = Upgraded->Receive();
	auto UpgradeSent = WsClient->Send({{'o', 'k'}, true});
	if (UpgradeSent.Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed || UpgradeSent.Error() ||
		UpgradeRead.Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed || UpgradeRead.Error() ||
		UpgradeRead.Result<WebSocket::FMessage>()->Bytes != Network::FBytes({'o', 'k'}))
	{
		return 29;
	}
	UpgradeServer->Close();
	#endif
	auto Server = VeroScript::WebSocket::Test::Listen(Context, "127.0.0.1", 0, Error);
	if (!Server || Error || !Server->Port())
	{
		return 4;
	}
	Server->Close();
	auto Reconnecting = VeroScript::WebSocket::Test::ConnectWithRetry(Context, "not-a-websocket-url");
	if (Reconnecting.Operation().Wait(std::chrono::seconds(10)) != Network::EWaitResult::Completed ||
		Reconnecting.Operation().Error().Code != Network::EError::InvalidArgument)
	{
		return 30;
	}
	#endif
	Context.Shutdown();
	#if TEST_WEBSOCKET
	auto ReconnectHistory = Reconnecting.Snapshot(Error);
	if (!ReconnectHistory || Error || !ReconnectHistory->bFinal || ReconnectHistory->Attempts.size() != 1 ||
		ReconnectHistory->Attempts[0].Error.Code != Network::EError::InvalidArgument)
	{
		return 31;
	}
	#endif
	std::filesystem::remove_all(FileRoot);
	return Context.IsClosed() ? 0 : 5;
	#endif
}
