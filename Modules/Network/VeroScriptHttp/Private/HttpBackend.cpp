// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpBackend.h"
#include "NetworkProvider.h"

namespace VeroScript::Http
{
	FClient::FClient(Network::FContext& Context, std::shared_ptr<Backend::IClientService> Service, FClientOptions Options)
	{
		if (!Service || !Service->GetFoundationIdentity() ||
		    std::string_view(Service->GetFoundationIdentity()) != Network::Backend::FoundationIdentity())
		{
			throw Detail::FClientCreationError(Network::EError::Unsupported);
		}
		if (!Options.BackendId.empty() && Options.BackendId != Service->GetBackendId())
		{
			throw Detail::FClientCreationError(Network::EError::InvalidArgument);
		}
		Network::FError Error;
		if (!Context.RetainCodeOwner(Service, Error))
		{
			throw Detail::FClientCreationError(Error.Code);
		}
		Lifetime = Network::Detail::FAccess::Context(Context);
		Impl = Service->Create(Context, std::move(Options));
		if (!Impl)
		{
			throw std::runtime_error("HTTP backend returned an empty client");
		}
	}

	FClient::~FClient()
	{
		Impl->Dispose();
	}

	Network::FOperation FClient::Send(FRequest Request)
	{
		return Impl->Send(std::move(Request));
	}

	FStreamingOperation FClient::SendStream(FRequest Request, std::uint64_t BufferBytes)
	{
		return Impl->SendStream(std::move(Request), BufferBytes);
	}

	FCapabilities FClient::Capabilities() const
	{
		return Impl->Capabilities();
	}

	bool FClient::BelongsTo(const Network::FContext& Context) const noexcept
	{
		return Impl->BelongsTo(Context);
	}

	Network::FOperation FClient::Cookies(Network::FOperationOptions Options)
	{
		return Impl->Cookies(Options);
	}

	Network::FOperation FClient::ClearCookies(Network::FOperationOptions Options)
	{
		return Impl->ClearCookies(Options);
	}

	Network::FOperation FClient::ImportCookies(FCookies Cookies, Network::FOperationOptions Options)
	{
		return Impl->ImportCookies(std::move(Cookies), Options);
	}

	void FClient::Close()
	{
		Impl->RequestClose();
	}

	FExchange::~FExchange()
	{
		Impl->Dispose();
	}

	const FIncomingRequest& FExchange::Request() const noexcept
	{
		return Impl->Request();
	}

	std::shared_ptr<Network::FUpgradeSource> FExchange::PrepareUpgrade(Network::FError& Error)
	{
		return Impl->PrepareUpgrade(weak_from_this().lock(), Error);
	}

	Network::FOperation FExchange::Reply(FResponse Response, Network::FOperationOptions Options)
	{
		return Impl->Reply(std::move(Response), Options);
	}

	FResponseOperation FExchange::ReplyStream(FResponse Response, FBodySource Source, Network::FOperationOptions Options)
	{
		return Impl->ReplyStream(std::move(Response), std::move(Source), Options);
	}

	FExchange::FExchange(std::shared_ptr<Backend::IExchange> Value) : Impl(std::move(Value))
	{
	}

	std::shared_ptr<FServer> FServer::Listen(Network::FContext& Context, std::string Address, std::uint16_t Port,
	    Network::FError& Error, FServerOptions Options)
	{
		Error = {};
		if (!Options.Service || !Options.Service->GetFoundationIdentity() ||
		    std::string_view(Options.Service->GetFoundationIdentity()) != Network::Backend::FoundationIdentity())
		{
			Error = {Network::EError::Unsupported, "HTTP server requires an explicit backend service", 0};
			return {};
		}
		if (!Options.BackendId.empty() && Options.BackendId != Options.Service->GetBackendId())
		{
			Error = {Network::EError::InvalidArgument, "HTTP server backend selection mismatch", 0};
			return {};
		}
		if (!Context.RetainCodeOwner(Options.Service, Error)) return {};
		auto Service = Options.Service;
		return Service->Listen(Context, std::move(Address), Port, Error, std::move(Options),
		    static_cast<Backend::FExchangeFactory>(Backend::FAccess::Wrap));
	}

	FServer::~FServer()
	{
		Impl->Dispose();
	}

	std::uint16_t FServer::Port() const noexcept
	{
		return Impl->Port();
	}

	Network::FOperation FServer::Accept(Network::FOperationOptions Options)
	{
		return Impl->Accept(Options);
	}

	Network::FOperation FServer::Shutdown(std::chrono::milliseconds Grace, Network::FOperationOptions Options)
	{
		return Impl->Shutdown(Grace, Options);
	}

	void FServer::Close()
	{
		Impl->RequestClose();
	}

	FServer::FServer(std::shared_ptr<Backend::IServer> Value) : Impl(std::move(Value))
	{
	}

	std::shared_ptr<FServer> Backend::FAccess::Wrap(
	    const std::shared_ptr<Network::Detail::FContext>& Context, std::shared_ptr<IServer> Value)
	{
		return Network::Detail::RetainObject(Context, std::shared_ptr<FServer>(new FServer(std::move(Value))));
	}

	std::shared_ptr<FExchange> Backend::FAccess::Wrap(
	    const std::shared_ptr<Network::Detail::FContext>& Context, std::shared_ptr<IExchange> Value)
	{
		return std::shared_ptr<FExchange>(new FExchange(std::move(Value)),
		    [Context](FExchange* Object)
		{
			delete Object;
		});
	}

	Backend::IExchange& Backend::FAccess::Implementation(FExchange& Value)
	{
		return *Value.Impl;
	}

	FStreamingOperation Backend::FAccess::Stream(Network::FOperation Operation,
	    std::shared_ptr<Network::FReadableStream> Reader, std::shared_ptr<Detail::FHttpStreamState> State)
	{
		FStreamingOperation Result;
		Result.Op = std::move(Operation);
		Result.Reader = std::move(Reader);
		Result.State = std::move(State);
		return Result;
	}

	FResponseOperation Backend::FAccess::Response(Network::FOperation Operation,
	    std::shared_ptr<Detail::FServerResponseState> Progress)
	{
		FResponseOperation Result;
		Result.Op = std::move(Operation);
		Result.Progress = std::move(Progress);
		return Result;
	}
	#include "HttpBackendResults.inl"
} // namespace VeroScript::Http
