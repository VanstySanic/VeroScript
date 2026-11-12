// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetworkBackend.h"
#include "VeroScriptNetworkIdentity.h"
#include "VeroScriptNetworkTls.h"
#include "NetworkProvider.h"

namespace VeroScript::Network
{
	const char* Backend::FoundationIdentity() noexcept
	{
		return VEROSCRIPT_NETWORK_FOUNDATION_ID;
	}
	FOperation Resolve(FContext& Context, std::string Host, std::uint16_t Port, FOperationOptions Options)
	{
		return Detail::Service(Detail::FAccess::Context(Context))->Resolve(Context, std::move(Host), Port, Options);
	}
	bool TlsAvailable(const FContext& Context) noexcept
	{
		if (std::string_view(Context.GetFoundationIdentity()) != Backend::FoundationIdentity()) return false;
		return Detail::Service(Detail::FAccess::Context(const_cast<FContext&>(Context)))->TlsAvailable();
	}
	std::shared_ptr<FTcpSocket> Backend::FAccess::Wrap(
	    const std::shared_ptr<Detail::FContext>& Context, std::shared_ptr<ITcpSocket> Value)
	{
		return Detail::RetainObject(Context, std::shared_ptr<FTcpSocket>(new FTcpSocket(std::move(Value))));
	}
	std::shared_ptr<FTcpListener> Backend::FAccess::Wrap(
	    const std::shared_ptr<Detail::FContext>& Context, std::shared_ptr<ITcpListener> Value)
	{
		return Detail::RetainObject(Context, std::shared_ptr<FTcpListener>(new FTcpListener(std::move(Value))));
	}
	std::shared_ptr<FUdpSocket> Backend::FAccess::Wrap(
	    const std::shared_ptr<Detail::FContext>& Context, std::shared_ptr<IUdpSocket> Value)
	{
		return Detail::RetainObject(Context, std::shared_ptr<FUdpSocket>(new FUdpSocket(std::move(Value))));
	}
	FOperation FTcpSocket::Connect(FContext& Context, std::string Host, std::uint16_t Port, FOperationOptions Options)
	{
		return Detail::Service(Detail::FAccess::Context(Context))->ConnectTcp(Context, std::move(Host), Port, Options);
	}

	FTcpSocket::~FTcpSocket()
	{
		Impl->Dispose();
	}

	FOperation FTcpSocket::Read(std::uint64_t MaxBytes, FOperationOptions Options)
	{
		return Impl->Read(MaxBytes, Options);
	}

	FOperation FTcpSocket::Write(FBytes Bytes, FOperationOptions Options)
	{
		return Impl->Write(std::move(Bytes), Options);
	}

	void FTcpSocket::Close()
	{
		Impl->RequestClose();
	}

	FTcpSocket::FTcpSocket(std::shared_ptr<Backend::ITcpSocket> Value) : Impl(std::move(Value))
	{
	}
	std::shared_ptr<FTcpListener> FTcpListener::Listen(FContext& Context, std::string Address, std::uint16_t Port,
	    FError& Error, int Backlog)
	{
		return Detail::Service(Detail::FAccess::Context(Context))->ListenTcp(Context, std::move(Address), Port, Error, Backlog);
	}

	FTcpListener::~FTcpListener()
	{
		Impl->Dispose();
	}

	std::uint16_t FTcpListener::Port() const noexcept
	{
		return Impl->Port();
	}

	FOperation FTcpListener::Accept(FOperationOptions Options)
	{
		return Impl->Accept(Options);
	}

	void FTcpListener::Close()
	{
		Impl->RequestClose();
	}

	FTcpListener::FTcpListener(std::shared_ptr<Backend::ITcpListener> Value) : Impl(std::move(Value))
	{
	}
	std::shared_ptr<FUdpSocket> FUdpSocket::Bind(FContext& Context, std::string Address, std::uint16_t Port, FError& Error)
	{
		return Detail::Service(Detail::FAccess::Context(Context))->BindUdp(Context, std::move(Address), Port, Error);
	}

	FUdpSocket::~FUdpSocket()
	{
		Impl->Dispose();
	}

	std::uint16_t FUdpSocket::Port() const noexcept
	{
		return Impl->Port();
	}

	FOperation FUdpSocket::SendTo(FBytes Bytes, FAddress Peer, FOperationOptions Options)
	{
		return Impl->SendTo(std::move(Bytes), std::move(Peer), Options);
	}

	FOperation FUdpSocket::Receive(std::uint64_t MaxBytes, FOperationOptions Options)
	{
		return Impl->Receive(MaxBytes, Options);
	}

	FOperation FUdpSocket::SetBroadcast(bool Enabled, FOperationOptions Options)
	{
		return Impl->SetBroadcast(Enabled, Options);
	}

	FOperation FUdpSocket::JoinMulticast(FMulticastGroup Group, FOperationOptions Options)
	{
		return Multicast(std::move(Group), true, Options);
	}

	FOperation FUdpSocket::LeaveMulticast(FMulticastGroup Group, FOperationOptions Options)
	{
		return Multicast(std::move(Group), false, Options);
	}

	void FUdpSocket::Close()
	{
		Impl->RequestClose();
	}

	FUdpSocket::FUdpSocket(std::shared_ptr<Backend::IUdpSocket> Value) : Impl(std::move(Value))
	{
	}

	FOperation FUdpSocket::Multicast(FMulticastGroup Group, bool Join, FOperationOptions Options)
	{
		return Impl->Multicast(std::move(Group), Join, Options);
	}
} // namespace VeroScript::Network
