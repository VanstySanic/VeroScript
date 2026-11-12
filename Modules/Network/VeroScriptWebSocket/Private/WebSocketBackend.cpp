// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptWebSocketBackend.h"
#include "WebSocketInternal.h"
#include "NetworkProvider.h"

namespace VeroScript::WebSocket
{
	namespace N = Network::Detail;
	bool CompressionSupported(const Backend::IService& Service) noexcept
	{
		return Service.CompressionSupported();
	}
	std::shared_ptr<FConnection> Backend::FAccess::Wrap(
	    const std::shared_ptr<N::FContext>& Context, std::shared_ptr<IConnection> Value)
	{
		return N::RetainObject(Context, std::shared_ptr<FConnection>(new FConnection(std::move(Value))));
	}
	std::shared_ptr<FServer> Backend::FAccess::Wrap(
	    const std::shared_ptr<N::FContext>& Context, std::shared_ptr<IServer> Value)
	{
		return N::RetainObject(Context, std::shared_ptr<FServer>(new FServer(std::move(Value))));
	}
	Network::FOperation FConnection::Connect(Network::FContext& Context, std::string Url, FOptions Options)
	{
		return Detail::FConnectionAccess::Connect(N::FAccess::Context(Context), std::move(Url), std::move(Options));
	}

	Network::FOperation FConnection::AcceptUpgrade(Network::FContext& Context,
	    std::shared_ptr<Network::FUpgradeSource> Source, FOptions Options)
	{
		Network::FError Error;
		if (Options.Service && (!Options.Service->GetFoundationIdentity() || std::string_view(Options.Service->GetFoundationIdentity()) != Network::Backend::FoundationIdentity()))
		{
			Error = {Network::EError::Unsupported, "WebSocket backend/foundation implementation mismatch", 0};
		}
		if (Options.Service && !Options.BackendId.empty() && Options.BackendId != Options.Service->GetBackendId())
		{
			Error = {Network::EError::InvalidArgument, "WebSocket backend selection mismatch", 0};
		}
		if (Error || !Options.Service || !Context.RetainCodeOwner(Options.Service, Error))
		{
			auto Operation = N::Begin(N::FAccess::Context(Context), Options.Operation);
			N::Fail(Operation, Error ? Error.Code : Network::EError::Unsupported, "WebSocket backend is unavailable");
			return N::FAccess::Wrap(Operation);
		}
		auto Service = Options.Service;
		return Service->AcceptUpgrade(Context, std::move(Source), std::move(Options));
	}

	Network::FOperation Detail::FConnectionAccess::Connect(
	    const std::shared_ptr<N::FContext>& Context, std::string Url, FOptions Options)
	{
		Network::FError Error;
		if (Options.Service && (!Options.Service->GetFoundationIdentity() || std::string_view(Options.Service->GetFoundationIdentity()) != Network::Backend::FoundationIdentity()))
		{
			Error = {Network::EError::Unsupported, "WebSocket backend/foundation implementation mismatch", 0};
		}
		if (Options.Service && !Options.BackendId.empty() && Options.BackendId != Options.Service->GetBackendId())
		{
			Error = {Network::EError::InvalidArgument, "WebSocket backend selection mismatch", 0};
		}
		if (Error || !Options.Service || !N::RetainCodeOwner(Context, Options.Service, Error))
		{
			auto Operation = N::Begin(Context, Options.Operation);
			N::Fail(Operation, Error ? Error.Code : Network::EError::Unsupported, "WebSocket backend is unavailable");
			return N::FAccess::Wrap(Operation);
		}
		auto Service = Options.Service;
		return Service->Connect(Context, std::move(Url), std::move(Options));
	}

	FConnection::~FConnection()
	{
		Impl->Dispose();
	}

	Network::FOperation FConnection::Send(FMessage Message, Network::FOperationOptions Options)
	{
		return Submit(std::move(Message), Options, false, false);
	}

	Network::FOperation FConnection::SendFragment(FMessage Message, Network::FOperationOptions Options)
	{
		return Submit(std::move(Message), Options, true, false);
	}

	Network::FOperation FConnection::Ping(std::string Payload, Network::FOperationOptions Options)
	{
		return Impl->Ping(std::move(Payload), Options);
	}

	bool FConnection::TryTakeControl(FControlEvent& Out)
	{
		return Impl->TryTakeControl(Out);
	}

	Network::FOperation FConnection::Receive(Network::FOperationOptions Options)
	{
		return Impl->Receive(Options);
	}

	Network::FOperation FConnection::Close(std::uint16_t Code, std::string Reason, Network::FOperationOptions Options)
	{
		return Impl->Close(Code, std::move(Reason), Options);
	}

	FCloseInfo FConnection::CloseInfo() const
	{
		return Impl->CloseInfo();
	}

	std::string FConnection::Subprotocol() const
	{
		return Impl->Subprotocol();
	}

	FCompressionInfo FConnection::CompressionInfo() const
	{
		return Impl->CompressionInfo();
	}

	Network::FOperation FConnection::Submit(FMessage Message, Network::FOperationOptions Options, bool Fragment, bool Ping)
	{
		return Impl->Submit(std::move(Message), Options, Fragment, Ping);
	}

	FConnection::FConnection(std::shared_ptr<Backend::IConnection> Value) : Impl(std::move(Value))
	{
	}

	std::shared_ptr<FServer> FServer::Listen(Network::FContext& Context, std::string Address,
	    std::uint16_t Port, Network::FError& Error, FOptions Options)
	{
		Error = {};
		if (!Options.Service || !Options.Service->GetFoundationIdentity() ||
		    std::string_view(Options.Service->GetFoundationIdentity()) != Network::Backend::FoundationIdentity())
		{
			Error = {Network::EError::Unsupported, "WebSocket server requires an explicit backend service", 0};
			return {};
		}
		if (!Options.BackendId.empty() && Options.BackendId != Options.Service->GetBackendId())
		{
			Error = {Network::EError::InvalidArgument, "WebSocket backend selection mismatch", 0};
			return {};
		}
		if (!Context.RetainCodeOwner(Options.Service, Error)) return {};
		auto Service = Options.Service;
		return Service->Listen(Context, std::move(Address), Port, Error, std::move(Options));
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

	void FServer::Close()
	{
		Impl->RequestClose();
	}

	FServer::FServer(std::shared_ptr<Backend::IServer> Value) : Impl(std::move(Value))
	{
	}
} // namespace VeroScript::WebSocket
